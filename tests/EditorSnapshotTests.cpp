#include "editor/Editor.h"

#include <iostream>
#include <stdexcept>

int main()
{
    auto &editor = Editor::GetInstance();
    SceneSession::AuthoredSnapshot output;
    output.revision = 91;
    std::string error;
    if (editor.TryGetAuthoredSceneSnapshot(output, error))
    {
        std::cerr << "stopped editor unexpectedly returned a scene snapshot\n";
        return 1;
    }
    if (error.find("stopped") == std::string::npos || output.revision != 91)
    {
        std::cerr << "stopped editor did not preserve output or report its lifecycle state\n";
        return 1;
    }
    return 0;
}
