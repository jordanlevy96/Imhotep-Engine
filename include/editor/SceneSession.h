/** @file SceneSession.h
 *  @brief Open/save command state shared by editor controls and headless tests.
 *  @lines ~173
 *
 *  Quick-stats (Public API):
 *  - Open() / Create() / Save() - Stage scene command transitions
 *  - SetPosition() / SetScaleText() - Edit authored transforms
 *  - Snapshot() / IsDirty() - Read current authored state
 */
#pragma once
#include "editor/SceneDocument.h"
#include <functional>
#include <set>
#include <utility>

/** Coordinates scene commands, dirty state, validation, and optional preview staging. */
class SceneSession
{
public:
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
    bool SetTransformText(const std::string &name, const std::string &group, size_t axis, const std::string &text);
    bool Edit(const std::function<bool(SceneDocument &)> &operation, const PreparePreview &prepare);
    std::set<std::pair<std::string, size_t>> m_invalidTransforms;
    SceneDocument m_document;
    std::string m_path;
    std::string m_error;
};
