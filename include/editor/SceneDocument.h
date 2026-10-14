/** @file SceneDocument.h Authored YAML scene data and persistence. */
#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>

/** Edits authored scene data without executing runtime scripts. */
class SceneDocument
{
public:
    bool Create(const std::string &name);
    bool AddObject(const std::string &name);

    /// shader/model are relative to resourceRoot/shaders and /models; other component fields are preserved.
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model);
    /// script is relative to resourceRoot; optional shader/model use its subdirectories.
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model);

    /// Resolves script paths from resourceRoot; validates without executing them.
    bool SetModules(const std::string &resourceRoot, const std::vector<std::string> &scripts);
    bool SetCapability(const std::string &name, bool enabled);
    bool SetStrictMode(bool enabled);

    SceneDocument Clone() const;
    bool IsLoaded() const { return m_loaded; }

    // Failure leaves the current document intact; authored data is not instantiated here.
    bool Load(const std::string &path);
    bool SetPosition(const std::string &objectName, double x, double y, double z);
    bool SetScale(const std::string &objectName, double x, double y, double z);

    static const std::vector<std::string> &FlyingPropsProperties();
    /// Accepts only the scalar keys exposed by FlyingPropsProperties().
    bool SetFlyingPropsProperty(const std::string &objectName, const std::string &resourceRoot,
                                const std::string &property, const std::string &value);

    /// Writes through a sibling temporary file; failure retains dirty state.
    bool Save(const std::string &path);

    /** Returns a detached copy; edits to it do not modify this document. */
    YAML::Node Snapshot() const { return YAML::Clone(m_scene); }
    bool IsDirty() const { return m_dirty; }
    const std::string &GetError() const { return m_error; }

private:
    bool SetTransformVector(const std::string &objectName, const std::string &group, double x, double y, double z);
    bool AssignComponent(const std::string &name, const YAML::Node &fields);
    YAML::Node m_scene;
    bool m_loaded = false;
    bool m_dirty = false;
    std::string m_error;
};
