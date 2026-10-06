/**
 * @file SceneModule.h
 * @brief Loaded Lua script with optional contract metadata
 *
 * SceneModule captures metadata from loaded Lua scripts, enabling:
 * - Role-based dependency resolution
 * - Capability requirements validation
 * - Backward compatibility with legacy scripts
 */

#pragma once

#include <sol/sol.hpp>
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>

/**
 * @brief Known script roles for scene-local wiring
 *
 * Roles define how modules participate in the scene context.
 * Each role should have at most one provider per scene.
 */
enum class SceneRole
{
    None,      ///< No role declared (legacy scripts)
    Game,      ///< Main game controller
    Constants, ///< Configuration/constants provider
    Data,      ///< Static data definitions
    Input,     ///< Input handler
    Grid,      ///< Game grid/world management
    Entity,    ///< Entity factory/class definition
    Custom     ///< User-defined role (stored in customRole)
};

/**
 * @brief Get known role names for validation
 * @return Set of recognized role strings
 */
const std::unordered_set<std::string> &GetKnownRoles();

/**
 * @brief Convert role string to enum
 * @param roleStr Role name from Lua
 * @return Corresponding enum value
 */
SceneRole ParseRole(const std::string &roleStr);

/**
 * @brief Represents a loaded Lua script with optional contract metadata
 *
 * SceneModule captures:
 * - Script path and returned Lua object
 * - Optional role declaration (how it fits into scene)
 * - Optional dependencies (which roles it requires)
 * - Optional engine capability requirements
 *
 * Scripts can declare contracts by returning a table with:
 * @code
 * return {
 *     _contract = {
 *         role = "game",                    -- optional
 *         requires = {"constants", "data"}, -- optional
 *         needs = {"ui", "input"}           -- optional engine capabilities
 *     },
 *     -- actual module content...
 * }
 * @endcode
 *
 * Legacy scripts (returning nil or tables without _contract) are supported
 * with warnings logged during validation phase.
 */
struct SceneModule
{
    // === Identification ===
    std::string path;  ///< Full path to script file
    std::string name;  ///< Derived name (filename without extension)

    // === Lua State ===
    sol::object returnValue;   ///< What the script returned (may be nil)
    bool loadSucceeded = false; ///< True only when the Lua file executed successfully
    std::string loadError;      ///< Lua load/runtime error when execution failed
    bool didReturnNil = false; ///< True if script returned nil/nothing
    bool isLegacy = false;     ///< True if no _contract declared

    // === Contract: Role ===
    SceneRole role = SceneRole::None;
    std::string customRole; ///< For SceneRole::Custom
    std::string roleString; ///< Original role string from Lua

    // === Contract: Dependencies ===
    std::vector<std::string> requires; ///< Roles this module depends on

    // === Contract: Engine Capabilities ===
    std::vector<std::string> needs; ///< Engine capabilities required

    // === Resolution State ===
    bool isInitialized = false; ///< Has init(ctx) been called?
    int initOrder = -1;         ///< Order in which init() was called

    // === Global Mutations ===
    std::vector<std::string> addedGlobals; ///< Globals added during script execution

    // === Validation Helpers ===
    bool HasRole() const { return role != SceneRole::None; }
    bool HasDependencies() const { return !requires.empty(); }
    bool HasCapabilityRequirements() const { return !needs.empty(); }

    /**
     * @brief Extract contract metadata from returned Lua table
     * @param lua Reference to Lua state for type checking
     * @return Vector of warning messages (empty if clean)
     */
    std::vector<std::string> ExtractContract(sol::state &lua);
};

/**
 * @brief Factory for creating SceneModule from loaded script
 */
class SceneModuleLoader
{
public:
    /**
     * @brief Load script and extract module metadata
     * @param lua Lua state to execute in
     * @param scriptPath Full path to Lua script
     * @return Loaded SceneModule with extracted contract
     */
    static SceneModule Load(sol::state &lua, const std::string &scriptPath);

private:
    /**
     * @brief Capture current global keys from Lua state
     * @param lua Lua state
     * @return Set of current global key names
     */
    static std::unordered_set<std::string> CaptureGlobalKeys(sol::state &lua);

    /**
     * @brief Detect if script mutated global namespace
     * @param lua Lua state
     * @param beforeKeys Keys in _G before execution
     * @return Vector of newly added global keys
     */
    static std::vector<std::string> DetectGlobalMutations(
        sol::state &lua,
        const std::unordered_set<std::string> &beforeKeys);
};
