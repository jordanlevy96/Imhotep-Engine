/** @file EditorSceneDocumentTests.cpp
 *  @brief Authored scene round-trip and failure preservation tests, without a runtime VM.
 */
#include "editor/SceneDocument.h"
#include "editor/SceneSession.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;
static void Require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
static void Write(const fs::path &path, const std::string &content)
{
    std::ofstream file(path);
    file << content;
}
int main()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("imhotep-scene-test-" + std::to_string(stamp));
    fs::create_directory(root);
    int result = 0;
    try
    {
        SceneDocument document;
        Require(!document.Save((root / "empty.yaml").string()), "unloaded save accepted");
        const auto source = fs::path(IMHOTEP_SOURCE_DIR) / "res/games/vaporqube/scenes/Scene.yaml";
        Require(document.Load(source.string()), "VaporQube scene failed to load");
        Require(!document.IsDirty(), "newly loaded scene is dirty");
        Require(document.SetPosition("light", 2, 40, -3), "position edit failed");
        Require(document.IsDirty(), "edit did not mark dirty");
        const auto output = root / "saved.yaml";
        Require(document.Save(output.string()), "save failed");
        Require(!document.IsDirty(), "save did not clear dirty");
        YAML::Node expected = YAML::LoadFile(source.string());
        expected["scene"]["objects"][0]["transform"]["pos"]["x"] = 2.0;
        expected["scene"]["objects"][0]["transform"]["pos"]["y"] = 40.0;
        expected["scene"]["objects"][0]["transform"]["pos"]["z"] = -3.0;
        Require(YAML::Dump(expected) == YAML::Dump(YAML::LoadFile(output.string())), "save changed unrelated scene fields");
        Require(document.Load(output.string()), "saved scene cannot reopen");
        Require(!document.SetPosition("missing", 0, 0, 0), "unknown object accepted");
        Require(!document.SetPosition("light", std::numeric_limits<double>::infinity(), 0, 0), "nonfinite position accepted");
        Require(!document.IsDirty(), "rejected edit dirtied scene");

        Require(document.SetPosition("light", 5, 6, 7), "second edit failed");
        Write(root / "invalid.yaml", "scene: [broken");
        Require(!document.Load((root / "invalid.yaml").string()), "malformed YAML accepted");
        Require(document.IsDirty(), "failed load discarded dirty state");
        Require(!document.Save((root / "missing-parent/out.yaml").string()), "invalid destination accepted");
        Require(document.IsDirty(), "failed save discarded dirty state");
        fs::create_directory(output.string() + ".imhotep-save");
        Require(!document.Save(output.string()), "reserved destination accepted");
        Require(YAML::Dump(expected) == YAML::Dump(YAML::LoadFile(output.string())), "failed save damaged existing file");
        fs::remove(output.string() + ".imhotep-save");
        Require(document.Save(output.string()), "save after recovery failed");
        Require(YAML::LoadFile(output.string())["scene"]["objects"][0]["transform"]["pos"]["x"].as<int>() == 5, "failed load replaced document");

        Write(root / "custom.yaml", "custom: keep\nscene:\n  objects:\n    - name: object\n      transform:\n        pos: {x: 0, y: 0, z: 0, custom: keep}\n      components: [{type: FutureComponent, value: keep}]\n");
        Require(document.Load((root / "custom.yaml").string()), "custom scene rejected");
        Require(document.SetPosition("object", 1, 2, 3), "custom position edit failed");
        Require(document.Save(output.string()), "custom save failed");
        const auto custom = YAML::LoadFile(output.string());
        Require(custom["custom"].as<std::string>() == "keep" && custom["scene"]["objects"][0]["transform"]["pos"]["custom"].as<std::string>() == "keep", "unknown fields lost");
        Require(custom["scene"]["objects"][0]["components"][0]["type"].as<std::string>() == "FutureComponent", "unknown component lost");
        Write(root / "duplicate.yaml", "scene: {objects: [{name: duplicate}, {name: duplicate}]}\n");
        Require(!document.Load((root / "duplicate.yaml").string()), "ambiguous object names accepted");
        fs::create_directory(root / "destination-directory");
        Require(!document.Save((root / "destination-directory").string()), "directory replaced as scene");
        Require(!fs::exists((root / "destination-directory").string() + ".imhotep-save"), "failed replacement left reservation behind");
        SceneSession session;
        Require(session.Open(source.string()), "session open failed");
        const auto originalPath = session.GetPath();
        Require(session.SetPosition("light", 8, 9, 10), "session edit failed");
        Require(!session.Open(source.string()), "dirty switch was not guarded");
        Require(!session.Open((root / "invalid.yaml").string(), true), "discard should not accept a broken file");
        Require(session.IsDirty() && session.GetPath() == originalPath, "failed open changed session");
        bool prepared = false;
        Require(!session.Open(source.string(), true, [&](const YAML::Node &, std::string &error)
        {
            prepared = true;
            error = "missing preview resource";
            return false;
        }), "failed preview committed document");
        Require(prepared && session.IsDirty() && session.GetPath() == originalPath, "failed preview lost document state");
        Require(session.Snapshot()["scene"]["objects"][0]["transform"]["pos"]["x"].as<int>() == 8, "failed switch lost edits");
        Require(!session.Save((root / "missing-parent/other.yaml").string()), "failed Save As reported success");
        Require(session.GetPath() == originalPath && session.IsDirty(), "failed Save As changed current path");
        Require(session.Save(output.string()), "Save As failed");
        Require(session.GetPath() == output.string() && !session.IsDirty(), "Save As did not establish clean path");
        auto snapshot = session.Snapshot();
        snapshot["scene"]["objects"][0]["name"] = "external mutation";
        Require(session.Snapshot()["scene"]["objects"][0]["name"].as<std::string>() == "light", "snapshot mutated document");
        Require(session.SetPosition("light", 8, 9, 10) && !session.IsDirty(), "unchanged position dirtied scene");
        Require(session.SetPosition("light", 11, 12, 13), "edit after Save As failed");
        Require(session.Save(), "Save current path failed");
        Require(session.Open(output.string()), "saved session cannot reopen");
        Require(session.Snapshot()["scene"]["objects"].size() == 5, "runtime objects appeared in authored save");
        Require(session.Snapshot()["scene"]["objects"][0]["transform"]["pos"]["x"].as<int>() == 11, "reopened session lost edit");
        Require(!session.SetPosition("light", std::numeric_limits<double>::max(), 0, 0), "engine-incompatible float overflow accepted");
        Write(root / "bad-transform.yaml", "scene: {objects: [{name: broken, transform: {pos: {x: invalid}}}]}\n");
        Require(!session.Open((root / "bad-transform.yaml").string()), "invalid numeric transform accepted");
        Require(!session.SetPositionText("light", 0, "-"), "invalid position draft accepted");
        Require(session.SetPositionText("light", 1, "12.125"), "valid neighboring draft rejected");
        Require(!session.Save(), "focus change allowed an invalid draft to save");
        Require(!session.Open(source.string()), "invalid draft allowed unconfirmed switch");
        Require(session.SetPositionText("light", 0, "11"), "corrected draft rejected");
        Require(session.Save(), "corrected draft did not allow save");
        Require(!session.SetPositionText("light", 2, "   "), "whitespace draft accepted");
        session.DiscardInvalidTransformDrafts("light");
        Require(!session.IsDirty(), "discarding rejected draft dirtied saved document");
        const auto yBefore = session.Snapshot()["scene"]["objects"][0]["transform"]["pos"]["y"].as<double>();
        Require(session.SetPositionText("light", 0, "1.23456789"), "precise text rejected");
        Require(session.Snapshot()["scene"]["objects"][0]["transform"]["pos"]["y"].as<double>() == yBefore, "editing X changed Y");
        // Build the first authored VaporQube objects through shared commands.
        SceneSession created;
        const std::string resources = (fs::path(IMHOTEP_SOURCE_DIR) / "res").string();
        Require(created.Create("AuthoredVaporQube"), "new scene failed");
        Require(created.IsDirty() && created.GetPath().empty(), "new scene was not unsaved");
        Require(!created.Save(), "new scene saved without a destination");
        Require(created.AddObject("light"), "named light creation failed");
        Require(created.SetPositionText("light", 1, "38"), "unsaved scene position editing failed");
        Require(created.AssignRenderComponent("light", resources, "Basic.shader", "cube.obj"), "light resource assignment failed");
        Require(created.AddObject("GameGrid"), "grid creation failed");
        Require(created.AssignLuaScript("GameGrid", resources, "games/vaporqube/scripts/GameGrid.lua", "OutlinedCube.shader", "cube.obj"), "grid Lua assignment failed");
        const auto authoredCopy = root / "authored.yaml";
        Require(created.Save(authoredCopy.string()), "authored scene save failed");
        Require(created.Open(authoredCopy.string()), "authored scene reopen failed");
        const auto built = created.Snapshot();
        Require(built["scene"]["objects"].size() == 2, "authored object count incorrect");
        Require(built["scene"]["objects"][0]["components"][0]["shader"].as<std::string>() == "Basic.shader", "render reference changed");
        Require(built["scene"]["objects"][1]["components"][0]["script"].as<std::string>() == "games/vaporqube/scripts/GameGrid.lua", "Lua path changed");
        Require(built["scene"]["objects"][1]["components"][0]["model"].as<std::string>() == "cube.obj", "Lua property lost");
        const auto beforeRejected = YAML::Dump(created.Snapshot());
        Require(!created.AddObject("GameGrid"), "duplicate name accepted");
        Require(!created.AddObject("bad/name"), "invalid object name accepted");
        Require(!created.AssignLuaScript("light", resources, "games/vaporqube/scripts/GameGrid.lua", "", ""), "Lua name mismatch accepted");
        Require(!created.AssignLuaScript("GameGrid", resources, "missing/GameGrid.lua", "", ""), "missing Lua accepted");
        Require(!created.AssignRenderComponent("light", resources, "../shaders/Basic.shader", "../missing.obj"), "missing resource accepted");
        Require(!created.AssignRenderComponent("light", resources, (fs::path(resources) / "shaders/Basic.shader").string(), "cube.obj"), "absolute resource path accepted");
        Require(!created.AddObject("rejected", [](const YAML::Node &, std::string &error)
        {
            error = "preview rejected";
            return false;
        }), "failed preview committed new object");
        Require(YAML::Dump(created.Snapshot()) == beforeRejected && !created.IsDirty(), "rejected operation changed clean scene");
        Require(created.AssignLuaScript("GameGrid", resources, "games/vaporqube/scripts/GameGrid.lua", "", ""), "unchanged Lua assignment failed");
        Require(!created.IsDirty(), "unchanged Lua assignment dirtied scene");
        Require(created.SetPosition("light", 1, 2, 3), "edit before new scene guard failed");
        Require(!created.Create("Replacement"), "New discarded dirty edits without explicit choice");
        Require(!created.Create("invalid name", true), "invalid New accepted");
        Require(created.GetPath() == authoredCopy.string() && created.IsDirty(), "failed New lost old path or dirty state");
        Require(!created.Create("Replacement", true, [](const YAML::Node &, std::string &error)
        {
            error = "preview rejected";
            return false;
        }), "failed New preview committed");
        Require(created.GetPath() == authoredCopy.string() && created.IsDirty(), "failed New preview lost edits");
        Require(created.Create("Replacement", true), "explicit discard and New failed");
        Require(created.Snapshot()["scene"]["objects"].size() == 0 && created.GetPath().empty(), "New retained old objects/path");
        // Reassigning a known component keeps custom properties and neighboring components.
        Write(root / "extended.yaml", "scene: {objects: [{name: light, custom: keep, components: [{type: RenderComponent, shader: Basic.shader, model: cube.obj, custom: keep}, {type: FutureComponent, custom: keep}]}]}\n");
        Require(created.Open((root / "extended.yaml").string(), true), "extended scene open failed");
        Require(created.AssignRenderComponent("light", resources, "Lighting.shader", "cube.obj"), "resource reassignment failed");
        const auto extended = created.Snapshot()["scene"]["objects"][0];
        Require(extended["custom"].as<std::string>() == "keep" && extended["components"][0]["custom"].as<std::string>() == "keep" && extended["components"][1]["custom"].as<std::string>() == "keep", "assignment discarded unknown fields");
        // Render references and Lua properties on the same object remain independent.
        Require(created.Open(authoredCopy.string(), true), "mixed component fixture open failed");
        Require(created.AssignRenderComponent("GameGrid", resources, "Basic.shader", "cube.obj"), "mixed render assignment failed");
        Require(created.AssignLuaScript("GameGrid", resources, "games/vaporqube/scripts/GameGrid.lua", "OutlinedCube.shader", "cube.obj"), "mixed Lua assignment failed");
        const auto mixed = created.Snapshot()["scene"]["objects"][1]["components"];
        Require(mixed[0]["type"].as<std::string>() == "LuaScript" && mixed[0]["shader"].as<std::string>() == "OutlinedCube.shader", "render assignment overwrote Lua properties");
        Require(mixed[1]["type"].as<std::string>() == "RenderComponent" && mixed[1]["shader"].as<std::string>() == "Basic.shader", "Lua assignment overwrote render references");
        Require(!created.AssignRenderComponent("light", "", "Basic.shader", "cube.obj"), "empty resource root accepted");
        std::vector<std::string> modules = {"games/vaporqube/scripts/GameConstants.lua", "games/vaporqube/scripts/GameInput.lua"};
        Require(created.SetModules(resources, modules), "module assignment failed");
        Require(created.SetCapability("ui", true) && created.SetCapability("input", true), "capabilities failed");
        Require(created.SetStrictMode(true), "strict mode failed");
        Require(created.Save(authoredCopy.string()) && created.Open(authoredCopy.string()), "module roundtrip failed");
        Require(created.Snapshot()["scripts"][1].as<std::string>() == modules[1], "module order changed");
        Require(created.Snapshot()["strict"].as<bool>() && created.Snapshot()["capabilities"]["ui"].as<bool>(), "configuration lost");
        const auto configured = YAML::Dump(created.Snapshot());
        Require(created.SetModules(resources, modules) && !created.IsDirty(), "module no-op dirtied scene");
        Require(!created.SetModules(resources, {modules[0], modules[0]}), "duplicate module accepted");
        Require(!created.SetModules(resources, {"missing.lua"}), "missing module accepted");
        Require(!created.SetCapability("unknown", true), "unknown capability accepted");
        Require(YAML::Dump(created.Snapshot()) == configured && !created.IsDirty(), "rejected module edit changed scene");
        std::reverse(modules.begin(), modules.end());
        Require(created.SetModules(resources, modules) && created.IsDirty(), "module reorder failed");
        // Recreate every authored field in the supplied scene through Editor operations.
        const auto reference = YAML::LoadFile(resources + "/games/vaporqube/scenes/Scene.yaml");
        SceneSession recreated;
        Require(recreated.Create(reference["name"].as<std::string>()), "Tetris New failed");
        std::vector<std::string> shared;
        for (const auto &script : reference["scripts"]) shared.push_back(script.as<std::string>());
        Require(recreated.SetModules(resources, shared), "Tetris modules failed");
        Require(recreated.SetCapability("ui", true) && recreated.SetCapability("input", true), "Tetris capabilities failed");
        for (const auto &object : reference["scene"]["objects"])
        {
            const auto name = object["name"].as<std::string>();
            Require(recreated.AddObject(name), "Tetris object failed");
            const auto transform = object["transform"];
            const char *axes[] = {"x", "y", "z"};
            for (size_t axis = 0; transform && axis < 3; ++axis)
            {
                if (transform["pos"])
                    Require(recreated.SetPositionText(name, axis, transform["pos"][axes[axis]].as<std::string>()), "Tetris position failed");
                if (transform["scale"])
                    Require(recreated.SetScaleText(name, axis, transform["scale"][axes[axis]].as<std::string>()), "Tetris scale failed");
            }
            for (const auto &component : object["components"])
            {
                if (component["type"].as<std::string>() == "RenderComponent")
                    Require(recreated.AssignRenderComponent(name, resources, component["shader"].as<std::string>(), component["model"].as<std::string>()), "Tetris mesh failed");
                else
                    Require(recreated.AssignLuaScript(name, resources, component["script"].as<std::string>(), component["shader"].as<std::string>(""), component["model"].as<std::string>("")), "Tetris script failed");
                if (name == "FlyingProps")
                    for (const auto &property : SceneDocument::FlyingPropsProperties())
                        Require(recreated.SetFlyingPropsProperty(name, resources, property, component[property].as<std::string>()), "FlyingProps field failed");
            }
        }
        Require(recreated.Save(authoredCopy.string()) && recreated.Open(authoredCopy.string()), "Tetris save/reopen failed");
        const auto complete = recreated.Snapshot();
        Require(complete["scene"]["objects"].size() == reference["scene"]["objects"].size(), "runtime objects entered authored scene");
        Require(complete["scene"]["objects"][0]["transform"]["scale"]["x"].as<double>() == 0.5, "light scale lost");
        for (size_t index = 0; index < reference["scene"]["objects"].size(); ++index)
        {
            const auto expected = reference["scene"]["objects"][index];
            const auto actual = complete["scene"]["objects"][index];
            Require(actual["name"].as<std::string>() == expected["name"].as<std::string>(), "object identity/order changed");
            for (const auto *field : {"type", "script", "shader", "model"})
            {
                const auto expectedField = expected["components"][0][field];
                if (expectedField)
                    Require(actual["components"][0][field].as<std::string>() == expectedField.as<std::string>(), "component reference changed on reopen");
            }
        }
        const auto actualProps = complete["scene"]["objects"][4]["components"][0];
        const auto expectedProps = reference["scene"]["objects"][4]["components"][0];
        for (const auto &property : SceneDocument::FlyingPropsProperties())
        {
            if (property == "models") Require(actualProps[property].as<std::string>() == expectedProps[property].as<std::string>(), "model list changed");
            else Require(actualProps[property].as<double>() == expectedProps[property].as<double>(), "numeric property changed");
        }
        const auto clean = YAML::Dump(complete);
        Require(!recreated.SetFlyingPropsProperty("GameGrid", resources, "speed", "12"), "unrelated script edited");
        Require(!recreated.SetFlyingPropsProperty("FlyingProps", resources, "ready", "12"), "method field overwritten");
        Require(!recreated.SetFlyingPropsProperty("FlyingProps", resources, "speed", "nan"), "nonfinite property accepted");
        Require(!recreated.SetFlyingPropsProperty("FlyingProps", resources, "maxProps", "2.5"), "fractional prop count accepted");
        Require(!recreated.SetFlyingPropsProperty("FlyingProps", resources, "models", "cube.obj,"), "empty model accepted");
        Require(!recreated.SetFlyingPropsProperty("FlyingProps", resources, "models", "../missing.obj"), "missing model accepted");
        Require(YAML::Dump(recreated.Snapshot()) == clean && !recreated.IsDirty(), "rejected property changed document");
        Require(!recreated.SetScaleText("light", 0, "-"), "invalid scale draft accepted");
        Require(!recreated.Save() && recreated.IsDirty(), "invalid scale draft escaped save guard");
        Require(recreated.SetPositionText("light", 0, "0") && !recreated.Save(), "position correction cleared scale error");
        Require(recreated.SetScaleText("light", 0, "0.5") && recreated.Save(), "scale correction failed");
        Require(!recreated.SetScaleText("light", 1, "1e100"), "overflow scale accepted");
        recreated.DiscardInvalidTransformDrafts("light");
        Require(!recreated.IsDirty(), "scale draft discard did not restore clean state");
        std::cout << "Editor scene document tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::remove_all(root);
    return result;
}
