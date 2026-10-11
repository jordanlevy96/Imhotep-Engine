/** @file Editor.h Editor application controller. */
#pragma once

#include "controllers/EngineCore.h"
#include "controllers/WindowManager.h"
#include "controllers/Registry.h"
#include "systems/HTMLRendererMT.h"
#include "systems/ReactiveUI.h"
#include "editor/SceneViewport.h"
#include "editor/UITemplateManager.h"
#include "editor/SceneSession.h"
#include "editor/PlaySession.h"
#include <unordered_map>
#include "util/Config.h"
#include <chrono>
#include <limits>

/** Editor window and authoring controller. */
class Editor
{
public:
    static Editor &GetInstance()
    {
        static Editor instance;
        return instance;
    }

    Editor(Editor const &) = delete;
    void operator=(Editor const &) = delete;

    bool Initialize();

    void Run();

    void Shutdown();

private:
    Editor() = default;
    ~Editor() { Shutdown(); }
    void Render();
    void LoadEditorUI();
    void UpdateSceneTree();
    void UpdateSystems(double deltaTime);
    bool HandleEditorInput(const InputEvent &event);
    bool HandleTextInput(const InputEvent &event);
    bool HandleUIClick(float x, float y, int button);
    void SelectEntity(EntityID entityId);
    void UpdateInspector();
    void ApplyInspectorEdits();
    void InitializeUIEditor();
    void UpdateUIEditor(double deltaTime);
    void LoadUITemplate(const std::string &templateName);
    void SaveUITemplate();
    void CreateUITemplate(const std::string &templateName);
    void UpdatePreview();
    void OpenSceneDocument(bool discardEdits = false);
    void CreateSceneDocument(bool discardEdits = false);
    void AddSceneObject();
    void AssignSceneComponent(bool luaScript);
    void LoadFlyingPropsProperty(int direction = 0);
    void ApplyFlyingPropsProperty();
    // Swap authored preview entities only after the session accepts the candidate.
    bool ApplySceneOperation(const std::function<bool(const SceneSession::PreparePreview &)> &operation,
                             const std::string &selectedName = "");
    void SaveSceneDocument(bool saveAs = false);
    void UpdateSceneStatus();
    // Negative index adds; otherwise direction is -1/0/+1 for move up/remove/move down.
    void EditSceneModules(int index, int direction);
    void ToggleSceneCapability(const std::string &name);
    void UpdatePlayStatus();
    PlaySession m_playSession;
    SceneSession m_sceneSession;
    std::unordered_map<EntityID, std::string> m_authoredEntities;

    // Configuration
    Config conf;

    // Core engine helper
    EngineCore m_core;

    // Singletons (references to engine systems)
    WindowManager *m_windowManager = nullptr;
    Registry *m_registry = nullptr;
    HTMLRendererMT *m_htmlRenderer = nullptr;
    ReactiveUI *m_reactiveUI = nullptr;
    Camera *m_camera = nullptr;

    // Editor-specific systems
    SceneViewport *m_viewport = nullptr;
    UITemplateManager *m_templateManager = nullptr;

    // Editor state
    bool m_initialized = false;
    bool m_shouldClose = false;
    EntityID m_selectedEntityId = ENTITY_NULL;

    // Timing for game loop (FrameTiming manages timing internally)
    double m_delta = 0.0;

    // Input handler IDs
    size_t m_keyboardHandlerId = std::numeric_limits<size_t>::max();
    size_t m_clickHandlerId = std::numeric_limits<size_t>::max();

    // UI Editor state
    std::chrono::steady_clock::time_point m_lastCodeChange;
    bool m_previewNeedsUpdate = false;
    static constexpr double PREVIEW_DEBOUNCE_MS = 500.0; // 500ms debounce for preview updates
    
    // Text input state
    std::string m_focusedInputField; // Lua path to focused input field (e.g., "uiEditor.newTemplateName")
    size_t m_cursorPos = 0;          // Cursor position within focused field
    size_t m_selectionStart = 0;     // Selection start (or same as cursorPos if no selection)
    size_t m_selectionEnd = 0;       // Selection end

    // Cached viewport rectangle (framebuffer coords, top-left origin).
    bool m_viewportRectValid = false;
    int m_viewportRectX = 0;
    int m_viewportRectY = 0;
    int m_viewportRectW = 0;
    int m_viewportRectH = 0;
};
