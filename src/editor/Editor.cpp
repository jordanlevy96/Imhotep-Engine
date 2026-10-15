/** @file Editor.cpp Editor window, authored-scene, and UI-template workflows. */

#include "editor/Editor.h"
#include "util/Logger.h"
#include "util/ConfigLoader.h"
#include "util/FrameTiming.h"
#include "systems/LuaUIState.h"
#include "systems/ScriptSystem.h"
#include "systems/TweenSystem.h"
#include "systems/HierarchySystem.h"
#include "systems/TemplateParser.h"
#include "controllers/Game.h"
#include "controllers/ScriptManager.h"
#include "util/FileIO.h"

#include <sol/sol.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace
{
    // Insert cursor character at specified position for display
    std::string InsertCursor(const std::string &text, size_t cursorPos)
    {
        if (cursorPos > text.size())
        {
            cursorPos = text.size();
        }
        return text.substr(0, cursorPos) + "|" + text.substr(cursorPos);
    }

    std::string FormatFloatValue(float value)
    {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss << std::setprecision(3) << value;

        std::string s = oss.str();
        size_t dot = s.find('.');
        if (dot != std::string::npos)
        {
            while (!s.empty() && s.back() == '0')
            {
                s.pop_back();
            }
            if (!s.empty() && s.back() == '.')
            {
                s.pop_back();
            }
        }

        if (s == "-0")
        {
            s = "0";
        }

        return s.empty() ? "0" : s;
    }

    bool TryParseFloatValue(const std::string &text, float &outValue)
    {
        if (text.empty())
        {
            return false;
        }

        const char *start = text.c_str();
        char *end = nullptr;
        outValue = std::strtof(start, &end);
        if (end == start)
        {
            return false;
        }

        while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
        {
            end++;
        }

        if (*end != '\0')
        {
            return false;
        }

        return std::isfinite(outValue);
    }
}

bool Editor::Initialize(const std::string &snapshotIpcEndpoint)
{
    LOG_INFO("[Editor] Starting initialization...");

    if (m_initialized)
    {
        LOG_WARNING("[Editor] Already initialized");
        return true;
    }

    // GLFW input handlers and the authored scene controls run synchronously from Run()'s glfwPollEvents().
    // Keep the snapshot owner on that same thread; the HTML renderer worker only rasterizes UI frames.
    {
        std::lock_guard<std::mutex> lock(m_snapshotOwnerMutex);
        m_editorThreadId = std::this_thread::get_id();
    }

    // Load config and initialize EngineCore (use editor-specific settings)
    if (!m_core.Initialize("../res/conf/editor_settings.yaml", conf, &m_camera))
    {
        return false;
    }

    // Get references to subsystems
    m_windowManager = m_core.GetWindowManager();
    m_htmlRenderer = m_core.GetHTMLRenderer();
    m_registry = m_core.GetRegistry();
    m_reactiveUI = m_core.GetReactiveUI();

    // Initialize scene viewport (800x600 for now) with camera from EngineCore
    m_viewport = new SceneViewport();
    Camera *camera = m_core.GetCamera();
    if (!m_viewport->Initialize(800, 600, camera))
    {
        LOG_CRITICAL("Failed to initialize SceneViewport");
        return false;
    }

    // Create the shell first, then project authored objects without executing their scripts.
    LoadEditorUI();
    OpenSceneDocument();
    if (m_sceneSession.GetPath().empty())
    {
        return false;
    }

    // Initialize UI Editor (populates templates in Lua state)
    InitializeUIEditor();

    // Force UI re-render to show templates in dropdown
    if (m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        m_reactiveUI->GetLuaState()->MarkDirty();
        std::string renderedHTML = m_reactiveUI->GetRenderedHTML();
        m_htmlRenderer->UpdateHTML(renderedHTML);
    }

    // Populate scene tree with entities from Registry
    UpdateSceneTree();

    // Register keyboard input handler for editor shortcuts and text input
    m_keyboardHandlerId = m_windowManager->RegisterInputHandler([this](const InputEvent &event)
    {
        if (event.type == InputTypes::Char)
        {
            return HandleTextInput(event);
        }
        return HandleEditorInput(event);
    });

    // Register click input handler for UI interactions
    m_clickHandlerId = m_windowManager->RegisterInputHandler([this](const InputEvent &event)
    {
        if (event.type != InputTypes::Click)
        {
            return false;
        }

        // Extract click payload (platforms may use vec2 or vec3)
        try
        {
            if (std::holds_alternative<glm::vec3>(event.input))
            {
                auto click = std::get<glm::vec3>(event.input);
                return HandleUIClick(click.x, click.y, static_cast<int>(click.z));
            }
            else if (std::holds_alternative<glm::vec2>(event.input))
            {
                auto click = std::get<glm::vec2>(event.input);
                return HandleUIClick(click.x, click.y, 0); // Default to left button
            }
            else
            {
                LOG_WARNING("[Editor] Click event has unexpected payload type");
                return false;
            }
        }
        catch (const std::bad_variant_access &)
        {
            LOG_WARNING("[Editor] Failed to read click event payload");
            return false;
        }
    });

    // Register selectEntity function for Lua event binding in ReactiveUI's state
    std::shared_ptr<LuaUIState> luaUIState = m_reactiveUI->GetLuaState();

    // Get Lua state and create methods table for ReactiveUI event dispatching
    sol::table stateTable = luaUIState->GetStateTable();

    // Get or create methods table (defensive - editor.lua should have it, but be safe)
    sol::object methodsObj = stateTable["methods"];
    sol::table methods;
    if (!methodsObj.valid() || methodsObj.get_type() == sol::type::lua_nil)
    {
        ScriptManager &scriptManager = ScriptManager::GetInstance();
        sol::state &lua = scriptManager.GetLuaState();
        methods = lua.create_table();
        stateTable["methods"] = methods;
        LOG_WARNING("[Editor] Created methods table - it should exist in editor.lua");
    }
    else
    {
        methods = methodsObj.as<sol::table>();
    }

    // Register selectEntity in the methods table - bind to C++ implementation
    methods["selectEntity"] = [this](sol::table self, uint32_t entityId)
    {
        SelectEntity(static_cast<EntityID>(entityId));
    };

    methods["openScene"] = [this](sol::table) { OpenSceneDocument(); };
    methods["discardAndOpenScene"] = [this](sol::table) { OpenSceneDocument(true); };
    methods["saveScene"] = [this](sol::table) { SaveSceneDocument(); };
    methods["saveSceneAs"] = [this](sol::table) { SaveSceneDocument(true); };
    methods["newScene"] = [this](sol::table) { CreateSceneDocument(); };
    methods["discardAndNewScene"] = [this](sol::table) { CreateSceneDocument(true); };
    methods["addSceneObject"] = [this](sol::table) { AddSceneObject(); };
    methods["assignSceneMesh"] = [this](sol::table) { AssignSceneComponent(false); };
    methods["assignSceneScript"] = [this](sol::table) { AssignSceneComponent(true); };
    methods["previousFlyingProperty"] = [this](sol::table) { LoadFlyingPropsProperty(-1); };
    methods["nextFlyingProperty"] = [this](sol::table) { LoadFlyingPropsProperty(1); };
    methods["applyFlyingProperty"] = [this](sol::table) { ApplyFlyingPropsProperty(); };
    methods["addSceneModule"] = [this](sol::table) { EditSceneModules(-1, 0); };
    methods["removeSceneModule"] = [this](sol::table, int index) { EditSceneModules(index, 0); };
    methods["moveSceneModuleUp"] = [this](sol::table, int index) { EditSceneModules(index, -1); };
    methods["moveSceneModuleDown"] = [this](sol::table, int index) { EditSceneModules(index, 1); };
    methods["toggleSceneCapability"] = [this](sol::table, const std::string &name) { ToggleSceneCapability(name); };
    methods["selectGameConfig"] = [this](sol::table)
    {
        const auto path = m_reactiveUI->GetLuaState()->GetValue("data.play.configPath");
        m_playSession.SelectConfig(conf.ResourcePath, path.is<std::string>() ? path.as<std::string>() : "");
        UpdatePlayStatus();
    };
    methods["startScenePlay"] = [this](sol::table)
    {
        m_playSession.Start(m_sceneSession.GetPath(), m_sceneSession.IsDirty(),
                            IMHOTEP_PLAY_EXECUTABLE, std::filesystem::current_path().string());
        UpdatePlayStatus();
    };
    methods["stopScenePlay"] = [this](sol::table)
    {
        m_playSession.Stop();
        UpdatePlayStatus();
    };
    methods["toggleSceneStrict"] = [this](sol::table)
    {
        const auto scene = m_sceneSession.Snapshot();
        m_sceneSession.SetStrictMode(!scene["strict"].as<bool>(false));
        UpdateSceneStatus();
    };

    // Register UI Editor methods
    methods["loadTemplate"] = [this](sol::table self)
    {
        // Read template name from Lua state (v-model will have already updated it)
        auto luaState = m_reactiveUI->GetLuaState();
        if (luaState)
        {
            sol::object nameObj = luaState->GetValue("data.uiEditor.currentTemplate");
            if (nameObj.valid() && nameObj.get_type() == sol::type::string)
            {
                std::string templateName = nameObj.as<std::string>();
                if (!templateName.empty())
                {
                    LoadUITemplate(templateName);
                }
            }
        }
    };

    methods["onCodeChange"] = [this](sol::table self)
    {
        // Mark preview as needing update (debounced)
        m_previewNeedsUpdate = true;
        m_lastCodeChange = std::chrono::steady_clock::now();
    };

    methods["saveTemplate"] = [this](sol::table self)
    {
        SaveUITemplate();
    };

    methods["previewTemplate"] = [this](sol::table self)
    {
        UpdatePreview();
    };

    methods["createTemplate"] = [this](sol::table self)
    {
        // Read template name from Lua state
        auto luaState = m_reactiveUI->GetLuaState();
        if (luaState)
        {
            sol::object nameObj = luaState->GetValue("data.uiEditor.newTemplateName");
            if (nameObj.valid() && nameObj.get_type() == sol::type::string)
            {
                std::string templateName = nameObj.as<std::string>();
                if (templateName.empty())
                {
                    luaState->SetValue("data.uiEditor.errorMessage", "Template name is empty");
                    luaState->MarkDirty();
                    return;
                }

                CreateUITemplate(templateName);
            }
        }
    };

    // Viewport click handler: map click inside viewport host to SceneViewport pixels, then pick entity.
    methods["onViewportClick"] = [this](sol::table self, sol::table eventTable)
    {
        if (!m_viewport || !m_htmlRenderer)
        {
            return;
        }

        // Clicking the viewport should unfocus any text fields.
        m_focusedInputField.clear();
        if (m_reactiveUI && m_reactiveUI->GetLuaState())
        {
            m_reactiveUI->GetLuaState()->SetValue("data.focusedField", "");
        }

        sol::object xObj = eventTable["x"];
        sol::object yObj = eventTable["y"];
        sol::object elemIdObj = eventTable["elemId"];
        sol::object buttonObj = eventTable["button"];

        if (!xObj.valid() || !yObj.valid() || !elemIdObj.valid())
        {
            return;
        }

        float clickX = 0.0f;
        float clickY = 0.0f;
        if (xObj.is<double>())
            clickX = static_cast<float>(xObj.as<double>());
        else if (xObj.is<float>())
            clickX = xObj.as<float>();
        else if (xObj.is<int>())
            clickX = static_cast<float>(xObj.as<int>());

        if (yObj.is<double>())
            clickY = static_cast<float>(yObj.as<double>());
        else if (yObj.is<float>())
            clickY = yObj.as<float>();
        else if (yObj.is<int>())
            clickY = static_cast<float>(yObj.as<int>());

        int button = 0;
        if (buttonObj.valid() && buttonObj.is<int>())
            button = buttonObj.as<int>();
        else if (buttonObj.valid() && buttonObj.is<double>())
            button = static_cast<int>(buttonObj.as<double>());

        // Only left click selects for now.
        if (button != 0)
        {
            return;
        }

        std::string elemId = elemIdObj.as<std::string>();

        int ex = 0, ey = 0, ew = 0, eh = 0;
        if (!m_htmlRenderer->TryGetInteractiveElementBounds(elemId, ex, ey, ew, eh))
        {
            if (m_viewportRectValid)
            {
                ex = m_viewportRectX;
                ey = m_viewportRectY;
                ew = m_viewportRectW;
                eh = m_viewportRectH;
            }
            else
            {
                return;
            }
        }

        if (ew <= 0 || eh <= 0)
        {
            return;
        }

        // Ensure picking buffer matches the UI viewport size (avoids a 1-frame mismatch during resize).
        if (std::abs(ew - m_viewport->GetWidth()) > 1 || std::abs(eh - m_viewport->GetHeight()) > 1)
        {
            m_viewport->Resize(ew, eh);
        }

        if (clickX < static_cast<float>(ex) || clickX >= static_cast<float>(ex + ew) ||
            clickY < static_cast<float>(ey) || clickY >= static_cast<float>(ey + eh))
        {
            return;
        }

        int vx = static_cast<int>(std::floor(clickX - static_cast<float>(ex)));
        int vy = static_cast<int>(std::floor(clickY - static_cast<float>(ey)));

        // Clamp to valid pixel range.
        vx = std::max(0, std::min(vx, m_viewport->GetWidth() - 1));
        vy = std::max(0, std::min(vy, m_viewport->GetHeight() - 1));

        EntityID picked = m_viewport->PickEntityAt(vx, vy);
        SelectEntity(picked);
    };

    methods["focusInput"] = [this](sol::table self, const std::string &fieldPath)
    {
        if (fieldPath.empty())
        {
            m_focusedInputField.clear();
            m_cursorPos = 0;
            m_selectionStart = 0;
            m_selectionEnd = 0;
            if (m_reactiveUI && m_reactiveUI->GetLuaState())
            {
                m_reactiveUI->GetLuaState()->SetValue("data.focusedField", "");
                m_reactiveUI->GetLuaState()->SetValue("data.cursorPos", 0);
            }
            return;
        }

        m_focusedInputField = fieldPath;

        // Initialize cursor at end of current value
        if (m_reactiveUI && m_reactiveUI->GetLuaState())
        {
            auto luaState = m_reactiveUI->GetLuaState();
            sol::object valueObj = luaState->GetValue(fieldPath);
            std::string value = "";
            if (valueObj.valid() && valueObj.get_type() == sol::type::string)
            {
                value = valueObj.as<std::string>();
            }
            m_cursorPos = value.size();
            m_selectionStart = m_cursorPos;
            m_selectionEnd = m_cursorPos;

            luaState->SetValue("data.focusedField", fieldPath);
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
        }
    };

    luaUIState->SetValue("methods", methods);

    m_initialized = true;
    if (!snapshotIpcEndpoint.empty())
    {
        std::string error;
        if (!m_snapshotIpc.Start(snapshotIpcEndpoint, error))
        {
            LOG_ERROR("[Editor] Could not enable snapshot IPC: {}", error);
            Shutdown();
            return false;
        }
    }
    return true;
}

bool Editor::TryGetAuthoredSceneSnapshot(SceneSession::AuthoredSnapshot &snapshot, std::string &error) const
{
    std::lock_guard<std::mutex> lock(m_snapshotOwnerMutex);
    if (m_editorThreadId == std::thread::id{})
    {
        error = "Editor is stopped or not initialized.";
        return false;
    }
    if (std::this_thread::get_id() != m_editorThreadId)
    {
        error = "Authored scene snapshots must be requested on the Editor owner thread.";
        return false;
    }
    if (!m_initialized)
    {
        error = "Editor is stopped or not initialized.";
        return false;
    }
    auto current = m_sceneSession.GetAuthoredSnapshot();
    if (current.documentIdentity.empty())
    {
        error = "Editor has no authored scene open.";
        return false;
    }
    snapshot = std::move(current);
    error.clear();
    return true;
}

void Editor::Run()
{
    if (!m_initialized)
    {
        LOG_ERROR("Cannot run editor - not initialized");
        return;
    }

    LOG_INFO("Starting editor main loop...");

    // Initialize frame timing (SIMPLE mode: delta-only, no fixed timestep)
    FrameTiming timing(FrameTimingMode::SIMPLE, 60.0);

    while (!m_shouldClose)
    {
        // CRITICAL: Poll input events first so window is responsive
        glfwPollEvents();
        m_snapshotIpc.ProcessPending([this](SceneSession::AuthoredSnapshot &snapshot, std::string &error)
        {
            return TryGetAuthoredSceneSnapshot(snapshot, error);
        });
        m_playSession.Poll();
        UpdatePlayStatus();
        if (glfwWindowShouldClose(m_windowManager->window))
        {
            if (!m_sceneSession.IsDirty())
            {
                break;
            }
            glfwSetWindowShouldClose(m_windowManager->window, GLFW_FALSE);
            auto state = m_reactiveUI->GetLuaState();
            state->SetValue("data.scene.error", "Save the scene before closing, or discard changes by reopening it.");
            state->MarkDirty();
        }

        // Update frame timing (calculates delta)
        timing.Update();
        m_delta = timing.GetDelta();

        // Update game systems (tweens, tweens + hierarchy)
        // Note: ScriptSystem is deliberately skipped in editor mode
        UpdateSystems(m_delta);

        // Update UI Editor (handles debounced preview updates)
        UpdateUIEditor(m_delta);

        // Render editor frame (viewport + UI)
        Render();

        // Swap buffers
        glfwSwapBuffers(m_windowManager->window);
    }

    LOG_INFO("Editor main loop ended");
}

void Editor::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_snapshotIpc.Stop();
    m_playSession.Stop();
    LOG_INFO("Shutting down editor...");

    // Unregister input handlers
    const size_t invalidId = std::numeric_limits<size_t>::max();

    if (m_keyboardHandlerId != invalidId)
    {
        m_windowManager->UnregisterInputHandler(m_keyboardHandlerId);
        m_keyboardHandlerId = invalidId;
    }
    if (m_clickHandlerId != invalidId)
    {
        m_windowManager->UnregisterInputHandler(m_clickHandlerId);
        m_clickHandlerId = invalidId;
    }

    // Shutdown systems (in reverse order of initialization)
    if (m_viewport)
    {
        m_viewport->Shutdown();
        delete m_viewport;
        m_viewport = nullptr;
    }
    m_core.Shutdown();

    m_initialized = false;
    LOG_INFO("Editor shutdown complete");
}

void Editor::Render()
{
    if (!m_htmlRenderer || !m_windowManager || !m_windowManager->window)
    {
        return;
    }

    // Ensure viewport matches framebuffer (handles Retina/HiDPI)
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(m_windowManager->window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    // Clear screen (base background behind transparent UI)
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    bool shouldRenderViewport = true;
    if (m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        auto luaState = m_reactiveUI->GetLuaState();

        sol::object viewObj = luaState->GetValue("data.activeView");
        if (viewObj.valid() && viewObj.get_type() == sol::type::string)
        {
            shouldRenderViewport = (viewObj.as<std::string>() == "scene");
        }
    }

    // Use current viewport bounds from the UI layout. This keeps rendering and picking aligned.
    if (shouldRenderViewport && m_viewport)
    {
        int ixCandidate = 0, iyCandidate = 0, iwCandidate = 0, ihCandidate = 0;
        if (m_htmlRenderer->TryFindInteractiveElementBoundsByHandler("click", "onViewportClick($event)", ixCandidate, iyCandidate, iwCandidate, ihCandidate))
        {
            // Clamp to framebuffer bounds.
            ixCandidate = std::max(0, std::min(ixCandidate, fbWidth - 1));
            iyCandidate = std::max(0, std::min(iyCandidate, fbHeight - 1));
            iwCandidate = std::max(1, std::min(iwCandidate, fbWidth - ixCandidate));
            ihCandidate = std::max(1, std::min(ihCandidate, fbHeight - iyCandidate));

            m_viewportRectValid = true;
            m_viewportRectX = ixCandidate;
            m_viewportRectY = iyCandidate;
            m_viewportRectW = iwCandidate;
            m_viewportRectH = ihCandidate;
        }
    }

    if (shouldRenderViewport && m_viewport && m_viewportRectValid)
    {
        int ix = m_viewportRectX;
        int iy = m_viewportRectY;
        int iw = m_viewportRectW;
        int ih = m_viewportRectH;

        // Clamp cached rect to framebuffer bounds (may change during window resize).
        ix = std::max(0, std::min(ix, fbWidth - 1));
        iy = std::max(0, std::min(iy, fbHeight - 1));
        iw = std::max(1, std::min(iw, fbWidth - ix));
        ih = std::max(1, std::min(ih, fbHeight - iy));

        // Keep SceneViewport sized to the UI viewport rect (camera aspect + picking buffer).
        if (std::abs(iw - m_viewport->GetWidth()) > 1 || std::abs(ih - m_viewport->GetHeight()) > 1)
        {
            m_viewport->Resize(iw, ih);
        }

        // Convert from UI coords (top-left origin) to OpenGL framebuffer coords (bottom-left origin).
        int fbX = ix;
        int fbY = fbHeight - (iy + ih);
        m_viewport->RenderToScreen(fbX, fbY, iw, ih);

        // Restore full-screen viewport for UI compositing.
        glViewport(0, 0, fbWidth, fbHeight);
    }

    // Update HTML only when the Lua UI state is dirty.
    if (m_reactiveUI)
    {
        auto luaState = m_reactiveUI->GetLuaState();
        bool wasDirty = (luaState != nullptr) && luaState->IsDirty();
        const std::string &renderedHTML = m_reactiveUI->GetRenderedHTML();

        if (wasDirty)
        {
            m_htmlRenderer->UpdateHTML(renderedHTML);
        }
    }

    // Render editor UI
    m_htmlRenderer->Render();
}

void Editor::LoadEditorUI()
{
    // Load editor Lua state from file
    auto luaState = std::make_shared<LuaUIState>();
    if (!luaState->LoadStateFile("../res/ui/state/editor.lua"))
    {
        LOG_ERROR("Failed to load editor Lua state");
        return;
    }

    m_reactiveUI->BindLuaState(luaState);

    // Load editor HTML template from file
    std::ifstream htmlFile("../res/ui/editor.html");
    if (!htmlFile.is_open())
    {
        LOG_ERROR("Failed to open editor HTML template: ../res/ui/editor.html");
        return;
    }

    std::string editorHTML((std::istreambuf_iterator<char>(htmlFile)), std::istreambuf_iterator<char>());
    htmlFile.close();

    // Register template with ReactiveUI so directives (v-for, v-if, etc.) are processed
    m_reactiveUI->RegisterTemplateWithDirectives("EditorUI", editorHTML);

    // Get the rendered HTML and send to HTMLRenderer
    std::string renderedHTML = m_reactiveUI->GetRenderedHTML();
    m_htmlRenderer->LoadHTML(renderedHTML);
}

void Editor::UpdateSceneTree()
{
    if (!m_reactiveUI || !m_reactiveUI->GetLuaState())
    {
        LOG_WARNING("Cannot update scene tree - ReactiveUI or LuaState not available");
        return;
    }

    // Get all entities from Registry
    std::vector<EntityID> entityIds;
    for (const auto &[id, name] : m_authoredEntities)
    {
        entityIds.push_back(id);
    }
    std::sort(entityIds.begin(), entityIds.end());

    // Get Lua state directly from ScriptManager
    ScriptManager &scriptManager = ScriptManager::GetInstance();
    sol::state &lua = scriptManager.GetLuaState();

    // Get Lua UI state for SetValue
    auto luaState = m_reactiveUI->GetLuaState();

    // Create entities array as a Lua table (using Lua state directly)
    sol::table entitiesTable = lua.create_table();

    for (size_t i = 0; i < entityIds.size(); i++)
    {
        EntityID id = entityIds[i];
        std::string name = m_registry->GetEntityName(id);

        // Create entity table (using Lua state directly)
        sol::table entityTable = lua.create_table();
        entityTable["id"] = static_cast<uint32_t>(id); // Cast to Lua-safe size
        entityTable["name"] = name;

        // Get parent ID from HierarchyComponent (if exists)
        if (m_registry->HasComponent<HierarchyComponent>(id))
        {
            EntityID parent = m_registry->GetComponent<HierarchyComponent>(id).Parent;
            entityTable["parent"] = static_cast<uint32_t>(parent); // Cast to Lua-safe size
        }
        // Leave parent as nil in Lua if no HierarchyComponent

        // Add to entities array (Lua arrays are 1-indexed)
        entitiesTable[i + 1] = entityTable;
    }

    // Set entities table in state
    luaState->SetValue("data.entities", entitiesTable);
    luaState->MarkDirty();
}

void Editor::UpdateSystems(double deltaTime)
{
    // Note: ScriptSystem::Update() is skipped in editor mode to avoid requiring
    // game-specific Lua EventQueue setup. Scripts should be tested in the game runtime.

    // Update animation system (tweens)
    TweenSystem::Update(deltaTime);

    // Update transform hierarchy (for parenting)
    HierarchySystem::Update();
}

bool Editor::HandleTextInput(const InputEvent &event)
{
    if (event.type != InputTypes::Char)
    {
        return false;
    }

    // Get character from event
    if (!std::holds_alternative<std::string>(event.input))
    {
        return false;
    }

    std::string ch = std::get<std::string>(event.input);
    if (ch.empty())
    {
        return false;
    }

    // If we have a focused input field, update its value
    if (!m_focusedInputField.empty() && m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        auto luaState = m_reactiveUI->GetLuaState();

        // Get current value
        sol::object currentValueObj = luaState->GetValue(m_focusedInputField);
        std::string currentValue = "";
        if (currentValueObj.valid() && currentValueObj.get_type() == sol::type::string)
        {
            currentValue = currentValueObj.as<std::string>();
        }

        // Clamp cursor position to valid range
        if (m_cursorPos > currentValue.size())
        {
            m_cursorPos = currentValue.size();
        }

        // Delete selection if there is one
        bool hasSelection = m_selectionStart != m_selectionEnd;
        if (hasSelection)
        {
            size_t selStart = std::min(m_selectionStart, m_selectionEnd);
            size_t selEnd = std::max(m_selectionStart, m_selectionEnd);
            if (selStart <= currentValue.size() && selEnd <= currentValue.size())
            {
                currentValue.erase(selStart, selEnd - selStart);
                m_cursorPos = selStart;
            }
            m_selectionStart = m_cursorPos;
            m_selectionEnd = m_cursorPos;
        }

        // Handle special characters
        if (ch == "\b" || ch == "\x7f") // Backspace
        {
            // If there was a selection, it was already deleted above
            if (!hasSelection && m_cursorPos > 0 && !currentValue.empty())
            {
                currentValue.erase(m_cursorPos - 1, 1);
                m_cursorPos--;
            }
        }
        else if (ch == "\r" || ch == "\n") // Enter - unfocus field
        {
            m_focusedInputField.clear();
            m_cursorPos = 0;
            m_selectionStart = 0;
            m_selectionEnd = 0;
            luaState->SetValue("data.focusedField", "");
            luaState->SetValue("data.cursorPos", 0);
            luaState->MarkDirty();
            return true;
        }
        else
        {
            // Insert character at cursor position
            currentValue.insert(m_cursorPos, ch);
            m_cursorPos += ch.size();
        }

        // Keep selection collapsed after edit
        m_selectionStart = m_cursorPos;
        m_selectionEnd = m_cursorPos;

        // Update value in Lua state
        luaState->SetValue(m_focusedInputField, currentValue);
        luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
        luaState->SetValue("data.inspectorDisplay", InsertCursor(currentValue, m_cursorPos));
        luaState->MarkDirty(); // Trigger UI re-render

        if (m_focusedInputField.rfind("data.inspector.", 0) == 0)
        {
            ApplyInspectorEdits();
        }

        return true; // Consume event
    }

    return false; // No focused input field, pass through
}

bool Editor::HandleEditorInput(const InputEvent &event)
{
    // Only handle keyboard events
    if (event.type != InputTypes::Key)
    {
        return false; // Pass through to Lua
    }

    // Get key name from variant
    std::string key = std::get<std::string>(event.input);

    // ESC - Quit editor
    if (key == "ESCAPE" && event.mods == 0)
    {
        if (m_sceneSession.IsDirty())
        {
            auto state = m_reactiveUI->GetLuaState();
            state->SetValue("data.scene.error", "Save the scene before closing, or discard changes by reopening it.");
            state->MarkDirty();
            return true;
        }
        LOG_INFO("[Editor] ESC pressed - closing editor");
        m_shouldClose = true;
        return true; // Consume event
    }

    // Scene shortcuts use the same commands as the visible Open/Save controls.
    if (key == "S" && (event.mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)))
    {
        SaveSceneDocument();
        return true;
    }
    if (key == "O" && (event.mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)))
    {
        OpenSceneDocument();
        return true;
    }

    // Ctrl+Z - Undo (future)
    if (key == "Z" && (event.mods & GLFW_MOD_CONTROL) && !(event.mods & GLFW_MOD_SHIFT))
    {
        LOG_INFO("[Editor] Ctrl+Z pressed - undo not implemented yet");
        // TODO: Implement undo
        return true;
    }

    // Ctrl+Shift+Z - Redo (future)
    if (key == "Z" && (event.mods & GLFW_MOD_CONTROL) && (event.mods & GLFW_MOD_SHIFT))
    {
        LOG_INFO("[Editor] Ctrl+Shift+Z pressed - redo not implemented yet");
        // TODO: Implement redo
        return true;
    }

    if (key == "ENTER" && !m_focusedInputField.empty())
    {
        m_focusedInputField.clear();
        m_cursorPos = m_selectionStart = m_selectionEnd = 0;
        auto state = m_reactiveUI->GetLuaState();
        state->SetValue("data.focusedField", "");
        state->MarkDirty();
        return true;
    }

    // Handle text input navigation when a field is focused
    if (!m_focusedInputField.empty() && m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        auto luaState = m_reactiveUI->GetLuaState();
        sol::object valueObj = luaState->GetValue(m_focusedInputField);
        std::string value = "";
        if (valueObj.valid() && valueObj.get_type() == sol::type::string)
        {
            value = valueObj.as<std::string>();
        }

        // Clamp cursor position
        if (m_cursorPos > value.size())
        {
            m_cursorPos = value.size();
        }

        bool shift = (event.mods & GLFW_MOD_SHIFT) != 0;
        bool ctrl = (event.mods & GLFW_MOD_CONTROL) != 0;
#ifdef __APPLE__
        bool cmd = (event.mods & GLFW_MOD_SUPER) != 0;
        ctrl = ctrl || cmd; // macOS uses Cmd instead of Ctrl
#endif

        // Left arrow - move cursor left
        if (key == "LEFT")
        {
            if (m_cursorPos > 0)
            {
                m_cursorPos--;
            }
            if (!shift)
            {
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
            }
            else
            {
                m_selectionEnd = m_cursorPos;
            }
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            return true;
        }

        // Right arrow - move cursor right
        if (key == "RIGHT")
        {
            if (m_cursorPos < value.size())
            {
                m_cursorPos++;
            }
            if (!shift)
            {
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
            }
            else
            {
                m_selectionEnd = m_cursorPos;
            }
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            return true;
        }

        // Home - move to start
        if (key == "HOME")
        {
            m_cursorPos = 0;
            if (!shift)
            {
                m_selectionStart = 0;
                m_selectionEnd = 0;
            }
            else
            {
                m_selectionEnd = 0;
            }
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            return true;
        }

        // End - move to end
        if (key == "END")
        {
            m_cursorPos = value.size();
            if (!shift)
            {
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
            }
            else
            {
                m_selectionEnd = m_cursorPos;
            }
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            return true;
        }

        // Backspace - delete character before cursor
        if (key == "BACKSPACE")
        {
            bool hasSelection = m_selectionStart != m_selectionEnd;
            if (hasSelection)
            {
                size_t selStart = std::min(m_selectionStart, m_selectionEnd);
                size_t selEnd = std::max(m_selectionStart, m_selectionEnd);
                if (selStart <= value.size() && selEnd <= value.size())
                {
                    value.erase(selStart, selEnd - selStart);
                    m_cursorPos = selStart;
                }
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
            }
            else if (m_cursorPos > 0 && !value.empty())
            {
                value.erase(m_cursorPos - 1, 1);
                m_cursorPos--;
            }
            luaState->SetValue(m_focusedInputField, value);
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            if (m_focusedInputField.rfind("data.inspector.", 0) == 0)
            {
                ApplyInspectorEdits();
            }
            return true;
        }

        // Delete - delete character after cursor
        if (key == "DELETE")
        {
            bool hasSelection = m_selectionStart != m_selectionEnd;
            if (hasSelection)
            {
                size_t selStart = std::min(m_selectionStart, m_selectionEnd);
                size_t selEnd = std::max(m_selectionStart, m_selectionEnd);
                if (selStart <= value.size() && selEnd <= value.size())
                {
                    value.erase(selStart, selEnd - selStart);
                    m_cursorPos = selStart;
                }
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
            }
            else if (m_cursorPos < value.size())
            {
                value.erase(m_cursorPos, 1);
            }
            luaState->SetValue(m_focusedInputField, value);
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            if (m_focusedInputField.rfind("data.inspector.", 0) == 0)
            {
                ApplyInspectorEdits();
            }
            return true;
        }

        // Ctrl+A - select all
        if (key == "A" && ctrl)
        {
            m_selectionStart = 0;
            m_selectionEnd = value.size();
            m_cursorPos = value.size();
            luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
            luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
            luaState->MarkDirty();
            return true;
        }

        // Ctrl+C - copy
        if (key == "C" && ctrl)
        {
            if (m_selectionStart != m_selectionEnd)
            {
                size_t selStart = std::min(m_selectionStart, m_selectionEnd);
                size_t selEnd = std::max(m_selectionStart, m_selectionEnd);
                std::string selected = value.substr(selStart, selEnd - selStart);
                glfwSetClipboardString(m_windowManager->window, selected.c_str());
            }
            return true;
        }

        // Ctrl+X - cut
        if (key == "X" && ctrl)
        {
            if (m_selectionStart != m_selectionEnd)
            {
                size_t selStart = std::min(m_selectionStart, m_selectionEnd);
                size_t selEnd = std::max(m_selectionStart, m_selectionEnd);
                std::string selected = value.substr(selStart, selEnd - selStart);
                glfwSetClipboardString(m_windowManager->window, selected.c_str());
                value.erase(selStart, selEnd - selStart);
                m_cursorPos = selStart;
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
                luaState->SetValue(m_focusedInputField, value);
                luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
                luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
                luaState->MarkDirty();
                if (m_focusedInputField.rfind("data.inspector.", 0) == 0)
                {
                    ApplyInspectorEdits();
                }
            }
            return true;
        }

        // Ctrl+V - paste
        if (key == "V" && ctrl)
        {
            const char *clipboard = glfwGetClipboardString(m_windowManager->window);
            if (clipboard)
            {
                std::string pasteText(clipboard);
                // Delete selection first if any
                if (m_selectionStart != m_selectionEnd)
                {
                    size_t selStart = std::min(m_selectionStart, m_selectionEnd);
                    size_t selEnd = std::max(m_selectionStart, m_selectionEnd);
                    value.erase(selStart, selEnd - selStart);
                    m_cursorPos = selStart;
                }
                // Insert pasted text at cursor
                value.insert(m_cursorPos, pasteText);
                m_cursorPos += pasteText.size();
                m_selectionStart = m_cursorPos;
                m_selectionEnd = m_cursorPos;
                luaState->SetValue(m_focusedInputField, value);
                luaState->SetValue("data.cursorPos", static_cast<int>(m_cursorPos));
                luaState->SetValue("data.inspectorDisplay", InsertCursor(value, m_cursorPos));
                luaState->MarkDirty();
                if (m_focusedInputField.rfind("data.inspector.", 0) == 0)
                {
                    ApplyInspectorEdits();
                }
            }
            return true;
        }
    }

    // Event not handled by editor, pass to Lua
    return false;
}

void Editor::SelectEntity(EntityID entityId)
{
    LOG_INFO("[Editor] SelectEntity called with entityId={}", static_cast<uint32_t>(entityId));

    const auto previous = m_authoredEntities.find(m_selectedEntityId);
    if (previous != m_authoredEntities.end())
    {
        m_sceneSession.DiscardInvalidTransformDrafts(previous->second);
        UpdateSceneStatus();
    }
    m_focusedInputField.clear();
    if (m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        m_reactiveUI->GetLuaState()->SetValue("data.focusedField", "");
    }

    if (entityId != ENTITY_NULL && m_authoredEntities.find(entityId) == m_authoredEntities.end())
    {
        entityId = ENTITY_NULL;
    }
    m_selectedEntityId = entityId;

    if (m_viewport)
    {
        m_viewport->SetSelectedEntity(entityId);
    }

    UpdateInspector();

    if (m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        m_reactiveUI->GetLuaState()->MarkDirty();
    }
}

void Editor::UpdateInspector()
{
    if (!m_reactiveUI || !m_reactiveUI->GetLuaState())
    {
        return;
    }

    auto luaState = m_reactiveUI->GetLuaState();

    if (m_selectedEntityId == ENTITY_NULL)
    {
        // Clear selection
        luaState->SetValue("data.selectedEntityId", nullptr);
        luaState->SetValue("data.selectedEntityName", "");
        luaState->SetValue("data.scene.script", "");
        luaState->SetValue("data.scene.shader", "");
        luaState->SetValue("data.scene.model", "");
        luaState->SetValue("data.scene.luaShader", "");
        luaState->SetValue("data.scene.luaModel", "");
        luaState->SetValue("data.inspector.posX", "0");
        luaState->SetValue("data.inspector.posY", "0");
        luaState->SetValue("data.inspector.posZ", "0");
        luaState->SetValue("data.inspector.rotX", "0");
        luaState->SetValue("data.inspector.rotY", "0");
        luaState->SetValue("data.inspector.rotZ", "0");
        luaState->SetValue("data.inspector.scaleX", "1");
        luaState->SetValue("data.inspector.scaleY", "1");
        luaState->SetValue("data.inspector.scaleZ", "1");
        return;
    }

    // Get entity name
    std::string entityName = m_registry->GetEntityName(m_selectedEntityId);
    LOG_INFO("[Editor] UpdateInspector: setting selectedEntityId to {}", static_cast<uint32_t>(m_selectedEntityId));
    luaState->SetValue("data.selectedEntityId", static_cast<uint32_t>(m_selectedEntityId));
    luaState->SetValue("data.selectedEntityName", entityName);

    if (m_registry->HasComponent<Transform>(m_selectedEntityId))
    {
        const Transform &transform = m_registry->GetComponent<Transform>(m_selectedEntityId);

        // Display authored coordinates, independent of any runtime projection changes.
        const auto snapshot = m_sceneSession.Snapshot();
        const char *axes[] = {"x", "y", "z"};
        const char *fields[] = {"data.inspector.posX", "data.inspector.posY", "data.inspector.posZ"};
        for (const auto &object : snapshot["scene"]["objects"])
        {
            if (object["name"].as<std::string>() != entityName)
            {
                continue;
            }
            for (const char *field : {"script", "shader", "model", "luaShader", "luaModel"})
            {
                luaState->SetValue(std::string("data.scene.") + field, "");
            }
            if (object["components"] && object["components"].IsSequence())
            {
                for (const auto &component : object["components"])
                {
                    const auto type = component["type"].as<std::string>();
                    if (type == "LuaScript")
                    {
                        for (const auto &[property, field] : std::vector<std::pair<std::string, std::string>>{
                            {"script", "script"}, {"shader", "luaShader"}, {"model", "luaModel"}})
                        {
                            if (component[property] && component[property].IsScalar())
                            {
                                luaState->SetValue("data.scene." + field, component[property].as<std::string>());
                            }
                        }
                    }
                    else if (type == "RenderComponent" || type == "Lighting")
                    {
                        for (const char *field : {"shader", "model"})
                        {
                            if (component[field] && component[field].IsScalar())
                            {
                                luaState->SetValue(std::string("data.scene.") + field, component[field].as<std::string>());
                            }
                        }
                    }
                }
            }
            const auto authoredTransform = object["transform"];
            const auto position = authoredTransform ? authoredTransform["pos"] : YAML::Node();
            for (int i = 0; i < 3; ++i)
            {
                std::ostringstream value;
                value << std::setprecision(std::numeric_limits<double>::max_digits10)
                      << (position && position[axes[i]] ? position[axes[i]].as<double>() : 0.0);
                luaState->SetValue(fields[i], value.str());
                const auto scale = authoredTransform ? authoredTransform["scale"] : YAML::Node();
                std::ostringstream scaleValue;
                scaleValue << std::setprecision(std::numeric_limits<double>::max_digits10)
                           << (scale && scale[axes[i]] ? scale[axes[i]].as<double>() : 1.0);
                const char *scaleFields[] = {"data.inspector.scaleX", "data.inspector.scaleY", "data.inspector.scaleZ"};
                luaState->SetValue(scaleFields[i], scaleValue.str());
            }
        }

        // Convert quaternion to Euler angles for display
        glm::vec3 euler = glm::eulerAngles(transform.Rotation) * glm::degrees(1.0f);
        luaState->SetValue("data.inspector.rotX", FormatFloatValue(euler.x));
        luaState->SetValue("data.inspector.rotY", FormatFloatValue(euler.y));
        luaState->SetValue("data.inspector.rotZ", FormatFloatValue(euler.z));


    }
    else
    {
        // No Transform component - show defaults
        luaState->SetValue("data.inspector.posX", "0");
        luaState->SetValue("data.inspector.posY", "0");
        luaState->SetValue("data.inspector.posZ", "0");
        luaState->SetValue("data.inspector.rotX", "0");
        luaState->SetValue("data.inspector.rotY", "0");
        luaState->SetValue("data.inspector.rotZ", "0");
        luaState->SetValue("data.inspector.scaleX", "1");
        luaState->SetValue("data.inspector.scaleY", "1");
        luaState->SetValue("data.inspector.scaleZ", "1");
    }
    LoadFlyingPropsProperty();
}

void Editor::ApplyInspectorEdits()
{
    const auto authored = m_authoredEntities.find(m_selectedEntityId);
    if (authored == m_authoredEntities.end() || !m_reactiveUI || !m_reactiveUI->GetLuaState())
    {
        return;
    }
    const std::string fields[] = {"data.inspector.posX", "data.inspector.posY", "data.inspector.posZ",
        "data.inspector.scaleX", "data.inspector.scaleY", "data.inspector.scaleZ"};
    const char *axes[] = {"x", "y", "z"};
    auto state = m_reactiveUI->GetLuaState();
    for (size_t axis = 0; axis < 6; ++axis)
    {
        if (m_focusedInputField != fields[axis])
        {
            continue;
        }
        const auto value = state->GetValue(fields[axis]);
        if (!value.is<std::string>())
        {
            return;
        }
        const bool scale = axis >= 3;
        const bool edited = scale ? m_sceneSession.SetScaleText(authored->second, axis - 3, value.as<std::string>())
                                  : m_sceneSession.SetPositionText(authored->second, axis, value.as<std::string>());
        if (edited)
        {
            const auto snapshot = m_sceneSession.Snapshot();
            for (const auto &object : snapshot["scene"]["objects"])
            {
                if (object["name"].as<std::string>() != authored->second)
                {
                    continue;
                }
                const auto position = object["transform"][scale ? "scale" : "pos"];
                auto &transform = m_registry->GetComponent<Transform>(m_selectedEntityId);
                for (int i = 0; i < 3; ++i)
                    (scale ? transform.Scale[i] : transform.Pos[i]) = position[axes[i]].as<float>();
            }
        }
        UpdateSceneStatus();
        return;
    }
}

bool Editor::HandleUIClick(float x, float y, int button)
{
    LOG_INFO("[Editor] HandleUIClick called: ({}, {}) button={}", x, y, button);

    if (!m_htmlRenderer)
    {
        LOG_INFO("[Editor] HandleUIClick: no htmlRenderer");
        return false;
    }

    // Prefer direct viewport picking so selection doesn't depend on async UI hit-testing.
    if (button == 0 && m_viewport && m_windowManager && m_windowManager->window)
    {
        bool shouldHandleViewport = true;

        if (m_reactiveUI && m_reactiveUI->GetLuaState())
        {
            auto luaState = m_reactiveUI->GetLuaState();

            sol::object viewObj = luaState->GetValue("data.activeView");
            if (viewObj.valid() && viewObj.get_type() == sol::type::string)
            {
                shouldHandleViewport = (viewObj.as<std::string>() == "scene");
            }

            sol::object resizingObj = luaState->GetValue("data.resizing.active");
            if (resizingObj.valid() && resizingObj.get_type() == sol::type::boolean)
            {
                if (resizingObj.as<bool>())
                {
                    shouldHandleViewport = false;
                }
            }
        }

        if (shouldHandleViewport)
        {
            // Always fetch fresh viewport bounds for click handling to avoid stale cached values
            int rx = 0, ry = 0, rw = 0, rh = 0;
            bool foundViewport = m_htmlRenderer->TryFindInteractiveElementBoundsByHandler("click", "onViewportClick($event)", rx, ry, rw, rh);
            if (foundViewport)
            {
                m_viewportRectValid = true;
                m_viewportRectX = rx;
                m_viewportRectY = ry;
                m_viewportRectW = rw;
                m_viewportRectH = rh;
            }

            if (foundViewport && rw > 0 && rh > 0)
            {
                int windowWidth = 0, windowHeight = 0;
                int fbWidth = 0, fbHeight = 0;
                glfwGetWindowSize(m_windowManager->window, &windowWidth, &windowHeight);
                glfwGetFramebufferSize(m_windowManager->window, &fbWidth, &fbHeight);

                float scaleX = (windowWidth > 0) ? (static_cast<float>(fbWidth) / static_cast<float>(windowWidth)) : 1.0f;
                float scaleY = (windowHeight > 0) ? (static_cast<float>(fbHeight) / static_cast<float>(windowHeight)) : 1.0f;

                float fbClickX = x * scaleX;
                float fbClickY = y * scaleY;

                // Clamp viewport rect to framebuffer bounds
                rx = std::max(0, std::min(rx, fbWidth - 1));
                ry = std::max(0, std::min(ry, fbHeight - 1));
                rw = std::max(1, std::min(rw, fbWidth - rx));
                rh = std::max(1, std::min(rh, fbHeight - ry));

                if (fbClickX >= static_cast<float>(rx) &&
                    fbClickX < static_cast<float>(rx + rw) &&
                    fbClickY >= static_cast<float>(ry) &&
                    fbClickY < static_cast<float>(ry + rh))
                {
                    int iw = rw;
                    int ih = rh;

                    if (std::abs(iw - m_viewport->GetWidth()) > 1 || std::abs(ih - m_viewport->GetHeight()) > 1)
                    {
                        m_viewport->Resize(iw, ih);
                    }

                    int vx = static_cast<int>(std::floor(fbClickX - static_cast<float>(rx)));
                    int vy = static_cast<int>(std::floor(fbClickY - static_cast<float>(ry)));

                    vx = std::max(0, std::min(vx, m_viewport->GetWidth() - 1));
                    vy = std::max(0, std::min(vy, m_viewport->GetHeight() - 1));

                    LOG_INFO("[Editor] Viewport click at ({}, {}) in viewport coords ({}, {})", fbClickX, fbClickY, vx, vy);
                    EntityID picked = m_viewport->PickEntityAt(vx, vy);
                    LOG_INFO("[Editor] PickEntityAt returned entity {}", static_cast<uint32_t>(picked));
                    SelectEntity(picked);
                    return true;
                }
            }
        }
    }

    // Pass click to HTMLRenderer for hit-testing and event dispatch.
    LOG_INFO("[Editor] HandleUIClick: passing to HandleClickEvent");
    bool result = m_htmlRenderer->HandleClickEvent(x, y, button);
    LOG_INFO("[Editor] HandleUIClick: HandleClickEvent returned {}", result);
    return result;
}

void Editor::InitializeUIEditor()
{
    // Initialize template manager
    m_templateManager = &UITemplateManager::GetInstance();
    m_templateManager->Initialize("../res/ui/templates/", "../res/ui/styles/", "../res/ui/state/");

    // Populate template list in Lua state
    auto luaState = m_reactiveUI->GetLuaState();
    if (!luaState)
    {
        LOG_ERROR("[Editor] Cannot initialize UI Editor - Lua state not available");
        return;
    }

    ScriptManager &scriptManager = ScriptManager::GetInstance();
    sol::state &lua = scriptManager.GetLuaState();

    std::vector<TemplateInfo> templates = m_templateManager->ListTemplates();
    sol::table templatesTable = lua.create_table();

    for (size_t i = 0; i < templates.size(); i++)
    {
        sol::table tplTable = lua.create_table();
        tplTable["name"] = templates[i].name;
        tplTable["hasHTML"] = templates[i].hasHTML;
        tplTable["hasCSS"] = templates[i].hasCSS;
        tplTable["hasLua"] = templates[i].hasLua;
        templatesTable[i + 1] = tplTable; // Lua arrays are 1-indexed
    }

    luaState->SetValue("data.uiEditor.templates", templatesTable);
    luaState->MarkDirty();

    if (templates.size() > 0)
    {
        LOG_INFO("[Editor] UI Editor: {} template(s) loaded", templates.size());
    }
}

void Editor::UpdateUIEditor(double deltaTime)
{
    if (!m_previewNeedsUpdate)
    {
        return;
    }

    // Check if debounce time has elapsed
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCodeChange);

    if (elapsed.count() >= PREVIEW_DEBOUNCE_MS)
    {
        UpdatePreview();
        m_previewNeedsUpdate = false;
    }
}

void Editor::LoadUITemplate(const std::string &templateName)
{
    if (!m_templateManager)
    {
        LOG_ERROR("[Editor] Cannot load template - template manager not initialized");
        return;
    }

    std::string html, css, lua;
    if (!m_templateManager->LoadTemplate(templateName, html, css, lua))
    {
        LOG_ERROR("[Editor] Failed to load template: {}", templateName);
        auto luaState = m_reactiveUI->GetLuaState();
        if (luaState)
        {
            luaState->SetValue("data.uiEditor.errorMessage", "Failed to load template: " + templateName);
            luaState->MarkDirty();
        }
        return;
    }

    // Update Lua state with template content
    auto luaState = m_reactiveUI->GetLuaState();
    if (luaState)
    {
        luaState->SetValue("data.uiEditor.currentTemplate", templateName);
        luaState->SetValue("data.uiEditor.htmlCode", html);
        luaState->SetValue("data.uiEditor.cssCode", css);
        luaState->SetValue("data.uiEditor.luaCode", lua);
        luaState->SetValue("data.uiEditor.isDirty", false);
        luaState->SetValue("data.uiEditor.errorMessage", "");
        luaState->MarkDirty();

        // Update preview immediately
        UpdatePreview();
    }

    LOG_INFO("[Editor] Loaded template: {}", templateName);
}

void Editor::SaveUITemplate()
{
    if (!m_templateManager)
    {
        LOG_ERROR("[Editor] Cannot save template - template manager not initialized");
        return;
    }

    auto luaState = m_reactiveUI->GetLuaState();
    if (!luaState)
    {
        LOG_ERROR("[Editor] Cannot save template - Lua state not available");
        return;
    }

    // Get current template name
    sol::object currentTemplateObj = luaState->GetValue("data.uiEditor.currentTemplate");
    if (!currentTemplateObj.valid() || currentTemplateObj.get_type() != sol::type::string)
    {
        LOG_ERROR("[Editor] Cannot save - no template selected");
        luaState->SetValue("data.uiEditor.errorMessage", "No template selected");
        luaState->MarkDirty();
        return;
    }

    std::string templateName = currentTemplateObj.as<std::string>();
    if (templateName.empty())
    {
        LOG_ERROR("[Editor] Cannot save - template name is empty");
        luaState->SetValue("data.uiEditor.errorMessage", "Template name is empty");
        luaState->MarkDirty();
        return;
    }

    // Get code content from Lua state
    sol::object htmlObj = luaState->GetValue("data.uiEditor.htmlCode");
    sol::object cssObj = luaState->GetValue("data.uiEditor.cssCode");
    sol::object luaObj = luaState->GetValue("data.uiEditor.luaCode");

    std::string html = htmlObj.valid() && htmlObj.get_type() == sol::type::string ? htmlObj.as<std::string>() : "";
    std::string css = cssObj.valid() && cssObj.get_type() == sol::type::string ? cssObj.as<std::string>() : "";
    std::string lua = luaObj.valid() && luaObj.get_type() == sol::type::string ? luaObj.as<std::string>() : "";

    // Save template
    if (m_templateManager->SaveTemplate(templateName, html, css, lua))
    {
        LOG_INFO("[Editor] Saved template: {}", templateName);
        luaState->SetValue("data.uiEditor.isDirty", false);
        luaState->SetValue("data.uiEditor.errorMessage", "");
    }
    else
    {
        LOG_ERROR("[Editor] Failed to save template: {}", templateName);
        luaState->SetValue("data.uiEditor.errorMessage", "Failed to save template");
    }

    luaState->MarkDirty();
}

void Editor::CreateUITemplate(const std::string &templateName)
{
    if (!m_templateManager)
    {
        LOG_ERROR("[Editor] Cannot create template - template manager not initialized");
        return;
    }

    if (templateName.empty())
    {
        auto luaState = m_reactiveUI ? m_reactiveUI->GetLuaState() : nullptr;
        if (luaState)
        {
            luaState->SetValue("data.uiEditor.errorMessage", "Template name is empty");
            luaState->MarkDirty();
        }
        return;
    }

    if (m_templateManager->TemplateExists(templateName))
    {
        LOG_WARNING("[Editor] Template already exists: {}", templateName);
        auto luaState = m_reactiveUI->GetLuaState();
        if (luaState)
        {
            luaState->SetValue("data.uiEditor.errorMessage", "Template already exists: " + templateName);
            luaState->MarkDirty();
        }
        return;
    }

    if (m_templateManager->CreateTemplate(templateName))
    {
        LOG_INFO("[Editor] Created template: {}", templateName);

        // Reload template list
        InitializeUIEditor();

        // Load the newly created template
        LoadUITemplate(templateName);
    }
    else
    {
        LOG_ERROR("[Editor] Failed to create template: {}", templateName);
        auto luaState = m_reactiveUI->GetLuaState();
        if (luaState)
        {
            luaState->SetValue("data.uiEditor.errorMessage", "Failed to create template");
            luaState->MarkDirty();
        }
    }
}

void Editor::UpdatePreview()
{
    if (!m_reactiveUI)
    {
        return;
    }

    auto luaState = m_reactiveUI->GetLuaState();
    if (!luaState)
    {
        return;
    }

    // Get code content from Lua state
    sol::object htmlObj = luaState->GetValue("data.uiEditor.htmlCode");
    sol::object cssObj = luaState->GetValue("data.uiEditor.cssCode");
    sol::object luaObj = luaState->GetValue("data.uiEditor.luaCode");

    std::string html = htmlObj.valid() && htmlObj.get_type() == sol::type::string ? htmlObj.as<std::string>() : "";
    std::string css = cssObj.valid() && cssObj.get_type() == sol::type::string ? cssObj.as<std::string>() : "";
    std::string lua = luaObj.valid() && luaObj.get_type() == sol::type::string ? luaObj.as<std::string>() : "";

    if (html.empty())
    {
        luaState->SetValue("data.uiEditor.previewHTML", "<p>No HTML content to preview</p>");
        luaState->SetValue("data.uiEditor.errorMessage", "");
        luaState->MarkDirty();
        return;
    }

    try
    {
        // Inject CSS into HTML
        std::string htmlWithCSS = html;
        size_t stylePos = htmlWithCSS.find("</head>");
        if (stylePos != std::string::npos)
        {
            std::string styleTag = "    <style>\n" + css + "    </style>\n";
            htmlWithCSS.insert(stylePos, styleTag);
        }
        else
        {
            // No </head> tag, prepend with style
            htmlWithCSS = "<style>\n" + css + "</style>\n" + htmlWithCSS;
        }

        // Try to process template directives if Lua code is provided
        std::string processedHTML = htmlWithCSS;

        if (!lua.empty())
        {
            try
            {
                ScriptManager &scriptManager = ScriptManager::GetInstance();
                sol::state &luaVM = scriptManager.GetLuaState();

                // Execute Lua code to get state table
                sol::load_result loadResult = luaVM.load(lua);
                if (loadResult.valid())
                {
                    sol::protected_function_result result = loadResult();
                    if (result.valid() && result.return_count() > 0 && result[0].is<sol::table>())
                    {
                        // TODO: Add SetStateTable to LuaUIState to avoid temp file workaround
                        std::string tempLuaFile = "../res/ui/state/.preview_temp.lua";
                        FileIO::WriteTextFile(tempLuaFile, lua);

                        TemplateParser parser;
                        parser.Parse(htmlWithCSS);

                        auto previewLuaState = std::make_shared<LuaUIState>();
                        if (previewLuaState->LoadStateFile(tempLuaFile))
                        {
                            processedHTML = parser.Evaluate(*previewLuaState);
                        }

                        std::filesystem::remove(tempLuaFile);
                    }
                }
            }
            catch (const std::exception &e)
            {
                LOG_WARNING("[Editor] Failed to process template directives in preview: {}", e.what());
                // Fall through to show HTML+CSS without directive processing
            }
        }

        // Update preview HTML
        luaState->SetValue("data.uiEditor.previewHTML", processedHTML);
        luaState->SetValue("data.uiEditor.errorMessage", "");
        luaState->MarkDirty();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[Editor] Exception while updating preview: {}", e.what());
        luaState->SetValue("data.uiEditor.errorMessage", "Preview error: " + std::string(e.what()));
        luaState->SetValue("data.uiEditor.previewHTML", "<p style='color: #f48771;'>Error: " + std::string(e.what()) + "</p>");
        luaState->MarkDirty();
    }
}

void Editor::UpdateSceneStatus()
{
    if (m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        auto state = m_reactiveUI->GetLuaState();
        state->SetValue("data.scene.currentPath", m_sceneSession.GetPath());
        state->SetValue("data.scene.dirty", m_sceneSession.IsDirty());
        state->SetValue("data.scene.error", m_sceneSession.GetError());
        const auto scene = m_sceneSession.Snapshot();
        sol::state &lua = ScriptManager::GetInstance().GetLuaState();
        auto modules = lua.create_table();
        if (scene["scripts"])
        {
            for (size_t i = 0; i < scene["scripts"].size(); ++i)
            {
                auto module = lua.create_table();
                module["index"] = static_cast<int>(i);
                module["path"] = scene["scripts"][i].as<std::string>();
                modules[i + 1] = module;
            }
        }
        state->SetValue("data.scene.modules", modules);
        sol::protected_function refreshPage = state->GetStateTable()["methods"]["refreshModulePage"];
        const auto refreshed = refreshPage(state->GetStateTable());
        if (!refreshed.valid())
        {
            sol::error error = refreshed;
            LOG_ERROR("[Editor] Module page refresh failed: {}", error.what());
        }
        const auto capabilities = scene["capabilities"];
        state->SetValue("data.scene.uiCapability", capabilities && capabilities["ui"] ? capabilities["ui"].as<bool>() : false);
        state->SetValue("data.scene.inputCapability", capabilities && capabilities["input"] ? capabilities["input"].as<bool>() : false);
        state->SetValue("data.scene.strict", scene["strict"].as<bool>(false));
        state->MarkDirty();
    }
}

bool Editor::ApplySceneOperation(const std::function<bool(const SceneSession::PreparePreview &)> &operation,
                                 const std::string &selectedName)
{
    std::unordered_map<EntityID, std::string> staged;
    auto discardPreview = [&]()
    {
        for (const auto &[id, name] : staged)
        {
            m_registry->DestroyEntity(id);
        }
        staged.clear();
    };
    bool opened = operation([&](const YAML::Node &scene, std::string &error)
        {
            try
            {
                std::unordered_map<std::string, EntityID> names;
                for (const auto &object : scene["scene"]["objects"])
                {
                    const auto name = object["name"].as<std::string>();
                    const auto id = m_registry->RegisterEntity(name);
                    staged[id] = name;
                    names[name] = id;
                    auto &transform = m_registry->GetComponent<Transform>(id);
                    const auto authored = object["transform"];
                    if (authored)
                    {
                        const char *axes[] = {"x", "y", "z"};
                        const char *colors[] = {"r", "g", "b", "a"};
                        for (int i = 0; i < 3; ++i)
                        {
                            if (authored["pos"] && authored["pos"][axes[i]])
                            {
                                transform.Pos[i] = authored["pos"][axes[i]].as<float>();
                            }
                            if (authored["scale"] && authored["scale"][axes[i]])
                            {
                                transform.Scale[i] = authored["scale"][axes[i]].as<float>();
                            }
                        }
                        for (int i = 0; i < 4; ++i)
                        {
                            if (authored["color"] && authored["color"][colors[i]])
                            {
                                transform.Color[i] = authored["color"][colors[i]].as<float>();
                            }
                        }
                    }
                }
                for (const auto &object : scene["scene"]["objects"])
                {
                    const auto id = names.at(object["name"].as<std::string>());
                    if (!object["components"])
                    {
                        continue;
                    }
                    if (!object["components"].IsSequence())
                    {
                        throw std::runtime_error("Object components must be a sequence");
                    }
                    for (const auto &component : object["components"])
                    {
                        const auto type = component["type"].as<std::string>();
                        if (type != "RenderComponent" && type != "Lighting")
                            continue; // Script and unknown components remain authored data only.
                        const auto shader = std::filesystem::path(conf.ResourcePath) / "shaders" / component["shader"].as<std::string>();
                        const auto model = std::filesystem::path(conf.ResourcePath) / "models" / component["model"].as<std::string>();
                        if (!std::filesystem::is_regular_file(shader) || !std::filesystem::is_regular_file(model))
                        {
                            throw std::runtime_error("Scene references a missing shader or model");
                        }
                        RenderComponent render(shader.string(), model.string());
                        m_registry->RegisterComponent<RenderComponent>(id, render);
                        if (type == "Lighting")
                        {
                            const auto light = names.find(component["light"].as<std::string>());
                            if (light == names.end())
                            {
                                throw std::runtime_error("Scene references an unknown light object");
                            }
                            Lighting lighting(light->second);
                            m_registry->RegisterComponent<Lighting>(id, lighting);
                        }
                    }
                }
                return true;
            }
            catch (const std::exception &exception)
            {
                error = exception.what();
                discardPreview();
                return false;
            }
        });
    if (opened)
    {
        for (const auto &[id, name] : m_authoredEntities)
        {
            m_registry->DestroyEntity(id);
        }
        m_authoredEntities = std::move(staged);
        EntityID selected = ENTITY_NULL;
        for (const auto &[id, name] : m_authoredEntities)
        {
            if (name == selectedName)
            {
                selected = id;
                break;
            }
        }
        SelectEntity(selected);
        m_cursorPos = m_selectionStart = m_selectionEnd = 0;
        UpdateSceneTree();
    }
    UpdateSceneStatus();
    return opened;
}

void Editor::OpenSceneDocument(bool discardEdits)
{
    if (!m_reactiveUI || !m_reactiveUI->GetLuaState())
    {
        return;
    }
    const auto requested = m_reactiveUI->GetLuaState()->GetValue("data.scene.path");
    if (requested.is<std::string>())
    {
        ApplySceneOperation([&](const SceneSession::PreparePreview &prepare)
        {
            return m_sceneSession.Open(requested.as<std::string>(), discardEdits, prepare);
        });
    }
}

void Editor::CreateSceneDocument(bool discardEdits)
{
    auto state = m_reactiveUI->GetLuaState();
    const auto name = state->GetValue("data.scene.name");
    if (name.is<std::string>() && ApplySceneOperation([&](const SceneSession::PreparePreview &prepare)
        {
            return m_sceneSession.Create(name.as<std::string>(), discardEdits, prepare);
        }))
    {
        state->SetValue("data.scene.path", "../res/scenes/" + name.as<std::string>() + ".yaml");
        state->MarkDirty();
    }
}

void Editor::AddSceneObject()
{
    const auto name = m_reactiveUI->GetLuaState()->GetValue("data.scene.objectName");
    if (name.is<std::string>())
    {
        ApplySceneOperation([&](const SceneSession::PreparePreview &prepare)
        {
            return m_sceneSession.AddObject(name.as<std::string>(), prepare);
        }, name.as<std::string>());
    }
}

void Editor::AssignSceneComponent(bool luaScript)
{
    auto state = m_reactiveUI->GetLuaState();
    const auto selected = m_authoredEntities.find(m_selectedEntityId);
    if (selected == m_authoredEntities.end())
    {
        state->SetValue("data.scene.error", "Select an authored object first.");
        state->MarkDirty();
        return;
    }
    const std::string name = selected->second;
    auto text = [&](const char *field)
    {
        const auto value = state->GetValue(field);
        return value.is<std::string>() ? value.as<std::string>() : std::string();
    };
    const auto shader = text(luaScript ? "data.scene.luaShader" : "data.scene.shader");
    const auto model = text(luaScript ? "data.scene.luaModel" : "data.scene.model");
    const auto script = text("data.scene.script");
    ApplySceneOperation([&](const SceneSession::PreparePreview &prepare)
    {
        return luaScript ? m_sceneSession.AssignLuaScript(name, conf.ResourcePath, script, shader, model, prepare)
                         : m_sceneSession.AssignRenderComponent(name, conf.ResourcePath, shader, model, prepare);
    }, name);
}

void Editor::SaveSceneDocument(bool saveAs)
{
    auto state = m_reactiveUI->GetLuaState();
    if (saveAs)
    {
        const auto requested = m_reactiveUI->GetLuaState()->GetValue("data.scene.path");
        if (!requested.is<std::string>() || requested.as<std::string>().empty())
        {
            state->SetValue("data.scene.error", "Enter a destination path for Save As.");
            state->MarkDirty();
            return;
        }
        m_sceneSession.Save(requested.as<std::string>());
    }
    else
    {
        m_sceneSession.Save();
    }
    UpdateSceneStatus();
}

void Editor::EditSceneModules(int index, int direction)
{
    const auto scene = m_sceneSession.Snapshot();
    std::vector<std::string> modules;
    if (scene["scripts"])
    {
        modules = scene["scripts"].as<std::vector<std::string>>();
    }
    if (index < 0)
    {
        const auto path = m_reactiveUI->GetLuaState()->GetValue("data.scene.modulePath");
        if (!path.is<std::string>())
        {
            return;
        }
        modules.push_back(path.as<std::string>());
    }
    else if (static_cast<size_t>(index) < modules.size())
    {
        if (direction == 0)
        {
            modules.erase(modules.begin() + index);
        }
        else if ((direction == -1 || direction == 1) && index + direction >= 0 &&
                 static_cast<size_t>(index + direction) < modules.size())
        {
            std::swap(modules[index], modules[index + direction]);
        }
    }
    m_sceneSession.SetModules(conf.ResourcePath, modules);
    UpdateSceneStatus();
}

void Editor::ToggleSceneCapability(const std::string &name)
{
    const auto scene = m_sceneSession.Snapshot();
    const auto capabilities = scene["capabilities"];
    const bool current = capabilities && capabilities[name] ? capabilities[name].as<bool>() : false;
    m_sceneSession.SetCapability(name, !current);
    UpdateSceneStatus();
}

void Editor::UpdatePlayStatus()
{
    if (!m_reactiveUI || !m_reactiveUI->GetLuaState()) return;
    auto state = m_reactiveUI->GetLuaState();
    const auto previous = state->GetValue("data.play.status");
    if (!previous.is<std::string>() || previous.as<std::string>() != m_playSession.GetStatus())
    {
        state->SetValue("data.play.status", m_playSession.GetStatus());
        state->SetValue("data.play.running", m_playSession.IsRunning());
        state->SetValue("data.play.selectedConfig", m_playSession.GetConfigPath());
        state->MarkDirty();
    }
}

void Editor::LoadFlyingPropsProperty(int direction)
{
    auto state = m_reactiveUI->GetLuaState();
    state->SetValue("data.scene.hasFlyingProps", false);
    const auto authored = m_authoredEntities.find(m_selectedEntityId);
    if (authored == m_authoredEntities.end()) return;
    if (direction != 0) m_reactiveUI->DispatchEvent("click", "focusInput('')", {});
    const auto &properties = SceneDocument::FlyingPropsProperties();
    const auto previous = state->GetValue("data.scene.propertyIndex");
    const int index = std::clamp((previous.is<int>() ? previous.as<int>() : 0) + direction,
                                 0, static_cast<int>(properties.size()) - 1);
    state->SetValue("data.scene.propertyIndex", index);
    state->SetValue("data.scene.propertyName", properties[index]);
    state->SetValue("data.scene.propertyValue", "");
    const auto snapshot = m_sceneSession.Snapshot();
    for (const auto &object : snapshot["scene"]["objects"])
    {
        if (object["name"].as<std::string>() != authored->second || !object["components"].IsSequence()) continue;
        for (const auto &component : object["components"])
        {
            if (component["type"].as<std::string>("") != "LuaScript" ||
                component["script"].as<std::string>("") != "scripts/backgrounds/FlyingProps.lua") continue;
            state->SetValue("data.scene.hasFlyingProps", true);
            const auto value = component[properties[index]];
            state->SetValue("data.scene.propertyValue", value && value.IsScalar() ? value.as<std::string>() : "");
        }
    }
    state->MarkDirty();
}

void Editor::ApplyFlyingPropsProperty()
{
    const auto authored = m_authoredEntities.find(m_selectedEntityId);
    if (authored == m_authoredEntities.end()) return;
    const auto state = m_reactiveUI->GetLuaState();
    const auto property = state->GetValue("data.scene.propertyName");
    const auto value = state->GetValue("data.scene.propertyValue");
    if (property.is<std::string>() && value.is<std::string>() &&
        m_sceneSession.SetFlyingPropsProperty(authored->second, conf.ResourcePath,
                                             property.as<std::string>(), value.as<std::string>()))
    {
        m_reactiveUI->DispatchEvent("click", "focusInput('')", {});
        LoadFlyingPropsProperty();
    }
    UpdateSceneStatus();
}
