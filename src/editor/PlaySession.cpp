/**
 * @file PlaySession.cpp
 * @brief Validate game configurations and manage the Editor-owned Play process.
 * @lines ~227
 *
 * Key functions:
 * - SelectConfig() - Validate an existing config and its UI references (line ~45)
 * - Start() - Launch the saved scene in a child process with isolated output (line ~87)
 * - Poll() - Reap an exited child and update status (line ~185)
 * - Stop() - Terminate and reap the owned child (line ~206)
 */
#include "editor/PlaySession.h"
#include "editor/SceneDocument.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <thread>
#if defined(__APPLE__) || defined(__linux__)
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
extern char **environ;
#endif

namespace fs = std::filesystem;
namespace
{
    fs::path ResourceFile(const fs::path &root, const fs::path &relative)
    {
        if (relative.empty() || relative.is_absolute())
        {
            throw std::runtime_error("Choose a resource-relative file path.");
        }
        const auto file = fs::canonical(root / relative);
        const auto local = file.lexically_relative(fs::canonical(root));
        if (local.empty() || *local.begin() == ".." || !fs::is_regular_file(file))
        {
            throw std::runtime_error("File must be inside the resource directory.");
        }
        return file;
    }
}

PlaySession::~PlaySession()
{
    Stop();
}

bool PlaySession::SelectConfig(const std::string &resourceRoot, const std::string &relativePath)
{
    try
    {
        if (IsRunning())
        {
            throw std::runtime_error("Stop Play before changing configuration.");
        }
        if (resourceRoot.empty())
        {
            throw std::runtime_error("Resource directory is required.");
        }
        const auto root = fs::canonical(resourceRoot);
        const auto path = ResourceFile(root, relativePath);
        auto config = YAML::LoadFile(path.string());
        const auto game = config["game"];
        if (!game.IsMap() || !config["window"].IsMap())
        {
            throw std::runtime_error("Configuration requires game and window maps.");
        }
        // Validate HUD references without running its Lua state or changing source files.
        for (const auto *key : {"htmlPath", "cssPath", "luaStatePath"})
        {
            ResourceFile(root, game[key].as<std::string>());
        }
        if (game["templateName"].as<std::string>().empty())
        {
            throw std::runtime_error("Configuration requires a template name.");
        }
        m_config = YAML::Clone(config);
        m_resourceRoot = root.string();
        m_configPath = relativePath;
        m_status = "Configuration selected: " + relativePath;
        return true;
    }
    catch (const std::exception &error)
    {
        m_status = error.what();
        return false;
    }
}

bool PlaySession::Start(const std::string &scenePath, bool dirty, const std::string &executable,
                        const std::string &workingDirectory)
{
    try
    {
        Poll();
        if (IsRunning())
        {
            throw std::runtime_error("Play is already running.");
        }
        if (dirty || scenePath.empty())
        {
            throw std::runtime_error("Save the authored scene before Play.");
        }
        if (m_configPath.empty())
        {
            throw std::runtime_error("Select an existing game configuration first.");
        }
        // Revalidate references in case files changed since selection.
        if (!SelectConfig(m_resourceRoot, m_configPath))
        {
            return false;
        }
        const auto cwd = fs::canonical(workingDirectory);
        if (fs::canonical(cwd / "../res") != fs::path(m_resourceRoot))
        {
            throw std::runtime_error("Play must run from this project's build directory.");
        }
        const auto scene = fs::canonical(scenePath);
        const auto relative = scene.lexically_relative(m_resourceRoot);
        ResourceFile(m_resourceRoot, relative);
        SceneDocument document;
        if (!document.Load(scene.string()))
        {
            throw std::runtime_error(document.GetError());
        }
        const auto binary = fs::canonical(executable);
#if defined(__APPLE__) || defined(__linux__)
        // Reserve a unique output directory atomically; never overwrite authored files.
        std::string directory = (cwd / ".editor-play-XXXXXX").string();
        std::vector<char> buffer(directory.begin(), directory.end());
        buffer.push_back('\0');
        if (!mkdtemp(buffer.data()))
        {
            throw std::runtime_error(std::strerror(errno));
        }
        directory = buffer.data();
        auto config = YAML::Clone(m_config);
        config["game"]["scenePath"] = relative.generic_string();
        config["game"]["logPath"] = "editor-play-" + fs::path(directory).filename().string() + ".log";
        const auto configPath = (fs::path(directory) / "settings.yaml").string();
        m_logPath = (fs::path(directory) / "output.log").string();
        std::ofstream output(configPath);
        output << config;
        output.close();
        if (!output)
        {
            throw std::runtime_error("Cannot write the Play configuration.");
        }
        std::vector<std::string> environment;
        for (char **entry = environ; *entry; ++entry)
        {
            if (std::strncmp(*entry, "USER_SETTINGS_PATH=", 19) != 0)
            {
                environment.emplace_back(*entry);
            }
        }
        std::vector<char *> env;
        for (auto &entry : environment) env.push_back(entry.data());
        env.push_back(nullptr);
        std::string binaryPath = binary.string();
        std::string argument = "--config";
        std::string configArgument = configPath;
        char *argv[] = {binaryPath.data(), argument.data(), configArgument.data(), nullptr};
        posix_spawn_file_actions_t actions;
        int result = posix_spawn_file_actions_init(&actions);
        if (result != 0) throw std::runtime_error(std::strerror(result));
        result = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
        if (result == 0) result = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, m_logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (result == 0) result = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        pid_t pid = -1;
        if (result == 0) result = posix_spawn(&pid, binaryPath.c_str(), &actions, nullptr, argv, env.data());
        posix_spawn_file_actions_destroy(&actions);
        if (result != 0) throw std::runtime_error(std::strerror(result));
        m_pid = pid;
        m_status = "Runtime launched; output: " + m_logPath;
        return true;
#else
        throw std::runtime_error("Separate-process Play is currently supported on macOS and Linux.");
#endif
    }
    catch (const std::exception &error)
    {
        m_status = std::string("Play failed: ") + error.what();
        return false;
    }
}

void PlaySession::Poll()
{
#if defined(__APPLE__) || defined(__linux__)
    if (!IsRunning()) return;
    int status = 0;
    const auto result = waitpid(m_pid, &status, WNOHANG);
    if (result == m_pid)
    {
        m_pid = -1;
        m_status = WIFEXITED(status) ? "Runtime exited with code " + std::to_string(WEXITSTATUS(status))
                                   : "Runtime ended by signal " + std::to_string(WTERMSIG(status));
        m_status += "; output: " + m_logPath;
    }
    else if (result < 0 && errno == ECHILD)
    {
        m_pid = -1;
        m_status = "Runtime is no longer owned by the Editor.";
    }
#endif
}

void PlaySession::Stop()
{
#if defined(__APPLE__) || defined(__linux__)
    Poll();
    if (!IsRunning()) return;
    // Only signal our unreaped child, never a process name or a selected external PID.
    kill(m_pid, SIGTERM);
    for (int attempt = 0; attempt < 50 && IsRunning(); ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Poll();
    }
    if (IsRunning())
    {
        kill(m_pid, SIGKILL);
        int status = 0;
        while (waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}
        m_pid = -1;
    }
    m_status = "Play stopped; output: " + m_logPath;
#endif
}
