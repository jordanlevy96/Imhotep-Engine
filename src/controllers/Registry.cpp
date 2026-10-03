/**
 * @file Registry.cpp
 * @brief Entity-Component-System (ECS) registry using EnTT
 * @lines ~345
 *
 * Purpose: Central entity database and scene management.
 * Wraps EnTT registry with game-specific entity lifecycle and scene loading.
 *
 * Key functions:
 * - RegisterEntity() - Create new entity with optional name/parent (line 16, 23)
 * - DestroyEntity() - Remove entity and all children recursively (line 35, ~35 lines)
 * - GetEntityByName() - Find entity by name string (line 71, ~20 lines)
 * - LoadScene() - Load YAML scene definition (line 160, ~140 lines)
 * - AttachScript() - Bind Lua/Python script to entity (line 302, 310)
 *
 * Entity management:
 * - Hierarchical parent/child relationships via HierarchyComponent
 * - Named entities for easy lookup (entityNames vector)
 * - Script attachment for game logic
 *
 * Scene loading:
 * - YAML format with entities, components, transforms
 * - Recursive hierarchy building
 * - Material and render component creation
 *
 * Integration: Core system used by all gameplay code, editor, and scene loader
 */

#include "controllers/Game.h"
#include "controllers/Registry.h"
#include "controllers/ScriptManager.h"
#include "systems/SceneLoader.h"

#include <iostream>

void Registry::Shutdown()
{
    for (size_t id = 0; id < entityNames.size(); id++)
    {
        DestroyEntity(id);
    }
}

EntityID Registry::RegisterEntity(EntityID parent)
{
    std::string name = std::to_string(i);

    return RegisterEntity(name, parent);
}

EntityID Registry::RegisterEntity(const std::string &name, EntityID parent)
{
    entityNames.push_back(name);
    entityAlive.push_back(true);
    Transform t = Transform();
    RegisterComponent(i, t);
    HierarchyComponent hc = HierarchyComponent(parent);
    RegisterComponent(i, hc);
    WorldTransform wt = WorldTransform();
    RegisterComponent(i, wt);
    return i++;
}

void Registry::DestroyEntity(EntityID id)
{
    if (!IsEntityAlive(id))
    {
        return;
    }

    // First, recursively destroy all children
    if (HierarchyComponents.HasComponent(id))
    {
        HierarchyComponent &hc = HierarchyComponents.GetComponent(id);

        // Copy children vector since we'll be modifying it during iteration
        std::vector<EntityID> children = hc.Children;
        for (EntityID child : children)
        {
            DestroyEntity(child);
        }

        // Remove this entity from parent's children list
        if (hc.Parent != static_cast<EntityID>(-1) && HierarchyComponents.HasComponent(hc.Parent))
        {
            HierarchyComponent &parentHc = HierarchyComponents.GetComponent(hc.Parent);
            auto it = std::find(parentHc.Children.begin(), parentHc.Children.end(), id);
            if (it != parentHc.Children.end())
            {
                parentHc.Children.erase(it);
            }
        }
    }

    // Now remove all components for this entity
    HierarchyComponents.RemoveComponent(id);
    LightingComponents.RemoveComponent(id);
    RenderComponents.RemoveComponent(id);
    ScriptComponents.RemoveComponent(id);
    TransformComponents.RemoveComponent(id);
    TweenComponents.RemoveComponent(id);
    WorldTransformComponents.RemoveComponent(id);
    entityAlive[id] = false;
}

EntityID Registry::GetEntityByName(const std::string &name)
{
    for (EntityID id = 0; id < entityNames.size(); ++id)
    {
        if (IsEntityAlive(id) && entityNames[id] == name)
        {
            return id;
        }
    }
    return ENTITY_NULL;
}

const std::string &Registry::GetEntityName(EntityID id) const
{
    static const std::string empty = "";
    if (IsEntityAlive(id))
    {
        return entityNames[id];
    }
    return empty;
}

void Registry::SetEntityName(EntityID id, const std::string &name)
{
    if (IsEntityAlive(id))
    {
        entityNames[id] = name;
    }
}

size_t Registry::GetEntityCount() const
{
    return static_cast<size_t>(std::count(entityAlive.begin(), entityAlive.end(), true));
}

bool Registry::IsEntityAlive(EntityID id) const
{
    return id < entityAlive.size() && entityAlive[id];
}

std::vector<EntityID> Registry::GetAllEntities() const
{
    std::vector<EntityID> ids;
    ids.reserve(GetEntityCount());
    for (size_t i = 0; i < entityNames.size(); i++)
    {
        if (IsEntityAlive(i))
        {
            ids.push_back(i);
        }
    }
    return ids;
}

template <>
SparseSet<HierarchyComponent> &Registry::GetComponentSet<HierarchyComponent>()
{
    return HierarchyComponents;
}

template <>
SparseSet<Lighting> &Registry::GetComponentSet<Lighting>()
{
    return LightingComponents;
}

template <>
SparseSet<RenderComponent> &Registry::GetComponentSet<RenderComponent>()
{
    return RenderComponents;
}

template <>
SparseSet<ScriptComponent> &Registry::GetComponentSet<ScriptComponent>()
{
    return ScriptComponents;
}

template <>
SparseSet<Transform> &Registry::GetComponentSet<Transform>()
{
    return TransformComponents;
}

template <>
SparseSet<Tween> &Registry::GetComponentSet<Tween>()
{
    return TweenComponents;
}

template <>
SparseSet<WorldTransform> &Registry::GetComponentSet<WorldTransform>()
{
    return WorldTransformComponents;
}

bool Registry::LoadScene(const std::string &src)
{
    const std::string &res = Game::GetInstance().conf.ResourcePath;
    try
    {
        // Use SceneLoader for script loading with contract validation
        SceneLoader sceneLoader;
        if (!sceneLoader.LoadScripts(src))
        {
            LOG_ERROR("Failed to load scene scripts: {}", src);
            return false;
        }

        LOG_DEBUG("[Registry] SceneLoader finished, getting YAML for entities...");

        // Get the parsed YAML from SceneLoader for entity creation
        const YAML::Node &yaml = sceneLoader.GetSceneYAML();

        LOG_DEBUG("[Registry] Checking for scene objects...");

        if (!yaml["scene"] || !yaml["scene"]["objects"])
        {
            LOG_WARNING("[Registry] No scene objects found in YAML");
            return true; // Not an error, just no entities to create
        }

        const YAML::Node &objectsNode = yaml["scene"]["objects"];
        LOG_DEBUG("[Registry] Found {} scene objects", objectsNode.size());
        for (const auto &objectNode : objectsNode)
        {
            const std::string &name = objectNode["name"].as<std::string>();
            EntityID id = RegisterEntity(name);
            Transform &transform = GetComponent<Transform>(id);
            if (objectNode["transform"])
            {
                if (objectNode["transform"]["pos"])
                {
                    transform.Pos.x = objectNode["transform"]["pos"]["x"].as<float>();
                    transform.Pos.y = objectNode["transform"]["pos"]["y"].as<float>();
                    transform.Pos.z = objectNode["transform"]["pos"]["z"].as<float>();
                }
                if (objectNode["transform"]["scale"])
                {
                    transform.Scale.x = objectNode["transform"]["scale"]["x"].as<float>();
                    transform.Scale.y = objectNode["transform"]["scale"]["y"].as<float>();
                    transform.Scale.z = objectNode["transform"]["scale"]["z"].as<float>();
                }
                if (objectNode["transform"]["color"])
                {
                    transform.Color.r = objectNode["transform"]["color"]["r"].as<float>();
                    transform.Color.g = objectNode["transform"]["color"]["g"].as<float>();
                    transform.Color.b = objectNode["transform"]["color"]["b"].as<float>();
                    // Load alpha if present, default to 1.0 (fully opaque)
                    transform.Color.a = objectNode["transform"]["color"]["a"] ?
                                        objectNode["transform"]["color"]["a"].as<float>() : 1.0f;
                }
            }

            if (objectNode["components"])
            {
                const YAML::Node &componentsNode = objectNode["components"];
                for (const auto &componentNode : componentsNode)
                {
                    const std::string componentType = componentNode["type"].as<std::string>();
                    if (componentType == "Lighting")
                    {
                        const std::string &shaderSrc = (const std::string &)(res) + "shaders/" + componentNode["shader"].as<std::string>();
                        const std::string &modelSrc = (const std::string &)(res) + "models/" + componentNode["model"].as<std::string>();
                        const std::string &lightName = componentNode["light"].as<std::string>();

                        EntityID light = GetEntityByName(lightName);
                        RenderComponent rc = RenderComponent(shaderSrc, modelSrc);
                        RegisterComponent<RenderComponent>(id, rc);
                        Lighting lightComp = Lighting(light);
                        RegisterComponent<Lighting>(id, lightComp);
                    }
                    else if (componentType == "RenderComponent")
                    {
                        const std::string &shaderSrc = (const std::string &)(res) + "shaders/" + componentNode["shader"].as<std::string>();
                        const std::string &modelSrc = (const std::string &)(res) + "models/" + componentNode["model"].as<std::string>();
                        RenderComponent rc = RenderComponent(shaderSrc, modelSrc);
                        RegisterComponent<RenderComponent>(id, rc);
                    }
                    else if (componentType == "LuaScript")
                    {
                        ScriptManager &sm = ScriptManager::GetInstance();
                        // Load the script and call the ready function
                        // Be careful with this ready call, if it relies on stuff that hasn't been initialized yet, it'll fail.

                        const std::string &scriptSrc = (const std::string &)(res) + componentNode["script"].as<std::string>();
                        sm.Run(scriptSrc);
                        sol::table scriptClass = sm.GetLuaTable(name);
                        scriptClass["__entityId"] = id;
                        scriptClass["__entityName"] = name;
                        for (const auto &property : componentNode)
                        {
                            std::string key = property.first.as<std::string>();
                            // "type" and "script" are already handled above
                            if (key == "type" || key == "script")
                            {
                                continue;
                            }

                            // Assume all other keys are meant as strings for the Lua script
                            scriptClass[key] = property.second.as<std::string>();
                        }

                        ScriptComponent sc = ScriptComponent(name, scriptClass);
                        RegisterComponent<ScriptComponent>(id, sc);
                    }
#ifdef USE_PYTHON_SCRIPTING
                    else if (componentType == "PythonScript")
                    {
                        const std::string &scriptName = componentNode["script"].as<std::string>();
                        ScriptManager &sm = ScriptManager::GetInstance();

                        py::object scriptClass = sm.ImportModule(scriptName);
                        for (const auto &property : componentNode)
                        {
                            std::string key = property.first.as<std::string>();
                            // "type" and "script" are already handled above
                            if (key == "type" || key == "script")
                            {
                                continue;
                            }

                            sm.SetClassAttribute(scriptName, key, property.second.as<std::string>());
                        }
                        ScriptComponent sc = ScriptComponent(name, scriptClass);
                        RegisterComponent<ScriptComponent>(id, sc);
                    }
#endif
                }
            }
        }

        LOG_INFO("Loaded scene: {}", res + src);
        return true;
    }
    catch (const YAML::Exception &e)
    {
        LOG_ERROR("Failed to load scene: {} ({})", res + src, e.what());
        return false;
    }
}

void Registry::AttachScript(EntityID entityId, const std::string &name, sol::table luaClass)
{
    ScriptComponent sc = ScriptComponent(name, luaClass);

    Registry *r = &GetInstance();
    r->RegisterComponent<ScriptComponent>(entityId, sc);
}

#ifdef USE_PYTHON_SCRIPTING
void Registry::AttachScript(EntityID entityId, const std::string &name, py::object pythonClass)
{
    ScriptComponent sc = ScriptComponent(name, pythonClass);

    Registry *r = &GetInstance();
    r->RegisterComponent<ScriptComponent>(entityId, sc);
}
#endif

std::shared_ptr<RenderComponent> Registry::CreateRenderComponent(const std::string &shaderSrc, const std::string &meshSrc)
{
    const std::string &res = Game::GetInstance().conf.ResourcePath;
    std::string shaderPath = (res) + "shaders/" + shaderSrc;
    std::string meshPath = (res) + "models/" + meshSrc;

    std::shared_ptr<RenderComponent> rc = std::make_shared<RenderComponent>(shaderPath, meshPath);

    return rc;
}

void Registry::CreateCube(std::shared_ptr<RenderComponent> cubeComp, glm::vec3 pos, glm::vec3 color)
{
    // static reference to registry for Lua binding
    Registry *r = &GetInstance();
    EntityID id = r->RegisterEntity();
    Transform &transform = r->GetComponent<Transform>(id);
    transform.Pos = pos;
    transform.Color = glm::vec4(color, 1.0f);  // Convert vec3 to vec4 with full opacity

    r->RegisterComponent<RenderComponent>(id, *cubeComp);

    // FIXME: hardcoded value
    EntityID lightID = r->GetEntityByName("light");
    Lighting lightComp = Lighting(lightID);
    r->RegisterComponent<Lighting>(id, lightComp);
}
