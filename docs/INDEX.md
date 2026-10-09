# Imhotep documentation

> Reviewed against `develop` at `161dd1f` on 2026-10-08.

Documentation is divided into current references and clearly labeled design history. Source code and CMake remain authoritative when a historical plan conflicts with current behavior.

## Start here

| Document | Use it for |
|---|---|
| [README](../README.md) | Prerequisites, configure, build, run, test, package |
| [Current architecture](architecture/CURRENT_ARCHITECTURE.md) | Engine mental model and four code-grounded diagrams |
| [Development workflows](guides/WORKFLOWS.md) | UI, scene modules, ECS components, systems, testing |
| [API and scene contracts](architecture/API_CONTRACTS.md) | Config, scene YAML, Lua module contracts, binding boundaries |
| [Testing](architecture/TESTING.md) | Five test targets and CI/local display split |

## Current architecture references

| Document | Status |
|---|---|
| [CURRENT_ARCHITECTURE.md](architecture/CURRENT_ARCHITECTURE.md) | Verified overview; separate Mermaid/SVG/PNG diagrams live in `architecture/diagrams/` |
| [UI_SYSTEM.md](architecture/UI_SYSTEM.md) | Current HTML/CSS/Lua data flow, scheduling, threading, and events |
| [API_CONTRACTS.md](architecture/API_CONTRACTS.md) | Current runtime and content-author contracts |
| [TESTING.md](architecture/TESTING.md) | Current test inventory and commands |
| [CODE_CONVENTIONS.md](guides/CODE_CONVENTIONS.md) | Current style conventions; examples should still be checked against nearby code |

## Subsystem documents with historical sections

| Document | How to read it |
|---|---|
| [GAME_DECOUPLING.md](architecture/GAME_DECOUPLING.md) | Core result is implemented; old Tetris migration plan is archival |
| [TRANSFORM_PIPELINE.md](architecture/TRANSFORM_PIPELINE.md) | WorldTransform/hierarchy/render phases implemented; dirty optimization remains planned |
| [EDITOR_VIEWPORT.md](architecture/EDITOR_VIEWPORT.md) | Current status note supersedes legacy PNG/base64 “current pipeline” text |
| [EDITOR_ARCHITECTURE.md](architecture/EDITOR_ARCHITECTURE.md) | Long-form design history and future editor roadmap |
| [TERRAIN_IMPLEMENTATION.md](TERRAIN_IMPLEMENTATION.md) | Experimental terrain implementation notes; not core getting-started guidance |

## Research and roadmap

| Document | Status |
|---|---|
| [VULKAN_MIGRATION.md](architecture/VULKAN_MIGRATION.md) | Research only; current renderer is OpenGL |
| [Incremental UI handoff](handoff.incremental-ui.md) | Phase 1 history plus unimplemented DOM/dependency roadmap |
| [Bundled Python handoff](handoff.bundled-python.md) | Packaging implementation history and remaining validation caveats |

## Project references

- [CHANGELOG.md](../CHANGELOG.md) — version history
- [REFERENCES.md](../REFERENCES.md) — libraries, assets, and attribution
- [CLAUDE.md](../CLAUDE.md) — concise repository instructions for coding agents

## Maintenance rules

1. Put present-tense behavior in a current reference only after checking code or a passing test.
2. Put proposals in roadmap/history documents and label them visibly.
3. Prefer real VaporQube paths and current APIs over legacy Tetris examples.
4. Keep generated diagram renders beside editable Mermaid sources.
5. Validate relative Markdown links and documented commands before merging docs changes.
