#include "editor/SceneSession.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void Require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static std::string Read(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int main()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("imhotep-mutation-test-" + std::to_string(stamp));
    fs::create_directories(root);
    int result = 0;
    try
    {
        const auto scenePath = root / "authored.yaml";
        const std::string source =
            "metadata: {keep: yes}\n"
            "scene:\n"
            "  objects:\n"
            "    - name: light\n"
            "      extra: retain\n"
            "      transform:\n"
            "        pos: {x: 1, y: 2, z: 3, authoredExtension: preserve}\n"
            "      components: [{type: FutureComponent, value: untouched}]\n"
            "    - name: other\n"
            "      transform: {pos: {x: 8, y: 9, z: 10}}\n";
        {
            std::ofstream file(scenePath, std::ios::binary);
            file << source;
        }
        const auto malformedPath = root / "malformed.yaml";
        {
            std::ofstream file(malformedPath);
            file << "scene: [broken\n";
        }

        SceneSession session;
        Require(session.GetRevision() == 0, "new session revision must start at zero");
        Require(session.Open(scenePath.string()), "scene failed to open");
        Require(session.GetRevision() == 1, "successful open must advance revision once");
        const auto original = session.Snapshot();
        const auto originalDump = YAML::Dump(original);
        const auto diskBefore = Read(scenePath);
        const auto light = session.GetObjectHandle("light");
        const auto other = session.GetObjectHandle("other");
        Require(light.has_value() && other.has_value(), "loaded object handles were not available");
        Require(!session.GetObjectHandle("missing"), "unknown object handle resolved");

        SceneSession otherSession;
        Require(otherSession.Open(scenePath.string()), "second session failed to open scene");
        const auto otherSessionRevision = otherSession.GetRevision();
        const auto otherSessionBefore = YAML::Dump(otherSession.Snapshot());
        const auto beforeRejected = YAML::Dump(session.Snapshot());
        Require(!session.SetPosition(*light, session.GetRevision() - 1, 4, 5, 6), "stale revision edit accepted");
        Require(!session.SetPosition(*light, session.GetRevision(), std::numeric_limits<double>::infinity(), 0, 0), "infinite coordinate accepted");
        Require(!session.SetPosition(*light, session.GetRevision(), std::numeric_limits<double>::quiet_NaN(), 0, 0), "NaN coordinate accepted");
        Require(!session.SetPosition(*light, session.GetRevision(), std::numeric_limits<double>::max(), 0, 0), "float-overflow coordinate accepted");
        Require(!otherSession.SetPosition(*light, otherSessionRevision, 4, 5, 6), "handle from another session accepted");
        Require(otherSession.GetRevision() == otherSessionRevision && YAML::Dump(otherSession.Snapshot()) == otherSessionBefore,
                "foreign handle changed its receiving session");
        Require(session.GetRevision() == 1 && YAML::Dump(session.Snapshot()) == beforeRejected && !session.IsDirty(),
                "rejected edit changed revision or document");

        Require(session.SetPosition(*light, 1, -11.25, 22.5, 3.75), "valid revision-checked edit failed");
        Require(session.GetRevision() == 2, "successful edit must advance revision exactly once");
        Require(session.IsDirty(), "position edit did not dirty authored scene");
        const auto edited = session.Snapshot();
        Require(edited["scene"]["objects"][0]["transform"]["pos"]["x"].as<double>() == -11.25,
                "position edit wrote incorrect X");
        Require(edited["scene"]["objects"][0]["transform"]["pos"]["authoredExtension"].as<std::string>() == "preserve",
                "position edit discarded custom transform data");
        Require(edited["scene"]["objects"][0]["extra"].as<std::string>() == "retain" &&
                edited["scene"]["objects"][0]["components"][0]["type"].as<std::string>() == "FutureComponent",
                "position edit changed unrelated authored YAML");
        const auto afterEdit = YAML::Dump(session.Snapshot());
        Require(!session.SetPosition(*light, 1, 7, 8, 9), "stale revision accepted after successful edit");
        Require(!session.UndoPosition(*other, 2), "undo accepted a different object handle");
        Require(!session.UndoPosition(*light, 1), "stale undo accepted");
        Require(session.GetRevision() == 2 && YAML::Dump(session.Snapshot()) == afterEdit,
                "stale operations changed the document or revision");

        Require(session.UndoPosition(*light, 2), "position undo failed");
        Require(session.GetRevision() == 3, "undo must advance revision exactly once");
        Require(YAML::Dump(session.Snapshot()) == originalDump, "undo failed to restore the exact authored document");
        Require(!session.IsDirty(), "undo to the clean loaded document should clear dirty state");
        Require(!session.UndoPosition(*light, 3), "second undo accepted without an edit");
        Require(session.GetRevision() == 3 && YAML::Dump(session.Snapshot()) == originalDump,
                "unavailable undo changed document or revision");

        Require(!session.Open(malformedPath.string(), true), "malformed reload succeeded");
        Require(session.GetRevision() == 3 && YAML::Dump(session.Snapshot()) == originalDump,
                "failed reload changed document or revision");
        Require(session.SetPosition(*light, 3, 12, 13, 14), "handle expired after failed reload");
        Require(session.Open(scenePath.string(), true), "valid reload failed");
        Require(session.GetRevision() == 5, "reload must advance revision once");
        Require(!session.SetPosition(*light, 5, 20, 21, 22), "handle survived a successful reload");
        const auto fresh = session.GetObjectHandle("light");
        Require(fresh.has_value(), "fresh handle missing after reload");
        Require(session.SetPosition(*fresh, 5, 30, 31, 32), "fresh post-reload handle rejected");
        Require(session.GetRevision() == 6, "post-reload edit revision incorrect");
        Require(session.SetStrictMode(true), "existing authored edit failed");
        Require(session.GetRevision() == 7, "shared session edit did not advance revision");
        Require(!session.UndoPosition(*fresh, 7), "position undo survived a later unrelated edit");
        Require(session.GetRevision() == 7, "rejected undo after another edit changed revision");
        Require(session.Create("Replacement", true), "creating a new document failed");
        Require(!session.SetPosition(*fresh, session.GetRevision(), 0, 0, 0), "handle survived document creation");
        Require(Read(scenePath) == diskBefore, "in-memory edit or undo wrote to disk");
    }
    catch (const std::exception &error)
    {
        std::cerr << "Editor scene mutation test failure: " << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    return result;
}
