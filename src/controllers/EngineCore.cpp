/**
 * @file EngineCore.cpp
 * @brief Shared engine initialization for Game and Editor modes
 * @lines ~360
 *
 * Purpose: Provides common initialization code to avoid duplication between Game and Editor.
 * Handles window, renderer, scene, script manager, and UI setup.
 *
 * Key functions:
 * - Initialize() overloads - Multiple initialization paths (line 35, 82, 94)
 * - InitializeLogger() - Setup Quill logging (line 136, ~20 lines)
 * - InitializeWindow() - Create GLFW window + GL context (line 157, ~20 lines)
 * - InitializeHTMLRenderer() - Setup multi-threaded UI renderer (line 177, ~20 lines)
 * - InitializeRegistry() - Setup ECS registry (line 198, ~5 lines)
 * - LoadScene() - Load scene from YAML (line 205, ~25 lines)
 * - InitializeScriptManager() - Setup Lua/Python VMs (line 229, ~5 lines)
 * - InitializeUI() - Load UI templates and state (line 236, ~110 lines)
 * - EndFrame() - Swap buffers, poll events (line 345, ~10 lines)
 *
 * Initialization patterns:
 * - Config file based (YAML settings)
 * - Programmatic (direct parameters)
 * - Hybrid (config + overrides)
 *
 * Integration: Used by both Game::Initialize() and Editor::Initialize()
 */

#include "controllers/EngineCore.h"
#include "controllers/ResourceManager.h"
#include "systems/SkyBackgroundRenderer.h"
#include "systems/SplashScreen.h"
#include "systems/TiledBackgroundRenderer.h"
#include "util/Logger.h"
#include "util/ConfigLoader.h"
#include "util/PathResolver.h"
#include "util/Version.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <variant>

EngineCore::EngineCore()
{
    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    m_fpsUpdateTime = std::chrono::high_resolution_clock::now();
}

EngineCore::~EngineCore()
{
    // Unregister UI click handler
    const size_t invalidId = std::numeric_limits<size_t>::max();

    // Avoid interacting with WindowManager during process teardown if the window is already destroyed.
    if (m_uiClickHandlerId != invalidId && m_windowManager && m_windowManager->window != nullptr)
    {
        m_windowManager->UnregisterInputHandler(m_uiClickHandlerId);
        m_uiClickHandlerId = invalidId;
    }

    if (m_uiMouseButtonHandlerId != invalidId && m_windowManager && m_windowManager->window != nullptr)
    {
        m_windowManager->UnregisterInputHandler(m_uiMouseButtonHandlerId);
        m_uiMouseButtonHandlerId = invalidId;
    }

    if (m_uiCursorHandlerId != invalidId && m_windowManager && m_windowManager->window != nullptr)
    {
        m_windowManager->UnregisterInputHandler(m_uiCursorHandlerId);
        m_uiCursorHandlerId = invalidId;
    }

    if (m_uiResizeHandlerId != invalidId && m_windowManager && m_windowManager->window != nullptr)
    {
        m_windowManager->UnregisterInputHandler(m_uiResizeHandlerId);
        m_uiResizeHandlerId = invalidId;
    }

    if (m_camera)
    {
        delete m_camera;
        m_camera = nullptr;
    }
}

bool EngineCore::Initialize(const std::string &configPath, Config &conf, Camera **cam)
{
    // Check for environment variable override
    const char *userSettingsPath = std::getenv("USER_SETTINGS_PATH");
    const std::string actualPath = userSettingsPath ? userSettingsPath : configPath;

    std::cout << "[EngineCore] Loading config from " << actualPath << std::endl;

    if (!ConfigLoader::LoadConfig(actualPath, conf))
    {
        std::cerr << "[EngineCore] ERROR: Failed to load config from " << actualPath << std::endl;
        return false;
    }

    // Override paths with PathResolver (handles dev, bundle, and installed modes)
    conf.ResourcePath = PathResolver::GetResourcePath();

    // Resolve log path: extract filename from config LogPath, combine with resolved log directory
    std::string logFilename = std::filesystem::path(conf.LogPath).filename().string();
    conf.LogPath = PathResolver::GetLogPath(conf.AppName) + logFilename;

    std::cout << "[EngineCore] Config loaded successfully" << std::endl;
    std::cout << "[EngineCore] AppName: " << conf.AppName << ", logPath: " << conf.LogPath << std::endl;

    // Initialize core engine (this initializes the logger, so use std::cerr for errors before this point)
    std::cout << "[EngineCore] Initializing subsystems..." << std::endl;

    if (!Initialize(conf))
    {
        std::cerr << "[EngineCore] ERROR: EngineCore initialization failed" << std::endl;
        return false;
    }

    *cam = m_camera;

    // Other OpenGL initialization
    stbi_set_flip_vertically_on_load(true);
    glEnable(GL_DEPTH_TEST);
    glFrontFace(GL_CW);

    // Load scene at the end of initialization (after all systems are ready)
    if (!LoadScene(conf.ScenePath))
    {
        LOG_CRITICAL("Failed to load scene: {}", conf.ScenePath);
        return false;
    }

    LOG_INFO("==================================");
    LOG_INFO("   Imhotep {} - Initialized", imhotep::Version::GetVersionString());
    LOG_INFO("==================================");
    LOG_INFO("[EngineCore] EngineCore initialized successfully");
    return true;
}

bool EngineCore::Initialize(Config conf)
{
    m_resourcePath = conf.ResourcePath;
    return Initialize(conf.LogPath,
                      conf.AppName,
                      conf.WindowWidth,
                      conf.WindowHeight,
                      conf.ResourcePath + conf.HTMLPath,
                      conf.ResourcePath + conf.CSSPath,
                      conf.ResourcePath + conf.LuaStatePath,
                      conf.TemplateName);
}

bool EngineCore::Initialize(const std::string &logPath, const std::string &appName, int width, int height, const std::string &htmlPath, const std::string &cssPath, const std::string &luaStatePath, const std::string &templateName)
{
    m_isShutdown = false;

    if (!InitializeLogger(logPath, appName))
    {
        std::cerr << "Failed to initialize logger" << std::endl;
        return false;
    }

    if (!InitializeWindow(width, height))
    {
        LOG_CRITICAL("Failed to initialize Window");
        return false;
    }

    // Show splash immediately after GL context is ready
    ShowSplash();

    if (!InitializeHTMLRenderer())
    {
        LOG_CRITICAL("Failed to initialize HTMLRenderer");
        return false;
    }

    // Pump splash between init steps to keep it visible
    if (IsSplashActive())
    {
        glfwPollEvents();
        RenderSplash();
        glfwSwapBuffers(m_windowManager->window);
    }

    if (!InitializeScriptManager())
    {
        LOG_CRITICAL("Failed to initialize ScriptManager");
        return false;
    }

    if (IsSplashActive())
    {
        glfwPollEvents();
        RenderSplash();
        glfwSwapBuffers(m_windowManager->window);
    }

    if (!InitializeRegistry())
    {
        LOG_CRITICAL("Failed to initialize Registry");
        return false;
    }

    if (IsSplashActive())
    {
        glfwPollEvents();
        RenderSplash();
        glfwSwapBuffers(m_windowManager->window);
    }

    if (!InitializeUI(htmlPath, cssPath, luaStatePath, templateName))
    {
        LOG_CRITICAL("Failed to initialize UI");
        return false;
    }

    if (IsSplashActive())
    {
        glfwPollEvents();
        RenderSplash();
        glfwSwapBuffers(m_windowManager->window);
    }

    return true;
}

void EngineCore::Shutdown()
{
    if (m_isShutdown)
    {
        return;
    }
    m_isShutdown = true;

    // Stop the UI worker before releasing any state it can observe.
    if (m_htmlRenderer)
    {
        m_htmlRenderer->Shutdown();
    }

    // Release renderer-owned GL resources while the context is current.
    if (m_splashScreen)
    {
        m_splashScreen->Shutdown();
        m_splashScreen.reset();
    }
    TiledBackgroundRenderer::GetInstance().Shutdown();
    SkyBackgroundRenderer::GetInstance().Shutdown();

    // Script components own Lua/Python handles. Destroy them before either VM
    // is finalized; Python decref operations additionally require the GIL.
    if (m_registry)
    {
#ifdef USE_PYTHON_SCRIPTING
        if (Py_IsInitialized())
        {
            py::gil_scoped_acquire gil;
            m_registry->Shutdown();
        }
        else
        {
            m_registry->Shutdown();
        }
#else
        m_registry->Shutdown();
#endif
    }

    // ReactiveUI is a singleton and otherwise retains Lua-backed objects past
    // ScriptManager shutdown.
    if (m_reactiveUI)
    {
        m_reactiveUI->UnbindLuaState();
    }
    m_luaState.reset();

    // Drop cached mesh/shader owners before destroying the GL context. Lua may
    // still hold shared RenderComponents, so the VM is also shut down before
    // the window to release those final references safely.
    ResourceManager::GetInstance().Shutdown();

    if (m_scriptManager)
    {
        m_scriptManager->Shutdown();
    }

    if (m_windowManager)
    {
        m_windowManager->Shutdown();
    }

    m_windowInitialized = false;
    m_htmlRendererInitialized = false;
}

bool EngineCore::InitializeLogger(const std::string &logPath, const std::string &appName)
{
    if (logPath.empty())
    {
        std::cerr << "Log path is empty" << std::endl;
        return false;
    }

    if (!imhotep::Logger::GetInstance().Initialize(logPath))
    {
        std::cerr << "Failed to initialize logger at path: " << logPath << std::endl;
        return false;
    }

    LOG_INFO("==================================");
    LOG_INFO("   {} {}", appName, imhotep::Version::GetVersionString());
    LOG_INFO("==================================");

    return true;
}

bool EngineCore::InitializeWindow(int width, int height)
{
    m_windowManager = &WindowManager::GetInstance();

    if (!m_windowManager->Initialize(width, height))
    {
        LOG_CRITICAL("Failed to initialize WindowManager");
        return false;
    }

    LOG_INFO("INIT - WindowManager: SUCCESS ({}x{})", width, height);

    // Initialize camera with window dimensions
    m_camera = new Camera(width, height);
    LOG_INFO("INIT - Camera: SUCCESS ({}x{})", width, height);

    m_windowInitialized = true;
    return true;
}

bool EngineCore::InitializeHTMLRenderer()
{
    if (!m_windowInitialized)
    {
        LOG_ERROR("Must initialize Window before HTMLRenderer");
        return false;
    }

    m_htmlRenderer = &HTMLRendererMT::GetInstance();

    // Get framebuffer size (handles Retina/HiDPI scaling)
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(m_windowManager->window, &fbWidth, &fbHeight);
    LOG_INFO("Framebuffer size: {}x{}", fbWidth, fbHeight);

    m_htmlRenderer->Initialize(m_windowManager->window, fbWidth, fbHeight);
    LOG_INFO("INIT - HTMLRendererMT: SUCCESS");

    // Register resize handler so the UI overlay always matches the framebuffer size.
    const size_t invalidId = std::numeric_limits<size_t>::max();
    if (m_uiResizeHandlerId == invalidId)
    {
        m_uiResizeHandlerId = m_windowManager->RegisterInputHandler([this](const InputEvent &event)
                                                                    {
            if (event.type != InputTypes::Resize || !m_htmlRenderer)
            {
                return false;
            }

            if (std::holds_alternative<glm::vec2>(event.input))
            {
                auto fbSize = std::get<glm::vec2>(event.input);
                m_htmlRenderer->Resize(static_cast<int>(fbSize.x), static_cast<int>(fbSize.y));
            }

            return false; });
    }

    m_htmlRendererInitialized = true;
    return true;
}

bool EngineCore::InitializeRegistry()
{
    m_registry = &Registry::GetInstance();
    LOG_INFO("INIT - Registry: SUCCESS");
    return true;
}

bool EngineCore::LoadScene(const std::string &scenePath)
{
    if (!m_registry)
    {
        LOG_ERROR("Registry not initialized - call InitializeRegistry() first");
        return false;
    }

    if (scenePath.empty())
    {
        LOG_WARNING("No scene path provided - skipping scene load");
        return true;
    }

    if (!m_registry->LoadScene(scenePath))
    {
        LOG_ERROR("Failed to load scene: {}", scenePath);
        return false;
    }

    LOG_INFO("Loaded scene: {}", scenePath);
    return true;
}

bool EngineCore::InitializeScriptManager()
{
    m_scriptManager = &ScriptManager::GetInstance();
    m_scriptManager->Initialize();
    return true;
}

bool EngineCore::InitializeUI(const std::string &htmlPath,
                              const std::string &cssPath,
                              const std::string &luaStatePath,
                              const std::string &templateName)
{
    if (!m_htmlRendererInitialized)
    {
        LOG_ERROR("Must initialize HTMLRenderer before UI");
        return false;
    }

    m_reactiveUI = &ReactiveUI::GetInstance();

    // Create and load Lua UI state
    m_luaState = std::make_shared<LuaUIState>();
    if (!m_luaState->LoadStateFile(luaStatePath))
    {
        LOG_ERROR("Failed to load UI state file: {}", luaStatePath);
        return false;
    }
    LOG_INFO("Loaded Lua UI state: {}", luaStatePath);

    // Bind Lua state to ReactiveUI
    m_reactiveUI->BindLuaState(m_luaState);

    // Create methods table in Lua state for event handlers
    sol::table stateTable = m_luaState->GetStateTable();
    sol::object methodsObj = stateTable["methods"];
    if (!methodsObj.valid() || methodsObj.get_type() == sol::type::lua_nil)
    {
        sol::state &lua = m_scriptManager->GetLuaState();
        sol::table methods = lua.create_table();
        stateTable["methods"] = methods;
        LOG_INFO("Created methods table in Lua UI state for event handlers");
    }

    // Load UI template from files
    std::string uiTemplate = ReactiveUI::LoadTemplateFromFiles(htmlPath, cssPath);
    if (uiTemplate.empty())
    {
        LOG_ERROR("Failed to load UI template files: {} + {}", htmlPath, cssPath);
        return false;
    }
    LOG_INFO("Loaded UI template: {} bytes", uiTemplate.size());

    m_reactiveUI->RegisterTemplateWithDirectives(templateName, uiTemplate);

    // Set event handlers before loading HTML to avoid race condition
    m_htmlRenderer->SetEventHandlers(m_reactiveUI->GetEventHandlers());
    LOG_INFO("Set {} event handlers before HTML render", m_reactiveUI->GetEventHandlers().size());

    // Load HTML into renderer
    m_htmlRenderer->LoadHTML(m_reactiveUI->GetRenderedHTML());

    // Register click handler to forward UI clicks to HTMLRenderer for hit-testing
    m_uiClickHandlerId = m_windowManager->RegisterInputHandler([this](const InputEvent &event)
                                                               {
        if (event.type == InputTypes::Click)
        {
            // Extract click position from the event payload
            if (std::holds_alternative<glm::vec3>(event.input))
            {
                auto clickData = std::get<glm::vec3>(event.input);
                return m_htmlRenderer->HandleClickEvent(clickData.x, clickData.y, static_cast<int>(clickData.z));
            }
            else if (std::holds_alternative<glm::vec2>(event.input))
            {
                auto clickData = std::get<glm::vec2>(event.input);
                return m_htmlRenderer->HandleClickEvent(clickData.x, clickData.y, 0);
            }
        }
        return false; });

    // Register mouse button handler to forward press/release to HTMLRenderer (@mousedown/@mouseup).
    m_uiMouseButtonHandlerId = m_windowManager->RegisterInputHandler([this](const InputEvent &event)
                                                                     {
        if (event.type != InputTypes::MouseButton)
        {
            return false;
        }

        if (std::holds_alternative<glm::vec4>(event.input))
        {
            auto mouseData = std::get<glm::vec4>(event.input);
            int button = static_cast<int>(mouseData.z);
            int action = static_cast<int>(mouseData.w);
            return m_htmlRenderer->HandleMouseButtonEvent(mouseData.x, mouseData.y, button, action);
        }
        return false; });

    // Register cursor handler for hover state (@mouseenter/@mouseleave, etc.). Never consumes.
    m_uiCursorHandlerId = m_windowManager->RegisterInputHandler([this](const InputEvent &event)
                                                                {
        if (event.type == InputTypes::Cursor && std::holds_alternative<glm::vec2>(event.input))
        {
            auto pos = std::get<glm::vec2>(event.input);
            m_htmlRenderer->UpdateHoverState(pos.x, pos.y);
        }
        return false; });

    LOG_INFO("INIT - ReactiveUI: SUCCESS (click handler ID={})", m_uiClickHandlerId);

    return true;
}

double EngineCore::BeginFrame()
{
    // Calculate delta time
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration_cast<std::chrono::duration<double>>(
                      currentTime - m_lastFrameTime)
                      .count();
    m_lastFrameTime = currentTime;

    // Update FPS counter (every second)
    m_frameCount++;
    auto timeSinceFPSUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  currentTime - m_fpsUpdateTime)
                                  .count();

    if (timeSinceFPSUpdate >= 1000)
    {
        m_currentFPS = m_frameCount * 1000.0 / timeSinceFPSUpdate;
        m_frameCount = 0;
        m_fpsUpdateTime = currentTime;
    }

    // Poll input events
    glfwPollEvents();

    // Clear buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    return m_deltaTime;
}

void EngineCore::EndFrame()
{
    if (m_windowManager && m_windowManager->window)
    {
        glfwSwapBuffers(m_windowManager->window);
    }
}

bool EngineCore::ShouldClose() const
{
    if (!m_windowManager || !m_windowManager->window)
    {
        return true;
    }
    return glfwWindowShouldClose(m_windowManager->window);
}

void EngineCore::ShowSplash()
{
    if (!m_windowInitialized || !m_windowManager)
        return;

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(m_windowManager->window, &fbWidth, &fbHeight);

    m_splashScreen = std::make_unique<SplashScreen>();
    if (m_splashScreen->Initialize(m_resourcePath + "textures/splash.png", fbWidth, fbHeight))
    {
        // Render first frame immediately and swap so the splash is visible
        m_splashScreen->Render();
        glfwSwapBuffers(m_windowManager->window);
        LOG_INFO("[EngineCore] Splash screen displayed");
    }
    else
    {
        // Non-fatal: splash is optional
        m_splashScreen.reset();
        LOG_WARNING("[EngineCore] Splash screen unavailable, continuing without it");
    }
}

void EngineCore::RenderSplash()
{
    if (m_splashScreen && m_splashScreen->IsReady())
    {
        m_splashScreen->Render();
    }
}

bool EngineCore::IsSplashActive() const
{
    return m_splashScreen && m_splashScreen->IsReady();
}

void EngineCore::DismissSplash()
{
    if (m_splashScreen)
    {
        m_splashScreen->Shutdown();
        m_splashScreen.reset();
        LOG_INFO("[EngineCore] Splash screen dismissed");
    }
}
