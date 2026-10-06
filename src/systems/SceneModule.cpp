/**
 * @file SceneModule.cpp
 * @brief Implementation of SceneModule contract extraction
 */

#include "systems/SceneModule.h"
#include "util/Logger.h"

namespace fs = std::filesystem;

const std::unordered_set<std::string> &GetKnownRoles()
{
    static const std::unordered_set<std::string> roles = {
        "game", "constants", "data", "input", "grid", "entity"};
    return roles;
}

SceneRole ParseRole(const std::string &roleStr)
{
    if (roleStr == "game")
        return SceneRole::Game;
    if (roleStr == "constants")
        return SceneRole::Constants;
    if (roleStr == "data")
        return SceneRole::Data;
    if (roleStr == "input")
        return SceneRole::Input;
    if (roleStr == "grid")
        return SceneRole::Grid;
    if (roleStr == "entity")
        return SceneRole::Entity;
    return SceneRole::Custom;
}

std::vector<std::string> SceneModule::ExtractContract(sol::state &lua)
{
    std::vector<std::string> warnings;

    // Handle nil return
    if (!returnValue.valid() || returnValue.get_type() == sol::type::nil)
    {
        didReturnNil = true;
        isLegacy = true;
        warnings.push_back("Script returned nil - consider returning a module table");
        return warnings;
    }

    // Must be a table to have contract
    if (returnValue.get_type() != sol::type::table)
    {
        isLegacy = true;
        warnings.push_back("Script returned non-table (" +
                           std::string(sol::type_name(lua, returnValue.get_type())) +
                           ") - contract extraction skipped");
        return warnings;
    }

    sol::table module = returnValue.as<sol::table>();

    // Check for _contract subtable
    sol::optional<sol::table> contract = module["_contract"];
    if (!contract.has_value())
    {
        isLegacy = true;
        // Not necessarily a warning - many valid scripts won't have contracts yet
        return warnings;
    }

    sol::table contractTable = contract.value();

    // Extract role
    sol::optional<std::string> roleOpt = contractTable["role"];
    if (roleOpt.has_value())
    {
        roleString = roleOpt.value();
        role = ParseRole(roleString);

        if (role == SceneRole::Custom &&
            GetKnownRoles().find(roleString) == GetKnownRoles().end())
        {
            warnings.push_back("Unknown role '" + roleString +
                               "' - will be treated as custom role");
        }
    }

    // Extract requires (dependencies)
    sol::optional<sol::table> requiresOpt = contractTable["requires"];
    if (requiresOpt.has_value())
    {
        sol::table requiresTable = requiresOpt.value();
        for (auto &kv : requiresTable)
        {
            if (kv.second.get_type() == sol::type::string)
            {
                requires.push_back(kv.second.as<std::string>());
            }
        }
    }

    // Extract needs (engine capabilities)
    sol::optional<sol::table> needsOpt = contractTable["needs"];
    if (needsOpt.has_value())
    {
        sol::table needsTable = needsOpt.value();
        for (auto &kv : needsTable)
        {
            if (kv.second.get_type() == sol::type::string)
            {
                needs.push_back(kv.second.as<std::string>());
            }
        }
    }

    return warnings;
}

std::unordered_set<std::string> SceneModuleLoader::CaptureGlobalKeys(sol::state &lua)
{
    std::unordered_set<std::string> keys;
    sol::table globals = lua.globals();

    for (auto &pair : globals)
    {
        if (pair.first.get_type() == sol::type::string)
        {
            keys.insert(pair.first.as<std::string>());
        }
    }

    return keys;
}

std::vector<std::string> SceneModuleLoader::DetectGlobalMutations(
    sol::state &lua,
    const std::unordered_set<std::string> &beforeKeys)
{
    std::vector<std::string> newKeys;
    sol::table globals = lua.globals();

    for (auto &pair : globals)
    {
        if (pair.first.get_type() == sol::type::string)
        {
            std::string key = pair.first.as<std::string>();
            if (beforeKeys.find(key) == beforeKeys.end())
            {
                newKeys.push_back(key);
            }
        }
    }

    return newKeys;
}

SceneModule SceneModuleLoader::Load(sol::state &lua, const std::string &scriptPath)
{
    SceneModule module;
    module.path = scriptPath;
    module.name = fs::path(scriptPath).stem().string();

    // Capture global state before execution
    auto globalsBefore = CaptureGlobalKeys(lua);

    // Execute script and capture return value
    try
    {
        sol::protected_function_result result = lua.script_file(scriptPath);
        if (result.valid())
        {
            module.loadSucceeded = true;
            module.returnValue = result.get<sol::object>();
        }
        else
        {
            sol::error err = result;
            module.loadError = err.what();
            LOG_ERROR("[SceneModule] Script execution failed for '{}': {}",
                      module.name, module.loadError);
            return module;
        }
    }
    catch (const std::exception &e)
    {
        module.loadError = e.what();
        LOG_ERROR("[SceneModule] Script load exception for '{}': {}",
                  module.name, module.loadError);
        return module;
    }

    // Detect global mutations
    module.addedGlobals = DetectGlobalMutations(lua, globalsBefore);
    for (const auto &global : module.addedGlobals)
    {
        LOG_WARNING("[SceneModule] Script '{}' added global '{}' - "
                    "consider returning module instead",
                    module.name, global);
    }

    // Extract contract metadata
    auto warnings = module.ExtractContract(lua);
    for (const auto &warning : warnings)
    {
        LOG_WARNING("[SceneModule] {}: {}", module.name, warning);
    }

    LOG_DEBUG("[SceneModule] Loaded '{}' - legacy: {}, role: '{}', requires: {}, needs: {}",
              module.name,
              module.isLegacy ? "yes" : "no",
              module.roleString,
              module.requires.size(),
              module.needs.size());

    return module;
}
