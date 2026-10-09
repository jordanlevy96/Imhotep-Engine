#include "editor/SceneDocument.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: imhotep-scene-inspect <project-root> <scene-path>\n";
        return 2;
    }

    std::error_code error;
    const fs::path root = fs::canonical(argv[1], error);
    if (error || !fs::is_directory(root))
    {
        std::cerr << "invalid project root\n";
        return 2;
    }
    const fs::path requested(argv[2]);
    if (requested.empty() || requested.is_absolute())
    {
        std::cerr << "scene path must be relative to project root\n";
        return 2;
    }
    const fs::path scene = fs::canonical(root / requested, error);
    if (error || !fs::is_regular_file(scene) || scene.lexically_relative(root).empty() ||
        *scene.lexically_relative(root).begin() == "..")
    {
        std::cerr << "scene path is missing or outside project root\n";
        return 2;
    }

    SceneDocument document;
    if (!document.Load(scene.string()))
    {
        std::cerr << document.GetError() << '\n';
        return 1;
    }
    YAML::Emitter output;
    output << document.Snapshot();
    if (!output.good())
    {
        std::cerr << output.GetLastError() << '\n';
        return 1;
    }
    std::cout << output.c_str() << '\n';
    return 0;
}
