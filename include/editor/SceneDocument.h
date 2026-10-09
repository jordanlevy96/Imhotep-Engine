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
    /** Create an unsaved authored scene with no runtime objects or scripts. */
    bool Create(const std::string &name);
    bool AddObject(const std::string &name);
    /** Assign supported component references; retain unrelated component properties. */
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model);
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model);
    SceneDocument Clone() const;
    bool IsLoaded() const { return m_loaded; }
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
    bool AssignComponent(const std::string &name, const YAML::Node &fields);
    YAML::Node m_scene;
    bool m_loaded = false;
    bool m_dirty = false;
    std::string m_error;
};
