/**
 * @file HTMLRendererMT.h
 * @brief Multi-threaded HTML/CSS renderer using litehtml and FreeType
 * @lines ~225
 *
 * Quick-stats (Public API):
 * - Initialize() - Setup GL, start render thread (line ~75)
 * - LoadHTML() / UpdateHTML() - Queue HTML for render (line ~85, 90)
 * - Render() - Upload texture to GPU (line ~95)
 * - Resize() - Handle window resize (line ~100)
 * - HandleClickEvent() - Process UI clicks (line ~110)
 * - Shutdown() - Stop render thread gracefully (line ~115)
 *
 * CRITICAL: Multi-threaded architecture
 * - Main thread: OpenGL, texture uploads, input
 * - Render thread: litehtml, FreeType rasterization
 * - Thread safety via m_mutex and m_bufferMutex
 *
 * Architecture: docs/architecture/UI_SYSTEM.md
 * Implementation: See src/systems/HTMLRendererMT.cpp (1380 lines)
 */

#pragma once

#include "util/Shader.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <litehtml.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>
#include <map>

/**
 * @brief Multi-threaded HTML/CSS renderer singleton
 *
 * Renders HTML/CSS on a background thread using litehtml + FreeType,
 * then composites result to screen via OpenGL texture. Prevents UI
 * rendering from blocking the main game loop.
 *
 * **Thread Safety:**
 * - Main thread: calls LoadHTML(), Render(), Resize()
 * - Render thread: internal rendering, writes to m_backBuffer
 * - Double buffering prevents race conditions
 *
 * **Performance:**
 * - HTML rendering: 5-15ms (async, doesn't block main thread)
 * - Main thread cost: ~2ms (texture upload + composite)
 *
 * @see docs/architecture/UI_SYSTEM.md for implementation details
 */
class HTMLRendererMT {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to HTMLRendererMT singleton
     */
    static HTMLRendererMT &GetInstance() {
        static HTMLRendererMT instance;
        return instance;
    }

    HTMLRendererMT(HTMLRendererMT const &) = delete;
    void operator=(HTMLRendererMT const &) = delete;

    /**
     * @brief Initialize renderer and start background thread
     * @param window GLFW window for OpenGL context
     * @param width Initial viewport width
     * @param height Initial viewport height
     */
    void Initialize(GLFWwindow *window, int width, int height);

    /**
     * @brief Shutdown renderer and stop background thread
     * @note Blocks until render thread completes
     */
    void Shutdown();

    /**
     * @brief Queue HTML for rendering on background thread
     * @param html HTML/CSS string to render
     * @note Thread-safe, wakes render thread via condition variable
     */
    void LoadHTML(const std::string& html);

    /// Returns a revision that completes when this HTML reaches the front buffer.
    std::uint64_t LoadHTMLAndGetRevision(const std::string& html);

    /// Waits at most timeout for this exact revision to reach the front buffer.
    bool WaitForHTMLRevision(std::uint64_t revision, std::chrono::milliseconds timeout) const;

    /**
     * @brief Semantic alias for LoadHTML (update existing HTML)
     * @param html HTML/CSS string to render
     */
    void UpdateHTML(const std::string& html);

    /**
     * @brief Composite rendered UI texture to screen (call per frame)
     * @note Must be called from main thread with active OpenGL context
     */
    void Render();

    /**
     * @brief Update viewport size and signal render thread
     * @param width New viewport width
     * @param height New viewport height
     */
    void Resize(int width, int height);

    /**
     * @brief Handle mouse click event with hit-testing
     * @param x Mouse X coordinate (window space)
     * @param y Mouse Y coordinate (window space)
     * @param button Mouse button (0=left, 1=right, 2=middle)
     * @return true if click was handled by an interactive element
     * @note Thread-safe, uses m_frontInteractiveElements
     * @note Only triggers on button press (not release)
     */
    bool HandleClickEvent(float x, float y, int button);

    /**
     * @brief Handle mouse button event with action (press/release)
     * @param x Mouse X coordinate (window space)
     * @param y Mouse Y coordinate (window space)
     * @param button Mouse button (0=left, 1=right, 2=middle)
     * @param action GLFW action (GLFW_PRESS or GLFW_RELEASE)
     * @return true if event was handled by an interactive element
     * @note Thread-safe, dispatches mousedown/mouseup events based on action
     * @note Use this for fine-grained control; HandleClickEvent for simple clicks
     */
    bool HandleMouseButtonEvent(float x, float y, int button, int action);

    /**
     * @brief Update hover state based on cursor position
     * @param x Mouse X coordinate (window space)
     * @param y Mouse Y coordinate (window space)
     * @note Generates synthetic mouseover/mouseout/mouseenter/mouseleave events
     */
    void UpdateHoverState(float x, float y);

    /**
     * @brief Try to get the bounds of an interactive element by its data-event-id
     * @param elemId Element ID (data-event-id)
     * @param x Out: left
     * @param y Out: top
     * @param width Out: width
     * @param height Out: height
     * @return true if element was found in the current interactive element set
     * @note Coordinates are in framebuffer/litehtml space (top-left origin)
     */
    bool TryGetInteractiveElementBounds(const std::string &elemId, int &x, int &y, int &width, int &height) const;

    /**
     * @brief Try to find an interactive element by matching a handler expression
     * @param eventType Event type to match (e.g., "click")
     * @param handlerExpr Handler expression string (e.g., "onViewportClick($event)")
     * @param x Out: left
     * @param y Out: top
     * @param width Out: width
     * @param height Out: height
     * @return true if a matching element was found in the current interactive element set
     * @note Coordinates are in framebuffer/litehtml space (top-left origin)
     */
    bool TryFindInteractiveElementBoundsByHandler(const std::string &eventType,
                                                 const std::string &handlerExpr,
                                                 int &x,
                                                 int &y,
                                                 int &width,
                                                 int &height) const;

    /**
     * @brief Check whether the render thread has completed at least one frame
     * @return true if the front buffer contains a rendered frame
     * @note Thread-safe (locks m_bufferMutex)
     */
    bool HasCompletedFirstRender() const;

    /**
     * @brief Get event handlers map (thread-safe copy)
     * @return Map of element ID → {eventType → handlerExpression}
     * @note Set by ReactiveUI after template evaluation
     */
    std::map<std::string, std::map<std::string, std::string>> GetEventHandlers() const {
        std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
        return m_eventHandlers;
    }

    /**
     * @brief Set event handlers from template parser (thread-safe)
     * @param handlers Map of element ID → {eventType → handlerExpression}
     * @note Called by ReactiveUI after template evaluation
     */
    void SetEventHandlers(const std::map<std::string, std::map<std::string, std::string>>& handlers) {
        std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
        m_eventHandlers = handlers;
    }

private:
    HTMLRendererMT() = default;
    ~HTMLRendererMT() { Shutdown(); }

    /**
     * @brief Main render thread loop (blocks until shutdown)
     * @note Runs on background thread, woken by m_cv
     */
    void RenderThreadLoop();

    /**
     * @brief Setup OpenGL state for UI compositing
     */
    void SetupGL();

    /**
     * @brief Upload pixel buffer to OpenGL texture (main thread)
     */
    void UpdateTextureFromPixelBuffer();

    /**
     * @brief Convert window coordinates to framebuffer coordinates
     * @param windowX X coordinate in window space
     * @param windowY Y coordinate in window space
     * @return Coordinates in framebuffer space (scaled for HiDPI)
     * @note On Retina/HiDPI displays, framebuffer is typically 2x window size
     */
    glm::vec2 WindowToFramebuffer(float windowX, float windowY) const;

    /**
     * @brief Frame buffer for double buffering
     */
    struct FrameBuffer {
        uint32_t width;                 ///< Buffer width in pixels
        uint32_t height;                ///< Buffer height in pixels
        uint32_t frameNumber;           ///< Incremented on each render
        std::vector<uint8_t> pixels;    ///< RGBA pixel data
    };

    /**
     * @brief Interactive HTML element with event handlers
     */
    struct InteractiveElement {
        std::string id;                             ///< Unique element ID (data-event-id)
        int x, y, width, height;                    ///< Bounding box (screen coordinates)
        std::map<std::string, std::string> handlers; ///< Event type → handler expression
        int zIndex;                                 ///< CSS z-index for overlap resolution
    };

    /**
     * @brief Software renderer (runs on render thread)
     * @note Implements litehtml::document_container interface
     */
    class SoftwareRenderer;

    GLFWwindow* m_window = nullptr;     ///< GLFW window handle
    int m_width = 800;                  ///< Current viewport width
    int m_height = 600;                 ///< Current viewport height

    /// Thread synchronization
    std::unique_ptr<std::thread> m_renderThread;    ///< Background render thread
    std::atomic<bool> m_running{false};             ///< Thread running flag
    std::mutex m_mutex;                             ///< Protects shared HTML/resize state
    std::condition_variable m_cv;                   ///< Wakes render thread on new work

    /// Shared state (protected by m_mutex)
    std::string m_pendingHTML;          ///< HTML queued for rendering
    bool m_hasNewHTML = false;          ///< New HTML available flag
    bool m_needsResize = false;         ///< Resize pending flag
    int m_newWidth = 0;                 ///< New width for resize
    int m_newHeight = 0;                ///< New height for resize

    /// Frame buffers (double buffered)
    FrameBuffer m_frontBuffer;          ///< Read by main thread (protected by m_bufferMutex)
    FrameBuffer m_backBuffer;           ///< Written by render thread (exclusive ownership)
    mutable std::mutex m_bufferMutex;   ///< Protects buffer swap
    mutable std::condition_variable m_frameCv; ///< Signals published HTML frame revisions
    uint64_t m_nextFrameNumber = 0;     ///< Monotonic counter for frame versioning (render thread only)
    uint64_t m_lastFrameNumber = 0;     ///< Last uploaded frame number (main thread only)
    uint64_t m_frontHTMLRevision = 0;   ///< HTML revision in front buffer; protected by m_bufferMutex
    uint64_t m_nextHTMLRevision = 0;    ///< Revision for new HTML submissions; protected by m_mutex
    uint64_t m_pendingHTMLRevision = 0; ///< Revision of pending HTML; protected by m_mutex

    /// Interactive elements (double buffered for thread safety)
    std::vector<InteractiveElement> m_frontInteractiveElements;  ///< Read by main thread
    std::vector<InteractiveElement> m_backInteractiveElements;   ///< Written by render thread
    mutable std::mutex m_interactiveElementsMutex;               ///< Protects element swap (readable from const APIs)
    mutable std::mutex m_eventHandlersMutex;                     ///< Protects event handlers
    std::map<std::string, std::map<std::string, std::string>> m_eventHandlers; ///< From TemplateParser
    std::string m_lastHoveredElement;                            ///< Track hover state for mouseout events
    std::string m_mouseCaptureElement;                           ///< Element that received last mousedown (for mouseup capture)
    int m_mouseCaptureButton = -1;                               ///< Mouse button captured for mouseup

    /// OpenGL resources (main thread only)
    Shader* m_compositeShader = nullptr;
    unsigned int m_texture = 0;
    unsigned int m_quadVAO = 0;
    unsigned int m_quadVBO = 0;
    glm::mat4 m_projection;

    /// Shutdown state
    bool m_isShutdown = false;
};
