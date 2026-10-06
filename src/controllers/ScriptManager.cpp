/**
 * @file ScriptManager.cpp
 * @brief Lua and Python VM singleton for game scripting
 * @lines ~480
 *
 * Purpose: Provides scripting interface for game logic and UI state.
 * Manages both Lua and Python virtual machines with C++ bindings.
 *
 * Key functions:
 * - Initialize() - Setup Lua + Python VMs, register C++ bindings (line ~480)
 * - Run() - Execute Lua script file (line ~157, ~5 lines)
 * - ProcessInput() - Queue input events for Lua scripts (line ~214, ~60 lines)
 * - Shutdown() - Clean up VMs (line ~603, ~35 lines)
 *
 * Lua bindings:
 * - Entity creation/destruction (Registry)
 * - Input event handling (keyboard, mouse)
 * - Game state management
 * - UI state updates (via LuaUIState)
 *
 * Python bindings:
 * - Data analysis and exports
 * - External tool integration
 *
 * Integration: Used by LuaUIState (shared Lua VM), Game (Tetris logic), Editor
 */

#include "Camera.h"
#include "controllers/Game.h"
#include "controllers/Registry.h"
#include "controllers/ScriptManager.h"
#include "util/Uniform.h"

// Generic engine bindings
#include "components/Tween.h"
#include "components/Lighting.h"
#include "util/SceneTraversal.h"
#include "util/TransformUtils.h"
#include "systems/ReactiveUI.h"
#include "systems/HTMLRendererMT.h"
#include "systems/TiledBackgroundRenderer.h"
#include "systems/SkyBackgroundRenderer.h"

#include "controllers/WindowManager.h"
#include "util/PathResolver.h"

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <variant>
#include <algorithm>
#include <cctype>

namespace
{
    void GetLuaSourceLocation(sol::state_view lua, std::string &source, int &line)
    {
        source = "?";
        line = 0;
        sol::function getinfo = lua["debug"]["getinfo"];
        if (getinfo.valid())
        {
            sol::table info = getinfo(2, "Sl"); // level 2 = caller, "Sl" = source + line
            if (info.valid())
            {
                sol::optional<std::string> src = info["short_src"];
                sol::optional<int> ln = info["currentline"];
                if (src)
                {
                    source = *src;
                }
                if (ln)
                {
                    line = *ln;
                }
            }
        }
    }

    std::string BuildLuaArgsString(sol::state_view lua, sol::variadic_args args, size_t skipCount)
    {
        std::string output;
        bool first = true;
        size_t index = 0;
        sol::function tostring = lua["tostring"];
        for (auto arg : args)
        {
            if (index++ < skipCount)
            {
                continue;
            }
            if (!first)
            {
                output += "\t";
            }
            first = false;
            sol::object result = tostring(arg);
            if (result.is<std::string>())
            {
                output += result.as<std::string>();
            }
            else
            {
                output += "<unprintable>";
            }
        }
        return output;
    }

    std::string NormalizeLogLevel(std::string level)
    {
        std::transform(level.begin(), level.end(), level.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return level;
    }

    void LogLuaWithLevel(const std::string &level, const std::string &source, int line, const std::string &message)
    {
        std::string normalized = NormalizeLogLevel(level);
        if (normalized == "tracel3" || normalized == "trace3" || normalized == "trace_l3")
        {
            LOG_TRACE_L3("[Lua] {}:{} - {}", source, line, message);
        }
        else if (normalized == "tracel2" || normalized == "trace2" || normalized == "trace_l2")
        {
            LOG_TRACE_L2("[Lua] {}:{} - {}", source, line, message);
        }
        else if (normalized == "tracel1" || normalized == "trace1" || normalized == "trace_l1" || normalized == "trace")
        {
            LOG_TRACE_L1("[Lua] {}:{} - {}", source, line, message);
        }
        else if (normalized == "debug")
        {
            LOG_DEBUG("[Lua] {}:{} - {}", source, line, message);
        }
        else if (normalized == "warning" || normalized == "warn")
        {
            LOG_WARNING("[Lua] {}:{} - {}", source, line, message);
        }
        else if (normalized == "error")
        {
            LOG_ERROR("[Lua] {}:{} - {}", source, line, message);
        }
        else if (normalized == "critical")
        {
            LOG_CRITICAL("[Lua] {}:{} - {}", source, line, message);
        }
        else
        {
            LOG_INFO("[Lua] {}:{} - {}", source, line, message);
        }
    }
} // namespace

// ============================================================================
// Lua Scripting
// ============================================================================

bool ScriptManager::Run(const std::string &scriptSrc)
{
    sol::protected_function_result result =
        lua.safe_script_file(scriptSrc, sol::script_pass_on_error);
    if (!result.valid())
    {
        sol::error error = result;
        LOG_ERROR("[Lua] Failed to execute '{}': {}", scriptSrc, error.what());
        return false;
    }
    return true;
}

void ScriptManager::CreateList(const std::string &key)
{
    sol::table targetTable = lua.create_table();
    lua[key] = targetTable;
    LOG_DEBUG("Created new Lua table with key '{}'", key);
}

void ScriptManager::AddInputEventToQueue(const InputEvent &event)
{
    LOG_TRACE_L3("Adding input event to Lua queue");
    LOG_TRACE_L3("Event type: {}", static_cast<int>(event.type));
    sol::table luaEvent = lua.create_table();
    luaEvent["type"] = event.type;
    luaEvent["mods"] = event.mods;
    luaEvent["action"] = event.action;

    // Convert std::variant to appropriate Lua type
    std::visit([&](auto &&arg)
               {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            luaEvent["input"] = arg;
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
            sol::table v = lua.create_table();
            v["x"] = arg.x;
            v["y"] = arg.y;
            luaEvent["input"] = v;
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            sol::table v = lua.create_table();
            v["x"] = arg.x;
            v["y"] = arg.y;
            v["z"] = arg.z;
            luaEvent["input"] = v;
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
            sol::table v = lua.create_table();
            v["x"] = arg.x;
            v["y"] = arg.y;
            v["z"] = arg.z;
            v["w"] = arg.w;
            luaEvent["input"] = v;
        } }, event.input);

    sol::optional<sol::table> queue = lua["EventQueue"];
    if (!queue.has_value())
    {
        LOG_DEBUG("[ScriptManager] EventQueue not ready, dropping input event");
        return;
    }
    queue->add(luaEvent);
    LOG_DEBUG("[ScriptManager] Added input event to Lua EventQueue");
}

void ScriptManager::ProcessInput()
{
    Game &game = Game::GetInstance();
    GLFWwindow *window = WindowManager::GetInstance().window;

    // Call Lua input handler for game-specific input
    sol::function handleInputFunction;
    sol::table inputModule;
    bool needsSelf = false;

    sol::function globalHandleInput = lua[HANDLE_INPUT_F];
    if (globalHandleInput.valid())
    {
        handleInputFunction = globalHandleInput;
    }
    else
    {
        sol::object modulesObj = lua["SceneModules"];
        if (modulesObj.valid() && modulesObj.get_type() == sol::type::table)
        {
            sol::table modulesTable = modulesObj.as<sol::table>();
            sol::object inputObj = modulesTable["input"];
            if (inputObj.valid() && inputObj.get_type() == sol::type::table)
            {
                inputModule = inputObj.as<sol::table>();
                sol::optional<sol::function> moduleHandle = inputModule["handleInput"];
                if (!moduleHandle.has_value())
                {
                    moduleHandle = inputModule.get<sol::optional<sol::function>>("HandleInput");
                }
                if (moduleHandle.has_value())
                {
                    handleInputFunction = moduleHandle.value();
                    needsSelf = true;
                }
            }
        }
    }

    if (!handleInputFunction.valid())
    {
        LOG_ERROR("[ScriptManager] HandleInput function is not valid!");
        return;
    }

    try
    {
        if (needsSelf)
        {
            handleInputFunction(inputModule);
        }
        else
        {
            handleInputFunction();
        }
    }
    catch (const sol::error &e)
    {
        LOG_ERROR("[ScriptManager] Error calling HandleInput: {}", e.what());
    }
}

namespace LuaBindings
{
    void RegisterEnums(sol::state &lua)
    {
        lua.new_enum("InputTypes",
                     "KEY", InputTypes::Key,
                     "CLICK", InputTypes::Click,
                     "CURSOR", InputTypes::Cursor,
                     "RESIZE", InputTypes::Resize,
                     "SCROLL", InputTypes::Scroll);

        lua.new_enum("CameraDirections",
                     "FORWARD", CameraDirections::FORWARD,
                     "BACK", CameraDirections::BACK,
                     "LEFT", CameraDirections::LEFT,
                     "RIGHT", CameraDirections::RIGHT);

        lua.new_enum("TransitionType",
                     "LINEAR", TransitionType::TRANS_LINEAR,
                     "SINE", TransitionType::TRANS_SINE);
    }

    void RegisterTypes(sol::state &lua)
    {
        lua.new_usertype<glm::vec2>("vec2",
                                    sol::call_constructor, sol::constructors<glm::vec2(float, float)>(),
                                    "x", &glm::vec2::x,
                                    "y", &glm::vec2::y);

        lua.new_usertype<glm::vec3>("vec3",
                                    sol::call_constructor, sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                    "x", &glm::vec3::x,
                                    "y", &glm::vec3::y,
                                    "z", &glm::vec3::z);

        lua.new_usertype<glm::vec4>("vec4",
                                    sol::call_constructor, sol::constructors<glm::vec4(), glm::vec4(float), glm::vec4(float, float, float, float)>(),
                                    "x", &glm::vec4::x,
                                    "y", &glm::vec4::y,
                                    "z", &glm::vec4::z,
                                    "w", &glm::vec4::w);

        lua.new_usertype<Transform>("Transform",
                                    "Pos", &Transform::Pos,
                                    "Color", &Transform::Color,
                                    "Scale", &Transform::Scale,
                                    "Rotation", &Transform::Rotation);

        lua.new_usertype<Tween>("Tween",
                                "Start", &Tween::Start,
                                "End", &Tween::End,
                                "Duration", &Tween::Duration,
                                "elapsed", &Tween::elapsed,
                                "isActive", &Tween::isActive);

        // The camera should be constructed before initializing Lua
        lua.new_usertype<Camera>("Camera",
                                 "transform", &Camera::transform,
                                 "fov", &Camera::fov,
                                 "moveSpeed", &Camera::moveSpeed,
                                 "front", &Camera::front,
                                 // casting is necessary here because the function is overloaded, which Lua does not support
                                 "SetPerspective", std::function<void(Camera *, float)>(static_cast<void (Camera::*)(float)>(&Camera::SetPerspective)),
                                 "Move", &Camera::Move,
                                 "SetYawPitch", &Camera::SetYawPitch,
                                 "RotateByMouse", &Camera::RotateByMouse);

        lua.new_usertype<Game>("Game",
                               "GetInstance", &Game::GetInstance,
                               "delta", &Game::delta,
                               "registry", &Game::registry,
                               "camera", &Game::cam,
                               "conf", &Game::conf,
                               "window", &Game::windowManager,
                               "htmlRenderer", &Game::htmlRenderer);

        lua.new_usertype<HTMLRendererMT>("HTMLRendererMT",
                                         "HandleClickEvent", &HTMLRendererMT::HandleClickEvent,
                                         "HandleMouseButtonEvent", &HTMLRendererMT::HandleMouseButtonEvent,
                                         "UpdateHoverState", &HTMLRendererMT::UpdateHoverState);

        lua.new_usertype<Config>("Config",
                                 "resPath", &Config::ResourcePath);

        lua.new_usertype<WindowManager>("Window",
                                        "CloseWindow", &WindowManager::CloseWindow,
                                        "GetSize", &WindowManager::GetSize,
                                        "IsKeyPressed", &WindowManager::IsKeyPressed);

        lua.new_usertype<Registry>("Registry",
                                   "GetInstance", &Registry::GetInstance,
                                   "GetEntityByName", &Registry::GetEntityByName);

        lua.new_usertype<InputEvent>(
            "InputEvent",
            "type", &InputEvent::type,
            "input", &InputEvent::input,
            "mods", &InputEvent::mods,
            "action", &InputEvent::action);
    }

    void RegisterFunctions(sol::state &lua)
    {
        lua.set_function("CreateRenderComponent", &Registry::CreateRenderComponent);
        lua.set_function("AttachScript", sol::resolve<void(EntityID, const std::string &, sol::table)>(&Registry::AttachScript));
        lua.set_function("CreateCube", &Registry::CreateCube);

        // ====================================================================
        // GENERIC ENGINE BINDINGS - Entity & Component Management
        // ====================================================================

        // Generic UI State Management
        lua.set_function("SetUIValue", [](const std::string &key, sol::object value)
                         {
            ReactiveUI& reactiveUI = ReactiveUI::GetInstance();
            auto luaState = reactiveUI.GetLuaState();
            if (luaState) {
                if (value.is<int>()) {
                    luaState->SetValue(key, value.as<int>());
                } else if (value.is<double>()) {
                    luaState->SetValue(key, value.as<double>());
                } else if (value.is<std::string>()) {
                    luaState->SetValue(key, value.as<std::string>());
                } else if (value.is<bool>()) {
                    luaState->SetValue(key, value.as<bool>());
                }
            } });

        lua.set_function("RefreshUI", []()
                         {
            ReactiveUI& reactiveUI = ReactiveUI::GetInstance();
            HTMLRendererMT& htmlRenderer = HTMLRendererMT::GetInstance();
            htmlRenderer.UpdateHTML(reactiveUI.GetRenderedHTML()); });

        // Entity Management
        lua.set_function("RegisterEntity", sol::overload(
                                               []()
                                               { return Registry::GetInstance().RegisterEntity(); },
                                               [](EntityID parent)
                                               { return Registry::GetInstance().RegisterEntity(parent); }));

        lua.set_function("DestroyEntity", [](EntityID id)
                         { Registry::GetInstance().DestroyEntity(id); });

        // Hierarchy Operations
        lua.set_function("AddChild", &AddChild);
        lua.set_function("GetParent", &GetParent);
        lua.set_function("RemoveChild", &RemoveChild);

        // Component Access
        lua.set_function("GetTransform", [](EntityID id) -> Transform &
                         { return Registry::GetInstance().GetComponent<Transform>(id); });

        lua.set_function("GetTween", [](EntityID id) -> Tween &
                         { return Registry::GetInstance().GetComponent<Tween>(id); });

        // Component Registration
        lua.set_function("RegisterRenderComponent", [](EntityID id, std::shared_ptr<RenderComponent> rc)
                         { Registry::GetInstance().RegisterComponent<RenderComponent>(id, *rc); });

        lua.set_function("RegisterLighting", [](EntityID id, EntityID lightID)
                         {
            Lighting lightComp = Lighting(lightID);
            Registry::GetInstance().RegisterComponent<Lighting>(id, lightComp); });

        // Transform Operations
        lua.set_function("TranslateEntity", &TransformUtils::translate);
        lua.set_function("RotateEntity", &TransformUtils::rotate);

        // Tween Component Creation (creates tween with C++ move_to function)
        lua.set_function("CreateTweenComponent", [](EntityID id, float duration)
                         {
            Transform& transform = Registry::GetInstance().GetComponent<Transform>(id);
            Tween tween = Tween(&TransformUtils::move_to,
                               transform.Pos,
                               transform.Pos + glm::vec3(0, -2, 0),
                               duration,
                               TransitionType::TRANS_LINEAR);
            Registry::GetInstance().RegisterComponent<Tween>(id, tween); });

        // Utility
        lua.set_function("GetEntityByName", [](const std::string &name) -> EntityID
                         { return Registry::GetInstance().GetEntityByName(name); });

        // ====================================================================
        // Small math/helpers for succinct Lua (game-specific scripts)
        // ====================================================================

        // Compute camera right vector in a Y-up world: right = normalize(cross(front, up)).
        lua.set_function("GetCameraRight", []() -> glm::vec3
                         {
            Game& game = Game::GetInstance();
            if (!game.cam) {
                return glm::vec3(1.0f, 0.0f, 0.0f);
            }

            const glm::vec3& front = game.cam->front;
            glm::vec3 right(-front.z, 0.0f, front.x);
            float len = glm::length(right);
            if (len < 0.0001f) {
                return glm::vec3(1.0f, 0.0f, 0.0f);
            }
            return right / len; });

        // Compute a spawn position relative to the current camera orientation.
        // distance: along camera.front, lateral: along camera.right, vertical: along +Y, baseYOffset: applied to vertical.
        lua.set_function("ComputeCameraSpawnPosition", [](float distance, float lateral, float vertical, float baseYOffset) -> glm::vec3
                         {
            Game& game = Game::GetInstance();
            if (!game.cam) {
                return glm::vec3(0.0f, 0.0f, 0.0f);
            }

            const glm::vec3& front = game.cam->front;
            const glm::vec3 camPos = game.cam->transform.Pos;

            glm::vec3 right(-front.z, 0.0f, front.x);
            float len = glm::length(right);
            if (len < 0.0001f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                right /= len;
            }

            glm::vec3 pos = camPos + front * distance + right * lateral;
            pos.y += vertical + baseYOffset;
            return pos; });

        // Move an entity along camera.front by distance units (positive = along front).
        lua.set_function("MoveEntityAlongCameraFront", [](EntityID id, float distance)
                         {
            Game& game = Game::GetInstance();
            if (!game.cam) {
                return;
            }

            Transform& t = Registry::GetInstance().GetComponent<Transform>(id);
            t.Pos += game.cam->front * distance; });

        // Project vector (entityPos - cameraPos) onto camera.front.
        lua.set_function("DistanceAlongCameraFront", [](EntityID id) -> float
                         {
            Game& game = Game::GetInstance();
            if (!game.cam) {
                return 0.0f;
            }

            Transform& t = Registry::GetInstance().GetComponent<Transform>(id);
            const glm::vec3 d = t.Pos - game.cam->transform.Pos;
            return glm::dot(d, game.cam->front); });

        // ====================================================================
        // Tiled background renderer (procedural tiles + GPU cache)
        // ====================================================================

        sol::table backgroundTiles = lua.create_table();

        backgroundTiles["Enable"] = [](bool enabled)
        { TiledBackgroundRenderer::GetInstance().SetEnabled(enabled); };

        backgroundTiles["Configure"] = [](sol::table cfg)
        {
            TiledBackgroundConfig c{};

            auto setNumber = [&](const char* key, auto setter)
            {
                sol::object v = cfg[key];
                if (!v.valid()) {
                    return;
                }
                if (v.is<double>()) {
                    setter(static_cast<float>(v.as<double>()));
                } else if (v.is<int>()) {
                    setter(static_cast<float>(v.as<int>()));
                } else if (v.is<float>()) {
                    setter(v.as<float>());
                } else if (v.is<std::string>()) {
                    try {
                        setter(std::stof(v.as<std::string>()));
                    } catch (...) {
                    }
                }
            };

            auto setInt = [&](const char* key, auto setter)
            {
                sol::object v = cfg[key];
                if (!v.valid()) {
                    return;
                }
                if (v.is<int>()) {
                    setter(v.as<int>());
                } else if (v.is<double>()) {
                    setter(static_cast<int>(v.as<double>()));
                } else if (v.is<std::string>()) {
                    try {
                        setter(std::stoi(v.as<std::string>()));
                    } catch (...) {
                    }
                }
            };

            auto setBool = [&](const char* key, auto setter)
            {
                sol::object v = cfg[key];
                if (!v.valid()) {
                    return;
                }
                if (v.is<bool>()) {
                    setter(v.as<bool>());
                } else if (v.is<int>()) {
                    setter(v.as<int>() != 0);
                } else if (v.is<std::string>()) {
                    std::string s = v.as<std::string>();
                    std::transform(s.begin(), s.end(), s.begin(),
                                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    if (s == "true" || s == "1" || s == "yes") {
                        setter(true);
                    } else if (s == "false" || s == "0" || s == "no") {
                        setter(false);
                    }
                }
            };

            setBool("enabled", [&](bool v) { c.enabled = v; });
            setNumber("planeY", [&](float v) { c.planeY = v; });
            setNumber("farDistance", [&](float v) { c.farDistance = v; });
            setNumber("widthMultiplier", [&](float v) { c.widthMultiplier = v; });
            setInt("tileResolution", [&](int v) { c.tileResolution = v; });
            setInt("heightResolution", [&](int v) { c.heightResolution = v; });
            setInt("cacheSlots", [&](int v) { c.cacheSlots = v; });
            setInt("maxUploadsPerFrame", [&](int v) { c.maxUploadsPerFrame = v; });
            setNumber("tileWorldSize", [&](float v) { c.tileWorldSize = v; });
            setInt("lodCount", [&](int v) { c.lodCount = v; });
            setNumber("lodScale", [&](float v) { c.lodScale = v; });
            setNumber("lodSplitFactor", [&](float v) { c.lodSplitFactor = v; });

            setInt("meshResolution", [&](int v) { c.meshResolution = v; });

            setNumber("gridSpacing", [&](float v) { c.gridSpacing = v; });
            setInt("majorEvery", [&](int v) { c.majorEvery = v; });

            bool hadMinorWidth = false;
            bool hadMajorWidth = false;
            bool hadLegacyLineWidth = false;
            float legacyLineWidth = 0.0f;

            setNumber("minorLineWidth", [&](float v) { c.minorLineWidth = v; hadMinorWidth = true; });
            setNumber("majorLineWidth", [&](float v) { c.majorLineWidth = v; hadMajorWidth = true; });

            // Horizon termination line (magenta): thickness in pixels.
            setNumber("horizonLinePixels", [&](float v) { c.horizonLinePixels = v; });

            // Mountains (distant elevation beyond horizon).
            setNumber("mountainExtraDistance", [&](float v) { c.mountainExtraDistance = v; });
            setNumber("mountainHeight", [&](float v) { c.mountainHeight = v; });
            setNumber("mountainNoiseScale", [&](float v) { c.mountainNoiseScale = v; });
            setNumber("mountainFeatureSize", [&](float v) { c.mountainFeatureSize = v; });
            setNumber("mountainDetail", [&](float v) { c.mountainDetail = v; });
            setNumber("mountainFadeDistance", [&](float v) { c.mountainFadeDistance = v; });
            setNumber("mountainRiseExponent", [&](float v) { c.mountainRiseExponent = v; });
            setNumber("skirtDepth", [&](float v) { c.skirtDepth = v; });
            setNumber("mountainGridScale", [&](float v) { c.mountainGridScale = v; });
            setNumber("mountainMajorStrength", [&](float v) { c.mountainMajorStrength = v; });

            // Mountain composition (optional side ranges).
            setNumber("mountainSideStrength", [&](float v) { c.mountainSideStrength = v; });
            setNumber("mountainSideOffsetX", [&](float v) { c.mountainSideOffsetX = v; });
            setNumber("mountainSideWidthX", [&](float v) { c.mountainSideWidthX = v; });
            setNumber("mountainSideBase", [&](float v) { c.mountainSideBase = v; });

            // Back-compat: lineWidth previously drove both minor and major widths (major was ~1.5x).
            setNumber("lineWidth", [&](float v) { legacyLineWidth = v; hadLegacyLineWidth = true; });
            if (hadLegacyLineWidth)
            {
                if (!hadMinorWidth)
                {
                    c.minorLineWidth = legacyLineWidth;
                }
                if (!hadMajorWidth)
                {
                    c.majorLineWidth = legacyLineWidth * 1.5f;
                }
            }

            TiledBackgroundRenderer::GetInstance().Configure(c);
        };

        lua["BackgroundTiles"] = backgroundTiles;

    // ====================================================================
    // Sky background renderer (fullscreen gradient + sun)
    // ====================================================================

    {
        sol::table backgroundSky = lua.create_table();

        backgroundSky["Enable"] = [](bool enabled)
        { SkyBackgroundRenderer::GetInstance().SetEnabled(enabled); };

        backgroundSky["Configure"] = [](sol::table cfg)
        {
            SkyBackgroundConfig c{};

            auto setNumber = [&](const char *key, auto setter)
            {
                sol::object v = cfg[key];
                if (!v.valid())
                {
                    return;
                }
                if (v.is<double>())
                {
                    setter(static_cast<float>(v.as<double>()));
                }
                else if (v.is<int>())
                {
                    setter(static_cast<float>(v.as<int>()));
                }
                else if (v.is<float>())
                {
                    setter(v.as<float>());
                }
                else if (v.is<std::string>())
                {
                    try
                    {
                        setter(std::stof(v.as<std::string>()));
                    }
                    catch (...)
                    {
                    }
                }
            };

            auto setInt = [&](const char *key, auto setter)
            {
                sol::object v = cfg[key];
                if (!v.valid())
                {
                    return;
                }
                if (v.is<int>())
                {
                    setter(v.as<int>());
                }
                else if (v.is<double>())
                {
                    setter(static_cast<int>(v.as<double>()));
                }
                else if (v.is<std::string>())
                {
                    try
                    {
                        setter(std::stoi(v.as<std::string>()));
                    }
                    catch (...)
                    {
                    }
                }
            };

            auto setBool = [&](const char *key, auto setter)
            {
                sol::object v = cfg[key];
                if (!v.valid())
                {
                    return;
                }
                if (v.is<bool>())
                {
                    setter(v.as<bool>());
                }
                else if (v.is<int>())
                {
                    setter(v.as<int>() != 0);
                }
            };

            auto setVec3 = [&](const char *key, glm::vec3 &out)
            {
                sol::object v = cfg[key];
                if (!v.valid() || !v.is<sol::table>())
                {
                    return;
                }

                sol::table t = v.as<sol::table>();
                auto getF = [&](const char *k, int idx, float &dst)
                {
                    sol::object o = t[k];
                    if (!o.valid())
                    {
                        o = t[idx];
                    }
                    if (!o.valid())
                    {
                        return;
                    }
                    if (o.is<double>())
                    {
                        dst = static_cast<float>(o.as<double>());
                    }
                    else if (o.is<int>())
                    {
                        dst = static_cast<float>(o.as<int>());
                    }
                    else if (o.is<float>())
                    {
                        dst = o.as<float>();
                    }
                };

                getF("r", 1, out.r);
                getF("g", 2, out.g);
                getF("b", 3, out.b);
            };

            auto setVec2 = [&](const char *key, glm::vec2 &out)
            {
                sol::object v = cfg[key];
                if (!v.valid() || !v.is<sol::table>())
                {
                    return;
                }

                sol::table t = v.as<sol::table>();
                auto getF = [&](const char *k, int idx, float &dst)
                {
                    sol::object o = t[k];
                    if (!o.valid())
                    {
                        o = t[idx];
                    }
                    if (!o.valid())
                    {
                        return;
                    }
                    if (o.is<double>())
                    {
                        dst = static_cast<float>(o.as<double>());
                    }
                    else if (o.is<int>())
                    {
                        dst = static_cast<float>(o.as<int>());
                    }
                    else if (o.is<float>())
                    {
                        dst = o.as<float>();
                    }
                };

                getF("x", 1, out.x);
                getF("y", 2, out.y);
            };

            setBool("enabled", [&](bool v) { c.enabled = v; });
            setVec3("topColor", c.topColor);
            setVec3("bottomColor", c.bottomColor);
            setNumber("horizonY", [&](float v) { c.horizonY = v; });
            setNumber("horizonGlow", [&](float v) { c.horizonGlow = v; });
            setVec3("horizonColor", c.horizonColor);
            setVec2("sunPos", c.sunPos);
            setNumber("sunRadius", [&](float v) { c.sunRadius = v; });
            setNumber("sunGlow", [&](float v) { c.sunGlow = v; });
            setVec3("sunColor", c.sunColor);
            setInt("sunStripeCount", [&](int v) { c.sunStripeCount = v; });
            setNumber("sunStripeFill", [&](float v) { c.sunStripeFill = v; });
            setNumber("sunStripeTopClear", [&](float v) { c.sunStripeTopClear = v; });
            setNumber("sunHorizonMix", [&](float v) { c.sunHorizonMix = v; });

            setBool("mountainsEnabled", [&](bool v) { c.mountainsEnabled = v; });
            setBool("mountainsOccludeSun", [&](bool v) { c.mountainsOccludeSun = v; });
            setVec3("mountainColor", c.mountainColor);
            setNumber("mountainBaseY", [&](float v) { c.mountainBaseY = v; });
            setNumber("mountainHeight", [&](float v) { c.mountainHeight = v; });
            setNumber("mountainScale", [&](float v) { c.mountainScale = v; });
            setNumber("mountainDetail", [&](float v) { c.mountainDetail = v; });
            setNumber("mountainScrollSpeed", [&](float v) { c.mountainScrollSpeed = v; });
            setNumber("mountainEdgePixels", [&](float v) { c.mountainEdgePixels = v; });

            SkyBackgroundRenderer::GetInstance().Configure(c);
        };

        lua["BackgroundSky"] = backgroundSky;
    }
    }
}

// ============================================================================
// Python Scripting
// ============================================================================

#ifdef USE_PYTHON_SCRIPTING
void ScriptManager::PreInitializePython()
{
    std::string pythonHome = PathResolver::GetBundledPythonHome();
    Config& conf = Game::GetInstance().conf;

    // Config override takes precedence
    if (!conf.PythonHome.empty())
    {
        pythonHome = conf.PythonHome;
    }

    if (!pythonHome.empty())
    {
        // Bundled mode - set PYTHONHOME and PYTHONPATH
        setenv("PYTHONHOME", pythonHome.c_str(), 1);

        std::string pythonPath = pythonHome + "/lib/python3.13:" +
                                 pythonHome + "/lib/python3.13/site-packages:" +
                                 conf.ResourcePath + "scripts";

        // Append any additional configured paths
        if (!conf.PythonPath.empty())
        {
            pythonPath += ":" + conf.PythonPath;
        }

        setenv("PYTHONPATH", pythonPath.c_str(), 1);
        LOG_INFO("Python bundled mode: PYTHONHOME={}", pythonHome);
        LOG_DEBUG("Python bundled mode: PYTHONPATH={}", pythonPath);
    }
    else
    {
        // Development mode - just add scripts to path
        std::string scriptsPath = conf.ResourcePath + "scripts";

        // Append any additional configured paths
        if (!conf.PythonPath.empty())
        {
            scriptsPath += ":" + conf.PythonPath;
        }

        setenv("PYTHONPATH", scriptsPath.c_str(), 1);
        LOG_INFO("Python development mode: PYTHONPATH={}", scriptsPath);
    }
}

py::object ScriptManager::ImportModule(const std::string &moduleName)
{
    // Check if the module is already imported
    py::dict sys_modules = py::module::import("sys").attr("modules").cast<py::dict>();
    if (sys_modules.contains(moduleName.c_str()))
    {
        // Module is already imported, return the existing module
        return sys_modules[moduleName.c_str()];
    }
    else
    {
        // Module is not imported yet, import and return the module
        return py::module::import(moduleName.c_str());
    }
}
#endif // USE_PYTHON_SCRIPTING

// ============================================================================
// Initialization & Shutdown - Lua and Python
// ============================================================================

void ScriptManager::Initialize()
{
    LOG_TRACE_L3("Initializing ScriptManager...");
    // Initialize Lua
    LuaBindings::RegisterEnums(lua);
    LuaBindings::RegisterTypes(lua);
    LuaBindings::RegisterFunctions(lua);
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::os, sol::lib::math, sol::lib::string, sol::lib::debug);

    // Override Lua's print and provide level-aware logging helpers
    lua.set_function("print", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_DEBUG("[Lua] {}:{} - {}", source, line, output); });

    lua.set_function("log", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);

        if (args.size() == 0)
        {
            LOG_INFO("[Lua] {}:{} -", source, line);
            return;
        }

        std::string level = "info";
        auto it = args.begin();
        if (it != args.end())
        {
            sol::function tostring = lua["tostring"];
            sol::object levelObj = tostring(*it);
            if (levelObj.is<std::string>())
            {
                level = levelObj.as<std::string>();
            }
        }

        std::string output = BuildLuaArgsString(lua, args, 1);
        LogLuaWithLevel(level, source, line, output); });

    lua.set_function("log_trace", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_TRACE_L1("[Lua] {}:{} - {}", source, line, output); });

    lua.set_function("log_debug", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_DEBUG("[Lua] {}:{} - {}", source, line, output); });

    lua.set_function("log_info", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_INFO("[Lua] {}:{} - {}", source, line, output); });

    lua.set_function("log_warning", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_WARNING("[Lua] {}:{} - {}", source, line, output); });

    lua.set_function("log_error", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_ERROR("[Lua] {}:{} - {}", source, line, output); });

    lua.set_function("log_critical", [](sol::variadic_args args, sol::this_state s)
                     {
        sol::state_view lua(s);
        std::string source;
        int line = 0;
        GetLuaSourceLocation(lua, source, line);
        std::string output = BuildLuaArgsString(lua, args, 0);
        LOG_CRITICAL("[Lua] {}:{} - {}", source, line, output); });

    CreateList(EVENT_QUEUE);

    LOG_TRACE_L1("Lua: Running init.lua");
    const std::string initScript = Game::GetInstance().conf.ResourcePath + "scripts/init.lua";
    if (!Run(initScript))
    {
        throw std::runtime_error("Could not execute Lua init script: " + initScript);
    }

#ifdef USE_PYTHON_SCRIPTING
    // Initialize Python
    PreInitializePython();
    guard = std::make_unique<py::scoped_interpreter>();
    LOG_TRACE_L1("Python: Running init.py");
    std::ifstream file(Game::GetInstance().conf.ResourcePath + "scripts/init.py");
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open Python init script");
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string script_content = buffer.str();
    py::exec(script_content, py::globals());
#endif

    LOG_INFO("INIT - ScriptManager: SUCCESS");
}

void ScriptManager::Shutdown()
{
    // Shutdown Lua
    lua["GameManager"] = sol::lua_nil;
    lua.collect_garbage();

#ifdef USE_PYTHON_SCRIPTING
    // Shutdown Python
    try
    {
        // Clear globals
        py::dict globals = py::globals();
        std::vector<std::string> keys_to_delete;

        // Collect all keys; can't modify dict while iterating over it
        for (auto item : globals)
        {
            std::string key = py::str(item.first).cast<std::string>();
            keys_to_delete.push_back(key);
        }

        for (const auto &key : keys_to_delete)
        {
            globals.attr("pop")(key, py::none());
        }
    }
    catch (const py::error_already_set &e)
    {
        LOG_ERROR("Error clearing Python globals: {}", e.what());
    }
    guard.reset(); // End the Python interpreter

    LOG_INFO("SHUTDOWN - Python");
#endif
}
