/** @file SceneSession.h
 *  @brief Open/save command state shared by editor controls and headless tests.
 *  @lines ~262
 *
 *  Quick-stats (Public API):
 *  - Open() / Create() / Save() - Stage scene command transitions
 *  - SetPosition() / SetScaleText() - Edit authored transforms
 *  - Snapshot() / IsDirty() - Read current authored state
 *  - GetAuthoredSnapshot() / GetObjectHandles() - Read revisioned scene state
 */
#pragma once
#include "editor/SceneDocument.h"
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

/** @brief Opaque identity for an authored object in one SceneSession document. */
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

/** Coordinates authored scene commands, revisioned edits, and optional preview staging. */
class SceneSession
{
public:
    struct AuthoredSnapshot
    {
        std::string documentIdentity; ///< Identity of the in-memory document.
        uint64_t revision = 0; ///< Revision captured with this snapshot.
        std::string path; ///< Current scene path, empty before Save As.
        bool dirty = false; ///< Whether authored edits are unsaved.
        YAML::Node scene; ///< Independent copy of authored YAML.
    };

    /**
     * @brief Construct an empty session with its own document identity.
     */
    SceneSession();

    /** Copying is disabled because handles and identity are scoped to one session. */
    SceneSession(const SceneSession &) = delete;

    /** Copy assignment is disabled because handles and identity are scoped to one session. */
    SceneSession &operator=(const SceneSession &) = delete;

    /**
     * Preview hook run before a candidate document is committed.
     * @param scene Candidate authored YAML
     * @param error Output error used when the callback rejects the candidate
     * @return true to accept the preview; false to reject the operation
     */
    using PreparePreview = std::function<bool(const YAML::Node &, std::string &)>;

    /**
     * @brief Open a scene, optionally rejecting the current preview before commit.
     * @param path Scene YAML path
     * @param discardEdits Allow replacing a dirty current document
     * @param prepare Optional callback that validates/stages the candidate preview
     * @return true when the new document and preview are accepted
     * @note Failure preserves the current document and reports the reason through GetError().
     */
    bool Open(const std::string &path, bool discardEdits = false, const PreparePreview &prepare = {});

    /**
     * @brief Create an empty scene and optionally stage its preview before commit.
     * @param name New scene name
     * @param discardEdits Allow replacing a dirty current document
     * @param prepare Optional callback that validates/stages the candidate preview
     * @return true when creation succeeds; false leaves the current document intact
     */
    bool Create(const std::string &name, bool discardEdits = false, const PreparePreview &prepare = {});

    /**
     * @brief Add an object and validate its candidate preview before commit.
     * @param name Unique object name
     * @param prepare Optional preview validation callback
     * @return true on commit; false leaves the document unchanged
     */
    bool AddObject(const std::string &name, const PreparePreview &prepare = {});

    /**
     * @brief Assign a render component and validate the resulting preview before commit.
     * @param name Existing object name
     * @param resourceRoot Project resource directory
     * @param shader Shader path relative to its shader resource directory
     * @param model Model path relative to its model resource directory
     * @param prepare Optional preview validation callback
     * @return true on commit; false leaves the document unchanged
     */
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model, const PreparePreview &prepare = {});

    /**
     * @brief Assign a Lua component and validate the resulting preview before commit.
     * @param name Existing object name
     * @param resourceRoot Project resource directory
     * @param script Script path relative to resourceRoot
     * @param shader Optional shader path
     * @param model Optional model path
     * @param prepare Optional preview validation callback
     * @return true on commit; false leaves the document unchanged
     */
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model,
                         const PreparePreview &prepare = {});

    /**
     * @brief Set ordered scene script modules after validating their paths.
     * @param resourceRoot Project resource directory
     * @param scripts Script paths relative to resourceRoot
     * @return true when applied; false when a path or edit is invalid
     */
    bool SetModules(const std::string &resourceRoot, const std::vector<std::string> &scripts);

    /**
     * @brief Set a scene capability.
     * @param name Capability key
     * @param enabled New state
     * @return true when applied; false when the document rejects the change
     */
    bool SetCapability(const std::string &name, bool enabled);

    /**
     * @brief Set strict module validation.
     * @param enabled New state
     * @return true when applied; false when the document rejects the change
     */
    bool SetStrictMode(bool enabled);

    /**
     * @brief Save the document to its current path or an explicit Save As path.
     * @param path Optional destination; empty uses the current path
     * @return true on save; false when validation or disk writing fails
     */
    bool Save(const std::string &path = "");

    /**
     * @brief Set an authored object's position.
     * @param name Unique object name
     * @param x X coordinate
     * @param y Y coordinate
     * @param z Z coordinate
     * @return true when applied; false on invalid input or unknown object
     */
    bool SetPosition(const std::string &name, double x, double y, double z);
    /** @return Current optimistic-concurrency revision; successful authored changes advance it. */
    uint64_t GetRevision() const { return m_revision; }

    /** @return Identity stable for this document and changed by successful Open or Create. */
    std::string GetDocumentIdentity() const;

    /** @return Independent snapshot of document identity, revision, path, dirty state, and YAML. */
    AuthoredSnapshot GetAuthoredSnapshot() const;

    /**
     * @brief Resolve an authored object name to its current opaque handle.
     * @param name Unique authored object name
     * @return Current document-scoped handle, or empty when the name is absent
     */
    std::optional<AuthoredObjectHandle> GetObjectHandle(const std::string &name) const;

    /** @return Object names paired with handles scoped to the current document. */
    std::vector<std::pair<std::string, AuthoredObjectHandle>> GetObjectHandles() const;

    /**
     * @brief Apply an in-memory position edit using a current handle and revision.
     * @param handle Object handle from this session's current document
     * @param expectedRevision Revision the caller inspected
     * @param x New X coordinate
     * @param y New Y coordinate
     * @param z New Z coordinate
     * @return true when applied; false for stale identity/revision or invalid coordinates, with GetError() set
     */
    bool SetPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision,
                     double x, double y, double z);

    /**
     * @brief Undo the most recent handle-based position edit.
     * @param handle Handle used by the edit
     * @param expectedRevision Current revision expected by the caller
     * @return true when restored; false for stale state or no undo record, with GetError() set
     */
    bool UndoPosition(const AuthoredObjectHandle &handle, uint64_t expectedRevision);

    /**
     * @brief Parse and apply one scale field while preserving other axes.
     * @param name Unique object name
     * @param axis Coordinate index from 0 to 2
     * @param text User-entered numeric value
     * @return true when applied; false when invalid, with saving blocked until corrected or discarded
     */
    bool SetScaleText(const std::string &name, size_t axis, const std::string &text);

    /**
     * @brief Set one supported FlyingProps property.
     * @param name Unique object name
     * @param root Project resource directory
     * @param property Supported property name
     * @param value Authored scalar value
     * @return true when applied; false when the object, component, or property is invalid
     */
    bool SetFlyingPropsProperty(const std::string &name, const std::string &root, const std::string &property, const std::string &value);

    /**
     * @brief Parse and apply one position field while retaining other axes.
     * @param name Unique object name
     * @param axis Coordinate index from 0 to 2
     * @param text User-entered numeric value
     * @return true when applied; false when invalid, with saving blocked until corrected or discarded
     */
    bool SetPositionText(const std::string &name, size_t axis, const std::string &text);

    /**
     * @brief Discard invalid transform drafts for one object.
     * @param name Object whose edited fields are being reloaded
     */
    void DiscardInvalidTransformDrafts(const std::string &name);

    /** @return Independent YAML copy of the current authored document. */
    YAML::Node Snapshot() const { return m_document.Snapshot(); }

    /** @return true if authored data or a rejected transform draft remains unsaved. */
    bool IsDirty() const { return m_document.IsDirty() || !m_invalidTransforms.empty(); }

    /** @return Current scene path, or empty for a newly created unsaved scene. */
    const std::string &GetPath() const { return m_path; }

    /** @return Most recent operation error, or empty after success. */
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
