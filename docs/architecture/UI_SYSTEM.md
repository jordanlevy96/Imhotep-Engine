# UI

The UI uses HTML and CSS templates with a Lua state table. It is rendered with litehtml, not a web browser.

## Rendering

`TemplateParser` evaluates template expressions and directives into a complete HTML string. A worker thread lays out and draws that string into pixels. The main/OpenGL thread uploads the pixels and composites the UI over the scene. The worker does not call OpenGL.

The parser supports interpolation (`{{ expression }}`), `v-if`, `v-for`, `v-table`, `v-model`, `v-html`, class binding, and event attributes. Supported HTML and CSS depend on litehtml and Imhotep's custom container.

## State refresh

UI state files return a table with `data` and may provide `computed` and `methods` tables. `SetUIValue(path, value)` updates a supported scalar (`int`, `double`, `string`, or `bool`) and marks the state dirty. It does not render by itself. `RefreshUI()` evaluates the template if state is dirty and queues it for rendering.

The template parse and Lua expressions are cached. Each queued render creates a new litehtml document and renders the complete result; there is no live DOM or partial-region redraw.

## Events

Template event attributes are replaced with internal IDs linked to handler expressions. After layout, the renderer records their bounds. The main thread hit-tests those bounds and calls the matching method in the Lua `methods` table. Coordinates are converted from window to framebuffer pixels for hit testing. If no C++ click handler consumes an event, the window manager can queue it for Lua input handling.

## Thread ownership

- Main/OpenGL thread: `LoadHTML`, `UpdateHTML`, `Resize`, `Render`, OpenGL resources, and published front-buffer reads.
- HTML worker: litehtml document/layout/draw work and writes to the back buffer.
- Mutexes protect buffer publication and hit-test bounds.

See [the UI diagrams](CURRENT_ARCHITECTURE.md) and [runtime contracts](API_CONTRACTS.md). Implementation: `src/systems/TemplateParser.cpp`, `src/systems/ReactiveUI.cpp`, and `src/systems/HTMLRendererMT.cpp`.
