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
#include "systems/ReactiveUI.h"
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
#include <iostream>
#include <filesystem>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <set>
#include <limits>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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

bool Game::RunVaporQubeScenarioTest(std::uint32_t seed, const std::string &captureDirectory,
                                   const std::vector<int> &captureTicks, int captureVideoFrames)
{
    if (!m_testMode || !scriptManager || !windowManager || !windowManager->window)
    {
        LOG_ERROR("[ScenarioTest] Requires initialized Game test mode and a live window");
        std::cerr << "[ScenarioTest] FAIL: initialized test-mode window is required\n";
        return false;
    }

    sol::state &lua = scriptManager->GetLuaState();
    sol::table math = lua["math"];
    sol::protected_function randomSeed = math["randomseed"];
    sol::protected_function_result seedResult = randomSeed(static_cast<lua_Integer>(seed));
    if (!seedResult.valid())
    {
        LOG_ERROR("[ScenarioTest] Could not set Lua random seed {}", seed);
        std::cerr << "[ScenarioTest] FAIL: could not set the Lua random seed\n";
        return false;
    }

    sol::table sceneModules = lua["SceneModules"];
    sol::table game = sceneModules["game"];
    sol::table grid = lua["GameGrid"];
    if (!game.valid() || !grid.valid())
    {
        LOG_ERROR("[ScenarioTest] VaporQube game or grid script is not loaded");
        std::cerr << "[ScenarioTest] FAIL: VaporQube game/grid scripts are unavailable\n";
        return false;
    }

    struct TestInputOverrideGuard
    {
        WindowManager &window;
        explicit TestInputOverrideGuard(WindowManager &manager) : window(manager)
        {
            window.EnableTestInputOverrides(true);
        }
        ~TestInputOverrideGuard()
        {
            window.EnableTestInputOverrides(false);
        }
    } inputOverride(*windowManager);

    constexpr float simulationStepMs = 1000.0f / 60.0f;
    int tick = 0;
    std::string captureError;
    std::filesystem::path resolvedCaptureDirectory;
    std::set<int> requestedCaptureTicks;
    std::set<int> capturedTicks;
    int capturedVideoFrames = 0;
    std::size_t totalCaptureBytes = 0;
    int acknowledgedUIScore = -1;
    std::uint64_t acknowledgedUIRevision = 0;
    constexpr std::size_t maxCaptureCount = 4;
    constexpr std::size_t maxCaptureBytes = 16 * 1024 * 1024;
    constexpr std::size_t maxTotalCaptureBytes = 64 * 1024 * 1024;
    constexpr std::size_t maxVideoBytes = 128 * 1024 * 1024;
    auto fail = [&](const std::string &message)
    {
        const std::string detail = captureError.empty() ? message : "capture failed: " + captureError;
        LOG_ERROR("[ScenarioTest] FAIL seed={} tick={}: {}", seed, tick, detail);
        std::cerr << "[ScenarioTest] FAIL seed=" << seed << " tick=" << tick << ": " << detail << '\n';
        return false;
    };
    const bool captureRequested = !captureTicks.empty() || captureVideoFrames > 0;
    if ((!captureDirectory.empty()) != captureRequested || captureTicks.size() > maxCaptureCount ||
        captureVideoFrames < 0 || captureVideoFrames > 300 ||
        (!captureTicks.empty() && captureVideoFrames > 0))
    {
        return fail("capture requires an existing output directory and either up to four selected ticks or up to 300 video frames");
    }
    if (!captureDirectory.empty())
    {
        std::error_code error;
        resolvedCaptureDirectory = std::filesystem::canonical(captureDirectory, error);
        if (error || !std::filesystem::is_directory(resolvedCaptureDirectory))
        {
            return fail("capture output directory does not exist or cannot be resolved");
        }
        for (int captureTick : captureTicks)
        {
            if (captureTick < 1 || captureTick > 5 || !requestedCaptureTicks.insert(captureTick).second)
            {
                return fail("capture ticks must be unique values from 1 through 5");
            }
        }
    }

    auto synchronizeCaptureUIAtTick = [&](int captureTick)
    {
        if (requestedCaptureTicks.find(captureTick) == requestedCaptureTicks.end() &&
            !(captureVideoFrames > 0 && captureTick <= captureVideoFrames))
        {
            return true;
        }
        auto uiState = ReactiveUI::GetInstance().GetLuaState();
        if (!uiState)
        {
            captureError = "reactive UI state is unavailable";
            return false;
        }
        const int expectedScore = grid["score"].get<int>();
        const sol::object scoreValue = uiState->GetValue("data.score");
        const sol::object startedValue = uiState->GetValue("data.gameStarted");
        if (!scoreValue.valid() || !scoreValue.is<int>() || scoreValue.as<int>() != expectedScore ||
            !startedValue.valid() || !startedValue.is<bool>() || !startedValue.as<bool>())
        {
            captureError = "HUD state does not match the running game and current score";
            return false;
        }

        const std::string renderedHTML = ReactiveUI::GetInstance().GetRenderedHTML();
        const std::size_t scoreLabel = renderedHTML.find("SCORE</span>");
        const std::size_t scoreValueTag = scoreLabel == std::string::npos ? std::string::npos :
            renderedHTML.find("class=\"stat-value\">", scoreLabel);
        const std::size_t scoreValueTagEnd = scoreValueTag == std::string::npos ? std::string::npos :
            renderedHTML.find('>', scoreValueTag);
        const std::size_t scoreStart = scoreValueTagEnd == std::string::npos ? std::string::npos :
            scoreValueTagEnd + 1;
        const std::size_t scoreEnd = scoreStart == std::string::npos ? std::string::npos :
            renderedHTML.find("</span>", scoreStart);
        if (scoreEnd == std::string::npos)
        {
            captureError = "rendered HUD has no score value element";
            return false;
        }
        std::string renderedScore = renderedHTML.substr(scoreStart, scoreEnd - scoreStart);
        renderedScore.erase(std::remove_if(renderedScore.begin(), renderedScore.end(),
                                           [](unsigned char character) { return std::isspace(character); }),
                            renderedScore.end());
        if (renderedScore != std::to_string(expectedScore) ||
            renderedHTML.find("START GAME") != std::string::npos)
        {
            captureError = "rendered HUD HTML does not show the current score in the running-game view";
            return false;
        }

        acknowledgedUIRevision = htmlRenderer->LoadHTMLAndGetRevision(renderedHTML);
        if (!htmlRenderer->WaitForHTMLRevision(acknowledgedUIRevision, std::chrono::milliseconds(2000)))
        {
            captureError = "UI render thread did not acknowledge the requested frame within 2000 ms";
            return false;
        }
        acknowledgedUIScore = expectedScore;
        return true;
    };

    auto captureFramebufferAtTick = [&](int captureTick)
    {
        if (requestedCaptureTicks.find(captureTick) == requestedCaptureTicks.end())
        {
            return true;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(windowManager->window, &width, &height);
        if (width <= 0 || height <= 0 ||
            static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() /
                                                   static_cast<std::size_t>(height) / 3)
        {
            captureError = "framebuffer dimensions are invalid";
            return false;
        }
        const std::size_t pixelBytes = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height) * 3;
        constexpr std::size_t ppmHeaderAllowance = 64;
        if (pixelBytes > maxCaptureBytes - ppmHeaderAllowance ||
            pixelBytes + ppmHeaderAllowance > maxTotalCaptureBytes - totalCaptureBytes)
        {
            captureError = "requested captures exceed the 16 MiB per-image or 64 MiB total limit";
            return false;
        }

        const std::string filename = "scenario-seed-" + std::to_string(seed) +
                                     "-tick-" + std::to_string(captureTick) + ".ppm";
        const std::filesystem::path outputPath = resolvedCaptureDirectory / filename;
        std::error_code filesystemError;
        if (std::filesystem::exists(outputPath, filesystemError) || filesystemError)
        {
            captureError = "capture destination already exists or cannot be checked: " + outputPath.string();
            return false;
        }

        GLint previousReadBuffer = GL_BACK;
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        while (glGetError() != GL_NO_ERROR)
        {
        }
        std::vector<unsigned char> pixels(pixelBytes);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        const GLenum readError = glGetError();
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        glReadBuffer(static_cast<GLenum>(previousReadBuffer));
        if (readError != GL_NO_ERROR)
        {
            captureError = "OpenGL glReadPixels failed with error " + std::to_string(readError);
            return false;
        }

        std::size_t nonBlackPixels = 0;
        const unsigned char firstRed = pixels[0];
        const unsigned char firstGreen = pixels[1];
        const unsigned char firstBlue = pixels[2];
        bool hasColorVariation = false;
        for (std::size_t index = 0; index < pixelBytes; index += 3)
        {
            const unsigned char red = pixels[index];
            const unsigned char green = pixels[index + 1];
            const unsigned char blue = pixels[index + 2];
            if (red != 0 || green != 0 || blue != 0)
            {
                ++nonBlackPixels;
            }
            if (red != firstRed || green != firstGreen || blue != firstBlue)
            {
                hasColorVariation = true;
            }
        }
        if (nonBlackPixels < 100 || !hasColorVariation)
        {
            captureError = "captured framebuffer is blank or has no color variation";
            return false;
        }

        const std::string outputFilename = outputPath.string();
        std::FILE *output = std::fopen(outputFilename.c_str(), "wbx");
        if (!output)
        {
            captureError = "cannot create capture file without overwriting: " + outputPath.string();
            return false;
        }
        const std::string header = "P6\n" + std::to_string(width) + " " +
                                   std::to_string(height) + "\n255\n";
        bool writeSucceeded = std::fwrite(header.data(), 1, header.size(), output) == header.size();
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
        for (int row = height - 1; writeSucceeded && row >= 0; --row)
        {
            writeSucceeded = std::fwrite(pixels.data() + static_cast<std::size_t>(row) * rowBytes,
                                         1, rowBytes, output) == rowBytes;
        }
        if (std::fclose(output) != 0)
        {
            writeSucceeded = false;
        }
        if (!writeSucceeded)
        {
            std::filesystem::remove(outputPath, filesystemError);
            captureError = "failed while writing capture file: " + outputPath.string();
            return false;
        }

        totalCaptureBytes += pixelBytes + ppmHeaderAllowance;
        capturedTicks.insert(captureTick);
        std::cout << "[ScenarioCapture] path=" << outputPath.string() << " width=" << width
                  << " height=" << height << " format=PPM-P6 readback=GL_BACK-pre-swap nonBlackPixels="
                  << nonBlackPixels << " colorVariation=1 bytes=" << pixelBytes
                  << " uiScore=" << acknowledgedUIScore << " uiRevision=" << acknowledgedUIRevision << '\n';
        return true;
    };
    auto captureVideoFrameAtTick = [&](int captureTick)
    {
        if (captureVideoFrames == 0 || captureTick > captureVideoFrames)
        {
            return true;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(windowManager->window, &width, &height);
        if (width <= 0 || height <= 0 ||
            static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() /
                                                   static_cast<std::size_t>(height) / 3)
        {
            captureError = "video framebuffer dimensions are invalid";
            return false;
        }
        const std::size_t pixelBytes = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height) * 3;
        if (pixelBytes > maxCaptureBytes)
        {
            captureError = "video frames exceed the 16 MiB per-frame readback limit";
            return false;
        }

        GLint previousReadBuffer = GL_BACK;
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        while (glGetError() != GL_NO_ERROR)
        {
        }
        std::vector<unsigned char> pixels(pixelBytes);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        const GLenum readError = glGetError();
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        glReadBuffer(static_cast<GLenum>(previousReadBuffer));
        if (readError != GL_NO_ERROR)
        {
            captureError = "video glReadPixels failed with error " + std::to_string(readError);
            return false;
        }

        const int videoWidth = width / 2;
        const int videoHeight = height / 2;
        if (videoWidth <= 0 || videoHeight <= 0)
        {
            captureError = "video framebuffer is too small to downsample";
            return false;
        }
        std::vector<unsigned char> downsampled(static_cast<std::size_t>(videoWidth) *
                                               static_cast<std::size_t>(videoHeight) * 3);
        for (int y = 0; y < videoHeight; ++y)
        {
            for (int x = 0; x < videoWidth; ++x)
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    unsigned int sum = 0;
                    for (int row = 0; row < 2; ++row)
                    {
                        for (int column = 0; column < 2; ++column)
                        {
                            const int sourceX = x * 2 + column;
                            const int sourceY = height - 1 - (y * 2 + row);
                            const std::size_t sourceIndex =
                                (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(sourceX)) * 3 + static_cast<std::size_t>(channel);
                            sum += pixels[sourceIndex];
                        }
                    }
                    const std::size_t destinationIndex =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(videoWidth) +
                         static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(channel);
                    downsampled[destinationIndex] = static_cast<unsigned char>(sum / 4);
                }
            }
        }
        std::vector<unsigned char> jpeg;
        auto appendJpeg = [](void *context, void *data, int size)
        {
            auto *output = static_cast<std::vector<unsigned char> *>(context);
            const auto *bytes = static_cast<const unsigned char *>(data);
            output->insert(output->end(), bytes, bytes + size);
        };
        if (!stbi_write_jpg_to_func(appendJpeg, &jpeg, videoWidth, videoHeight, 3,
                                    downsampled.data(), 85) || jpeg.empty())
        {
            captureError = "could not encode video frame as JPEG";
            return false;
        }
        if (jpeg.size() > maxVideoBytes - totalCaptureBytes)
        {
            captureError = "video capture exceeds the 128 MiB output limit";
            return false;
        }

        const std::string filename = "scenario-seed-" + std::to_string(seed) + "-frame-";
        const std::string frameNumber = std::to_string(captureTick);
        const std::filesystem::path outputPath = resolvedCaptureDirectory /
            (filename + std::string(4 - std::min<std::size_t>(4, frameNumber.size()), '0') +
             frameNumber + ".jpg");
        std::error_code filesystemError;
        if (std::filesystem::exists(outputPath, filesystemError) || filesystemError)
        {
            captureError = "video frame destination already exists or cannot be checked: " + outputPath.string();
            return false;
        }
        std::FILE *output = std::fopen(outputPath.string().c_str(), "wbx");
        if (!output)
        {
            captureError = "cannot create video frame without overwriting: " + outputPath.string();
            return false;
        }
        const bool writeSucceeded = std::fwrite(jpeg.data(), 1, jpeg.size(), output) == jpeg.size();
        if (std::fclose(output) != 0 || !writeSucceeded)
        {
            std::filesystem::remove(outputPath, filesystemError);
            captureError = "failed while writing video frame: " + outputPath.string();
            return false;
        }
        totalCaptureBytes += jpeg.size();
        ++capturedVideoFrames;
        return true;
    };
    auto refreshCaptureBackgroundStats = [&]()
    {
        const auto stats = TiledBackgroundRenderer::GetInstance().GetLastStats();
        if (stats.pendingUploads != 0 || stats.residentTiles < stats.selectedTiles)
        {
            captureError = "gameplay-camera background cache became incomplete before capture";
            return false;
        }
        auto uiState = ReactiveUI::GetInstance().GetLuaState();
        if (!uiState)
        {
            captureError = "reactive UI state is unavailable while refreshing background telemetry";
            return false;
        }
        uiState->SetValue("data.bg_tilesSelected", stats.selectedTiles);
        uiState->SetValue("data.bg_tilesCandidates", stats.visibleCandidates);
        uiState->SetValue("data.bg_cacheResident", stats.residentTiles);
        uiState->SetValue("data.bg_uploads", stats.uploadsThisFrame);
        uiState->SetValue("data.bg_pending", stats.pendingUploads);
        uiState->SetValue("data.bg_cacheHits", stats.cacheHits);
        uiState->SetValue("data.bg_cacheMisses", stats.cacheMisses);
        uiState->SetValue("data.bg_evictions", stats.evictions);
        const auto revision = htmlRenderer->LoadHTMLAndGetRevision(ReactiveUI::GetInstance().GetRenderedHTML());
        if (!htmlRenderer->WaitForHTMLRevision(revision, std::chrono::milliseconds(2000)))
        {
            captureError = "UI render thread did not acknowledge refreshed background statistics";
            return false;
        }
        Render();
        return true;
    };
    auto queueKey = [&](const std::string &key)
    {
        InputEvent event;
        event.type = InputTypes::Key;
        event.input = key;
        event.action = GLFW_PRESS;
        scriptManager->AddInputEventToQueue(event);
    };
    auto step = [&](bool advanceScenarioTick = true)
    {
        if (m_core.ShouldClose())
        {
            return false;
        }
        glfwPollEvents();
        if (m_core.ShouldClose())
        {
            return false;
        }

        scriptManager->ProcessInput();
        delta = simulationStepMs;
        ScriptSystem::Update(simulationStepMs);
        TweenSystem::Update(simulationStepMs);
        HierarchySystem::Update();
        if (advanceScenarioTick)
        {
            ++tick;
            if (!synchronizeCaptureUIAtTick(tick))
            {
                return false;
            }
        }
        Render();
        const bool captureThisTick = advanceScenarioTick &&
            (requestedCaptureTicks.find(tick) != requestedCaptureTicks.end() ||
             (captureVideoFrames > 0 && tick <= captureVideoFrames));
        if (captureThisTick && !refreshCaptureBackgroundStats())
        {
            return false;
        }
        if (advanceScenarioTick &&
            (!captureFramebufferAtTick(tick) || !captureVideoFrameAtTick(tick)))
        {
            return false;
        }
        m_core.EndFrame();
        return true;
    };
    auto activePiece = [&]() -> sol::table
    {
        sol::object active = grid["activeTetrimino"];
        return active.valid() && active.get_type() == sol::type::table ? active.as<sol::table>() : sol::table();
    };
    auto positionOf = [&](const sol::table &piece, glm::vec2 &position)
    {
        sol::protected_function getPosition = piece["getGridPosition"];
        if (!getPosition.valid())
        {
            return false;
        }
        sol::protected_function_result result = getPosition(piece);
        if (!result.valid())
        {
            return false;
        }
        position = result.get<glm::vec2>();
        return true;
    };
    auto waitBeforeVideoAction = [&]()
    {
        constexpr int pauseTicks = 45;
        for (int pauseTick = 0; pauseTick < pauseTicks; ++pauseTick)
        {
            if (!step())
            {
                return false;
            }
        }
        return true;
    };

    LOG_INFO("[ScenarioTest] Starting VaporQube start/move/rotate/drop scenario (seed={}, 5 fixed ticks)", seed);
    queueKey("ENTER");
    // ENTER establishes the gameplay camera in GameGrid.setupPlayfield(). For captures,
    // render that state once without consuming a scenario tick, then warm the cache for
    // the resulting camera before any frame is recorded.
    if (!step(!captureRequested))
    {
        return fail("window closed before game start");
    }
    if (!game["isStarted"].get<bool>())
    {
        return fail("ENTER did not start the game");
    }

    sol::table piece = activePiece();
    glm::vec2 initialPosition(0.0f);
    if (!piece.valid() || !positionOf(piece, initialPosition))
    {
        return fail("starting the game did not create a readable active piece");
    }

    if (captureRequested)
    {
        bool backgroundReady = false;
        constexpr int maxBackgroundWarmupFrames = 30;
        for (int warmupFrame = 0; warmupFrame < maxBackgroundWarmupFrames; ++warmupFrame)
        {
            if (m_core.ShouldClose())
            {
                return fail("window closed while preparing the gameplay background for capture");
            }
            glfwPollEvents();
            if (m_core.ShouldClose())
            {
                return fail("window closed while preparing the gameplay background for capture");
            }

            delta = simulationStepMs;
            Render();
            m_core.EndFrame();

            const auto stats = TiledBackgroundRenderer::GetInstance().GetLastStats();
            if (stats.pendingUploads == 0 && stats.residentTiles >= stats.selectedTiles)
            {
                backgroundReady = true;
                break;
            }
        }
        if (!backgroundReady)
        {
            captureError = "gameplay-camera background tile cache did not finish uploading within 30 warm-up frames";
            return fail("cannot capture before the procedural background is ready");
        }
    }

    if (captureVideoFrames > 0 && !waitBeforeVideoAction())
    {
        return fail("window closed while waiting before held movement");
    }

    windowManager->SetTestKeyPressed("RIGHT", true);
    if (!step())
    {
        return fail("window closed during held movement");
    }
    windowManager->SetTestKeyPressed("RIGHT", false);
    if (!step())
    {
        return fail("window closed after held movement");
    }

    glm::vec2 movedPosition(0.0f);
    if (!positionOf(piece, movedPosition) || movedPosition.x != initialPosition.x + 1.0f)
    {
        return fail("held RIGHT polling did not move the active piece exactly one column");
    }

    if (captureVideoFrames > 0 && !waitBeforeVideoAction())
    {
        return fail("window closed while waiting before rotation");
    }

    const int initialRotation = piece["rotationState"].get<int>();
    queueKey("X");
    if (!step())
    {
        return fail("window closed during rotation");
    }
    const int rotatedState = piece["rotationState"].get<int>();
    if (rotatedState == initialRotation)
    {
        return fail("X did not advance the active piece rotation state");
    }

    if (captureVideoFrames > 0 && !waitBeforeVideoAction())
    {
        return fail("window closed while waiting before hard drop");
    }

    queueKey("SPACE");
    if (!step())
    {
        return fail("window closed during hard drop");
    }
    const int score = grid["score"].get<int>();
    if (!game["isStarted"].get<bool>() || game["isGameOver"].get<bool>() || score <= 0)
    {
        return fail("hard drop did not leave a running game with a positive score");
    }

    sol::table constants = sceneModules["constants"];
    sol::table board = grid["grid"];
    const int width = constants["GRID_WIDTH"].get<int>();
    const int height = constants["GRID_HEIGHT"].get<int>();
    const int emptyCell = constants["GRID_EMPTY_CELL"].get<int>();
    int occupiedCells = 0;
    for (int x = 0; x < width; ++x)
    {
        sol::table column = board.raw_get<sol::table>(x);
        for (int y = 0; y < height; ++y)
        {
            const sol::object cell = column.raw_get<sol::object>(y);
            if (cell.valid() && cell.as<int>() != emptyCell)
            {
                ++occupiedCells;
            }
        }
    }
    if (occupiedCells <= 0)
    {
        return fail("hard drop did not write locked cells to the board");
    }
    while (tick < captureVideoFrames)
    {
        if (!step())
        {
            return fail("window closed while capturing the video sequence");
        }
    }
    if (capturedTicks.size() != requestedCaptureTicks.size())
    {
        return fail("not all requested scenario ticks were captured");
    }
    if (capturedVideoFrames != captureVideoFrames)
    {
        return fail("not all requested video frames were captured");
    }

    LOG_INFO("[ScenarioTest] PASS seed={} ticks={} start=1 movedColumns=1 rotation={} score={} occupiedCells={}",
             seed, tick, rotatedState, score, occupiedCells);
    std::cout << "[ScenarioTest] PASS seed=" << seed << " ticks=" << tick
              << " movedColumns=1 rotation=" << rotatedState << " score=" << score
              << " occupiedCells=" << occupiedCells << " videoFrames=" << capturedVideoFrames << '\n';
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
