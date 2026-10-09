/**
 * @file Editor.h
 * @brief Main editor application controller
 *
 * Phase 1: Basic window with HTML UI panels
 */

#pragma once

#include "controllers/EngineCore.h"
#include "controllers/WindowManager.h"
#include "controllers/Registry.h"
#include "systems/HTMLRendererMT.h"
#include "systems/ReactiveUI.h"
#include "editor/SceneViewport.h"
#include "editor/UITemplateManager.h"
#include "editor/SceneSession.h"
#include <unordered_map>
#include "util/Config.h"
#include <chrono>
#include <limits>

/**
 * @brief Editor application singleton
 *
 * Manages editor lifecycle, UI state, and coordinates between
 * engine systems for editing workflows.
 *
 * **Phase 1 scope**: Window initialization, basic panel layout
 */
class Editor
{
public:
    /**
     * @brief Get singleton instance
     * @return Reference to Editor singleton
     */
    static Editor &GetInstance()
    {
        static Editor instance;
        return instance;
    }

    Editor(Editor const &) = delete;
    void operator=(Editor const &) = delete;

    /**
     * @brief Initialize editor window and UI
     * @return true if initialization succeeded
     */
    bool Initialize();

    /**
     * @brief Run editor main loop
     * @note Blocking call until editor is closed
     */
    void Run();

    /**
     * @brief Cleanup and shutdown editor
     */
    void Shutdown();

private:
    Editor() = default;
    ~Editor() { Shutdown(); }

    /**
     * @brief Render editor frame
     * @note Called every frame from Run()
     */
    void Render();

    /**
     * @brief Load editor UI templates
     */
    void LoadEditorUI();

    /**
     * @brief Update scene tree data in Lua state
     * @note Populates entity list with names and hierarchy
     */
    void UpdateSceneTree();

    /**
     * @brief Update game systems (scripts, tweens, etc.)
     * @param deltaTime Time since last update in milliseconds
     */
    void UpdateSystems(double deltaTime);

    /**
     * @brief Handle editor-specific input (shortcuts and UI events)
     * @param event Input event to handle
     * @return true if event was consumed
     */
    bool HandleEditorInput(const InputEvent &event);

    /**
     * @brief Handle text input (character input for text fields)
     * @param event Input event with character data
     * @return true if event was consumed
     */
    bool HandleTextInput(const InputEvent &event);

    /**
     * @brief Handle UI click events from HTMLRenderer
     * @param x Click X coordinate
     * @param y Click Y coordinate
     * @param button Mouse button (0=left, 1=right, 2=middle)
     * @return true if click was handled by editor UI, false if it should propagate to game
     */
    bool HandleUIClick(float x, float y, int button);

    /**
     * @brief Select an entity in the editor
     * @param entityId ID of entity to select
     */
    void SelectEntity(EntityID entityId);

    /**
     * @brief Update inspector panel with selected entity's transform data
     */
    void UpdateInspector();

    /**
     * @brief Apply inspector-edited values to the selected entity
     * @note Called during text input while an inspector field is focused
     */
    void ApplyInspectorEdits();

    /**
     * @brief Initialize UI Editor system
     * @note Called during Initialize() to set up template manager and populate template list
     */
    void InitializeUIEditor();

    /**
     * @brief Update UI Editor (handles debounced preview updates)
     * @param deltaTime Time since last update in milliseconds
     */
    void UpdateUIEditor(double deltaTime);

    /**
     * @brief Load template into UI Editor
     * @param templateName Name of template to load (without extension)
     */
    void LoadUITemplate(const std::string &templateName);

    /**
     * @brief Save current template from UI Editor to disk
     */
    void SaveUITemplate();

    /**
     * @brief Create new template
     * @param templateName Name for new template
     */
    void CreateUITemplate(const std::string &templateName);

    /**
     * @brief Update preview with current HTML/CSS/Lua code
     * @note Creates temporary ReactiveUI instance for preview rendering
     */
    void UpdatePreview();

    void OpenSceneDocument(bool discardEdits = false);
    void SaveSceneDocument(bool saveAs = false);
    void UpdateSceneStatus();
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
