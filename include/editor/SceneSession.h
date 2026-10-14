/** @file SceneSession.h Editor scene commands and preview staging. */
#pragma once

#include "editor/SceneDocument.h"
#include <functional>
#include <set>
#include <utility>

class SceneSession
{
public:
    /// Runs before a candidate document is committed; false rejects it and supplies an error.
    using PreparePreview = std::function<bool(const YAML::Node &, std::string &)>;

    /// Dirty documents require discardEdits. Rejection preserves the current document.
    bool Open(const std::string &path, bool discardEdits = false, const PreparePreview &prepare = {});
    bool Create(const std::string &name, bool discardEdits = false, const PreparePreview &prepare = {});
    bool AddObject(const std::string &name, const PreparePreview &prepare = {});
    /// shader/model are relative to resourceRoot/shaders and /models.
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model, const PreparePreview &prepare = {});
    /// script is relative to resourceRoot; shader/model are optional.
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model,
                         const PreparePreview &prepare = {});

    /// Script paths are relative to resourceRoot.
    bool SetModules(const std::string &resourceRoot, const std::vector<std::string> &scripts);
    bool SetCapability(const std::string &name, bool enabled);
    bool SetStrictMode(bool enabled);

    /// Empty path saves to the current path.
    bool Save(const std::string &path = "");
    bool SetPosition(const std::string &name, double x, double y, double z);

    /// Invalid field text remains a dirty draft and blocks saving until corrected or discarded.
    bool SetScaleText(const std::string &name, size_t axis, const std::string &text);
    bool SetPositionText(const std::string &name, size_t axis, const std::string &text);
    bool SetFlyingPropsProperty(const std::string &name, const std::string &root,
                                const std::string &property, const std::string &value);
    void DiscardInvalidTransformDrafts(const std::string &name);

    /** Returns a detached copy of the current authored YAML. */
    YAML::Node Snapshot() const { return m_document.Snapshot(); }
    /// Includes invalid transform drafts that are not yet in the document.
    bool IsDirty() const { return m_document.IsDirty() || !m_invalidTransforms.empty(); }
    const std::string &GetPath() const { return m_path; }
    const std::string &GetError() const { return m_error; }

private:
    bool SetTransformText(const std::string &name, const std::string &group, size_t axis, const std::string &text);
    bool Edit(const std::function<bool(SceneDocument &)> &operation, const PreparePreview &prepare);
    std::set<std::pair<std::string, size_t>> m_invalidTransforms;
    SceneDocument m_document;
    std::string m_path;
    std::string m_error;
};
