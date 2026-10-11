/** @file EditorUIStateTests.cpp
 *  @brief Exercise Editor tabs, pagination and emitted handlers without graphics.
 *  @lines ~93
 *
 *  Test cases:
 *  - main() - Check tab visibility, module paging, and dispatch of rendered handlers
 */
#include "systems/ReactiveUI.h"
#include "controllers/ScriptManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void Require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
int main()
{
    imhotep::Logger::GetInstance().Initialize("logs/editor-ui-state-tests.log");
    auto &ui = ReactiveUI::GetInstance();
    try
    {
        auto &lua = ScriptManager::GetInstance().GetLuaState();
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
        auto state = std::make_shared<LuaUIState>();
        Require(state->LoadStateFile(std::string(IMHOTEP_SOURCE_DIR) + "/res/ui/state/editor.lua"), "state load failed");
        auto methods = state->GetValue("methods").as<sol::table>();
        methods["focusInput"] = [&](sol::table, std::string field) { state->SetValue("data.focusedField", field); };
        int selectedIndex = -1;
        methods["moveSceneModuleUp"] = [&](sol::table, int index) { selectedIndex = index; };
        auto modules = lua.create_table();
        for (int i = 0; i < 6; ++i)
        {
            auto module = lua.create_table();
            module["index"] = i;
            module["path"] = "Module" + std::to_string(i) + ".lua";
            modules[i + 1] = module;
        }
        state->SetValue("data.scene.modules", modules);
        ui.BindLuaState(state);
        std::ifstream stream(std::string(IMHOTEP_SOURCE_DIR) + "/res/ui/editor.html");
        const std::string html{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        ui.RegisterTemplateWithDirectives("editor", html);
        auto rendered = ui.GetRenderedHTML();
        const auto sceneFilePosition = rendered.find("Scene file");
        const auto scenePathPosition = rendered.find("../", sceneFilePosition);
        const auto scenePathEnd = rendered.find("EditorTest.yaml", scenePathPosition);
        Require(rendered.find("Scene Hierarchy") != std::string::npos && rendered.find("Shared Lua modules") == std::string::npos, "default sidebar tab incorrect");
        Require(sceneFilePosition != std::string::npos && scenePathPosition != std::string::npos &&
                    scenePathEnd != std::string::npos && rendered.substr(scenePathPosition, scenePathEnd - scenePathPosition).find('\n') != std::string::npos,
                "scene path is not broken onto lines within the left panel");
        state->SetValue("data.focusedField", "data.scene.name");
        ui.DispatchEvent("click", "setSceneSidebarTab('modules')", {});
        Require(state->GetValue("data.focusedField").as<std::string>().empty(), "tab switch retained hidden focus");
        rendered = ui.GetRenderedHTML();
        Require(rendered.find("Module4.lua") != std::string::npos && rendered.find("Module5.lua") == std::string::npos && rendered.find("Scene Hierarchy") == std::string::npos, "module page is not bounded");
        bool dispatched = false;
        for (const auto &[id, handlers] : ui.GetEventHandlers())
        {
            const auto click = handlers.find("click");
            if (click != handlers.end() && click->second == "moveSceneModuleUp(4)")
            {
                ui.DispatchEvent("click", click->second, {});
                dispatched = true;
                break;
            }
        }
        Require(dispatched && selectedIndex == 4, "rendered module handler did not dispatch its index");
        ui.DispatchEvent("click", "nextModulePage", {});
        rendered = ui.GetRenderedHTML();
        Require(rendered.find("Module5.lua") != std::string::npos && rendered.find("Module0.lua") == std::string::npos, "next module page failed");
        ui.DispatchEvent("click", "setSceneSidebarTab('game')", {});
        rendered = ui.GetRenderedHTML();
        Require(rendered.find("Play Saved Scene") != std::string::npos && rendered.find("Shared Lua modules") == std::string::npos, "game tab failed");
        state->SetValue("data.selectedEntityId", 1);
        state->SetValue("data.scene.hasFlyingProps", true);
        ui.DispatchEvent("click", "setAssignmentMode('properties')", {});
        rendered = ui.GetRenderedHTML();
        Require(rendered.find("Apply Value to Scene") != std::string::npos && rendered.find("17</span>") != std::string::npos, "bounded FlyingProps controls missing");
        bool scaleHandler = false;
        for (const auto &[id, handlers] : ui.GetEventHandlers())
        {
            const auto click = handlers.find("click");
            if (click != handlers.end() && click->second == "focusInput('data.inspector.scaleX')") scaleHandler = true;
        }
        Require(scaleHandler, "scale input has no click handler");
        ui.UnbindLuaState();
        std::cout << "Editor UI state tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        ui.UnbindLuaState();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
