/** @file PlaySession.h
 *  @brief Existing game configuration selection and one owned runtime child.
 */
#pragma once
#include <string>
#include <yaml-cpp/yaml.h>

class PlaySession
{
public:
    ~PlaySession();
    PlaySession() = default;
    PlaySession(const PlaySession &) = delete;
    PlaySession &operator=(const PlaySession &) = delete;
    bool SelectConfig(const std::string &resourceRoot, const std::string &relativePath);
    bool Start(const std::string &scenePath, bool dirty, const std::string &executable,
               const std::string &workingDirectory);
    void Poll();
    void Stop();
    bool IsRunning() const { return m_pid > 0; }
    const std::string &GetStatus() const { return m_status; }
    const std::string &GetConfigPath() const { return m_configPath; }
    const std::string &GetLogPath() const { return m_logPath; }
private:
    YAML::Node m_config;
    std::string m_resourceRoot;
    std::string m_configPath;
    std::string m_logPath;
    std::string m_status = "Select an existing game configuration.";
    int m_pid = -1;
};
