/** @file EditorPreviewAssetTests.cpp
 *  @brief Invalid resources must reject a staged scene before graphics upload.
 *  @lines ~92
 *
 *  Test cases:
 *  - main() - Reject malformed shaders/meshes and verify cleanup after simulated GL failures
 */
#include "editor/SceneSession.h"
#include "util/Shader.h"
#include "util/Mesh.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("imhotep-assets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    int result = 0;
    try
    {
        const auto scene = root / "scene.yaml";
        std::ofstream(scene) << "scene: {objects: [{name: light}]}\n";
        SceneSession session;
        if (!session.Open(scene.string()) || !session.SetPosition("light", 1, 2, 3))
        {
            throw std::runtime_error("Could not prepare edited scene");
        }
        const auto snapshot = YAML::Dump(session.Snapshot());
        auto reject = [&](const auto &load)
        {
            if (session.Open(scene.string(), true, [&](const YAML::Node &, std::string &)
                {
                    load();
                    return true;
                }))
                throw std::runtime_error("Invalid resource committed scene");
            if (!session.IsDirty() || YAML::Dump(session.Snapshot()) != snapshot || session.GetPath() != scene.string())
            {
                throw std::runtime_error("Resource failure lost edited document");
            }
        };
        const auto shader = root / "bad.shader";
        std::ofstream(shader) << "invalid text before section\n#shader vertex\n";
        reject([&] { Shader resource(shader.string()); });
        std::ofstream(shader) << "#shader vertex\nvoid main() {}\n";
        reject([&] { Shader resource(shader.string()); });
        const auto mesh = root / "bad.obj";
        std::ofstream(mesh) << "v 0 0 0\n";
        reject([&] { Mesh resource(mesh.string()); });
        std::ofstream(mesh) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        reject([&] { Mesh resource(mesh.string()); });
        reject([&] { Mesh resource((root / "missing.obj").string()); });
        // Simulate driver failures without a window or GL context; verify exception cleanup.
        static unsigned int nextShader = 0;
        static unsigned int failedShader = 0;
        static int deletedShaders = 0;
        static int deletedPrograms = 0;
        glad_glCreateShader = [](GLenum) -> GLuint { return ++nextShader; };
        glad_glShaderSource = [](GLuint, GLsizei, const GLchar *const *, const GLint *) {};
        glad_glCompileShader = [](GLuint) {};
        glad_glGetShaderiv = [](GLuint shaderId, GLenum, GLint *status) { *status = shaderId == failedShader ? GL_FALSE : GL_TRUE; };
        glad_glGetShaderInfoLog = [](GLuint, GLsizei, GLsizei *, GLchar *message) { message[0] = '\0'; };
        glad_glDeleteShader = [](GLuint) { ++deletedShaders; };
        glad_glCreateProgram = []() -> GLuint { return 10; };
        glad_glAttachShader = [](GLuint, GLuint) {};
        glad_glLinkProgram = [](GLuint) {};
        glad_glGetProgramiv = [](GLuint, GLenum, GLint *status) { *status = GL_FALSE; };
        glad_glGetProgramInfoLog = [](GLuint, GLsizei, GLsizei *, GLchar *message) { message[0] = '\0'; };
        glad_glDeleteProgram = [](GLuint) { ++deletedPrograms; };
        std::ofstream(shader) << "#shader vertex\nvoid main() {}\n#shader fragment\nvoid main() {}\n";
        for (unsigned int failAt : {1u, 2u, 0u})
        {
            nextShader = 0;
            deletedShaders = 0;
            deletedPrograms = 0;
            failedShader = failAt;
            reject([&] { Shader resource(shader.string()); });
            if (deletedShaders != (failAt == 1 ? 1 : 2) || deletedPrograms != (failAt == 0 ? 1 : 0))
            {
                throw std::runtime_error("Failed shader construction leaked GL objects");
            }
        }
        std::cout << "Editor invalid resource tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    fs::remove_all(root);
    return result;
}
