# Editor

Build the editor with the normal project build. Its executable is `build/imhotep-editor`; run it from `build/` so resource paths resolve.

The editor loads `res/conf/editor_settings.yaml` and opens a saved scene through its Scene view. It uses the shared `EngineCore`, an HTML/CSS/Lua interface, and `SceneViewport` for scene drawing and picking.

## Scene authoring and play

The Scene view can create, open, and save scene YAML. It supports adding objects, editing position and scale, assigning render components or Lua scripts, configuring scene modules and capabilities, and saving a scene to a selected path. The editor keeps edits in a scene session and reports validation and save errors in its UI.

The Game view can select an existing game configuration and play the saved scene in a separate process. The editor tracks and stops the process it starts. Runtime gameplay uses its own registry and Lua state; unsaved editor changes do not become the runtime scene.

Current flow diagrams: [Editor interaction](diagrams/editor-current-flow.mmd) and [play session](diagrams/editor-play-flow.mmd).

## Viewport

The editor reads the viewport rectangle from the laid-out UI bounds. It renders the scene directly into that rectangle on the OpenGL thread. The viewport resizes to the rectangle, and click coordinates are converted between the UI's top-left origin and OpenGL's bottom-left origin. Picking uses an offscreen entity-ID pass.

The viewport is hidden when the active view is not `scene`. Its size follows the UI layout and framebuffer size, including high-resolution displays.

## Main implementation files

- `src/editor/Editor.cpp` — startup, UI state, input, and frame rendering.
- `src/editor/SceneViewport.cpp` and `include/editor/SceneViewport.h` — scene rendering, selection, and picking.
- `src/editor/SceneDocument.cpp`, `SceneSession.cpp`, and `PlaySession.cpp` — scene files, edits, and owned game process.
- `res/ui/editor.html`, `res/ui/styles/editor.css`, and `res/ui/state/editor.lua` — editor interface.
- `res/conf/editor_settings.yaml` — editor configuration.

The editor and viewport use the main thread's OpenGL context. Keep their OpenGL work on that thread. The UI Template view still has gaps in text editing and error recovery; selecting a template and seeing a preview does not guarantee that every editing control works end to end.
