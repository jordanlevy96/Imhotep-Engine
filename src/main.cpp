#include <controllers/Game.h>
#include "util/PathResolver.h"
#include <algorithm>
#include <cstring>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    // Check for flags
    bool smokeTest = false;
    bool clickTest = false;
    bool scenarioTest = false;
    bool scenarioSeedProvided = false;
    std::uint32_t scenarioSeed = 1;
    std::string scenarioCaptureDirectory;
    std::vector<int> scenarioCaptureTicks;
    std::string configPath;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--smoke-test") == 0)
        {
            smokeTest = true;
        }
        else if (std::strcmp(argv[i], "--click-test") == 0)
        {
            clickTest = true;
        }
        else if (std::strcmp(argv[i], "--scenario-test") == 0)
        {
            scenarioTest = true;
        }
        else if (std::strcmp(argv[i], "--scenario-seed") == 0)
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Usage: imhotep --scenario-test [--scenario-seed <uint32>] [--config <path>]\n";
                return 2;
            }
            const std::string value = argv[++i];
            std::uint32_t parsedSeed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsedSeed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            {
                std::cerr << "Usage: imhotep --scenario-test [--scenario-seed <uint32>] [--config <path>]\n";
                return 2;
            }
            scenarioSeed = parsedSeed;
            scenarioSeedProvided = true;
        }
        else if (std::strcmp(argv[i], "--scenario-capture-dir") == 0)
        {
            if (i + 1 >= argc || scenarioCaptureDirectory.size() != 0)
            {
                std::cerr << "Usage: imhotep --scenario-test [--scenario-seed <uint32>] [--scenario-capture-dir <existing-dir> --scenario-capture-tick <1..5> ...] [--config <path>]\n";
                return 2;
            }
            scenarioCaptureDirectory = argv[++i];
        }
        else if (std::strcmp(argv[i], "--scenario-capture-tick") == 0)
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Usage: imhotep --scenario-test [--scenario-seed <uint32>] [--scenario-capture-dir <existing-dir> --scenario-capture-tick <1..5> ...] [--config <path>]\n";
                return 2;
            }
            const std::string value = argv[++i];
            int parsedTick = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsedTick);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                parsedTick < 1 || parsedTick > 5 || scenarioCaptureTicks.size() >= 4)
            {
                std::cerr << "Capture ticks must be unique values from 1 to 5 (maximum four captures).\n";
                return 2;
            }
            if (std::find(scenarioCaptureTicks.begin(), scenarioCaptureTicks.end(), parsedTick) !=
                scenarioCaptureTicks.end())
            {
                std::cerr << "Capture ticks must be unique values from 1 to 5 (maximum four captures).\n";
                return 2;
            }
            scenarioCaptureTicks.push_back(parsedTick);
        }
        else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
        {
            configPath = argv[++i];
        }
    }

    const bool scenarioCaptureRequested = !scenarioCaptureDirectory.empty() || !scenarioCaptureTicks.empty();
    if ((scenarioSeedProvided && !scenarioTest) || (scenarioTest && (smokeTest || clickTest)) ||
        (scenarioCaptureRequested && (!scenarioTest || scenarioCaptureDirectory.empty() ||
                                      scenarioCaptureTicks.empty())))
    {
        std::cerr << "Scenario testing is a separate test mode. Usage: imhotep --scenario-test [--scenario-seed <uint32>] [--scenario-capture-dir <existing-dir> --scenario-capture-tick <1..5> ...] [--config <path>]\n";
        return 2;
    }

    // Resolve config path if not explicitly specified
    if (configPath.empty())
    {
        // IMHOTEP_GAME_CONFIG is set by CMake; fallback uses PathResolver for both modes
        const char *gameConfig = "games/vaporqube/conf/settings.yaml";
#ifdef IMHOTEP_GAME_CONFIG
        gameConfig = IMHOTEP_GAME_CONFIG;
#endif
        configPath = PathResolver::GetResourcePath() + gameConfig;
    }

    Game &game = Game::GetInstance();

    if (smokeTest || clickTest || scenarioTest)
    {
        game.SetTestMode(true);
    }

    if (!game.Initialize(configPath))
    {
        return 1;
    }

    if (scenarioTest)
    {
        const bool passed = game.RunVaporQubeScenarioTest(scenarioSeed, scenarioCaptureDirectory,
                                                            scenarioCaptureTicks);
        game.Shutdown();
        return passed ? 0 : 1;
    }
    else if (clickTest)
    {
        bool passed = game.RunClickTest();
        game.Shutdown();
        return passed ? 0 : 1;
    }
    else if (smokeTest)
    {
        game.RunFrames(10);
    }
    else
    {
        game.Run();
    }

    game.Shutdown();
    return 0;
}
