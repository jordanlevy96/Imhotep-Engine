/**
 * @file Editor.cpp
 * @brief Scene and UI template editor implementation
 * @lines ~910
 *
 * Purpose: Interactive editor for scene manipulation and UI template creation.
 * Combines 3D viewport with hierarchy panel, inspector, and UI template editor.
 *
 * Key functions:
 * - Initialize() - Setup editor UI, viewport, input handlers (line 24, ~160 lines)
 * - Run() - Main editor loop (line 185, ~40 lines)
 * - Render() - Coordinate viewport + UI rendering (line 259, ~40 lines)
 * - LoadEditorUI() - Load editor HTML templates (line 298, ~30 lines)
 * - UpdateSceneTree() - Refresh hierarchy panel (line 329, ~50 lines)
 * - SelectEntity() - Handle entity selection (line 504, ~15 lines)
 * - UpdateInspector() - Refresh inspector panel with entity data (line 521, ~75 lines)
 * - HandleEditorInput() - Keyboard/mouse input routing (line 449, ~55 lines)
 *
 * UI Template Editor:
 * - InitializeUIEditor() - Setup template editing UI (line 607, ~40 lines)
 * - LoadUITemplate() - Load template for editing (line 664, ~40 lines)
 * - SaveUITemplate() - Write template changes to disk (line 704, ~60 lines)
 * - CreateUITemplate() - New template wizard (line 763, ~50 lines)
 * - UpdatePreview() - Live preview of template changes (line 811, ~100 lines)
 *
 * Architecture:
 * - Uses EngineCore for shared initialization
 * - SceneViewport for 3D rendering with selection
 * - ReactiveUI for editor panels (HTML/CSS)
 * - Input priority system (editor intercepts before game)
 *
 * Integration: Standalone editor mode, shares engine code with Game
 */

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

bool Editor::Initialize()
{
    LOG_INFO("[Editor] Starting initialization...");

    if (m_initialized)
    {
        LOG_WARNING("[Editor] Already initialized");
        return true;
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

    // Load scene (editor test scene without game scripts)
    if (!m_core.LoadScene("scenes/EditorTest.yaml"))
    {
        LOG_ERROR("Failed to load scene: scenes/EditorTest.yaml");
        return false;
    }

    // Load editor UI (creates Lua state)
    LoadEditorUI();

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

    while (!glfwWindowShouldClose(m_windowManager->window) && !m_shouldClose)
    {
        // CRITICAL: Poll input events first so window is responsive
        glfwPollEvents();

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
    std::vector<EntityID> entityIds = m_registry->GetAllEntities();

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
        LOG_INFO("[Editor] ESC pressed - closing editor");
        m_shouldClose = true;
        return true; // Consume event
    }

    // Ctrl+S - Save scene (future)
    if (key == "S" && (event.mods & GLFW_MOD_CONTROL))
    {
        LOG_INFO("[Editor] Ctrl+S pressed - save not implemented yet");
        // TODO: Implement scene saving
        return true;
    }

    // Ctrl+O - Open scene (future)
    if (key == "O" && (event.mods & GLFW_MOD_CONTROL))
    {
        LOG_INFO("[Editor] Ctrl+O pressed - open not implemented yet");
        // TODO: Implement scene loading
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

    m_focusedInputField.clear();
    if (m_reactiveUI && m_reactiveUI->GetLuaState())
    {
        m_reactiveUI->GetLuaState()->SetValue("data.focusedField", "");
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

        luaState->SetValue("data.inspector.posX", FormatFloatValue(transform.Pos.x));
        luaState->SetValue("data.inspector.posY", FormatFloatValue(transform.Pos.y));
        luaState->SetValue("data.inspector.posZ", FormatFloatValue(transform.Pos.z));

        // Convert quaternion to Euler angles for display
        glm::vec3 euler = glm::eulerAngles(transform.Rotation) * glm::degrees(1.0f);
        luaState->SetValue("data.inspector.rotX", FormatFloatValue(euler.x));
        luaState->SetValue("data.inspector.rotY", FormatFloatValue(euler.y));
        luaState->SetValue("data.inspector.rotZ", FormatFloatValue(euler.z));

        luaState->SetValue("data.inspector.scaleX", FormatFloatValue(transform.Scale.x));
        luaState->SetValue("data.inspector.scaleY", FormatFloatValue(transform.Scale.y));
        luaState->SetValue("data.inspector.scaleZ", FormatFloatValue(transform.Scale.z));
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
}

void Editor::ApplyInspectorEdits()
{
    if (!m_reactiveUI || !m_reactiveUI->GetLuaState() || !m_registry)
    {
        return;
    }

    if (m_selectedEntityId == ENTITY_NULL)
    {
        return;
    }

    if (!m_registry->HasComponent<Transform>(m_selectedEntityId))
    {
        return;
    }

    auto luaState = m_reactiveUI->GetLuaState();
    Transform &transform = m_registry->GetComponent<Transform>(m_selectedEntityId);

    auto getString = [&](const char *path, std::string &outValue) -> bool
    {
        sol::object obj = luaState->GetValue(path);
        if (!obj.valid() || obj.get_type() == sol::type::lua_nil)
        {
            return false;
        }

        if (obj.get_type() == sol::type::string)
        {
            outValue = obj.as<std::string>();
            return true;
        }

        if (obj.get_type() == sol::type::number)
        {
            outValue = FormatFloatValue(static_cast<float>(obj.as<double>()));
            return true;
        }

        return false;
    };

    float parsed = 0.0f;
    std::string text;

    if (getString("data.inspector.posX", text) && TryParseFloatValue(text, parsed))
    {
        transform.Pos.x = parsed;
    }
    if (getString("data.inspector.posY", text) && TryParseFloatValue(text, parsed))
    {
        transform.Pos.y = parsed;
    }
    if (getString("data.inspector.posZ", text) && TryParseFloatValue(text, parsed))
    {
        transform.Pos.z = parsed;
    }

    glm::vec3 eulerDegrees = glm::eulerAngles(transform.Rotation) * glm::degrees(1.0f);
    bool rotationChanged = false;

    if (getString("data.inspector.rotX", text) && TryParseFloatValue(text, parsed))
    {
        eulerDegrees.x = parsed;
        rotationChanged = true;
    }
    if (getString("data.inspector.rotY", text) && TryParseFloatValue(text, parsed))
    {
        eulerDegrees.y = parsed;
        rotationChanged = true;
    }
    if (getString("data.inspector.rotZ", text) && TryParseFloatValue(text, parsed))
    {
        eulerDegrees.z = parsed;
        rotationChanged = true;
    }

    if (rotationChanged)
    {
        transform.Rotation = glm::quat(glm::radians(eulerDegrees));
    }

    if (getString("data.inspector.scaleX", text) && TryParseFloatValue(text, parsed))
    {
        transform.Scale.x = parsed;
    }
    if (getString("data.inspector.scaleY", text) && TryParseFloatValue(text, parsed))
    {
        transform.Scale.y = parsed;
    }
    if (getString("data.inspector.scaleZ", text) && TryParseFloatValue(text, parsed))
    {
        transform.Scale.z = parsed;
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
