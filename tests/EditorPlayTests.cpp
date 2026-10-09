/** @file EditorPlayTests.cpp
 *  @brief Headless process lifecycle and runtime configuration contract checks.
 */
#include "editor/PlaySession.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <stdexcept>
#if defined(__APPLE__) || defined(__linux__)
#include <signal.h>
#endif
namespace fs = std::filesystem;
static void Require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
static void Write(const fs::path &path, const std::string &value)
{
    std::ofstream(path) << value;
}
static std::string Read(const fs::path &path)
{
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
int main(int argc, char **argv)
{
    // The same binary acts as a fake runtime: exercise real spawn/argv/env/cwd.
    if (argc == 3 && std::string(argv[1]) == "--config")
    {
        const auto config = YAML::LoadFile(argv[2]);
        if (std::getenv("USER_SETTINGS_PATH")) return 91;
        if (!fs::exists(fs::path("../res") / config["game"]["scenePath"].as<std::string>())) return 92;
        if (config["game"]["custom"].as<std::string>() != "preserved") return 93;
        std::cout << "Fake runtime configuration accepted" << std::endl;
        if (config["game"]["templateName"].as<std::string>() == "Wait")
        {
#if defined(__APPLE__) || defined(__linux__)
            signal(SIGTERM, SIG_IGN); // Exercise bounded Stop escalation.
#endif
            for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return 7; // A startup failure must reach the Editor, with captured output.
    }
    const auto root = fs::temp_directory_path() / ("imhotep-play-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try
    {
        fs::create_directories(root / "res");
        fs::create_directory(root / "build");
        for (const auto *file : {"hud.html", "hud.css", "hud.lua"}) Write(root / "res" / file, "");
        const auto configPath = root / "res/game.yaml";
        const std::string config = "game: {htmlPath: hud.html, cssPath: hud.css, luaStatePath: hud.lua, templateName: Test, custom: preserved}\nwindow: {windowWidth: 800}\n";
        Write(configPath, config);
        const auto scene = root / "res/scene.yaml";
        const std::string authored = "scene: {objects: []}\n";
        Write(scene, authored);
        PlaySession session;
        Require(!session.Start(scene.string(), false, argv[0], (root / "build").string()), "Play accepted no configuration");
        Require(session.SelectConfig((root / "res").string(), "game.yaml"), "valid config rejected");
        Require(!session.SelectConfig((root / "res").string(), "../build/missing.yaml"), "outside config accepted");
        Require(session.GetConfigPath() == "game.yaml", "failed selection lost config");
        const auto binary = fs::absolute(argv[0]).string();
        Require(!session.Start(scene.string(), true, binary, (root / "build").string()), "dirty scene played");
        Require(!session.Start(scene.string(), false, "/missing/runtime", (root / "build").string()), "missing runtime accepted");
#if defined(__APPLE__) || defined(__linux__)
        setenv("USER_SETTINGS_PATH", "/must-not-reach-child", 1);
        Require(session.Start(scene.string(), false, binary, (root / "build").string()), "spawn failed");
        Require(!session.Start(scene.string(), false, binary, (root / "build").string()), "second child started");
        for (int i = 0; i < 200 && session.IsRunning(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            session.Poll();
        }
        Require(!session.IsRunning() && session.GetStatus().find("code 7") != std::string::npos, "exit code not reported");
        Require(Read(session.GetLogPath()).find("configuration accepted") != std::string::npos, "child output not captured");
        Require(std::getenv("USER_SETTINGS_PATH") != nullptr, "parent environment changed");
        Write(configPath, "game: {htmlPath: hud.html, cssPath: hud.css, luaStatePath: hud.lua, templateName: Wait, custom: preserved}\nwindow: {}\n");
        Require(session.Start(scene.string(), false, binary, (root / "build").string()), "restart failed");
        for (int i = 0; i < 200 && Read(session.GetLogPath()).empty(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Require(!session.SelectConfig((root / "res").string(), "game.yaml"), "running config switched");
        session.Stop();
        Require(!session.IsRunning(), "Stop retained child");
        session.Stop(); // Idempotent, no stale PID signal.
        Require(Read(scene) == authored, "Stop changed authored scene");
        Require(Read(configPath).find("templateName: Wait") != std::string::npos, "Play overwrote selected config");
        {
            PlaySession scoped;
            Require(scoped.SelectConfig((root / "res").string(), "game.yaml"), "scoped selection failed");
            Require(scoped.Start(scene.string(), false, binary, (root / "build").string()), "scoped launch failed");
        } // Destruction must stop and reap its owned child.
        Write(configPath, config);
        Require(Read(scene) == authored && Read(configPath) == config, "Play changed source documents");
        fs::remove(root / "res/hud.lua");
        Require(!session.Start(scene.string(), false, binary, (root / "build").string()), "deleted HUD reference accepted");
#endif
        std::cout << "Editor Play session tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::remove_all(root);
    return result;
}
