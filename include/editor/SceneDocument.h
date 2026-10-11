/**
 * @file SceneDocument.h
 * @brief Authored YAML scene editing without runtime script execution.
 */
#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>

/** Authored YAML scene model with validation, edit, and save operations. */
class SceneDocument
{
public:
    /**
     * @brief Create an unsaved authored scene with no objects or scripts.
     * @param name Scene name used in the document metadata
     * @return true on success; false when the name is invalid
     */
    bool Create(const std::string &name);

    /**
     * @brief Add an empty authored object.
     * @param name Unique, nonempty object name
     * @return true on success; false when the name is invalid or already used
     */
    bool AddObject(const std::string &name);

    /**
     * @brief Assign a render component to an authored object.
     * @param name Existing object name
     * @param resourceRoot Project resource directory
     * @param shader Shader path relative to resourceRoot/shaders
     * @param model Model path relative to resourceRoot/models
     * @return true when both resource paths are valid and the assignment succeeds
     * @note Existing properties and other component types are preserved; failures are reported by GetError().
     */
    bool AssignRenderComponent(const std::string &name, const std::string &resourceRoot,
                               const std::string &shader, const std::string &model);

    /**
     * @brief Assign a Lua script component and its render references.
     * @param name Existing object name
     * @param resourceRoot Project resource directory
     * @param script Lua script path relative to resourceRoot
     * @param shader Optional shader path relative to resourceRoot/shaders
     * @param model Optional model path relative to resourceRoot/models
     * @return true when the script and supplied resource paths are valid
     * @note Unrelated component properties are preserved; failures are reported by GetError().
     */
    bool AssignLuaScript(const std::string &name, const std::string &resourceRoot,
                         const std::string &script, const std::string &shader, const std::string &model);

    /**
     * @brief Set the ordered scene-level Lua module list without executing scripts.
     * @param resourceRoot Project resource directory
     * @param scripts Script paths relative to resourceRoot
     * @return true when every path is valid
     */
    bool SetModules(const std::string &resourceRoot, const std::vector<std::string> &scripts);

    /**
     * @brief Enable or disable a scene capability.
     * @param name Capability key
     * @param enabled New capability state
     * @return true when the value was applied
     */
    bool SetCapability(const std::string &name, bool enabled);

    /**
     * @brief Set strict module validation for the scene.
     * @param enabled New strict-mode state
     * @return true when applied
     */
    bool SetStrictMode(bool enabled);

    /** @return Independent copy of this document and its load/dirty/error state. */
    SceneDocument Clone() const;

    /** @return true after a scene has been loaded or created successfully. */
    bool IsLoaded() const { return m_loaded; }

    /**
     * @brief Load and validate authored YAML without creating runtime entities.
     * @param path YAML scene file to read
     * @return true on success; false on parse, schema, or file errors
     * @note Failure preserves the current document and sets GetError().
     */
    bool Load(const std::string &path);

    /**
     * @brief Set an object's authored position.
     * @param objectName Unique authored object name
     * @param x X coordinate
     * @param y Y coordinate
     * @param z Z coordinate
     * @return true when applied; false for a missing object or non-finite/out-of-range values
     */
    bool SetPosition(const std::string &objectName, double x, double y, double z);

    /**
     * @brief Set an object's authored scale.
     * @param objectName Unique authored object name
     * @param x X scale
     * @param y Y scale
     * @param z Z scale
     * @return true when applied; false for a missing object or non-finite/out-of-range values
     */
    bool SetScale(const std::string &objectName, double x, double y, double z);

    /** @return Ordered property names exposed for the supplied FlyingProps script. */
    static const std::vector<std::string> &FlyingPropsProperties();

    /**
     * @brief Set one supported scalar property on an object's FlyingProps component.
     * @param objectName Authored object containing the script component
     * @param resourceRoot Project resource directory
     * @param property Supported property name
     * @param value Property value as authored text
     * @return true when the component and property are valid and updated
     */
    bool SetFlyingPropsProperty(const std::string &objectName, const std::string &resourceRoot,
                                const std::string &property, const std::string &value);

    /**
     * @brief Save the authored YAML to a path through a sibling temporary file.
     * @param path Destination file
     * @return true on success; false on write or replacement errors
     * @note Failed saves retain dirty state and set GetError().
     */
    bool Save(const std::string &path);

    /** @return Independent YAML copy; modifying it does not change this document. */
    YAML::Node Snapshot() const { return YAML::Clone(m_scene); }

    /** @return true when authored data has edits or the created scene has not yet been saved. */
    bool IsDirty() const { return m_dirty; }

    /** @return Most recent operation error, or an empty string after success. */
    const std::string &GetError() const { return m_error; }

private:
    bool SetTransformVector(const std::string &objectName, const std::string &group, double x, double y, double z);
    bool AssignComponent(const std::string &name, const YAML::Node &fields);
    YAML::Node m_scene;
    bool m_loaded = false;
    bool m_dirty = false;
    std::string m_error;
};
