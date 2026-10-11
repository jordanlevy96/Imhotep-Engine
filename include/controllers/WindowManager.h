/**
 * @file WindowManager.h
 * @brief GLFW window and input event management
 */

#pragma once

#include "util/InputEvent.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <unordered_set>

/**
 * @brief GLFW window manager singleton handling window lifecycle and input
 *
 * Creates OpenGL context via GLFW, manages window state, and dispatches
 * input events using a multi-tiered priority system.
 *
 * **Input Dispatch Architecture (Priority Order):**
 * 1. **C++ Handlers** (LIFO): Registered via `RegisterInputHandler()`
 *    - Handlers called in reverse registration order
 *    - First handler to return true consumes event
 * 2. **Lua Event Queue**: Events not consumed by C++ go to `ScriptManager::ProcessInput()`
 * 3. **UI Events**: Specific UI elements trigger ReactiveUI event handlers
 *
 * **Supported input types:**
 * - Keyboard (key press/release/repeat)
 * - Mouse clicks (Click + MouseButton events)
 * - Mouse movement
 * - Window resize
 * - Scroll wheel
 * - Character input (text fields)
 *
 * @see InputEvent.h for detailed input architecture documentation
 * @see ScriptManager for Lua event queue processing
 * @see ReactiveUI::DispatchEvent() for UI-specific event handling
 */
class WindowManager
{
public:
    /**
     * @brief Get singleton instance
     * @return Reference to WindowManager singleton
     */
    static WindowManager &GetInstance()
    {
        static WindowManager instance;
        return instance;
    }

    WindowManager(WindowManager const &) = delete;
    void operator=(WindowManager const &) = delete;

    /**
     * @brief Initialize GLFW window and OpenGL context
     * @param width Initial window width
     * @param height Initial window height
     * @return true if initialization succeeded
     * @note Creates OpenGL 3.3 Core Profile context
     */
    bool Initialize(int const width, int const height);

    /**
     * @brief Shutdown GLFW and destroy window
     */
    void Shutdown();

    /**
     * @brief Signal window to close (exits main loop)
     */
    void CloseWindow();

    /**
     * @brief Get current window dimensions
     * @return vec2 (width, height)
     */
    glm::vec2 GetSize();

    /**
     * @brief Register C++ input handler with LIFO priority
     * @param handler Callback that returns true to consume event
     * @return Handler ID for unregistration
     *
     * @note Handlers are called in REVERSE registration order (LIFO - Last In, First Out).
     *       Handlers registered later have higher priority and are called first.
     *       This allows UI layers to intercept events before game logic.
     *
     * @note First handler to return true consumes the event, stopping further propagation.
     *       Events not consumed by any C++ handler are queued to Lua via ScriptManager.
     *
     * @par Example Priority:
     * @code
     * id1 = RegisterInputHandler(gameHandler);   // Called 3rd (lowest priority)
     * id2 = RegisterInputHandler(uiHandler);     // Called 2nd
     * id3 = RegisterInputHandler(editorHandler); // Called 1st (highest priority)
     * @endcode
     *
     * @see InputEvent.h for full input architecture documentation
     * @see UnregisterInputHandler() to remove handlers
     */
    size_t RegisterInputHandler(InputHandler handler);

    /**
     * @brief Unregister input handler by ID
     * @param id Handler ID from RegisterInputHandler
     */
    void UnregisterInputHandler(size_t id);

    /**
     * @brief Poll whether a key is currently pressed
     * @param keyName Key name string (e.g., "LEFT", "RIGHT", "DOWN")
     * @return true if the key is currently held down
     */
    bool IsKeyPressed(const std::string &keyName) const;

    /**
     * @brief Enable deterministic key-state overrides for an engine test scenario.
     * @note Disabled by default; while enabled, physical key, click, cursor, and scroll input is ignored.
     */
    void EnableTestInputOverrides(bool enabled);

    /** @brief Return whether physical input callbacks are suppressed by test overrides. */
    bool ShouldIgnorePhysicalInput() const { return m_testInputOverridesEnabled; }

    /**
     * @brief Set one held-key state while test input overrides are enabled.
     */
    void SetTestKeyPressed(const std::string &keyName, bool pressed);

    GLFWwindow *window; ///< GLFW window handle (public for renderer access)

private:
    WindowManager() {};

    /// GLFW keyboard callback (dispatches to ScriptManager)
    static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);

    /// GLFW mouse button callback
    static void click_callback(GLFWwindow *window, int button, int action, int mods);

    /// GLFW cursor position callback
    static void cursorPos_callback(GLFWwindow *window, double xpos, double ypos);

    /// GLFW window resize callback
    static void resize_callback(GLFWwindow *window, int in_width, int in_height);

    /// GLFW scroll wheel callback
    static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);

    /// GLFW character input callback (for text fields)
    static void char_callback(GLFWwindow *window, unsigned int codepoint);

    /// Input handler storage (handler ID, callback)
    std::vector<std::pair<size_t, InputHandler>> m_inputHandlers;

    bool m_testInputOverridesEnabled = false;
    std::unordered_set<std::string> m_testPressedKeys;

    /// Next handler ID for registration
    size_t m_nextHandlerId = 0;

    bool m_isShutdown = false;
};
