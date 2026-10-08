# Reactive HTML UI

> Current implementation reference, verified at `161dd1f`. For diagrams and the complete START GAME trace, see [Current architecture](CURRENT_ARCHITECTURE.md).

## Mental model

Imhotep UI is authored as HTML, CSS, and a Lua state table. It is not a browser and it does not maintain a virtual DOM.

```text
HTML + CSS + Lua state
        ↓ main thread
TemplateParser evaluates directives into a complete HTML string
        ↓ queued across a condition variable
HTMLRendererMT worker creates a litehtml document, lays it out, and rasterizes RGBA pixels
        ↓ mutex-protected front/back swap
main thread uploads an OpenGL texture and composites it over the scene
```

## Loading and internal representations

`EngineCore::InitializeUI`:

1. loads the Lua state file into the shared `ScriptManager` Lua VM;
2. binds it to the `ReactiveUI` singleton;
3. injects CSS into the HTML template;
4. registers the template with `TemplateParser`;
5. queues the first evaluated HTML string in `HTMLRendererMT`; and
6. registers click, mouse-button, cursor, and resize handlers with `WindowManager`.

Two parse trees exist for different purposes:

- **Gumbo tree** — cached by `TemplateParser` for the source template. Each evaluation walks this tree and serializes complete HTML.
- **litehtml document** — created from the evaluated HTML on every queued worker render. It owns CSS layout and draw geometry for that render.

Current optimization caches the template parse and compiled Lua expressions. It does not implement DOM mutation, dependency-directed updates, or dirty-region rasterization.

## Supported template features

- `{{ expression }}` interpolation
- `v-if="condition"`
- `v-for="item in collection"`
- `v-table="expression"`
- `v-model` for input/textarea values
- `v-html`
- `:class` / `v-bind:class`
- `@click` and other event attributes handled by the event map

`TemplateParser` evaluates expressions in the Lua state environment. Its event pass replaces directives with opaque `data-event-id` attributes and separately records `event-id → event type → handler expression`.

## State and refresh scheduling

The Lua state file returns a table with `data`, optional `computed`, and `methods`. VaporQube’s real state is `res/games/vaporqube/ui/state/game.lua`.

`LuaUIState::SetValue` compares supported scalar values, writes changes, and marks the state dirty. It does not schedule or render by itself.

`ReactiveUI::GetRenderedHTML` checks the dirty flag. If dirty, it reevaluates the entire template and clears the flag; otherwise it returns cached HTML.

The Lua binding `RefreshUI()` calls `GetRenderedHTML()` and then `HTMLRendererMT::UpdateHTML()`. Because `UpdateHTML` always queues the supplied string, an explicit refresh can cause a new litehtml document/render even if the state was clean and the cached string is unchanged.

Frequent values can be batched by calling `SetUIValue` several times and refreshing once. Some engine paths, such as FPS tracking, later request and queue rendered HTML themselves; this is not a general observer/subscription mechanism.

## Rendering and thread ownership

Main/OpenGL thread:

- creates GL resources and owns texture upload/composition;
- calls `LoadHTML`, `UpdateHTML`, `Resize`, and `Render`;
- reads the published front pixel buffer under `m_bufferMutex`;
- hit-tests the published front interactive-element list under its own mutex.

HTML worker thread:

- waits on new HTML or resize work;
- creates and lays out the litehtml document;
- draws through the custom `document_container` using FreeType into the back RGBA buffer;
- extracts interactive element bounds after layout; and
- swaps pixels and bounds into their front buffers.

The worker does not make OpenGL calls.

## Events: primary and fallback paths

For the VaporQube `START GAME` element:

```html
<div class="start-button" @click="onStartGame">START GAME</div>
```

The primary successful path is:

1. GLFW invokes `WindowManager::click_callback`.
2. `WindowManager` tries registered C++ handlers in reverse registration order.
3. The handler installed by `EngineCore::InitializeUI` calls `HTMLRendererMT::HandleClickEvent`.
4. The renderer converts window coordinates to framebuffer coordinates, reverse-hit-tests current bounds, and calls `ReactiveUI::DispatchEvent`.
5. `DispatchEvent` resolves `methods.onStartGame` and invokes it with the state table.
6. Lua calls `GameLogic:start("standard")`, writes UI state, and refreshes.
7. The C++ handler returns `true`, so `WindowManager` does not append that click to Lua `EventQueue`.

If no C++ handler consumes the click, `WindowManager` appends it to Lua `EventQueue`; VaporQube’s `GameInput.lua` later calls `HandleClickEvent`. That is a fallback, not the normal successful-button route.

## Limits and design history

- CSS/HTML support is whatever litehtml plus Imhotep’s custom container implements, not full browser behavior.
- Handler argument parsing currently supports no argument or one simple argument/`$event`.
- State updates are scalar-oriented through `SetUIValue`.
- The old claims of dependency graphs, DOM diffing, dirty-region rendering, and sub-millisecond incremental mutation remain roadmap ideas. See [incremental UI handoff](../handoff.incremental-ui.md) for that historical design work.

## Verification

`engine.click` verifies real template evaluation, layout bounds, HiDPI coordinate conversion, hit-testing, Lua handler execution, and `data.gameStarted` changing from false to true. `engine.smoke` verifies boot/render/shutdown with a real window. Both need a display and are local gates on macOS.
