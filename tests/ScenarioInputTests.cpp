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
        window.EnableTestInputOverrides(true);
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
    }
    catch (const std::exception &error)
    {
        std::cerr << "Scenario input test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Scenario input override tests passed\n";
    return 0;
}
