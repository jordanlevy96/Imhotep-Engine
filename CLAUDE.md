# CLAUDE.md - AI Assistant Context for Imhotep

> Last Updated: 2026-03-16
> Version: 0.1.0

## Project Overview

**Imhotep** is an experimental C++ game engine (OpenGL, Lua, Python). Ships VaporQube as its proof-of-concept game.

**Key innovation**: Declarative, reactive UI using HTML/CSS templates + Lua state (Vue.js-inspired), rendered via litehtml on a dedicated thread.

---

## Content Policy

- Do not add AI-generated creative writing, narrative, or marketing copy.
- Do not add AI-generated imagery or artwork.
- Filler text: Lorem Ipsum or CC0/public-domain only.

---

## Repository Guidelines

- `src/` — implementations; `include/` — public headers. Mirror the structure.
- `res/` — runtime assets; `external/` — third-party submodules (don't edit).
- `build/` — gitignored CMake output. Always run the engine from `build/`.
- Resource paths use `../res/` prefix from `build/` (e.g. `../res/shaders/Composite.shader`).
- Commit messages follow Conventional Commits: `feat(vaporqube): ...`, `fix: ...`, `docs: ...`.
- When adding a dependency: update **README.md** (install) and **REFERENCES.md** (license).

**Build**:
```sh
cd build && cmake .. && make -j8
./imhotep
```

**Tests**: `cd build && cmake -DIMHOTEP_BUILD_TESTS=ON .. && make -j8 && ctest --output-on-failure`

**Coding style**: 4-space indent, Allman braces, PascalCase types/methods, camelCase locals, `m_` member prefix.

---

## Architecture

```
Game / Editor entrypoints (src/main.cpp, src/editor/main.cpp)
├─► EngineCore      — startup and system wiring
│   Init order: Logger → Window → Splash → HTMLRenderer → ScriptManager → Registry → UI
├─► WindowManager   — GLFW + OpenGL context
├─► Camera          — 3D camera
├─► RenderSystem    — 3D scene rendering
├─► HTMLRendererMT  — UI rendering (RUNS ON SEPARATE THREAD)
│   ├─► ReactiveUI      — template + state management
│   ├─► TemplateParser  — v-if, v-for directives
│   └─► LuaUIState      — Lua state files
├─► ScriptManager   — Lua + Python VMs
└─► Registry        — ECS (entities & components)
```

---

## Key Rules

### Threading (HTMLRendererMT)
litehtml runs on a **render thread** — never access its internals from the main thread.

- **Main thread safe**: `LoadHTML()`, `Render()`, `Resize()`, reading `m_frontBuffer` (with `m_bufferMutex`)
- **Render thread only**: writing `m_backBuffer`, FreeType ops, litehtml rendering
- Key mutexes: `m_mutex` (HTML state), `m_bufferMutex` (front/back buffer swap)

See `docs/architecture/UI_SYSTEM.md` for full details.

### Language Separation
Each language has a strict domain — do not cross boundaries:

- **C++** — engine, main loop, rendering, input
- **Lua** — UI state, all game logic
- **Python** — data exports, analytics, external tooling

### Bundled Python Distribution
PathResolver (`include/util/PathResolver.h`) detects dev vs. `.app` bundle at runtime.
CMake options: `IMHOTEP_BUILD_BUNDLE`, `IMHOTEP_BUNDLE_PYTHON`, `IMHOTEP_PYTHON_BUNDLE_PATH`.
See `docs/handoff.bundled-python.md` for full details.

---

## Decision Guide

**Ask first**:
- Architecture changes, new libraries, new design patterns
- Breaking changes to existing systems
- Deleting significant code

**Proceed confidently**:
- Bug fixes and features following established patterns
- Single-file refactoring
- Documentation improvements

---

## Common Pitfalls

- **Threading**: never touch `m_backBuffer` from the main thread.
- **Paths**: always run from `build/`; all resource paths use `../res/` prefix.
- **Abandoned approaches**: check `CHANGELOG.md` before suggesting alternatives.

---

## Where to Find Things

| Task / Area | Go here |
|-------------|---------|
| Add UI screen, shader, component, dependency | `docs/guides/WORKFLOWS.md` |
| Naming, Doxygen, quick-stats, logging | `docs/guides/CODE_CONVENTIONS.md` |
| UI threading, directives, events | `docs/architecture/UI_SYSTEM.md` |
| Test strategy and extending tests | `docs/architecture/TESTING.md` |
| Editor design and phases | `docs/architecture/EDITOR_ARCHITECTURE.md` |
| Bundled Python / PathResolver | `docs/handoff.bundled-python.md` |
| Why architecture evolved this way | `CHANGELOG.md` |
| All docs with status | `docs/INDEX.md` |
| Build setup, dependencies, platform install | `README.md` |

---

## Project Structure Quick Reference

```
include/          Headers (.h): components/, controllers/, systems/, util/
src/              Implementations (.cpp) — mirrors include/
res/
  ui/state/       Engine Lua state files (fps.lua, editor.lua, …)
  shaders/        GLSL shaders (Basic, Composite, Lighting, Picking, …)
  scenes/         Engine scene YAML files
  conf/           Engine config (editor_settings.yaml, tetriminos.yaml)
  scripts/        Engine Lua scripts
  games/
    vaporqube/
      conf/       settings.yaml
      scenes/     Scene.yaml
      scripts/    Game*.lua
      ui/         game.html, game.css, game.lua
external/         Third-party submodules
docs/
  architecture/   System design docs
  guides/         How-to recipes (WORKFLOWS.md, CODE_CONVENTIONS.md)
scripts/          export.sh — cross-platform distribution packaging
build/            CMake output (gitignored)
```
