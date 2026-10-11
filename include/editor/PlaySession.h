/** @file PlaySession.h
 *  @brief Existing game configuration selection and one owned runtime child.
 */
#pragma once
#include <string>
#include <yaml-cpp/yaml.h>

/** Owns the selected game configuration and a single separately launched runtime. */
class PlaySession
{
public:
    /** Stop and reap the owned runtime child, if one is still running. */
    ~PlaySession();

    /** Construct a session with no selected configuration or running child. */
    PlaySession() = default;

    /** Copying is disabled because the session owns a child process. */
    PlaySession(const PlaySession &) = delete;

    /** Copy assignment is disabled because the session owns a child process. */
    PlaySession &operator=(const PlaySession &) = delete;

    /**
     * @brief Select and validate an existing game configuration.
     * @param resourceRoot Project resource directory
     * @param relativePath Configuration path relative to resourceRoot
     * @return true when the config and its referenced UI files are valid
     * @note On failure, the prior selection is retained and GetStatus() describes the error.
     */
    bool SelectConfig(const std::string &resourceRoot, const std::string &relativePath);

    /**
     * @brief Launch the saved scene in an owned child process.
     * @param scenePath Saved authored scene path
     * @param dirty Whether the current authored document has unsaved changes
     * @param executable Runtime executable path
     * @param workingDirectory Project build directory used by the child
     * @return true if the child was started; false if validation or launch failed
     * @note Requires a selected config and a clean, saved scene. Status and output path are available through getters.
     */
    bool Start(const std::string &scenePath, bool dirty, const std::string &executable,
               const std::string &workingDirectory);

    /** Reap an exited child without blocking and update its status. */
    void Poll();

    /** Terminate and reap the owned child; safe to call repeatedly. */
    void Stop();

    /** @return true while the owned child process is running. */
    bool IsRunning() const { return m_pid > 0; }

    /** @return Current selection, launch, or process-exit status. */
    const std::string &GetStatus() const { return m_status; }

    /** @return Selected configuration path relative to the resource root. */
    const std::string &GetConfigPath() const { return m_configPath; }

    /** @return Log path for the most recent Play launch, or empty before launch. */
    const std::string &GetLogPath() const { return m_logPath; }
private:
    YAML::Node m_config;
    std::string m_resourceRoot;
    std::string m_configPath;
    std::string m_logPath;
    std::string m_status = "Select an existing game configuration.";
    int m_pid = -1;
};
