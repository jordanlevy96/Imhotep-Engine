/**
 * @file SceneDocument.h
 * @brief Authored YAML scene editing without runtime script execution.
 */
#pragma once

#include <yaml-cpp/yaml.h>
#include <string>

class SceneDocument
{
public:
    /** Load a scene without creating entities. Failed loads preserve the document. */
    bool Load(const std::string &path);
    /** Edit one uniquely named authored object; reject invalid coordinates. */
    bool SetPosition(const std::string &objectName, double x, double y, double z);
    /** Save through a sibling temporary file. Failed saves retain dirty state. */
    bool Save(const std::string &path);
    /** Return an independent copy; callers cannot mutate the authored document. */
    YAML::Node Snapshot() const { return YAML::Clone(m_scene); }
    bool IsDirty() const { return m_dirty; }
    const std::string &GetError() const { return m_error; }

private:
    YAML::Node m_scene;
    bool m_loaded = false;
    bool m_dirty = false;
    std::string m_error;
};
