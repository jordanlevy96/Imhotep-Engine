/**
 * @file EditorTemplateTests.cpp
 * @brief Headless editor template persistence regression tests.
 *
 * Exercises save/reload and failed reads using disposable files.
 */
#include "editor/UITemplateManager.h"
#include "util/FileIO.h"
#include "util/Logger.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void Require(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

int main()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("imhotep-template-test-" + std::to_string(stamp));
    fs::create_directories(root);
    imhotep::Logger::GetInstance().Initialize((root / "test.log").string());
    int result = 0;
    try
    {
        auto &manager = UITemplateManager::GetInstance();
        manager.Initialize((root / "templates").string(), (root / "styles").string(), (root / "state").string());
        Require(manager.SaveTemplate("sample", "<p>Lorem ipsum</p>\n", "p { color: red; }\n", "return { data = {} }\n"), "save failed");
        std::string html, css, lua;
        Require(manager.LoadTemplate("sample", html, css, lua), "reload failed");
        Require(html == "<p>Lorem ipsum</p>\n" && css == "p { color: red; }\n" && lua == "return { data = {} }\n", "round trip changed content");

        Require(manager.SaveTemplate("sample", "", "", ""), "empty save failed");
        Require(manager.LoadTemplate("sample", html, css, lua), "empty document could not be reopened");
        Require(html.empty() && css.empty() && lua.empty(), "empty round trip changed content");

        Require(FileIO::WriteTextFile((root / "templates/standalone.html").string(), "<div></div>"), "fixture write failed");
        css = "old CSS";
        lua = "old Lua";
        Require(manager.LoadTemplate("standalone", html, css, lua), "optional files should be optional");
        Require(html == "<div></div>" && css.empty() && lua.empty(), "missing companion left stale content");

        auto expectPreserved = [&](const std::string &name)
        {
            html = "current HTML";
            css = "current CSS";
            lua = "current Lua";
            Require(!manager.LoadTemplate(name, html, css, lua), "invalid load unexpectedly succeeded");
            Require(html == "current HTML" && css == "current CSS" && lua == "current Lua", "failed load replaced the open document");
        };
        expectPreserved("missing");
        fs::create_directory(root / "styles/standalone.css");
        expectPreserved("standalone");
        fs::remove(root / "styles/standalone.css");
        fs::create_directory(root / "state/standalone.lua");
        expectPreserved("standalone");
        fs::create_directory(root / "templates/directory.html");
        expectPreserved("directory");

        Require(FileIO::WriteTextFile((root / "outside.html").string(), "preserve"), "outside fixture write failed");
        for (const std::string name : {"../outside", "", "nested/name"})
        {
            expectPreserved(name);
            Require(!manager.SaveTemplate(name, "replace", "", ""), "invalid save accepted");
            Require(!manager.DeleteTemplate(name), "invalid delete accepted");
            Require(!manager.TemplateExists(name), "invalid lookup accepted");
        }
        Require(FileIO::ReadTextFile((root / "outside.html").string()) == "preserve", "invalid operation modified outside file");
        Require(FileIO::WriteTextFile((root / "blocked").string(), "file"), "failure fixture write failed");
        manager.Initialize((root / "blocked/templates").string(), (root / "blocked/styles").string(), (root / "blocked/state").string());
        Require(!manager.SaveTemplate("sample", "html", "css", "lua"), "failed disk write reported success");
        std::cout << "Editor template persistence tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    imhotep::Logger::GetInstance().Shutdown();
    fs::remove_all(root);
    return result;
}
