/**
 * @file main.cpp (editor)
 * @brief Imhotep Editor entry point
 */

#include "editor/Editor.h"
#include "util/Logger.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    // Initialize logger with separate file for editor (must be done before EngineCore initialization)
    imhotep::Logger::GetInstance().Initialize("../logs/imhotep-editor.log");

    Editor &editor = Editor::GetInstance();

    std::string snapshotIpcEndpoint;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--mcp-snapshot-socket" && i + 1 < argc)
        {
            snapshotIpcEndpoint = argv[++i];
        }
        else
        {
            std::cerr << "Usage: imhotep-editor [--mcp-snapshot-socket <absolute-private-socket-path>]\n";
            return 2;
        }
    }

    if (!editor.Initialize(snapshotIpcEndpoint))
    {
        std::cerr << "Editor initialization failed" << std::endl;
        return 1;
    }

    editor.Run();
    editor.Shutdown();

    LOG_INFO("Editor exited cleanly");
    return 0;
}
