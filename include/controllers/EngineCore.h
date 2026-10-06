/**
 * @file EngineCore.h
 * @brief Core engine initialization and common utilities for Game and Editor
 *
 * This class provides shared initialization logic for both the game runtime
 * and the editor, reducing code duplication and ensuring consistent setup.
 */

#pragma once

#include "Camera.h"
#include "controllers/WindowManager.h"
#include "controllers/Registry.h"
#include "controllers/ScriptManager.h"
#include "systems/HTMLRendererMT.h"
#include "systems/ReactiveUI.h"
#include "systems/LuaUIState.h"
#include "util/Config.h"
#include <limits>
#include <memory>
#include <string>

extern "C" void stbi_set_flip_vertically_on_load(int flag);

class SplashScreen;

/**
 * @brief Core engine subsystems initialization helper
 *
 * Usage pattern:
 * @code
 * EngineCore core;
 * core.Initialize(...);
 * // ...
 * core.BeginFrame();
 * // ... rendering ...
 * core.EndFrame();
 * @endcode
 */
class EngineCore
{
public:
    EngineCore();
    ~EngineCore();

    /**
     * @brief Initialize core engine subsystems
     * @param configPath Path to config file (e.g., "res/games/vaporqube/conf/settings.yaml")
     * @param conf Configuration struct
     * @param cam 3D camera pointer
     * @return true if successful, false otherwise
     */
    bool Initialize(const std::string &configPath, Config &conf, Camera **cam);

    /**
     * @brief Initialize core engine subsystems
     * @param conf Configuration struct
     * @return true if successful, false otherwise
     */
    bool Initialize(Config conf);

    /**
     * @brief Initialize core engine subsystems
     * @param logPath Path to log file (e.g., "logs/imhotep.log")
     * @param appName Application name for log header
     * @param logPath Path to log file (e.g., "logs/imhotep.log")
     * @param width Window width in screen coordinates
     * @param height Window height in screen coordinates
     * @param htmlPath Optional HTML template file to load (relative to res/)
     * @param cssPath Optional CSS file to load (relative to res/)
     * @param luaStatePath Optional Lua state file to load (relative to res/)
     * @param templateName Optional name of template to register with ReactiveUI
     * @return true if successful, false otherwise
     */
    bool Initialize(const std::string &logPath, const std::string &appName, int width, int height, const std::string &htmlPath = "", const std::string &cssPath = "", const std::string &luaStatePath = "", const std::string &templateName = "");

    /**
     * @brief Shut down owned and referenced subsystems in dependency order
     *
     * Stops worker threads first, releases VM-owned objects while the VM is
     * alive, releases GPU objects while the GL context is current, and destroys
     * the window last.
     */
    void Shutdown();

    /**
     * @brief Initialize logging system
     * @param logPath Path to log file (e.g., "logs/imhotep.log")
     * @param appName Application name for log header
     * @return true if successful, false otherwise
     */
    bool InitializeLogger(const std::string &logPath, const std::string &appName);

    /**
     * @brief Initialize window and OpenGL context
     * @param width Window width in screen coordinates
     * @param height Window height in screen coordinates
     * @return true if successful, false otherwise
     */
    bool InitializeWindow(int width, int height);

    /**
     * @brief Initialize HTML renderer (must be called after InitializeWindow)
     * @return true if successful, false otherwise
     */
    bool InitializeHTMLRenderer();

    /**
     * @brief Initialize Registry (ECS system)
     * @return true if successful, false otherwise
     * @note To load a scene, call LoadScene() after initialization
     */
    bool InitializeRegistry();

    /**
     * @brief Load a scene into the Registry
     * @param scenePath Scene file to load (relative to res/)
     * @return true if successful, false otherwise
     */
    bool LoadScene(const std::string &scenePath);

    /**
     * @brief Initialize ScriptManager (Lua + Python VMs)
     * @note Editor may skip this if it doesn't need game scripts
     * @return true if successful, false otherwise
     */
    bool InitializeScriptManager();

    /**
     * @brief Initialize ReactiveUI system with Lua-based UI
     * @param htmlPath Path to HTML template (e.g., "../res/ui/game.html")
     * @param cssPath Path to CSS stylesheet (e.g., "../res/ui/styles/game.css")
     * @param luaStatePath Path to Lua state file (e.g., "../res/ui/state/fps.lua")
     * @param templateName Name for the template (for logging)
     * @return true if successful, false otherwise
     */
    bool InitializeUI(const std::string &htmlPath,
                      const std::string &cssPath,
                      const std::string &luaStatePath,
                      const std::string &templateName);

    /**
     * @brief Begin a new frame (timing, input polling, clear buffers)
     * @return Delta time since last frame in seconds
     */
    double BeginFrame();

    /**
     * @brief End frame (swap buffers, update FPS)
     */
    void EndFrame();

    /**
     * @brief Check if window should close
     * @return true if user requested close
     */
    bool ShouldClose() const;

    // Splash screen management
    /**
     * @brief Show splash screen (loads PNG and renders first frame)
     * @note Call after InitializeWindow() succeeds
     */
    void ShowSplash();

    /**
     * @brief Render one splash frame (no swap)
     * @note Caller must swap buffers after this call
     */
    void RenderSplash();

    /**
     * @brief Check if splash is currently active
     * @return true if splash was shown and not yet dismissed
     */
    bool IsSplashActive() const;

    /**
     * @brief Dismiss splash and release its GL resources
     */
    void DismissSplash();

    // Accessor methods for subsystems
    WindowManager *GetWindowManager() const { return m_windowManager; }
    HTMLRendererMT *GetHTMLRenderer() const { return m_htmlRenderer; }
    Registry *GetRegistry() const { return m_registry; }
    ReactiveUI *GetReactiveUI() const { return m_reactiveUI; }
    ScriptManager *GetScriptManager() const { return m_scriptManager; }
    std::shared_ptr<LuaUIState> GetLuaUIState() const { return m_luaState; }
    Camera *GetCamera() const { return m_camera; }

    double GetDeltaTime() const { return m_deltaTime; }
    double GetFPS() const { return m_currentFPS; }

private:
    // Subsystem references (singletons, not owned)
    WindowManager *m_windowManager = nullptr;
    HTMLRendererMT *m_htmlRenderer = nullptr;
    Registry *m_registry = nullptr;
    ReactiveUI *m_reactiveUI = nullptr;
    ScriptManager *m_scriptManager = nullptr;

    // Owned state
    Camera *m_camera = nullptr; ///< 3D perspective camera (owned by EngineCore)
    std::shared_ptr<LuaUIState> m_luaState;
    std::unique_ptr<SplashScreen> m_splashScreen; ///< Boot splash (created/destroyed during init)

    // Timing
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    double m_deltaTime = 0.0;

    // FPS tracking
    std::chrono::high_resolution_clock::time_point m_fpsUpdateTime;
    int m_frameCount = 0;
    double m_currentFPS = 0.0;

    // Resource path (set by PathResolver, valid for dev/bundle/installed modes)
    std::string m_resourcePath = "../res/";

    // Initialization flags
    bool m_windowInitialized = false;
    bool m_htmlRendererInitialized = false;
    bool m_isShutdown = false;

    // Input handler ID for UI click forwarding
    size_t m_uiClickHandlerId = std::numeric_limits<size_t>::max();
    size_t m_uiMouseButtonHandlerId = std::numeric_limits<size_t>::max();
    size_t m_uiCursorHandlerId = std::numeric_limits<size_t>::max();
    size_t m_uiResizeHandlerId = std::numeric_limits<size_t>::max();
};
