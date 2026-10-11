/** @file SceneSession.h Editor scene commands and preview staging. */
#pragma once

#include "editor/SceneDocument.h"
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

/** Opaque object identity valid only for its originating session and document. */
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

/** Maintains document identity and revision for optimistic edits. */
class SceneSession
{
public:
    /// Fields copied together for a consistent external read.
    struct AuthoredSnapshot
    {
        std::string documentIdentity;
        uint64_t revision = 0;
        std::string path;
        bool dirty = false;
        YAML::Node scene;
    };

    SceneSession();
    SceneSession(const SceneSession &) = delete;
    SceneSession &operator=(const SceneSession &) = delete;

    /// Runs before commit; false rejects the candidate and supplies an error.
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
    /// Successful authored changes advance this optimistic-concurrency revision.
    uint64_t GetRevision() const { return m_revision; }

    /// Stable within a document; changes after successful Open or Create.
    std::string GetDocumentIdentity() const;

    /// Detached copy of document identity, revision, path, dirty state, and YAML.
    AuthoredSnapshot GetAuthoredSnapshot() const;

    /// Returns a document-scoped handle, or empty when the name is absent.
    std::optional<AuthoredObjectHandle> GetObjectHandle(const std::string &name) const;

    /// Returns names paired with current-document handles.
    std::vector<std::pair<std::string, AuthoredObjectHandle>> GetObjectHandles() const;

    /// Rejects stale handles/revisions and invalid coordinates; failure sets GetError().
    bool SetPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision,
                     double x, double y, double z);

    /// Restores the last handle-based position edit if the handle and revision still match.
    bool UndoPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision);

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
