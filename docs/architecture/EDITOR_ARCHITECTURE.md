# Imhotep Editor - Full Architecture Plan

**Last Updated**: January 16, 2026

> **Status note (2026-10-08): Design history / roadmap.** This long-form plan predates the current editor viewport, scene-module contracts, and several implemented panels. Its phase table and code snippets are not an authoritative description of current behavior. Use source plus [CURRENT_ARCHITECTURE.md](CURRENT_ARCHITECTURE.md), [EDITOR_VIEWPORT.md](EDITOR_VIEWPORT.md), and [API_CONTRACTS.md](API_CONTRACTS.md) for current contracts.

## Implementation Status

| Phase | Name | Status | Date |
|-------|------|--------|------|
| 1 | Foundation & Selection | ✅ Complete | 2026-01-16 |
| 2 | Scene Viewport | Planned | - |
| 3 | Inspector & Editing | Planned | - |
| 4 | Hot Reload | Planned | - |
| 5 | UI Editor | Planned | - |
| 6 | Advanced Features | Planned | - |

**Phase 1 Complete**: Click-to-select entity interaction, inspector panel with Transform display, and viewport selection highlighting. See `docs/handoff.20260116.md` for implementation details.

## Overview

Build a standalone editor application for Imhotep using the engine's own HTML/CSS/Lua reactive UI system. The editor will provide:

- **Scene editing**: Visual viewport, entity hierarchy, transform gizmos, component inspector
- **UI development**: Live template editing, Lua state inspector, visual CSS editor
- **Hot reload**: Watch files and auto-reload on changes
- **Shared architecture**: Editor links against core engine library, directly manipulates scenes/UI
- **Scene persistence**: Save/load scenes with full undo/redo support
- **Error resilience**: Graceful recovery from malformed files, auto-backup of unsaved changes

## Architecture

### High-Level Structure

```
imhotep-editor (executable)
├─► Links against libcore.so (shared engine library)
├─► Own window with HTMLRendererMT UI
├─► SceneViewport (embedded game view)
├─► FileWatcher (hot reload)
└─► EditorSystems (scene manipulation, gizmos, selection)

Game window              Editor window
┌──────────────┐        ┌─────────────────────────────────┐
│              │        │ Menu Bar    [File][Edit][View]  │
│  3D Scene    │◄──────►│ ┌─────────────┬─────────────┐   │
│  (playing)   │        │ │ Scene Tree  │ 3D Viewport │   │
│              │        │ │ - Light     │ [Game View] │   │
└──────────────┘        │ │ - TetrisGrid│             │   │
                        │ │             │             │   │
                        │ ├─────────────┼─────────────┤   │
                        │ │ Inspector   │ UI Editor   │   │
                        │ │ Transform   │ [Live HTML] │   │
                        │ │ Components  │ [CSS]       │   │
                        │ └─────────────┴─────────────┘   │
                        └─────────────────────────────────┘
```

### Shared Library Pattern

```
libcore.so (existing):
├─ Registry (ECS)
├─ ScriptManager (Lua/Python)
├─ ReactiveUI + HTMLRendererMT
├─ Camera, WindowManager, etc.
└─ All engine systems

imhotep (game executable):
├─ Links libcore.so
├─ main() → Game::Initialize() → Game::Run()
└─ Runs game loop

imhotep-editor (new):
├─ Links libcore.so (same as game!)
├─ main() → Editor::Initialize() → Editor::Run()
├─ Creates TWO contexts:
│   ├─ Editor window (tool UI)
│   └─ Scene viewport (embedded game preview)
└─ Runs editor loop
```

## Core Components to Build

### 1. Editor Application (`src/editor/Editor.cpp`)

**Responsibilities**:

- Initialize editor window with HTMLRendererMT
- Load editor UI templates (scene tree, inspector, etc.)
- Manage editor state (selection, tools, modes)
- Handle editor-specific input (viewport camera, gizmos)
- Coordinate between editor UI and scene viewport

**Key methods**:

```cpp
class Editor {
    bool Initialize();
    void Run();  // Editor main loop
    void Render();
    void Shutdown();

    // Selection
    void SelectEntity(EntityID id);
    EntityID GetSelectedEntity();

    // Tools
    void SetTool(EditorTool tool);  // Move, Rotate, Scale
    void SetEditMode(EditMode mode); // Scene, UI, Play

private:
    WindowManager* m_editorWindow;
    HTMLRendererMT* m_editorUI;
    SceneViewport* m_viewport;
    FileWatcher* m_fileWatcher;
    EditorState m_state;
};
```

### 2. Scene Viewport (`src/editor/SceneViewport.cpp`)

**Responsibilities**:

- Embed game rendering in editor window
- Separate camera for editor (not game camera)
- Handle viewport-specific input (orbit, pan, zoom)
- Render transform gizmos
- Entity picking (click to select)

**Key features**:

```cpp
class SceneViewport {
    void Initialize(int width, int height);
    void Render(Registry& registry);
    void HandleInput(InputEvent& event);

    // Picking
    EntityID PickEntity(int mouseX, int mouseY);

    // Gizmos
    void RenderGizmo(EntityID entity, GizmoMode mode);

    // Camera
    Camera& GetEditorCamera();

private:
    Camera m_editorCamera;
    GLuint m_framebuffer;  // Render to texture
    GLuint m_colorTexture; // Displayed in editor UI
    GLuint m_pickingTexture; // For entity picking
    GizmoRenderer m_gizmos;
};
```

### 3. File Watcher (`src/editor/FileWatcher.cpp`)

**Responsibilities**:

- Watch res/ directory for file changes
- Trigger hot reload on HTML/CSS/Lua changes
- Reload scenes on YAML changes
- Notify editor UI of changes

**Platform-specific implementations**:

- macOS: FSEvents API
- Linux: inotify
- Windows: ReadDirectoryChangesW

```cpp
class FileWatcher {
    void Initialize(const std::string& watchPath);
    void Update();  // Call each frame, checks for changes

    // Callbacks
    void OnFileChanged(const std::string& path);
    void OnFileCreated(const std::string& path);
    void OnFileDeleted(const std::string& path);

private:
    std::vector<std::string> m_watchPaths;
    std::map<std::string, time_t> m_lastModified;
    #ifdef __APPLE__
        FSEventStreamRef m_stream;
    #elif __linux__
        int m_inotifyFd;
    #elif _WIN32
        HANDLE m_dirHandle;
    #endif
};
```

### 4. Editor UI Templates (HTML/CSS/Lua)

#### Scene Tree Panel (`res/editor/templates/scene_tree.html`)

```html
<div class="panel scene-tree">
  <h2>Scene Hierarchy</h2>
  <div
    v-for="entity in entities"
    class="entity-row"
    :class="{selected: entity.id == selectedId}"
    @click="selectEntity(entity.id)"
  >
    <span class="entity-icon">{{ entity.icon }}</span>
    <span class="entity-name">{{ entity.name }}</span>
  </div>
</div>
```

#### Inspector Panel (`res/editor/templates/inspector.html`)

```html
<div class="panel inspector">
  <h2>Inspector</h2>
  <div v-if="selectedEntity">
    <h3>{{ selectedEntity.name }}</h3>

    <!-- Transform Component -->
    <div class="component">
      <h4>Transform</h4>
      <label>Position</label>
      <input
        type="number"
        v-model="selectedEntity.transform.x"
        @change="updateTransform"
      />
      <input
        type="number"
        v-model="selectedEntity.transform.y"
        @change="updateTransform"
      />
      <input
        type="number"
        v-model="selectedEntity.transform.z"
        @change="updateTransform"
      />
    </div>

    <!-- Other components -->
    <div v-for="comp in selectedEntity.components" class="component">
      <h4>{{ comp.type }}</h4>
      <!-- Component-specific fields -->
    </div>
  </div>
</div>
```

#### UI Editor Panel (`res/editor/templates/ui_editor.html`)

```html
<div class="panel ui-editor">
  <h2>UI Editor</h2>

  <!-- Template selector -->
  <select v-model="currentTemplate" @change="loadTemplate">
    <option v-for="tpl in templates">{{ tpl.name }}</option>
  </select>

  <!-- Code editor (syntax highlighted textarea or Monaco editor) -->
  <div class="code-section">
    <h3>HTML Template</h3>
    <textarea v-model="htmlCode" @input="onHTMLChange"></textarea>
  </div>

  <div class="code-section">
    <h3>CSS Styles</h3>
    <textarea v-model="cssCode" @input="onCSSChange"></textarea>
  </div>

  <div class="code-section">
    <h3>Lua State</h3>
    <textarea v-model="luaCode" @input="onLuaChange"></textarea>
  </div>

  <!-- Live preview -->
  <div class="preview-panel">
    <h3>Live Preview</h3>
    <div class="preview-content" v-html="previewHTML"></div>
  </div>
</div>
```

### 5. Editor State Management (`src/editor/EditorState.cpp`)

```cpp
struct EditorState {
    EntityID selectedEntity = -1;
    EditorTool currentTool = EditorTool::SELECT;
    EditMode editMode = EditMode::SCENE;

    // Scene editing
    std::vector<EntityID> clipboard;
    bool gridSnap = true;
    float snapSize = 1.0f;

    // UI editing
    std::string currentUITemplate;
    bool livePreview = true;

    // Settings
    bool showGrid = true;
    bool showGizmos = true;
    glm::vec3 backgroundColor{0.2f, 0.2f, 0.2f};
};

enum class EditorTool {
    SELECT,     // Pick entities
    MOVE,       // Transform gizmo (translate)
    ROTATE,     // Transform gizmo (rotate)
    SCALE       // Transform gizmo (scale)
};

enum class EditMode {
    SCENE,      // 3D scene editing
    UI,         // UI template editing
    PLAY        // Game preview mode
};
```

## File Structure

```
imhotep/
├── src/
│   ├── editor/                      # NEW: Editor-specific code
│   │   ├── main.cpp                 # Editor executable entry point
│   │   ├── Editor.cpp/.h            # Main editor class
│   │   ├── SceneViewport.cpp/.h     # Embedded game view
│   │   ├── FileWatcher.cpp/.h       # Hot reload system
│   │   ├── EditorState.cpp/.h       # Editor state management
│   │   ├── Gizmos.cpp/.h            # Transform gizmos rendering
│   │   └── EntityPicker.cpp/.h      # Click-to-select entities
│   │
│   ├── controllers/                 # EXISTING: Engine core
│   ├── systems/                     # EXISTING: Engine systems
│   └── util/                        # EXISTING: Utilities
│
├── include/
│   └── editor/                      # NEW: Editor headers
│       ├── Editor.h
│       ├── SceneViewport.h
│       ├── FileWatcher.h
│       ├── EditorState.h
│       ├── Gizmos.h
│       └── EntityPicker.h
│
├── res/
│   └── editor/                      # NEW: Editor UI resources
│       ├── templates/
│       │   ├── main_layout.html     # Overall editor layout
│       │   ├── scene_tree.html      # Scene hierarchy panel
│       │   ├── inspector.html       # Component inspector
│       │   ├── ui_editor.html       # UI template editor
│       │   └── menu_bar.html        # Top menu bar
│       │
│       ├── styles/
│       │   ├── editor.css           # Main editor styles
│       │   ├── panels.css           # Panel styling
│       │   └── syntax.css           # Code editor syntax highlighting
│       │
│       └── state/
│           └── editor.lua           # Editor UI state
│
├── CMakeLists.txt                   # MODIFY: Add editor target
└── build/
    ├── libcore.so                   # Existing shared library
    ├── imhotep                      # Existing game executable
    └── imhotep-editor               # NEW: Editor executable
```

## Implementation Phases

### Phase 1: Foundation & Selection (Complete)

**Status**: ✅ Complete (January 16, 2026)

**Goal**: Get basic editor window running with UI panels, entity selection, and inspector

**Tasks**:

1. **Refactor build system**
   - Make `libcore` a true shared library
   - Ensure all engine code is in `libcore`
   - Create new `imhotep-editor` CMake target

2. **Create Editor application skeleton**
   - `src/editor/main.cpp` - Entry point
   - `src/editor/Editor.cpp` - Main loop
   - Initialize window with HTMLRendererMT
   - Load basic editor UI template

3. **Build basic UI layout**
   - Top menu bar (File, Edit, View)
   - Panel system with resizable splits
   - Empty panels for: Scene Tree, Viewport, Inspector, UI Editor

4. **Editor UI state**
   - `res/editor/state/editor.lua` - Editor state management
   - Bind to ReactiveUI
   - Test reactive updates

**Deliverable**: Editor window with working entity selection, inspector panel, and 3D viewport

#### Phase 1 Implementation Notes

**What was implemented**:
- Standalone `imhotep-editor` executable
- Basic HTML/CSS UI using HTMLRendererMT
- Three-panel layout (Scene Hierarchy, Viewport, Inspector)
- **Scene Hierarchy Panel**:
  - Lists all entities from Registry
  - Click to select entity
  - Blue highlight on selected entity
  - Dynamic v-for rendering from Lua state
- **Inspector Panel**:
  - Displays selected entity Transform component
  - Shows Position, Rotation (Euler angles), Scale
  - Read-only display (editing in Phase 2)
  - "No selection" state when nothing selected
- **3D Viewport**:
  - Renders game scene to FBO texture
  - Displays as base64 PNG in HTML img tag
  - Yellow wireframe selection highlight on selected entity
  - 800x600 resolution
- **Input Handling**:
  - ESC key to quit editor
  - Click-to-select working via HTMLRendererMT hit-testing
  - Event routing through ReactiveUI to C++ handlers
- Dark theme UI (VS Code-inspired)
- Proper HiDPI/Retina display support

**Critical fixes applied during implementation**:

1. **Logger Initialization (Segfault Fix)**: `LOG_*` macros crashed before `Logger::Initialize()` was called. Solution: Initialize logger in `main.cpp` before `Editor::GetInstance()`.

2. **Input Key Case Sensitivity**: WindowManager's `keyMap` uses UPPERCASE strings ("ESCAPE" not "escape"). Input handlers must compare against uppercase.

3. **HiDPI/Retina Rendering**: Must use `glfwGetFramebufferSize()` not `glfwGetWindowSize()` when initializing HTMLRendererMT and setting glViewport.

4. **CSS Color Contrast**: Changed text from `#888888` to `#d4d4d4` for readability on dark background.

5. **TemplateParser Loop Variable Expansion (Jan 16)**: `ProcessIterationInterpolations()` only handled `{{ entity.id }}` syntax but not bare `entity.id` in event handlers. Added second regex pass to expand bare references.

6. **Selection Highlight (Jan 16)**: Made `RenderSystem::RenderEntity` public and added wireframe overlay rendering in `SceneViewport::Render()`.

**Build configuration**:
```cmake
add_executable(imhotep-editor
    src/editor/main.cpp
    src/editor/Editor.cpp
    src/editor/SceneViewport.cpp
)
target_link_libraries(imhotep-editor PRIVATE core)
```

**Binary output**: `build/imhotep-editor`, logs to `build/logs/editor.log`

**Phase 1 Complete** - All MVP features working:
- ✅ Entity selection from Scene Hierarchy
- ✅ Inspector shows Transform data
- ✅ Viewport selection highlight visible
- ✅ 60 FPS performance

**Known Phase 1 limitations** (addressed in later phases):
- No transform editing (read-only inspector)
- No viewport picking (select via tree only)
- No hot reload
- No undo/redo
- No scene save/load
- Menu items non-interactive

### Phase 2: Scene Viewport

**Goal**: Embed game rendering, camera control, entity picking

1. **SceneViewport implementation**

   - Render to texture (FBO)
   - Display texture in editor UI panel
   - Separate editor camera (orbit/pan/zoom)
   - Sync with Registry entities

2. **Entity picking**

   - Color-coded picking buffer
   - Mouse click → EntityID lookup
   - Highlight selected entity

3. **Scene Tree panel**
   - Display all entities from Registry
   - Show hierarchy (parent/child)
   - Click to select entity
   - Bind selection to viewport

**Deliverable**: Can see game scene in editor, click entities to select, see in tree

### Phase 3: Inspector & Editing

**Goal**: View and modify entity components

1. **Inspector panel**

   - Display selected entity name
   - Show Transform component (editable)
   - Show RenderComponent (shader, mesh paths)
   - Show other components (read-only initially)

2. **Component editing**

   - Input fields for Transform (pos, rot, scale)
   - Update Registry on change
   - Instant visual feedback in viewport

3. **Transform gizmos**
   - Render 3-axis arrows for position
   - Drag arrows to move entity
   - Snap to grid (optional)

**Deliverable**: Can select entity, modify transform, see changes live

### Phase 4: Hot Reload

**Goal**: File watching and automatic reloading

1. **FileWatcher implementation**

   - Platform-specific file monitoring
   - Detect changes to res/ files
   - Debounce rapid changes

2. **Hot reload handlers**

   - HTML/CSS changes → Reload ReactiveUI templates
   - Lua state changes → Reload LuaUIState
   - Scene YAML changes → Reload scene
   - Shader changes → Recompile shaders

3. **Editor UI notification**
   - Show "Reloading..." indicator
   - Display errors if reload fails
   - Log changes to console panel

**Deliverable**: Edit files externally, see changes instantly in editor

### Phase 5: UI Editor

**Goal**: Visual UI template editing with live preview

1. **UI Editor panel**

   - Template selector dropdown
   - Code editor for HTML/CSS/Lua
   - Syntax highlighting (basic)
   - Save buttons

2. **Live preview**

   - Separate preview panel
   - Render current template
   - Update on code change (debounced)
   - Show errors inline

3. **Template management**

   - Create new template
   - Duplicate template
   - Delete template
   - Organize in folders

4. **Lua state inspector**
   - Display current state values
   - Edit values at runtime
   - See immediate UI updates

**Deliverable**: Can edit UI templates visually, see live preview, manipulate state

### Phase 6: Advanced Features

**Goal**: Professional workflow enhancements

1. **Scene manipulation**

   - Create new entities
   - Delete entities
   - Duplicate entities
   - Drag-drop in hierarchy
   - Copy/paste entities

2. **Component management**

   - Add components to entities
   - Remove components
   - Edit component properties (all types)

3. **Undo/Redo**

   - Command pattern for all actions
   - Undo stack (Ctrl+Z)
   - Redo stack (Ctrl+Shift+Z)

4. **Save/Load**

   - Save scene to YAML
   - Save UI templates to files
   - Project management

5. **Play Mode**
   - Switch to Play mode
   - Run game logic in viewport
   - Switch back to Edit mode (preserves changes)

**Deliverable**: Full-featured editor with professional workflow

## Critical Files to Create

### New Files

1. **src/editor/main.cpp** - Editor entry point
2. **src/editor/Editor.cpp/.h** - Main editor class
3. **src/editor/SceneViewport.cpp/.h** - Game view embedding
4. **src/editor/FileWatcher.cpp/.h** - Hot reload system
5. **src/editor/EditorState.cpp/.h** - Editor state
6. **src/editor/Gizmos.cpp/.h** - Transform gizmos
7. **src/editor/EntityPicker.cpp/.h** - Entity selection
8. **res/editor/templates/\*.html** - Editor UI templates
9. **res/editor/styles/\*.css** - Editor CSS
10. **res/editor/state/editor.lua** - Editor Lua state

### Files to Modify

1. **CMakeLists.txt** - Add `imhotep-editor` target, ensure `libcore` is shared
2. **src/main.cpp** - Ensure game remains separate executable
3. **src/controllers/Registry.cpp** - Add editor-friendly entity creation/manipulation APIs
4. **src/systems/ReactiveUI.cpp** - May need extensions for code editor widgets

## Technical Considerations

### Build System Changes

**Current CMakeLists.txt** (line 5-12, 178) creates `libcore.so` but bundles main.cpp:

```cmake
add_library(core SHARED
    "src/Camera.cpp"
    ...
    "src/main.cpp"  # <- Line 12: Should NOT be in library!
)
...
add_executable(imhotep src/main.cpp)  # <- Line 178: Also defines main()
```

**Problem**: Both `libcore.so` and `imhotep` define `main()`. When `imhotep-editor` links against `libcore.so`, linker will fail with duplicate symbol error.

**Fix** (CMakeLists.txt):

```cmake
# Remove line 12 ("src/main.cpp") from add_library(core SHARED ...)

# Line 178 stays as-is:
add_executable(imhotep src/main.cpp)
target_link_libraries(imhotep PRIVATE core)

# Add after line 179:
add_executable(imhotep-editor
    "src/editor/main.cpp"
    "src/editor/Editor.cpp"
    "src/editor/SceneViewport.cpp"
    "src/editor/FileWatcher.cpp"
    "src/editor/EditorState.cpp"
    "src/editor/Gizmos.cpp"
    "src/editor/EntityPicker.cpp"
)
target_link_libraries(imhotep-editor PRIVATE core)
target_include_directories(imhotep-editor PRIVATE ${core_INCLUDE_DIRS})
```

**Verification**: After fix, run:

```bash
nm -g build/libcore.dylib | grep " T _main"  # Should return nothing
```

### Rendering Architecture

**Single window with FBO-based viewport** (preferred approach):

```cpp
// Editor window (uses existing singleton - will need refactoring)
WindowManager& wm = WindowManager::GetInstance();
wm.Initialize(1920, 1080);

// Editor UI renderer (full window)
HTMLRendererMT& editorUI = HTMLRendererMT::GetInstance();
editorUI.Initialize(wm.window, 1920, 1080);

// Scene viewport (renders to FBO texture, displayed in editor UI)
SceneViewport* viewport = new SceneViewport();
viewport->Initialize(800, 600);  // Size determined by panel layout
viewport->SetRegistry(&Registry::GetInstance());

// Each frame:
// 1. viewport->Render() draws scene to FBO
// 2. Pass FBO texture ID to editor Lua state
// 3. Editor HTML displays texture via custom image loader
```

**Why single window?** WindowManager is currently a singleton. Multi-window support would require significant refactoring. Single window with FBO viewport is simpler and sufficient for Phase 1-4.

**Future multi-window** (Phase 6+): Refactor WindowManager to manage `std::vector<GLFWwindow*>` with context sharing for undocked panels.

### Shared Registry Pattern

Both editor and game manipulate the same Registry:

```cpp
// In Editor::Initialize()
registry = &Registry::GetInstance();  // Same singleton as game uses!

// When editing
EntityID entity = registry->GetEntityByName("Light");
Transform& t = registry->GetComponent<Transform>(entity);
t.Pos = glm::vec3(10, 20, 30);  // Update directly! (Note: field is Pos, not position)
```

### Required Registry Extensions

The editor needs APIs that don't currently exist. Add to `Registry.h`:

```cpp
// Get total entity count
size_t GetEntityCount() const { return entityNames.size(); }

// Get all entity IDs (for scene tree)
std::vector<EntityID> GetAllEntities() const {
    std::vector<EntityID> ids;
    for (size_t i = 0; i < entityNames.size(); i++) {
        ids.push_back(i);
    }
    return ids;
}

// Get entity name by ID
const std::string& GetEntityName(EntityID id) const {
    return entityNames[id];
}

// Rename entity
void SetEntityName(EntityID id, const std::string& name) {
    if (id < entityNames.size()) {
        entityNames[id] = name;
    }
}

// Save scene to YAML (inverse of LoadScene)
bool SaveScene(const std::string& path);

// Clone entity with all components
EntityID CloneEntity(EntityID source);
```

**SaveScene implementation** (add to `Registry.cpp`):

```cpp
bool Registry::SaveScene(const std::string& path) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "scene" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "objects" << YAML::Value << YAML::BeginSeq;

    for (size_t i = 0; i < entityNames.size(); i++) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << entityNames[i];

        if (TransformComponents.HasComponent(i)) {
            Transform& t = TransformComponents.GetComponent(i);
            out << YAML::Key << "transform" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "pos" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "x" << YAML::Value << t.Pos.x;
            out << YAML::Key << "y" << YAML::Value << t.Pos.y;
            out << YAML::Key << "z" << YAML::Value << t.Pos.z;
            out << YAML::EndMap;
            // ... scale, color similarly
            out << YAML::EndMap;
        }
        // ... other components
        out << YAML::EndMap;
    }

    out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;

    std::ofstream fout(path);
    fout << out.c_str();
    return fout.good();
}
```

### Hot Reload Implementation

```cpp
// FileWatcher detects change
void FileWatcher::OnFileChanged(const std::string& path) {
    if (path.ends_with(".html") || path.ends_with(".css")) {
        // Reload UI template
        ReactiveUI::GetInstance().LoadTemplateFromFiles(htmlPath, cssPath);
        ReactiveUI::GetInstance().ForceRender();
    }
    else if (path.ends_with(".lua")) {
        // Reload Lua state
        LuaUIState* state = ReactiveUI::GetInstance().GetLuaState().get();
        state->LoadStateFile(path);
        state->MarkDirty();
    }
    else if (path.ends_with(".yaml")) {
        // Reload scene
        registry->Shutdown();
        registry->LoadScene(path);
    }
}
```

### Input Routing Architecture

**Problem**: Current input flow goes directly to Lua (WindowManager.cpp lines 240-305):

```
GLFW → WindowManager callbacks → ScriptManager event queue → Lua handlers
```

The editor needs C++ to handle:

- Gizmo dragging (transform manipulation)
- Viewport camera (orbit/pan/zoom)
- Keyboard shortcuts (Ctrl+Z, Ctrl+S, etc.)
- Panel interactions

** Potential Solution**: Add interceptor pattern to WindowManager.

**Add to `WindowManager.h`**:

```cpp
/// Input handler callback type (return true to consume event)
using InputHandler = std::function<bool(const InputEvent&)>;

class WindowManager {
public:
    // ... existing methods ...

    /**
     * @brief Register C++ input handler (called before Lua)
     * @param handler Callback that returns true to consume event
     * @return Handler ID for unregistration
     */
    size_t RegisterInputHandler(InputHandler handler);

    /**
     * @brief Unregister input handler
     * @param id Handler ID from RegisterInputHandler
     */
    void UnregisterInputHandler(size_t id);

private:
    std::vector<std::pair<size_t, InputHandler>> m_inputHandlers;
    size_t m_nextHandlerId = 0;
};
```

**Modify callbacks** (e.g., `click_callback`):

```cpp
void WindowManager::click_callback(GLFWwindow* window, int button, int action, int mods) {
    if (action != GLFW_PRESS) return;

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    InputEvent event;
    event.type = InputTypes::Click;
    event.input = glm::vec3(xpos, ypos, button);

    // NEW: Try C++ handlers first
    WindowManager& wm = GetInstance();
    for (auto& [id, handler] : wm.m_inputHandlers) {
        if (handler(event)) {
            return;  // Event consumed by C++ handler
        }
    }

    // Fall through to Lua
    ScriptManager& sm = ScriptManager::GetInstance();
    sm.AddToTable(EVENT_QUEUE, event);
}
```

**Editor usage**:

```cpp
void Editor::Initialize() {
    WindowManager& wm = WindowManager::GetInstance();

    // Register gizmo handler (high priority)
    m_gizmoHandlerId = wm.RegisterInputHandler([this](const InputEvent& e) {
        if (e.type == InputTypes::Click) {
            return m_gizmos.HandleClick(e);  // Returns true if hit gizmo
        }
        return false;
    });

    // Register keyboard shortcuts
    m_shortcutHandlerId = wm.RegisterInputHandler([this](const InputEvent& e) {
        if (e.type == InputTypes::Key) {
            return HandleKeyboardShortcut(e);
        }
        return false;
    });
}
```

### Keyboard Shortcuts System

Editor needs keyboard shortcuts independent of Lua game logic.

```cpp
// src/editor/KeyboardShortcuts.h
struct Shortcut {
    int key;           // GLFW key code
    int mods;          // GLFW_MOD_CONTROL, GLFW_MOD_SHIFT, etc.
    std::function<void()> action;
};

class KeyboardShortcuts {
public:
    void Register(int key, int mods, std::function<void()> action);
    bool HandleKey(int key, int mods);  // Returns true if shortcut matched

private:
    std::vector<Shortcut> m_shortcuts;
};

// In Editor::Initialize()
m_shortcuts.Register(GLFW_KEY_Z, GLFW_MOD_CONTROL, [this]() {
    m_undoStack.Undo();
});
m_shortcuts.Register(GLFW_KEY_Z, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT, [this]() {
    m_undoStack.Redo();
});
m_shortcuts.Register(GLFW_KEY_S, GLFW_MOD_CONTROL, [this]() {
    SaveScene();
});
m_shortcuts.Register(GLFW_KEY_DELETE, 0, [this]() {
    DeleteSelectedEntity();
});
```

**Note**: GLFW key callback needs modification to pass `mods` parameter. Current implementation (WindowManager.cpp:243-253) ignores `mods`.

## Risks & Mitigations

1. **Risk**: Editor UI complexity overwhelms the reactive UI system

   - **Mitigation**: Start simple, add features incrementally. The Tetris UI proves the system works.
   - **Fallback**: If performance degrades, implement virtual scrolling for large lists (scene tree with 1000+ entities).

2. **Risk**: Single OpenGL context limits architecture

   - **Mitigation**: Use FBO for viewport rendering, single context is sufficient.
   - **Note**: We're NOT using two contexts. SceneViewport renders to FBO, texture displayed in editor UI.

3. **Risk**: File watching doesn't work on all platforms

   - **Mitigation**: Implement polling fallback first (simple `stat()` checks), add native APIs (FSEvents, inotify) as optimization.
   - **Polling cost**: ~1ms per 100 files checked. Debounce to 500ms intervals.

4. **Risk**: Gizmo rendering is complex

   - **Mitigation**: Use simple line rendering initially, can upgrade later.
   - **Reference**: Blender's transform gizmos for visual design.

5. **Risk**: Viewport texture display in litehtml

   - **Problem**: litehtml doesn't understand OpenGL textures natively.
   - **Solution A**: Implement custom `document_container::load_image()` that returns a sentinel URL, then in `draw_background()` detect sentinel and blit from FBO texture.
   - **Solution B**: Copy FBO to CPU buffer, encode as data URI (slow but simple for Phase 1).
   - **Mitigation**: Start with Solution B, optimize to Solution A in Phase 3.

6. **Risk**: Undo/redo complexity

   - **Mitigation**: Command pattern with simple value snapshots initially. Deep cloning only for complex operations.
   - **Memory**: Limit undo stack to 100 operations or 50MB.

7. **Risk**: Editor crashes lose unsaved work
   - **Mitigation**: Auto-save to `~/.imhotep/autosave/` every 60 seconds. Crash recovery dialog on startup.

## Success Criteria

**Phase 1 (Foundation & Selection)** - ✅ Complete:

- [x] Editor launches successfully with panel layout
- [x] Window displays at correct resolution with HiDPI support
- [x] UI renders with readable text and proper contrast
- [x] ESC key closes editor cleanly
- [x] Scene tree displays all entities from Registry
- [x] Can select entities (click in scene tree)
- [x] Selected entity highlighted in tree (blue background)
- [x] Inspector shows selected entity Transform component
- [x] Viewport renders game scene to texture
- [x] Selected entity highlighted in viewport (yellow wireframe)
- [x] 60 FPS performance

**Phase 2 (Camera Controls & Viewport Picking)** - Planned:

- [ ] Can orbit/pan/zoom viewport camera
- [ ] Can select entities by clicking in 3D viewport (picking)
- [ ] Viewport gizmos for transform manipulation

**Phase 3 (Editing & Persistence)**:

- [ ] Can modify Transform values in inspector
- [ ] Changes reflect immediately in viewport
- [ ] Can save/load scenes (Ctrl+S/O)
- [ ] Hot reload works for HTML/CSS/Lua files
- [ ] Basic keyboard shortcuts work (Ctrl+Z, Ctrl+S)

**Phase 5-6 (Advanced)**:

- [ ] Can edit UI templates with live preview
- [ ] Can manipulate Lua state at runtime
- [ ] Undo/redo works for all operations
- [ ] Can save/load scenes
- [ ] Auto-save prevents data loss

**Non-Goals** (explicit exclusions):

- ❌ ImGui code anywhere
- ❌ Multi-window support (deferred to future)
- ❌ Asset import pipeline (use external tools)
- ❌ Visual scripting (Lua is sufficient)

## Pitfalls & Anti-Patterns to Avoid

### 1. **DON'T: Mix Editor and Game State**

❌ **Wrong**:

```cpp
// In Editor.cpp
bool gameIsRunning = true;  // Global state confusion!
```

✅ **Correct**:

```cpp
// Clear separation
EditorState m_editorState;  // Editor-only (selection, tools, etc.)
// Registry is shared but represents game state
```

**Why**: Keep editor state (UI, selection, tools) completely separate from game state (entities, components). The Registry is shared, but it represents the game world, not editor state.

---

### 2. **DON'T: Call Game Loop from Editor**

❌ **Wrong**:

```cpp
// In Editor::Run()
Game::GetInstance().Run();  // Deadlock! Both have event loops
```

✅ **Correct**:

```cpp
// Editor has its own loop, renders scene manually
void Editor::Render() {
    m_viewport->RenderScene(registry);  // Direct rendering, no game loop
}
```

**Why**: The editor has its own main loop. Don't invoke `Game::Run()` from editor code because it blocks with its own event loop. Render the scene directly in the editor viewport instead.

---

### 3. **DON'T: Forget to Sync Viewport Texture to Editor UI**

❌ **Wrong**:

```cpp
// Render to FBO but never display it
viewport->Render();
// Editor UI shows nothing!
```

✅ **Correct**:

```cpp
// 1. Render scene to texture
viewport->Render();

// 2. Get texture ID
GLuint texId = viewport->GetColorTexture();

// 3. Pass to editor UI (via Lua state or CSS background-image)
editorState->SetValue("viewportTexture", texId);
```

**Why**: The viewport renders to an offscreen framebuffer. You must explicitly pass that texture to the HTML UI for display.

---

### 4. **DON'T: Use ImGui Anywhere**

❌ **Wrong**:

```cpp
// Re-introducing legacy immediate-mode debug UI
legacyDebugUI->RenderWindow();
```

✅ **Correct**:

```cpp
// Use your own HTML/CSS/Lua system exclusively
htmlRenderer->Render();
```

**Why**: ImGui is obsolete legacy code. The editor uses HTMLRendererMT + ReactiveUI for all UI, proving the system works for complex interfaces.

---

### 5. **DON'T: Tightly Couple Editor to Specific Game Logic**

❌ **Wrong**:

```cpp
// In Inspector.cpp
if (entity.name == "TetrisGrid") {
    ShowTetrisSpecificPanel();  // Editor knows about Tetris!
}
```

✅ **Correct**:

```cpp
// Generic component inspection
for (auto& comp : entity.GetComponents()) {
    RenderComponentFields(comp);  // Works for any component
}
```

**Why**: The editor should be game-agnostic. It manipulates generic ECS components, not game-specific logic. Tetris, strategy games, FPS - all use the same editor.

---

### 6. **DON'T: Modify Registry Without Dirty Tracking**

❌ **Wrong**:

```cpp
Transform& t = registry->GetComponent<Transform>(entity);
t.position.x = 10.0f;  // Changed but viewport doesn't know!
```

✅ **Correct**:

```cpp
Transform& t = registry->GetComponent<Transform>(entity);
t.position.x = 10.0f;
m_viewport->MarkDirty();  // OR use dirty flag system
// Next frame, viewport re-renders
```

**Why**: The viewport caches rendering. Without dirty flags, it won't know to re-render when components change.

---

### 7. **DON'T: Create Multiple WindowManager Instances**

❌ **Wrong**:

```cpp
WindowManager editorWindow;  // Instance 1
WindowManager gameWindow;    // Instance 2 - CONFLICT!
```

✅ **Correct**:

```cpp
// WindowManager is a singleton
WindowManager& wm = WindowManager::GetInstance();

// If you need multiple windows:
// - Refactor WindowManager to support multiple GLFWwindow*
// - OR create a separate EditorWindow class
```

**Why**: WindowManager is currently a singleton. If you need multiple windows, refactor it to manage multiple GLFWwindow pointers, not multiple instances.

---

### 8. **DON'T: Assume Viewport Size = Window Size**

❌ **Wrong**:

```cpp
viewport->Initialize(windowWidth, windowHeight);
// Viewport takes full window - no room for panels!
```

✅ **Correct**:

```cpp
// Viewport is one panel among many
viewport->Initialize(800, 600);  // Fixed size or calculated from layout

// In HTML:
// <div class="viewport-panel" style="width: 800px; height: 600px">
```

**Why**: The viewport is embedded in the editor UI. It's one panel alongside Scene Tree, Inspector, etc. Its size is determined by the panel layout, not the window size.

---

### 9. **DON'T: Hot Reload Without Error Handling**

❌ **Wrong**:

```cpp
void OnFileChanged(const std::string& path) {
    registry->LoadScene(path);  // What if YAML is malformed?
}
```

✅ **Correct**:

```cpp
void OnFileChanged(const std::string& path) {
    try {
        registry->LoadScene(path);
        ShowNotification("Reloaded: " + path, NotificationType::SUCCESS);
    } catch (const std::exception& e) {
        ShowNotification("Error: " + e.what(), NotificationType::ERROR);
        // Keep old scene intact
    }
}
```

**Why**: File edits can introduce syntax errors. Always catch exceptions during hot reload and show errors in the editor UI. Never crash the editor.

---

### 10. **DON'T: Block the Editor Loop with File I/O**

❌ **Wrong**:

```cpp
void Editor::Run() {
    while (!shouldClose) {
        fileWatcher->ScanDirectoryRecursive();  // Blocks for 100ms!
        Render();
    }
}
```

✅ **Correct**:

```cpp
void Editor::Run() {
    while (!shouldClose) {
        fileWatcher->Update();  // Fast check, uses OS events
        Render();
    }
}
```

**Why**: Use native file watching APIs (FSEvents, inotify, ReadDirectoryChangesW) that notify via events, not polling. Polling blocks the UI thread.

---

### 11. **DON'T: Expose OpenGL Textures Directly to HTML UI**

❌ **Wrong**:

```html
<!-- Can't do this! -->
<img src="gl://texture/42" />
```

✅ **Correct**:

```cpp
// Option 1: Copy texture to CPU, base64 encode
std::string base64 = EncodeTextureToBase64(texId);
luaState->SetValue("viewportImage", "data:image/png;base64," + base64);

// Option 2: Use custom litehtml image loader
// Implement document_container::get_image() to fetch GL textures
```

**Why**: litehtml doesn't understand OpenGL textures. You must either encode to base64 data URIs or implement a custom image loader.

---

### 12. **DON'T: Forget Thread Safety for HTMLRendererMT**

❌ **Wrong**:

```cpp
// Main thread
htmlRenderer->LoadHTML(newHTML);

// Render thread (at the same time)
htmlRenderer->RenderToBuffer();  // RACE CONDITION!
```

✅ **Correct**:

```cpp
// HTMLRendererMT already handles this internally with mutexes
// Just call LoadHTML() from main thread, it's safe
htmlRenderer->LoadHTML(newHTML);
```

**Why**: HTMLRendererMT uses mutexes and condition variables. Review `docs/architecture/UI_SYSTEM.md` for thread safety guarantees.

---

### 13. **DON'T: Hardcode Paths**

❌ **Wrong**:

```cpp
registry->LoadScene("/Users/jordan/dev/cppengine/res/scenes/MainScene.yaml");
```

✅ **Correct**:

```cpp
registry->LoadScene("../res/scenes/MainScene.yaml");  // Relative to build/
// OR use conf.ResourcePath from settings.yaml
```

**Why**: Paths are relative to the executable location (build/ directory). Use relative paths or the ResourcePath config variable.

---

### 14. **DON'T: Build Editor UI Before Reading UI_SYSTEM.md**

❌ **Wrong**:

```cpp
// Guessing at directive syntax
<div v-for="entity in entities">  // Is this right?
```

✅ **Correct**:

```cpp
// Read docs first!
// docs/architecture/UI_SYSTEM.md has:
// - Full directive syntax
// - v-if, v-for, {{ }} examples
// - Event handling (@click, etc.)
// - Lua state structure
```

**Why**: The reactive UI system has specific syntax and patterns. Don't guess - read the comprehensive docs that already exist.

---

### 15. **DON'T: Over-Engineer Phase 1**

❌ **Wrong** (Phase 1):

```cpp
// Trying to build everything at once
class UndoRedoSystem { ... };
class AdvancedGizmoRenderer { ... };
class MonacoEditorIntegration { ... };
```

✅ **Correct** (Phase 1):

```cpp
// Just get a window with panels
Editor::Initialize() {
    CreateWindow();
    LoadBasicUI();
    // That's it!
}
```

**Why**: Phase 1 is foundation only - a window with empty panels. Resist the urge to jump ahead. Each phase builds on the previous.

---

## Error Recovery Strategy

### Auto-Save System

```cpp
// src/editor/AutoSave.cpp
class AutoSave {
public:
    void Initialize(const std::string& projectPath) {
        m_autosaveDir = GetHomeDir() + "/.imhotep/autosave/";
        std::filesystem::create_directories(m_autosaveDir);
        m_lastSaveTime = std::chrono::steady_clock::now();
    }

    void Update() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastSaveTime);

        if (elapsed.count() >= 60 && m_hasUnsavedChanges) {
            SaveBackup();
            m_lastSaveTime = now;
        }
    }

    void SaveBackup() {
        std::string timestamp = GetTimestamp();  // e.g., "2026-01-03_051500"
        std::string backupPath = m_autosaveDir + "scene_" + timestamp + ".yaml";
        Registry::GetInstance().SaveScene(backupPath);

        // Keep only last 5 backups
        CleanOldBackups(5);
    }

    bool HasRecoverableBackup() {
        return !GetLatestBackup().empty();
    }

    void RecoverFromBackup() {
        std::string latest = GetLatestBackup();
        if (!latest.empty()) {
            Registry::GetInstance().LoadScene(latest);
        }
    }

private:
    std::string m_autosaveDir;
    std::chrono::steady_clock::time_point m_lastSaveTime;
    bool m_hasUnsavedChanges = false;
};
```

### Crash Recovery Dialog

On editor startup, check for autosave files newer than last clean shutdown:

```cpp
void Editor::Initialize() {
    if (m_autosave.HasRecoverableBackup()) {
        // Show recovery dialog via HTML UI
        m_editorState.showRecoveryDialog = true;
        // Dialog offers: "Recover" or "Discard"
    }
}
```

### Graceful Error Handling

```cpp
// Wrapper for all file operations
template<typename Func>
bool SafeFileOperation(const std::string& description, Func operation) {
    try {
        operation();
        return true;
    } catch (const YAML::Exception& e) {
        ShowNotification("YAML Error: " + std::string(e.what()), NotificationType::ERROR);
        LOG_ERROR("[Editor] {} failed: {}", description, e.what());
    } catch (const std::exception& e) {
        ShowNotification("Error: " + std::string(e.what()), NotificationType::ERROR);
        LOG_ERROR("[Editor] {} failed: {}", description, e.what());
    }
    return false;
}

// Usage
void Editor::LoadScene(const std::string& path) {
    SafeFileOperation("Load scene", [&]() {
        Registry::GetInstance().LoadScene(path);
        m_editorState.currentScenePath = path;
    });
}
```

---

## Undo/Redo System

### Command Pattern Implementation

```cpp
// src/editor/UndoSystem.h
class Command {
public:
    virtual ~Command() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual std::string GetDescription() const = 0;
    virtual size_t GetMemorySize() const { return sizeof(*this); }
};

class TransformCommand : public Command {
public:
    TransformCommand(EntityID entity, const Transform& oldValue, const Transform& newValue)
        : m_entity(entity), m_oldValue(oldValue), m_newValue(newValue) {}

    void Execute() override {
        Registry::GetInstance().GetComponent<Transform>(m_entity) = m_newValue;
    }

    void Undo() override {
        Registry::GetInstance().GetComponent<Transform>(m_entity) = m_oldValue;
    }

    std::string GetDescription() const override {
        return "Transform " + Registry::GetInstance().GetEntityName(m_entity);
    }

private:
    EntityID m_entity;
    Transform m_oldValue;
    Transform m_newValue;
};

class UndoStack {
public:
    void Execute(std::unique_ptr<Command> cmd) {
        cmd->Execute();

        // Clear redo stack on new action
        m_redoStack.clear();

        // Add to undo stack
        m_undoStack.push_back(std::move(cmd));
        m_totalMemory += m_undoStack.back()->GetMemorySize();

        // Enforce limits
        while (m_undoStack.size() > MAX_UNDO_COUNT || m_totalMemory > MAX_UNDO_MEMORY) {
            m_totalMemory -= m_undoStack.front()->GetMemorySize();
            m_undoStack.pop_front();
        }
    }

    void Undo() {
        if (m_undoStack.empty()) return;
        auto cmd = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        cmd->Undo();
        m_redoStack.push_back(std::move(cmd));
    }

    void Redo() {
        if (m_redoStack.empty()) return;
        auto cmd = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));
    }

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

private:
    static constexpr size_t MAX_UNDO_COUNT = 100;
    static constexpr size_t MAX_UNDO_MEMORY = 50 * 1024 * 1024;  // 50MB

    std::deque<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
    size_t m_totalMemory = 0;
};
```

### Batching Related Changes

```cpp
// For drag operations (many small transform changes)
class CompoundCommand : public Command {
public:
    void Add(std::unique_ptr<Command> cmd) {
        m_commands.push_back(std::move(cmd));
    }

    void Execute() override {
        for (auto& cmd : m_commands) cmd->Execute();
    }

    void Undo() override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
            (*it)->Undo();
        }
    }

private:
    std::vector<std::unique_ptr<Command>> m_commands;
};

// Usage: Start compound on mouse down, commit on mouse up
void Editor::OnGizmoStartDrag() {
    m_currentCompound = std::make_unique<CompoundCommand>();
}

void Editor::OnGizmoDrag(const Transform& newValue) {
    // Apply immediately for visual feedback
    // (don't add to compound - we'll snapshot at end)
}

void Editor::OnGizmoEndDrag() {
    // Create single command for entire drag operation
    auto cmd = std::make_unique<TransformCommand>(
        m_selectedEntity, m_dragStartTransform, currentTransform);
    m_undoStack.Execute(std::move(cmd));
}
```

---

## Viewport Texture Integration

### The Problem

litehtml renders HTML/CSS to a pixel buffer, but doesn't understand OpenGL textures. The SceneViewport renders to an FBO texture. How do we display the viewport texture inside the editor HTML UI?

### Solution: Custom Image Sentinel Pattern

**Phase 1 (Simple but slow)**:

```cpp
// Copy FBO to CPU, encode as data URI
std::vector<uint8_t> pixels(viewport->GetWidth() * viewport->GetHeight() * 4);
glBindTexture(GL_TEXTURE_2D, viewport->GetColorTexture());
glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

// Encode as PNG → base64
std::string base64 = EncodePNGBase64(pixels, width, height);
luaState->SetValue("viewportImage", "data:image/png;base64," + base64);
```

```html
<div class="viewport-panel">
  <img src="{{ viewportImage }}" />
</div>
```

**Cost**: ~5-10ms per frame for 800x600 viewport. Acceptable for Phase 1-2.

**Phase 3+ (Optimized)**:

Modify `SoftwareRenderer::draw_background()` to detect sentinel URL and composite directly:

```cpp
// In SoftwareRenderer::draw_background() (HTMLRendererMT.cpp)
void SoftwareRenderer::draw_background(uint_ptr hdc, const background_paint& bg) {
    // Check for viewport sentinel
    if (bg.image.starts_with("viewport://")) {
        // Extract viewport name: "viewport://scene" → "scene"
        std::string viewportName = bg.image.substr(11);

        // Get viewport texture from editor
        GLuint texId = Editor::GetInstance().GetViewportTexture(viewportName);

        // Read viewport pixels (FBO → CPU)
        // This is fast because it's already on GPU, just need one readback
        std::vector<uint8_t> vpPixels;
        ReadTextureToBuffer(texId, vpPixels);

        // Blit into current pixel buffer at bg.position_x, bg.position_y
        BlitPixels(vpPixels, bg.position_x, bg.position_y, bg.image_size.width, bg.image_size.height);
        return;
    }

    // Normal background rendering
    // ... existing code ...
}
```

```html
<div
  class="viewport-panel"
  style="background-image: url('viewport://scene');"
></div>
```

**Why this works**: The sentinel URL triggers special handling in the software renderer. We do ONE texture readback per viewport per frame instead of full PNG encoding.

**Alternative - Direct Composite (Phase 5+)**:

Don't embed viewport in HTML at all. Render HTML to texture, then composite viewport texture on top in a second pass:

```cpp
void Editor::Render() {
    // 1. Render scene to viewport FBO
    m_viewport->Render();

    // 2. Render editor HTML UI (with placeholder for viewport)
    m_editorUI->Render();

    // 3. Composite: draw viewport texture over the placeholder region
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_compositeShader->Use();

    // Draw editor UI
    glBindTexture(GL_TEXTURE_2D, m_editorUI->GetTexture());
    DrawFullscreenQuad();

    // Draw viewport texture over viewport region
    glBindTexture(GL_TEXTURE_2D, m_viewport->GetColorTexture());
    DrawQuadAt(m_viewportRect);  // Position from CSS layout
}
```

**Best approach**: Start with Phase 1 (data URI), optimize if performance is unacceptable.

---

## Performance Budget

### Target Frame Time: 16.67ms (60 FPS)

**Budget breakdown:**

- Scene rendering (viewport): 4ms - 3D scene to FBO
- Editor UI HTML rendering: 8ms - Async on render thread (doesn't block main)
- Viewport texture readback: 2ms - Only if using data URI approach
- Editor UI composite: 1ms - Texture upload + fullscreen quad
- Input handling: 0.5ms - Events, gizmo hit-testing
- File watcher: 0.5ms - Event-based, not polling
- **Total main thread: ~8ms** - 50% headroom for 60 FPS

### Worst-Case Scenarios

**Large scene tree (1000+ entities)**:

- Scene tree HTML generation: could take 50ms+
- **Mitigation**: Virtual scrolling, only render visible items
- Implementation: Track scroll position in Lua, compute visible range, only generate HTML for visible entities

**Complex inspector (many components)**:

- Inspector HTML generation: could take 10ms+
- **Mitigation**: Lazy expansion, collapsed sections by default
- Implementation: Only expand component details on user click

**Hot reload during editing**:

- Full UI re-render: 15ms+
- **Mitigation**: Already async on render thread, no main thread impact
- Just ensure dirty flag propagates correctly

### Profiling Points

Add timing instrumentation to these functions:

```cpp
// Add to Editor.cpp
void Editor::Render() {
    PROFILE_SCOPE("Editor::Render");

    {
        PROFILE_SCOPE("Viewport::Render");
        m_viewport->Render();
    }

    {
        PROFILE_SCOPE("EditorUI::Render");
        m_editorUI->Render();
    }

    // ...
}
```

Use existing Quill logging with timestamps for manual profiling:

```cpp
LOG_TRACE_L1("Editor::Render start");
// ... work ...
LOG_TRACE_L1("Editor::Render end");
```

---

## Related Documentation

- **UI System Architecture**: `docs/architecture/UI_SYSTEM.md` - Full reactive UI system details, threading model, event handling
- **Project Overview**: `WARP.md` (CLAUDE.md) - AI assistant context and common workflows
- **Build System**: `README.md` - Build instructions and dependencies
- **Changelog**: `CHANGELOG.md` - Historical decisions and abandoned approaches

---

## Prerequisite Changes to Engine Code

Before starting Phase 1, these engine changes must be made:

### 1. Fix CMakeLists.txt (BLOCKING)

Remove `src/main.cpp` from `libcore` sources (line 12). Without this, `imhotep-editor` cannot link.

### 2. Add Keyboard Modifiers to InputEvent

Current `key_callback` (WindowManager.cpp:243-253) ignores `mods` parameter:

```cpp
// CURRENT (broken for shortcuts)
void WindowManager::key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        ScriptManager &sm = ScriptManager::GetInstance();
        InputEvent event;
        event.type = InputTypes::Key;
        event.input = GLFW_KEY(key);  // mods is ignored!
        APPEND_EVENT()
    }
}
```

**Fix**: Extend `InputEvent` to include modifiers:

```cpp
// In WindowManager.h
struct InputEvent
{
    int type;
    std::variant<std::string, glm::vec2, glm::vec3> input;
    int mods = 0;  // NEW: GLFW_MOD_CONTROL, GLFW_MOD_SHIFT, etc.
};

// In WindowManager.cpp key_callback:
event.mods = mods;  // NEW: Pass through modifiers
```

### 3. Add Registry Query Methods (Phase 2)

Add these methods to `Registry.h` (see "Required Registry Extensions" section above):

- `GetEntityCount()`
- `GetAllEntities()`
- `GetEntityName(EntityID)`
- `SetEntityName(EntityID, string)`
- `SaveScene(string)` - New implementation needed
- `CloneEntity(EntityID)` - New implementation needed

### 4. Add Input Handler Registration (Phase 2)

Add C++ input interceptors to `WindowManager` (see "Input Routing Architecture" section above).

---

_Last Updated: January 16, 2026_
