/**
 * @file Game.cpp
 * @brief Main game loop implementation (VaporQube) using EngineCore
 * @lines ~285
 *
 * Purpose: Implements core game loop with fixed/variable timestep options.
 * Coordinates rendering, input processing, and Lua script updates.
 *
 * Key functions:
 * - Initialize() - Setup window, renderer, scene, UI (line 21, ~50 lines)
 * - Run() - Main loop dispatch to fixed or variable (line 70, ~15 lines)
 * - RunFixedLoop() - Fixed 60 FPS game loop (line 84, ~40 lines)
 * - RunVariableLoop() - Variable timestep game loop (line 126, ~50 lines)
 * - Render() - Coordinate 3D + UI rendering (line 179, ~15 lines)
 * - TrackFPS() - FPS counter and display (line 197, ~75 lines)
 * - Shutdown() - Cleanup resources (line 279, ~10 lines)
 *
 * Game loop modes:
 * - FIXED: 60 FPS locked, consistent physics timestep
 * - VARIABLE: Uncapped FPS, delta time per frame
 * - Mode selected via Lua state: data.gameMode
 *
 * Integration: Uses EngineCore for shared initialization, coordinates all systems
 */

#include "controllers/Game.h"
#include "controllers/EngineCore.h"
#include "systems/HierarchySystem.h"
#include "systems/RenderSystem.h"
#include "systems/ScriptSystem.h"
#include "systems/SkyBackgroundRenderer.h"
#include "systems/TweenSystem.h"
#include "systems/TiledBackgroundRenderer.h"
#include "util/TransformUtils.h"
#include "util/Logger.h"
#include "util/ConfigLoader.h"
#include "util/FrameTiming.h"
#include <chrono>
#include <thread>

bool Game::Initialize(const std::string& configPath)
{
    std::cout << "[Game] Starting initialization..." << std::endl;

    // Load config and initialize EngineCore
    if (!m_core.Initialize(configPath, conf, &cam))
    {
        return false;
    }

    // Get references to subsystems
    windowManager = m_core.GetWindowManager();
    htmlRenderer = m_core.GetHTMLRenderer();
    registry = m_core.GetRegistry();
    scriptManager = m_core.GetScriptManager();

    // Hold splash until HTML renderer completes first frame
    if (m_core.IsSplashActive())
    {
        double minSplashSeconds = m_testMode ? 0.0 : 1.0;
        auto splashStart = std::chrono::high_resolution_clock::now();

        LOG_INFO("[Game] Holding splash (min {:.1f}s, testMode={})", minSplashSeconds, m_testMode);

        while (!m_core.ShouldClose())
        {
            glfwPollEvents();

            auto elapsed = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - splashStart).count();

            bool rendererReady = htmlRenderer->HasCompletedFirstRender();
            bool timeReady = elapsed >= minSplashSeconds;

            if (rendererReady && timeReady)
            {
                LOG_INFO("[Game] Splash hold complete ({:.2f}s, rendererReady={})", elapsed, rendererReady);
                break;
            }

            m_core.RenderSplash();
            glfwSwapBuffers(windowManager->window);

            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        m_core.DismissSplash();
    }

    // Initialize FPS tracking
    m_fpsUpdateTime = std::chrono::high_resolution_clock::now();

    // Store Game instance in GLFW user pointer for resize callback
    glfwSetWindowUserPointer(windowManager->window, this);

    LOG_INFO("Game initialization complete");
    return true;
}

float Game::GetSimulationMultiplier() const
{
    switch (m_simSpeed)
    {
    case SimulationSpeed::PAUSED:
        return 0.0f;
    case SimulationSpeed::SLOW:
        return 0.5f;
    case SimulationSpeed::NORMAL:
        return 1.0f;
    case SimulationSpeed::FAST:
        return 2.0f;
    case SimulationSpeed::FASTER:
        return 3.0f;
    case SimulationSpeed::FASTEST:
        return 5.0f;
    case SimulationSpeed::UNCAPPED:
        return -1.0f; // Special: run as fast as possible
    default:
        return 1.0f;
    }
}

void Game::Run()
{
    LOG_INFO("Starting main loop in {} mode", m_gameMode == GameMode::FIXED ? "FIXED" : "VARIABLE");

    if (m_gameMode == GameMode::VARIABLE)
    {
        RunVariableLoop();
    }
    else
    {
        RunFixedLoop();
    }
}

void Game::RunFrames(int count)
{
    LOG_INFO("Running {} smoke-test frames", count);

    FrameTiming timing(FrameTimingMode::FIXED, conf.targetFPS);
    glfwSwapInterval(0);

    for (int i = 0; i < count && !m_core.ShouldClose(); i++)
    {
        glfwPollEvents();
        timing.Update();
        delta = timing.GetDelta();

        scriptManager->ProcessInput();

        while (timing.ShouldUpdateFixedStep())
        {
            ScriptSystem::Update(timing.GetFixedDelta());
        }

        TweenSystem::Update(delta);
        HierarchySystem::Update();
        Render();
        m_core.EndFrame();
    }

    LOG_INFO("Smoke test complete ({} frames)", count);
}

bool Game::RunClickTest()
{
    LOG_INFO("[ClickTest] Starting click event integration test");

    FrameTiming timing(FrameTimingMode::FIXED, conf.targetFPS);
    glfwSwapInterval(0);

    // Phase 1: Run frames until interactive elements are populated
    int x, y, w, h;
    bool found = false;
    const int maxWarmupFrames = 60;

    for (int i = 0; i < maxWarmupFrames && !m_core.ShouldClose(); i++)
    {
        glfwPollEvents();
        timing.Update();
        delta = timing.GetDelta();
        scriptManager->ProcessInput();

        while (timing.ShouldUpdateFixedStep())
        {
            ScriptSystem::Update(timing.GetFixedDelta());
        }

        TweenSystem::Update(delta);
        HierarchySystem::Update();
        Render();
        m_core.EndFrame();

        // Check if START GAME button is available
        if (htmlRenderer->TryFindInteractiveElementBoundsByHandler("click", "onStartGame", x, y, w, h))
        {
            found = true;
            LOG_INFO("[ClickTest] Found 'onStartGame' button after {} frames: ({},{}) {}x{}", i + 1, x, y, w, h);
            break;
        }
    }

    if (!found)
    {
        LOG_ERROR("[ClickTest] FAIL: 'onStartGame' button not found after {} frames", maxWarmupFrames);
        return false;
    }

    // Phase 2: Validate bounding box is reasonable
    if (w <= 0 || h <= 0)
    {
        LOG_ERROR("[ClickTest] FAIL: Button has invalid dimensions: {}x{}", w, h);
        return false;
    }

    LOG_INFO("[ClickTest] PASS: Button bounds are valid ({}x{})", w, h);

    // Phase 3: Verify pre-state (game not started)
    ReactiveUI &reactiveUI = ReactiveUI::GetInstance();
    auto luaState = reactiveUI.GetLuaState();
    if (!luaState)
    {
        LOG_ERROR("[ClickTest] FAIL: LuaUIState is null");
        return false;
    }

    sol::object preValue = luaState->GetValue("data.gameStarted");
    if (preValue.valid() && preValue.is<bool>() && preValue.as<bool>() == true)
    {
        LOG_ERROR("[ClickTest] FAIL: data.gameStarted was already true before click");
        return false;
    }

    LOG_INFO("[ClickTest] PASS: data.gameStarted is false before click");

    // Phase 4: Simulate click at center of button
    // TryFindInteractiveElementBoundsByHandler returns framebuffer coords.
    // HandleClickEvent expects window coords and converts internally.
    // Reverse the framebuffer→window scale to get correct window coords.
    int winW, winH, fbW, fbH;
    glfwGetWindowSize(windowManager->window, &winW, &winH);
    glfwGetFramebufferSize(windowManager->window, &fbW, &fbH);

    float centerFbX = x + w / 2.0f;
    float centerFbY = y + h / 2.0f;
    float windowX = centerFbX * static_cast<float>(winW) / static_cast<float>(fbW);
    float windowY = centerFbY * static_cast<float>(winH) / static_cast<float>(fbH);

    LOG_INFO("[ClickTest] Clicking at window ({:.0f},{:.0f}) → fb ({:.0f},{:.0f})", windowX, windowY, centerFbX, centerFbY);

    bool handled = htmlRenderer->HandleClickEvent(windowX, windowY, 0);
    if (!handled)
    {
        LOG_ERROR("[ClickTest] FAIL: HandleClickEvent returned false (click not consumed)");
        return false;
    }

    LOG_INFO("[ClickTest] PASS: Click was handled by an interactive element");

    // Phase 5: Run a few more frames to let state propagate
    for (int i = 0; i < 5 && !m_core.ShouldClose(); i++)
    {
        glfwPollEvents();
        timing.Update();
        delta = timing.GetDelta();
        scriptManager->ProcessInput();

        while (timing.ShouldUpdateFixedStep())
        {
            ScriptSystem::Update(timing.GetFixedDelta());
        }

        TweenSystem::Update(delta);
        HierarchySystem::Update();
        Render();
        m_core.EndFrame();
    }

    // Phase 6: Verify post-state (game started)
    sol::object postValue = luaState->GetValue("data.gameStarted");
    if (!postValue.valid() || !postValue.is<bool>() || postValue.as<bool>() != true)
    {
        LOG_ERROR("[ClickTest] FAIL: data.gameStarted is not true after click");
        return false;
    }

    LOG_INFO("[ClickTest] PASS: data.gameStarted is true after click");
    LOG_INFO("[ClickTest] All assertions passed");
    return true;
}

void Game::RunFixedLoop()
{
    LOG_INFO("Starting FIXED loop (60 FPS)");

    // Initialize frame timing with fixed timestep
    FrameTiming timing(FrameTimingMode::FIXED, conf.targetFPS);
    timing.SetVSync(true); // Enable VSync for fixed-speed games
    glfwSwapInterval(1);

    while (!m_core.ShouldClose())
    {
        // CRITICAL: Poll events first so window appears and responds
        glfwPollEvents();

        // Update frame timing (calculates delta and accumulators)
        timing.Update();
        delta = timing.GetDelta();

        // Process input
        scriptManager->ProcessInput();

        // Update game logic at fixed timestep
        while (timing.ShouldUpdateFixedStep())
        {
            ScriptSystem::Update(timing.GetFixedDelta());
        }

        // Update animation and hierarchy
        TweenSystem::Update(delta);
        HierarchySystem::Update();

        // Render (every frame in fixed mode)
        Render();
        m_core.EndFrame();

        // Track FPS for UI display
        TrackFPS();
    }

    LOG_INFO("Exited FIXED main loop");
}

void Game::RunVariableLoop()
{
    LOG_INFO("Starting VARIABLE loop (decoupled sim/render)");

    // Initialize frame timing with variable speed (60 FPS sim, conf.targetFPS render)
    FrameTiming timing(FrameTimingMode::VARIABLE, 60.0, conf.targetFPS);
    timing.SetVSync(false); // We control frame timing
    glfwSwapInterval(0);

    while (!m_core.ShouldClose())
    {
        // CRITICAL: Poll events first so window appears and responds
        glfwPollEvents();

        // Update frame timing (calculates delta and accumulators)
        timing.Update();
        delta = timing.GetDelta();

        // Sync simulation speed with current setting
        timing.SetSimulationMultiplier(GetSimulationMultiplier());

        // Process input
        scriptManager->ProcessInput();

        // Update game logic at variable speed
        while (timing.ShouldUpdateSimulation())
        {
            ScriptSystem::Update(timing.GetFixedDelta());
        }

        // Update animation and hierarchy
        TweenSystem::Update(delta);
        HierarchySystem::Update();

        // Render at capped framerate (decoupled from simulation)
        if (timing.ShouldRenderFrame())
        {
            Render();
            m_core.EndFrame();
            TrackFPS();
        }

        // Sleep to avoid spinning CPU
        auto frameElapsed = timing.GetFrameElapsedMS();
        if (frameElapsed < 1.0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    LOG_INFO("Exited VARIABLE main loop");
}

void Game::Render()
{
    // Ensure window scaling is up to date
    int width, height;
    glfwGetFramebufferSize(windowManager->window, &width, &height);
    glViewport(0, 0, width, height);

    // 0. Clear (sky is rendered as a fullscreen pass if enabled)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 0.25 Sky background (optional)
    SkyBackgroundRenderer::GetInstance().Render(cam, static_cast<float>(delta));

    // 0.5 Procedural tiled background (optional)
    TiledBackgroundRenderer::GetInstance().Render(cam, static_cast<float>(delta));

    // 1. Game Objects
    RenderSystem::Update(cam, delta);

    // 2. UI
    htmlRenderer->Render();
}

void Game::TrackFPS()
{
    m_frameCount++;
    auto now = std::chrono::high_resolution_clock::now();
    m_fpsTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_fpsUpdateTime)
                    .count();

    // Update immediately after first 10 frames (to replace initial "0" values quickly)
    // Then update every second thereafter
    if ((m_frameCount == 10 && m_fpsTime < 1000.0) || m_fpsTime >= 1000.0)
    {
        int currentFPS = (int)(m_frameCount / (m_fpsTime / 1000.0));
        double currentFrameTime = m_fpsTime / m_frameCount;

        // Get reactive UI and Lua state
        ReactiveUI &reactiveUI = ReactiveUI::GetInstance();
        auto luaState = reactiveUI.GetLuaState();

        if (luaState)
        {
            // Update Lua state values - LuaUIState handles dirty flag
            luaState->SetValue("data.fps", currentFPS);
            luaState->SetValue("data.frameTime", std::to_string(currentFrameTime).substr(0, 5));

            // BackgroundTiles stats (updated by renderer during Render())
            const auto bgStats = TiledBackgroundRenderer::GetInstance().GetLastStats();
            luaState->SetValue("data.bg_tilesSelected", bgStats.selectedTiles);
            luaState->SetValue("data.bg_tilesCandidates", bgStats.visibleCandidates);
            luaState->SetValue("data.bg_cacheResident", bgStats.residentTiles);
            luaState->SetValue("data.bg_uploads", bgStats.uploadsThisFrame);
            luaState->SetValue("data.bg_pending", bgStats.pendingUploads);
            luaState->SetValue("data.bg_cacheHits", bgStats.cacheHits);
            luaState->SetValue("data.bg_cacheMisses", bgStats.cacheMisses);
            luaState->SetValue("data.bg_evictions", bgStats.evictions);

            // Update game mode and speed info
            const char *modeName = (m_gameMode == GameMode::FIXED) ? "FIXED" : "VARIABLE";
            luaState->SetValue("data.gameMode", std::string(modeName));

            if (m_gameMode == GameMode::VARIABLE)
            {
                // Show speed info for variable mode
                const char *speedName;
                switch (m_simSpeed)
                {
                case SimulationSpeed::PAUSED:
                    speedName = "PAUSED";
                    break;
                case SimulationSpeed::SLOW:
                    speedName = "SLOW";
                    break;
                case SimulationSpeed::NORMAL:
                    speedName = "NORMAL";
                    break;
                case SimulationSpeed::FAST:
                    speedName = "FAST";
                    break;
                case SimulationSpeed::FASTER:
                    speedName = "FASTER";
                    break;
                case SimulationSpeed::FASTEST:
                    speedName = "FASTEST";
                    break;
                case SimulationSpeed::UNCAPPED:
                    speedName = "UNCAPPED";
                    break;
                default:
                    speedName = "UNKNOWN";
                    break;
                }

                float multiplier = GetSimulationMultiplier();
                std::string multiplierStr = (multiplier == -1.0f) ? "MAX" : std::to_string(multiplier).substr(0, 3) + "x";

                luaState->SetValue("data.simSpeed", std::string(speedName));
                luaState->SetValue("data.simMultiplier", multiplierStr);
            }

            // GetRenderedHTML only re-renders if Lua state is dirty
            htmlRenderer->UpdateHTML(reactiveUI.GetRenderedHTML());
        }

        m_frameCount = 0;
        m_fpsUpdateTime = now;
    }
}

void Game::CloseWindow()
{
    glfwSetWindowShouldClose(windowManager->window, true);
}

void Game::Shutdown()
{
    LOG_INFO("[Game] Shutting down");

    m_core.Shutdown();
    LOG_INFO("[Game] Shutdown complete");
}
