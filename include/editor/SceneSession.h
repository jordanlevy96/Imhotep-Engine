/** @file SceneSession.h
 *  @brief Open/save command state shared by editor controls and headless tests.
 */
#pragma once
#include "editor/SceneDocument.h"
#include <functional>
#include <set>
#include <utility>

class SceneSession
{
public:
    using PreparePreview = std::function<bool(const YAML::Node &, std::string &)>;
    /** Stage a load and optional preview before replacing the current document. */
    bool Open(const std::string &path, bool discardEdits = false, const PreparePreview &prepare = {});
    bool Create(const std::string &name, bool discardEdits = false, const PreparePreview &prepare = {});
    bool AddObject(const std::string &name, const PreparePreview &prepare = {});
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model, const PreparePreview &prepare = {});
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model,
                         const PreparePreview &prepare = {});
    /** Empty path saves the current document; another path is an explicit Save As. */
    bool Save(const std::string &path = "");
    bool SetPosition(const std::string &name, double x, double y, double z);
    /** Validate a text-field draft while preserving unrelated authored coordinates. */
    bool SetPositionText(const std::string &name, size_t axis, const std::string &text);
    /** Revert rejected text when selection changes and fields reload authored values. */
    void DiscardInvalidPositionDrafts(const std::string &name);
    YAML::Node Snapshot() const { return m_document.Snapshot(); }
    bool IsDirty() const { return m_document.IsDirty() || !m_invalidPositions.empty(); }
    const std::string &GetPath() const { return m_path; }
    const std::string &GetError() const { return m_error; }
private:
    bool Edit(const std::function<bool(SceneDocument &)> &operation, const PreparePreview &prepare);
    std::set<std::pair<std::string, size_t>> m_invalidPositions;
    SceneDocument m_document;
    std::string m_path;
    std::string m_error;
};
