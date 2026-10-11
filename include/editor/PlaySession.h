/** @file PlaySession.h Selection and ownership for one launched game process. */
#pragma once

#include <string>
#include <yaml-cpp/yaml.h>

/** Owns the selected game configuration and at most one child process. */
class PlaySession
{
public:
    PlaySession() = default;
    ~PlaySession();

    PlaySession(const PlaySession &) = delete;
    PlaySession &operator=(const PlaySession &) = delete;

    /// Failure preserves the current selection and updates GetStatus().
    bool SelectConfig(const std::string &resourceRoot, const std::string &relativePath);

    /// Requires a selected config and a saved, clean scene; owns the launched child.
    bool Start(const std::string &scenePath, bool dirty, const std::string &executable,
               const std::string &workingDirectory);

    void Poll(); // Reaps an exited child without blocking.
    void Stop(); // Terminates and reaps the child; safe to call repeatedly.

    bool IsRunning() const { return m_pid > 0; }
    const std::string &GetStatus() const { return m_status; }
    /// Selected path relative to resourceRoot.
    const std::string &GetConfigPath() const { return m_configPath; }
    /// Empty before the first launch.
    const std::string &GetLogPath() const { return m_logPath; }

private:
    YAML::Node m_config;
    std::string m_resourceRoot;
    std::string m_configPath;
    std::string m_logPath;
    std::string m_status = "Select an existing game configuration.";
    int m_pid = -1;
};
