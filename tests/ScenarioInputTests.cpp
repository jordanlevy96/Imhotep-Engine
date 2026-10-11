/**
 * @file ScenarioInputTests.cpp
 * @brief Headless checks for deterministic test key-state overrides.
 * @lines ~66
 *
 * Test contract: verify the override's initial state, held and released keys, physical-input
 * suppression while enabled, and restoration of physical polling after it is disabled.
 */
#include "controllers/WindowManager.h"

#include <iostream>
#include <stdexcept>

int main()
{
    WindowManager &window = WindowManager::GetInstance();
    struct ResetOverrides
    {
        WindowManager &window;
        ~ResetOverrides() { window.EnableTestInputOverrides(false); }
    } reset{window};

    try
    {
        if (window.ShouldIgnorePhysicalInput())
        {
            throw std::runtime_error("physical input was suppressed before enabling scenario overrides");
        }
        window.EnableTestInputOverrides(true);
        if (!window.ShouldIgnorePhysicalInput())
        {
            throw std::runtime_error("scenario overrides did not suppress physical key, cursor, click, and scroll input");
        }
        if (window.IsKeyPressed("RIGHT"))
        {
            throw std::runtime_error("RIGHT unexpectedly pressed at scenario start");
        }
        window.SetTestKeyPressed("RIGHT", true);
        if (!window.IsKeyPressed("RIGHT"))
        {
            throw std::runtime_error("scenario RIGHT hold was not exposed");
        }
        if (window.IsKeyPressed("LEFT"))
        {
            throw std::runtime_error("scenario LEFT hold was unexpectedly exposed");
        }
        window.SetTestKeyPressed("RIGHT", false);
        if (window.IsKeyPressed("RIGHT"))
        {
            throw std::runtime_error("scenario RIGHT release was not exposed");
        }
        window.EnableTestInputOverrides(false);
        if (window.ShouldIgnorePhysicalInput())
        {
            throw std::runtime_error("physical input suppression remained enabled after the scenario override was disabled");
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Scenario input test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Scenario input override tests passed\n";
    return 0;
}
