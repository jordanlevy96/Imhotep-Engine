/** @file EditorRuntimeContractTests.cpp
 *  @brief Real scene-module startup and executable config rejection without a display.
 */
#include "editor/PlaySession.h"
#include "controllers/Game.h"
#include "controllers/Registry.h"
#include "controllers/ScriptManager.h"
#include "editor/SceneDocument.h"
#include "systems/SceneLoader.h"
#include "util/ConfigLoader.h"
#include "util/Logger.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
namespace fs = std::filesystem;
static void Require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
static std::string Read(const fs::path &path)
{
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
int main()
{
    const auto root = fs::temp_directory_path() / ("imhotep-runtime-contract-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "res/scenes");
    fs::create_directory(root / "build");
    imhotep::Logger::GetInstance().Initialize("logs/editor-runtime-contract.log");
    int result = 0;
    try
    {
        const fs::path resources = fs::path(IMHOTEP_SOURCE_DIR) / "res";
        const auto referencePath = resources / "games/vaporqube/scenes/Scene.yaml";
        const auto referenceBytes = Read(referencePath);
        const auto reference = YAML::Load(referenceBytes);
        auto &lua = ScriptManager::GetInstance().GetLuaState();
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
        // Native value constructors used at module load time; no window/graphics/Python initialization.
        lua.new_usertype<glm::vec2>("Vec2", sol::constructors<glm::vec2(float, float)>(), "x", &glm::vec2::x, "y", &glm::vec2::y);
        lua.new_usertype<glm::vec3>("Vec3", sol::constructors<glm::vec3(float, float, float)>(), "x", &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z);
        lua.set_function("vec2", [](float x, float y) { return glm::vec2(x, y); });
        lua.set_function("vec3", [](float x, float y, float z) { return glm::vec3(x, y, z); });
        for (const auto *name : {"log_info", "log_debug", "log_trace", "log_warn", "log_error"})
            lua.set_function(name, [](sol::variadic_args) {});
        Game::GetInstance().conf.ResourcePath = resources.string() + "/";
        {
            SceneLoader loader;
            Require(loader.LoadScripts("games/vaporqube/scenes/Scene.yaml"), "real VaporQube module startup failed");
            Require(loader.IsLoaded() && loader.GetModules().size() == 5, "wrong shared module count");
            for (const auto *role : {"constants", "data", "entity", "game", "input"})
                Require(lua["SceneModules"][role].get<sol::object>().is<sol::table>(), "missing initialized module role");
            for (const auto &module : loader.GetModules())
            {
                if (module.roleString == "entity" || module.roleString == "input")
                    Require(module.isInitialized && module.initOrder >= 0, "shared module init failed");
            }
            Require(loader.GetContext()->HasCapability("ui") && loader.GetContext()->HasCapability("input"), "capabilities lost");
        }
        // Negative contracts use the same actual scripts and loader against private fixture files.
        const auto copyResource = [&](const std::string &path)
        {
            fs::create_directories((root / "res" / path).parent_path());
            fs::copy_file(resources / path, root / "res" / path, fs::copy_options::overwrite_existing);
        };
        for (const auto &script : reference["scripts"]) copyResource(script.as<std::string>());
        Game::GetInstance().conf.ResourcePath = (root / "res").string() + "/";
        const std::string partialTransformScene =
            "scene:\n  objects:\n    - name: partial-transform\n"
            "      transform:\n        pos: {x: 12}\n        scale: {y: 2}\n        color: {b: 0.25}\n";
        std::ofstream(root / "res/partial-transform.yaml") << partialTransformScene;
        SceneDocument authoredPartialTransform;
        Require(authoredPartialTransform.Load((root / "res/partial-transform.yaml").string()),
                "authoring rejected supported partial transform maps");
        auto &registry = Registry::GetInstance();
        Require(registry.LoadScene("partial-transform.yaml"), "runtime rejected supported partial transform maps");
        const auto partialId = registry.GetEntityByName("partial-transform");
        Require(partialId != ENTITY_NULL, "runtime did not create partial transform entity");
        const auto &partialTransform = registry.GetComponent<Transform>(partialId);
        Require(partialTransform.Pos == glm::vec3(12.0f, 0.0f, 0.0f) &&
                    partialTransform.Scale == glm::vec3(1.0f, 2.0f, 1.0f) &&
                    partialTransform.Color == glm::vec4(1.0f, 1.0f, 0.25f, 1.0f),
                "runtime partial transform defaults differ from authored semantics");
        registry.DestroyEntity(partialId);

        // Render resource constructors report malformed/missing files with runtime_error.
        // A scene load must report false and roll back entities instead of unwinding startup.
        std::ofstream(root / "res/bad-resource.yaml")
            << "scene:\n  objects:\n    - name: bad-resource\n"
               "      components: [{type: RenderComponent, shader: missing.shader, model: missing.obj}]\n";
        bool resourceFailureEscaped = false;
        bool resourceSceneLoaded = false;
        try
        {
            resourceSceneLoaded = registry.LoadScene("bad-resource.yaml");
        }
        catch (const std::exception &)
        {
            resourceFailureEscaped = true;
        }
        Require(!resourceFailureEscaped && !resourceSceneLoaded, "runtime resource error escaped scene loading");
        Require(registry.GetEntityByName("bad-resource") == ENTITY_NULL,
                "failed runtime resource load left a partial entity behind");

        auto invalid = YAML::Clone(reference);
        invalid["capabilities"]["input"] = false;
        std::ofstream(root / "res/invalid.yaml") << invalid;
        { SceneLoader loader; Require(!loader.LoadScripts("invalid.yaml"), "missing input capability accepted"); }
        invalid = YAML::Clone(reference);
        invalid["strict"] = true;
        invalid["scripts"].remove(0); // constants role is required by dependent modules.
        std::ofstream(root / "res/invalid.yaml") << invalid;
        { SceneLoader loader; Require(!loader.LoadScripts("invalid.yaml"), "missing dependency accepted in strict mode"); }

        const auto sourceConfig = resources / "games/vaporqube/conf/settings.yaml";
        const auto configBytes = Read(sourceConfig);
        auto config = YAML::Load(configBytes);
        for (const auto *key : {"htmlPath", "cssPath", "luaStatePath"}) copyResource(config["game"][key].as<std::string>());
        const auto authored = root / "res/scenes/authored.yaml";
        std::ofstream(authored) << referenceBytes;
        // This malformed numeric field guarantees the real executable stops before InitializeWindow.
        config["window"]["windowWidth"] = "invalid-width-for-headless-rejection";
        std::ofstream(root / "res/game.yaml") << config;
        const auto authoredConfigBytes = Read(root / "res/game.yaml");
        PlaySession play;
        Require(play.SelectConfig((root / "res").string(), "game.yaml"), "fixture selection failed");
        Require(play.Start(authored.string(), false, IMHOTEP_PLAY_EXECUTABLE, (root / "build").string()), "real executable spawn failed");
        for (int i = 0; i < 500 && play.IsRunning(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            play.Poll();
        }
        Require(!play.IsRunning() && play.GetStatus().find("code 1") != std::string::npos, "real config failure did not exit with 1");
        const auto runtimeOutput = Read(play.GetLogPath());
        Require(runtimeOutput.find("YAML parsing error") != std::string::npos && runtimeOutput.find("Initializing subsystems") == std::string::npos, "failure was not before window initialization");
        const auto generatedPath = fs::path(play.GetLogPath()).parent_path() / "settings.yaml";
        auto generated = YAML::LoadFile(generatedPath.string());
        Require(generated["game"]["scenePath"].as<std::string>() == "scenes/authored.yaml", "Play pointed at the wrong scene");
        for (const auto *key : {"htmlPath", "cssPath", "luaStatePath", "templateName"})
            Require(generated["game"][key].as<std::string>() == config["game"][key].as<std::string>(), "Play changed HUD configuration");
        generated["window"]["windowWidth"] = 1400;
        std::ofstream(root / "valid-generated.yaml") << generated;
        Config loaded;
        Require(ConfigLoader::LoadConfig((root / "valid-generated.yaml").string(), loaded), "real config loader rejected corrected generated config");
        Require(loaded.ScenePath == "scenes/authored.yaml" && loaded.ResourcePath == "../res/", "runtime config paths changed");
        Require(Read(authored) == referenceBytes && Read(root / "res/game.yaml") == authoredConfigBytes, "Play mutated authored input");
        Require(Read(referencePath) == referenceBytes && Read(sourceConfig) == configBytes, "reference files changed");
        std::cout << "Real SceneLoader: five modules initialized; missing capability/dependency rejected.\n"
                  << "Real runtime: generated config rejected before window initialization, exit 1; authored files unchanged.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::remove_all(root);
    return result;
}
