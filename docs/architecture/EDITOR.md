# Editor

Build the editor with the normal project build. Its executable is `build/imhotep-editor`; run it from `build/` so resource paths resolve.

The editor loads `res/conf/editor_settings.yaml` and `res/scenes/EditorTest.yaml`. It uses the shared `EngineCore`, an HTML/CSS/Lua interface, and `SceneViewport` for scene drawing and picking.

## Viewport

The editor reads the viewport rectangle from the laid-out UI bounds. It renders the scene directly into that rectangle on the OpenGL thread. The viewport resizes to the rectangle, and click coordinates are converted between the UI's top-left origin and OpenGL's bottom-left origin. Picking uses an offscreen entity-ID pass.

The viewport is hidden when the active view is not `scene`. Its size follows the UI layout and framebuffer size, including high-resolution displays.

## Main implementation files

- `src/editor/Editor.cpp` — startup, UI state, input, and frame rendering.
- `src/editor/SceneViewport.cpp` and `include/editor/SceneViewport.h` — scene rendering, selection, and picking.
- `res/ui/editor.html`, `res/ui/styles/editor.css`, and `res/ui/state/editor.lua` — editor interface.
- `res/conf/editor_settings.yaml` and `res/scenes/EditorTest.yaml` — editor configuration and loaded scene.

The editor and viewport use the main thread's OpenGL context. Keep their OpenGL work on that thread.
