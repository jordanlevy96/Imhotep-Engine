# Imhotep Documentation Index

> Last Updated: 2026-03-16

## Quick Reference

| Document | Purpose | Status |
|----------|---------|--------|
| [CLAUDE.md](../CLAUDE.md) | AI assistant context, quick reference | Stable |
| [README.md](../README.md) | Build instructions, getting started | Stable |
| [CHANGELOG.md](../CHANGELOG.md) | Version history, decisions | Stable |
| [REFERENCES.md](../REFERENCES.md) | Library credits, licenses | Stable |

---

## Guides

| Document | Purpose | Status |
|----------|---------|--------|
| [WORKFLOWS.md](guides/WORKFLOWS.md) | Common how-to recipes: UI, shaders, ECS, build, test | Stable |
| [CODE_CONVENTIONS.md](guides/CODE_CONVENTIONS.md) | Naming, Doxygen, quick-stats headers, logging, versioning | Stable |

## Architecture Documentation

| Document | Purpose | Status |
|----------|---------|--------|
| [UI_SYSTEM.md](architecture/UI_SYSTEM.md) | HTML/CSS/Lua reactive UI, threading, events | Stable |
| [GAME_DECOUPLING.md](architecture/GAME_DECOUPLING.md) | Removing game logic from C++, pluggable games | Planned |
| [EDITOR_ARCHITECTURE.md](architecture/EDITOR_ARCHITECTURE.md) | Editor design, all implementation phases | Phase 1 Complete |
| [EDITOR_VIEWPORT.md](architecture/EDITOR_VIEWPORT.md) | Viewport rendering, texture transfer, click handling | Partial |
| [TRANSFORM_PIPELINE.md](architecture/TRANSFORM_PIPELINE.md) | Hierarchical transform refactor plan | Planned |
| [TESTING.md](architecture/TESTING.md) | Three-tier test strategy, extending tests | Stable |
| [VULKAN_MIGRATION.md](architecture/VULKAN_MIGRATION.md) | OpenGL to Vulkan migration analysis | Research |
| [TERRAIN_IMPLEMENTATION.md](TERRAIN_IMPLEMENTATION.md) | EU/Factorio-style world map implementation notes | Partial |

---

## Handoff Documents

| Document | Date | Purpose |
|----------|------|---------|
| [handoff.bundled-python.md](handoff.bundled-python.md) | 2026-02-05 | Bundled Python distribution for macOS app bundles |
| [handoff.incremental-ui.md](handoff.incremental-ui.md) | 2026-01-19 | Incremental UI update architecture - Phase 1 instrumentation complete |

---

## Status Legend

- **Stable**: Documentation is current and accurate
- **Phase N Complete**: Implementation through phase N finished
- **Partial**: Some work complete, some planned
- **Planned**: Design documented, implementation not started
- **Research**: Analysis/learning resource, not immediate priority

---

## Document Summaries

### CLAUDE.md
Quick-reference for AI assistants and developers: project overview, build instructions, common workflows, file organization, and current dependency inventory. For detailed system docs, see architecture documents.

### UI_SYSTEM.md
Canonical reference for the reactive UI system: template directives (v-if, v-for), multi-threaded rendering, event handling, Lua state management, thread safety.

### GAME_DECOUPLING.md
Architecture for removing game-specific logic from C++ engine:
- Generic UI API (`SetUIValue`, `RefreshUI`) replacing Tetris-specific bindings
- Scene-driven game loading via `scripts:` section in YAML
- Lua game controllers replacing C++ game lifecycle methods
- File reorganization into `games/` folder structure

### EDITOR_ARCHITECTURE.md
Full editor design and implementation roadmap:
- **Phase 1** (✅ Complete): Foundation & Selection - window, panels, click-to-select, inspector, viewport highlighting
- **Phase 2** (Planned): Camera controls and viewport picking
- **Phase 3** (Planned): Transform editing and persistence (save/load)
- **Phase 4** (Planned): Hot reload
- **Phase 5** (Planned): UI template editor
- **Phase 6** (Planned): Advanced features (undo/redo, gizmos)

### EDITOR_VIEWPORT.md
Editor viewport system:
- **Texture Transfer** (Partial): Using PNG+base64 pipeline (acceptable performance)
- **Click Handling** (✅ Complete): v-for handler expression substitution working
- **Selection Highlight** (✅ Complete): Wireframe overlay on selected entity

### TRANSFORM_PIPELINE.md
Transform system refactor for proper hierarchy support:
- Phase 1-5 all planned (WorldTransform component, HierarchySystem, dirty flags)

### TESTING.md
Three-tier automated test strategy:
- **Tier 1 — `engine.smoke`**: Full engine boot + 10 frames via `--smoke-test` flag (needs display)
- **Tier 1b — `engine.click`**: Click event integration test — verifies HTML element bounds, hit-testing, and Lua event dispatch (needs display)
- **Tier 2 — `engine.unit`**: Headless C++ unit tests for ExpressionCache (12 tests) and FrameTiming (11 tests)
- **Tier 3 — `vaporqube.lua.behavior`**: Lua gameplay contract tests with mocked bindings (8 tests, `tests/GameLuaTests.cpp`)
- Includes guides for extending each tier and candidates for future unit tests

### VULKAN_MIGRATION.md
Future graphics API migration research: current OpenGL analysis, Vulkan requirements, migration strategy.

### TERRAIN_IMPLEMENTATION.md
Implementation notes for an EU/Factorio-style world map: stable chunked data, quadtree render tiles, GPU cache + streaming, province borders/picking, and incremental recompute for dynamic changes.

### handoff.bundled-python.md
Bundled Python distribution for portable game deployment:
- **PathResolver** utility for runtime environment detection (dev vs .app bundle)
- **PreInitializePython()** sets PYTHONHOME/PYTHONPATH before interpreter
- CMake install targets for macOS app bundles with optional bundled Python
- `scripts/package-macos.sh` packaging script (not yet tested end-to-end)
- ARM64 Python auto-detection and architecture validation
- Engine bindings: `getResourcePath()`, `isInstalledBundle()`, `getExecutableDir()`

### handoff.incremental-ui.md
Incremental UI update optimization architecture:
- **Phase 1 instrumentation** (✅ Complete): ExpressionCache + performance metrics
- **Baseline established**: 3.2ms avg render (68% under 10ms budget), 99.8% cache hit rate
- **Phase 2-3 planned**: Dependency tracking, litehtml fork with DOM mutation API
- Includes validation results from live Tetris gameplay testing
- Documents three-phase optimization strategy for sub-1ms UI updates

---

## Updating Documentation

When modifying docs:
1. Update "Last Updated" date in document header
2. Update status in this index
3. Cross-reference related documents
4. Remove stale references to deleted/legacy code paths
