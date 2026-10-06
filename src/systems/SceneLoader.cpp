/**
 * @file SceneLoader.cpp
 * @brief Scene module loading with contract validation and dependency resolution
 * @lines ~545
 *
 * Purpose: Loads scene configuration with modular Lua scripts using contract system.
 * Validates role/capability requirements and resolves script dependencies via topological sort.
 *
 * Key functions:
 * - LoadScripts() - Load scene modules from YAML (line 11, ~115 lines)
 * - LoadAllScripts() - Execute Lua scripts in dependency order (line 127, ~25 lines)
 * - ValidateRoles() - Check required roles provided (line 151, ~25 lines)
 * - ValidateCapabilities() - Check required capabilities available (line 177, ~95 lines)
 * - TopologicalSort() - Order modules by dependencies (line 275, ~90 lines)
 * - DetectCycles() - Find circular dependencies (line 364, ~65 lines)
 * - InitializeModules() - Call module init() functions (line 431, ~110 lines)
 *
 * Contract system:
 * - Modules declare roles (what they implement)
 * - Modules declare capabilities (what they provide)
 * - Modules declare requires (what they need)
 * - Loader validates all contracts before execution
 *
 * Scene file format: YAML with scripts list, each with path and metadata
 *
 * Integration: Used by Registry::LoadScene() for modular scene composition
 */

#include "systems/SceneLoader.h"
#include "controllers/Game.h"
#include "controllers/ScriptManager.h"
#include "util/Logger.h"

bool SceneLoader::LoadScripts(const std::string &scenePath)
{
    const std::string &res = Game::GetInstance().conf.ResourcePath;

    try
    {
        m_sceneYAML = YAML::LoadFile(res + scenePath);

        // Parse scene name
        std::string sceneName = "Unnamed Scene";
        if (m_sceneYAML["name"])
        {
            sceneName = m_sceneYAML["name"].as<std::string>();
        }

        // Parse capabilities
        std::unordered_set<std::string> capabilities;
        if (m_sceneYAML["capabilities"])
        {
            for (const auto &cap : m_sceneYAML["capabilities"])
            {
                std::string capName = cap.first.as<std::string>();
                bool enabled = cap.second.as<bool>(false);
                if (enabled)
                {
                    capabilities.insert(capName);
                    LOG_INFO("[SceneLoader] Capability enabled: {}", capName);
                }
            }
        }

        // Parse strict mode
        bool strictMode = false;
        if (m_sceneYAML["strict"])
        {
            strictMode = m_sceneYAML["strict"].as<bool>(false);
            if (strictMode)
            {
                LOG_INFO("[SceneLoader] Strict mode enabled");
            }
        }

        // Create context
        m_context = std::make_unique<SceneContext>(sceneName, capabilities);
        m_context->SetStrictMode(strictMode);

        // Load scripts
        if (m_sceneYAML["scripts"])
        {
            std::vector<std::string> scriptPaths;
            for (const auto &scriptPath : m_sceneYAML["scripts"])
            {
                scriptPaths.push_back(res + scriptPath.as<std::string>());
            }

            sol::state &lua = ScriptManager::GetInstance().GetLuaState();
            if (!LoadAllScripts(lua, scriptPaths))
            {
                return false;
            }
        }

        LOG_DEBUG("[SceneLoader] All scripts loaded, validating roles...");

        // Validate roles (warnings only in non-strict mode)
        auto roleWarnings = ValidateRoles();
        for (const auto &warning : roleWarnings)
        {
            if (strictMode)
            {
                LOG_ERROR("[SceneLoader] {}", warning);
            }
            else
            {
                LOG_WARNING("[SceneLoader] {}", warning);
            }
        }
        if (strictMode && !roleWarnings.empty())
        {
            return false;
        }

        // Validate capabilities (always errors)
        auto capErrors = ValidateCapabilities();
        for (const auto &error : capErrors)
        {
            LOG_ERROR("[SceneLoader] {}", error);
        }
        if (!capErrors.empty())
        {
            return false;
        }

        LOG_DEBUG("[SceneLoader] Initializing {} modules...", m_modules.size());

        // Initialize modules in dependency order
        sol::state &lua = ScriptManager::GetInstance().GetLuaState();
        if (!InitializeModules(lua))
        {
            return false;
        }

        LOG_DEBUG("[SceneLoader] Modules initialized successfully");

        m_isLoaded = true;
        LOG_INFO("[SceneLoader] Scene '{}' loaded successfully with {} modules",
                 sceneName, m_modules.size());
        return true;
    }
    catch (const YAML::Exception &e)
    {
        LOG_ERROR("[SceneLoader] YAML parse error for '{}': {}", scenePath, e.what());
        return false;
    }
}

bool SceneLoader::LoadAllScripts(sol::state &lua, const std::vector<std::string> &scriptPaths)
{
    m_modules.clear();
    m_roleToModule.clear();

    for (const auto &path : scriptPaths)
    {
        LOG_INFO("[SceneLoader] Loading script: {}", path);
        SceneModule module = SceneModuleLoader::Load(lua, path);
        if (!module.loadSucceeded)
        {
            LOG_ERROR("[SceneLoader] Aborting scene load because script '{}' failed: {}",
                      path, module.loadError);
            return false;
        }
        m_modules.push_back(std::move(module));
    }

    // Build role-to-module map
    for (auto &module : m_modules)
    {
        if (module.HasRole())
        {
            m_roleToModule[module.roleString] = &module;
        }
    }

    return true;
}

std::vector<std::string> SceneLoader::ValidateRoles()
{
    std::vector<std::string> warnings;
    std::unordered_map<std::string, std::string> roleToFirstModule;

    for (const auto &module : m_modules)
    {
        if (module.HasRole())
        {
            auto it = roleToFirstModule.find(module.roleString);
            if (it != roleToFirstModule.end())
            {
                warnings.push_back("Duplicate role '" + module.roleString +
                                   "' - first declared by '" + it->second +
                                   "', also claimed by '" + module.name + "'");
            }
            else
            {
                roleToFirstModule[module.roleString] = module.name;
            }
        }
    }

    return warnings;
}

std::vector<std::string> SceneLoader::ValidateCapabilities()
{
    std::vector<std::string> errors;

    for (const auto &module : m_modules)
    {
        auto missing = m_context->GetMissingCapabilities(module.needs);
        for (const auto &cap : missing)
        {
            errors.push_back("Module '" + module.name + "' needs capability '" + cap +
                             "' but scene does not declare it");
        }
    }

    return errors;
}

DependencyResolutionResult SceneLoader::ResolveDependencies()
{
    DependencyResolutionResult result;

    // Separate legacy modules (no dependencies declared)
    std::vector<SceneModule *> modulesWithDeps;
    for (auto &module : m_modules)
    {
        if (module.isLegacy || !module.HasDependencies())
        {
            result.legacyModules.push_back(&module);
        }
        else
        {
            modulesWithDeps.push_back(&module);
        }
    }

    // Check for cycles
    auto cycles = DetectCycles(m_modules, m_roleToModule);
    if (!cycles.empty())
    {
        result.success = false;
        for (const auto &cycle : cycles)
        {
            result.errors.push_back("Dependency cycle detected: " + cycle);
        }
        return result;
    }

    // Check for missing dependencies
    for (const auto &module : m_modules)
    {
        for (const auto &dep : module.requires)
        {
            if (m_roleToModule.find(dep) == m_roleToModule.end())
            {
                if (m_context->IsStrictMode())
                {
                    result.errors.push_back(
                        "Module '" + module.name + "' requires role '" + dep +
                        "' but no module provides it");
                }
                else
                {
                    result.warnings.push_back(
                        "Module '" + module.name + "' requires role '" + dep +
                        "' but no module provides it");
                }
            }
        }
    }

    if (!result.errors.empty())
    {
        result.success = false;
        return result;
    }

    // Perform topological sort on modules with dependencies
    result.initOrder = TopologicalSort(m_modules, m_roleToModule);

    // If no modules with deps, just use legacy modules
    if (result.initOrder.empty() && modulesWithDeps.empty())
    {
        result.success = true;
        return result;
    }

    // Check for cycles (topological sort failed)
    if (result.initOrder.empty() && !modulesWithDeps.empty())
    {
        result.success = false;
        result.errors.push_back("Topological sort failed - possible dependency cycle");
        return result;
    }

    result.success = true;
    return result;
}

std::vector<SceneModule *> SceneLoader::TopologicalSort(
    std::vector<SceneModule> &modules,
    const std::unordered_map<std::string, SceneModule *> &roleMap)
{
    // Filter to only modules with dependencies
    std::vector<SceneModule *> modulesWithDeps;
    for (auto &module : modules)
    {
        if (!module.isLegacy && module.HasDependencies())
        {
            modulesWithDeps.push_back(&module);
        }
    }

    // Also include modules that are dependencies (have roles)
    std::unordered_set<SceneModule *> needed;
    for (auto *module : modulesWithDeps)
    {
        needed.insert(module);
        for (const auto &dep : module->requires)
        {
            auto it = roleMap.find(dep);
            if (it != roleMap.end())
            {
                needed.insert(it->second);
            }
        }
    }

    // Build in-degree map
    std::unordered_map<SceneModule *, int> inDegree;
    std::unordered_map<SceneModule *, std::vector<SceneModule *>> dependents;

    for (auto *module : needed)
    {
        inDegree[module] = 0;
    }

    for (auto *module : needed)
    {
        for (const auto &dep : module->requires)
        {
            auto it = roleMap.find(dep);
            if (it != roleMap.end() && needed.count(it->second))
            {
                dependents[it->second].push_back(module);
                inDegree[module]++;
            }
        }
    }

    // Kahn's algorithm
    std::vector<SceneModule *> result;
    std::queue<SceneModule *> queue;

    // Start with modules that have no dependencies
    for (auto *module : needed)
    {
        if (inDegree[module] == 0)
        {
            queue.push(module);
        }
    }

    while (!queue.empty())
    {
        SceneModule *current = queue.front();
        queue.pop();
        result.push_back(current);

        for (SceneModule *dependent : dependents[current])
        {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0)
            {
                queue.push(dependent);
            }
        }
    }

    // If result size != needed size, there was a cycle
    if (result.size() != needed.size())
    {
        return {};
    }

    return result;
}

std::vector<std::string> SceneLoader::DetectCycles(
    const std::vector<SceneModule> &modules,
    const std::unordered_map<std::string, SceneModule *> &roleMap)
{
    std::vector<std::string> cycles;
    std::unordered_set<const SceneModule *> visited;
    std::unordered_set<const SceneModule *> recursionStack;
    std::vector<std::string> path;

    std::function<bool(const SceneModule *)> dfs = [&](const SceneModule *module) -> bool
    {
        visited.insert(module);
        recursionStack.insert(module);
        path.push_back(module->name);

        for (const auto &dep : module->requires)
        {
            auto it = roleMap.find(dep);
            if (it == roleMap.end())
                continue;

            const SceneModule *depModule = it->second;

            if (recursionStack.find(depModule) != recursionStack.end())
            {
                // Cycle detected - build cycle string
                std::string cycleStr;
                bool inCycle = false;
                for (const auto &name : path)
                {
                    if (name == depModule->name)
                        inCycle = true;
                    if (inCycle)
                    {
                        if (!cycleStr.empty())
                            cycleStr += " -> ";
                        cycleStr += name;
                    }
                }
                cycleStr += " -> " + depModule->name;
                cycles.push_back(cycleStr);
                return true;
            }

            if (visited.find(depModule) == visited.end())
            {
                if (dfs(depModule))
                    return true;
            }
        }

        path.pop_back();
        recursionStack.erase(module);
        return false;
    };

    for (const auto &module : modules)
    {
        if (visited.find(&module) == visited.end() && module.HasDependencies())
        {
            dfs(&module);
        }
    }

    return cycles;
}

bool SceneLoader::InitializeModules(sol::state &lua)
{
    LOG_DEBUG("[SceneLoader] InitializeModules starting...");

    // Resolve dependencies
    auto resolution = ResolveDependencies();

    LOG_DEBUG("[SceneLoader] Dependencies resolved: success={}, initOrder={}, legacy={}",
              resolution.success, resolution.initOrder.size(), resolution.legacyModules.size());

    // Log warnings
    for (const auto &warning : resolution.warnings)
    {
        LOG_WARNING("[SceneLoader] {}", warning);
    }

    // Check for errors
    if (!resolution.success)
    {
        for (const auto &error : resolution.errors)
        {
            LOG_ERROR("[SceneLoader] {}", error);
        }
        return false;
    }

    LOG_DEBUG("[SceneLoader] Building Lua context table...");

    // Build Lua context table
    sol::table ctx = m_context->ToLuaTable(lua);

    LOG_DEBUG("[SceneLoader] Context table built successfully");

    // Expose role modules for non-contract scripts (e.g., entity scripts)
    sol::table roleModules = lua.create_table();
    for (auto &module : m_modules)
    {
        if (module.HasRole() && module.returnValue.valid())
        {
            roleModules[module.roleString] = module.returnValue;
        }
    }
    lua["SceneModules"] = roleModules;

    int order = 0;

    // Initialize modules with dependencies first (in topological order)
    for (SceneModule *module : resolution.initOrder)
    {
        // Register role value BEFORE initializing dependents
        if (module->HasRole())
        {
            m_context->UpdateContextWithRole(lua, ctx, module->roleString, module->returnValue);
        }

        // Call init(ctx) if present
        if (module->returnValue.get_type() == sol::type::table)
        {
            sol::table moduleTable = module->returnValue.as<sol::table>();
            sol::optional<sol::function> initFunc = moduleTable["init"];

            if (initFunc.has_value())
            {
                try
                {
                    initFunc.value()(moduleTable, ctx);
                    module->isInitialized = true;
                    module->initOrder = order++;
                    LOG_DEBUG("[SceneLoader] Initialized module '{}' (order {})",
                              module->name, module->initOrder);
                }
                catch (const sol::error &e)
                {
                    LOG_ERROR("[SceneLoader] Error in {}.init(): {}",
                              module->name, e.what());
                    if (m_context->IsStrictMode())
                    {
                        return false;
                    }
                }
            }
        }
    }

    // Initialize legacy modules last
    for (SceneModule *module : resolution.legacyModules)
    {
        // Register role if present
        if (module->HasRole())
        {
            m_context->UpdateContextWithRole(lua, ctx, module->roleString, module->returnValue);
        }

        // Call init(ctx) if present
        if (module->returnValue.get_type() == sol::type::table)
        {
            sol::table moduleTable = module->returnValue.as<sol::table>();
            sol::optional<sol::function> initFunc = moduleTable["init"];

            if (initFunc.has_value())
            {
                try
                {
                    initFunc.value()(moduleTable, ctx);
                    module->isInitialized = true;
                    module->initOrder = order++;
                    LOG_DEBUG("[SceneLoader] Initialized legacy module '{}' (order {})",
                              module->name, module->initOrder);
                }
                catch (const sol::error &e)
                {
                    LOG_ERROR("[SceneLoader] Error in {}.init(): {}",
                              module->name, e.what());
                    // Continue with other modules - legacy modules are best-effort
                }
            }
        }
    }

    LOG_INFO("[SceneLoader] Initialized {} modules ({} with deps, {} legacy)",
             order, resolution.initOrder.size(), resolution.legacyModules.size());

    return true;
}
