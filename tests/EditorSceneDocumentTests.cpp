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
        session.DiscardInvalidPositionDrafts("light");
        Require(!session.IsDirty(), "discarding rejected draft dirtied saved document");
        const auto yBefore = session.Snapshot()["scene"]["objects"][0]["transform"]["pos"]["y"].as<double>();
        Require(session.SetPositionText("light", 0, "1.23456789"), "precise text rejected");
        Require(session.Snapshot()["scene"]["objects"][0]["transform"]["pos"]["y"].as<double>() == yBefore, "editing X changed Y");
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
