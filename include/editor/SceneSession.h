/** @file SceneSession.h
 *  @brief Open/save command state shared by editor controls and headless tests.
 */
#pragma once
#include "editor/SceneDocument.h"
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

/** Opaque handle to an authored object in one SceneSession document instance. */
class AuthoredObjectHandle
{
private:
    friend class SceneSession;
    AuthoredObjectHandle(uint64_t owner, uint64_t document, uint64_t value)
        : m_owner(owner), m_document(document), m_value(value) {}
    uint64_t m_owner = 0;
    uint64_t m_document = 0;
    uint64_t m_value = 0;
};

class SceneSession
{
public:
    struct AuthoredSnapshot
    {
        std::string documentIdentity;
        uint64_t revision = 0;
        std::string path;
        bool dirty = false;
        YAML::Node scene;
    };

    using PreparePreview = std::function<bool(const YAML::Node &, std::string &)>;
    SceneSession();
    SceneSession(const SceneSession &) = delete;
    SceneSession &operator=(const SceneSession &) = delete;
    /** Stage a load and optional preview before replacing the current document. */
    bool Open(const std::string &path, bool discardEdits = false, const PreparePreview &prepare = {});
    bool Create(const std::string &name, bool discardEdits = false, const PreparePreview &prepare = {});
    bool AddObject(const std::string &name, const PreparePreview &prepare = {});
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model, const PreparePreview &prepare = {});
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model,
                         const PreparePreview &prepare = {});
    bool SetModules(const std::string &resourceRoot, const std::vector<std::string> &scripts);
    bool SetCapability(const std::string &name, bool enabled);
    bool SetStrictMode(bool enabled);
    /** Empty path saves the current document; another path is an explicit Save As. */
    bool Save(const std::string &path = "");
    bool SetPosition(const std::string &name, double x, double y, double z);
    /** Revision for optimistic authored-scene edits; successful state transitions advance it once. */
    uint64_t GetRevision() const { return m_revision; }
    /** Stable for this in-memory document; changes on successful Open or Create. */
    std::string GetDocumentIdentity() const;
    /** Copy authored session state without changing document, revision, or dirty state. */
    AuthoredSnapshot GetAuthoredSnapshot() const;
    /** Resolve a name to an opaque handle scoped to this session's current document. */
    std::optional<AuthoredObjectHandle> GetObjectHandle(const std::string &name) const;
    /** List object names paired with opaque handles for the current document. */
    std::vector<std::pair<std::string, AuthoredObjectHandle>> GetObjectHandles() const;
    /** Apply one in-memory position edit if both its handle and expected revision are current. */
    bool SetPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision,
                     double x, double y, double z);
    /** Undo the most recent handle-based position edit, guarded by the current revision. */
    bool UndoPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision);
    /** Validate transform drafts while preserving unrelated authored coordinates. */
    bool SetScaleText(const std::string &name, size_t axis, const std::string &text);
    bool SetFlyingPropsProperty(const std::string &name, const std::string &root, const std::string &property, const std::string &value);
    bool SetPositionText(const std::string &name, size_t axis, const std::string &text);
    /** Revert rejected text when selection changes and fields reload authored values. */
    void DiscardInvalidTransformDrafts(const std::string &name);
    YAML::Node Snapshot() const { return m_document.Snapshot(); }
    bool IsDirty() const { return m_document.IsDirty() || !m_invalidTransforms.empty(); }
    const std::string &GetPath() const { return m_path; }
    const std::string &GetError() const { return m_error; }
private:
    struct PositionUndo
    {
        uint64_t handleValue;
        SceneDocument before;
    };
    bool AdvanceRevision();
    void ResetObjectHandles();
    void SyncObjectHandles();
    std::optional<std::string> ResolveObjectHandle(const AuthoredObjectHandle &handle) const;
    bool SetTransformText(const std::string &name, const std::string &group, size_t axis, const std::string &text);
    bool Edit(const std::function<bool(SceneDocument &)> &operation, const PreparePreview &prepare);
    std::set<std::pair<std::string, size_t>> m_invalidTransforms;
    SceneDocument m_document;
    uint64_t m_sessionId = 0;
    uint64_t m_documentId = 0;
    uint64_t m_revision = 0;
    uint64_t m_nextHandleValue = 1;
    std::map<std::string, uint64_t> m_objectHandles;
    std::map<uint64_t, std::string> m_handleObjects;
    std::optional<PositionUndo> m_positionUndo;
    std::string m_path;
    std::string m_error;
};
