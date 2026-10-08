# Code Conventions

> Last Updated: 2026-10-08

Style rules, documentation formats, and logging reference for the Imhotep engine.

---

## Naming Conventions

| Kind | Style | Examples |
|------|-------|---------|
| Classes / Types | PascalCase | `HTMLRendererMT`, `ReactiveUI`, `WindowManager` |
| Files | Match class name | `HTMLRendererMT.h`, `HTMLRendererMT.cpp` |
| Methods / Functions | PascalCase | `Initialize()`, `LoadHTML()`, `GetInstance()` |
| Member variables | `m_` prefix | `m_frontBuffer`, `m_texture`, `m_width` |
| Parameters / locals | camelCase | `width`, `height`, `needsRender` |

**Meaningful suffixes**:

- `-MT` = Multi-Threaded (`HTMLRendererMT`)
- `-Manager` = Singleton controller (`WindowManager`, `ScriptManager`)
- `-System` = ECS system (`RenderSystem`, `TweenSystem`, `ScriptSystem`)

---

## Doxygen Comment Style

All public headers use Doxygen comments for IDE integration (VS Code, CLion, Visual Studio).

**Class / function format**:

```cpp
/**
 * @brief One-line description
 *
 * Detailed explanation if needed.
 *
 * @param paramName Parameter description
 * @return Return value description
 * @note Thread safety, performance, or other important notes
 * @see Related doc or code reference
 */
```

**Member variable format**:

```cpp
int m_width = 800;  ///< Short description after declaration
```

**Required for**: all public API classes and functions, complex internal functions, thread-safety-critical code.

**Examples to follow**: `include/util/Logger.h`, `include/Camera.h`, `include/systems/HTMLRendererMT.h`

---

## Quick-Stats File Headers

Every C++ file gets a quick-stats block in its `@file` Doxygen comment. This lets you grep for a file to find key function line numbers without reading the whole file — typically cuts token cost 50–80%.

**Format for `.cpp` files**:

```cpp
/**
 * @file FileName.cpp
 * @brief Brief description
 * @lines ~XXX
 *
 * Purpose: What this file does
 *
 * Key functions:
 * - FunctionName() - Description (line ~XX, ~YY lines)
 * - AnotherFunction() - Description (line ~ZZ, ~AA lines)
 *
 * Thread safety / performance / integration notes (as needed)
 */
```

**Format for `.h` headers**:

```cpp
/**
 * @file FileName.h
 * @brief Brief description
 * @lines ~XXX
 *
 * Quick-stats (Public API):
 * - PublicMethod() - Description (line ~XX)
 * - AnotherMethod() - Description (line ~YY)
 *
 * Implementation reference: See src/path/FileName.cpp
 */
```

**Workflow**: `grep "@file HTMLRendererMT"` → see `"RenderThreadLoop() - line ~1143"` → `Read offset=1143`.

**Rules**:
- Use `~` prefix for approximate line numbers (they drift over time)
- Note thread ownership for multi-threaded code
- Include performance metrics for instrumented code
- Current coverage: ~27/83 files (33%) — add a header when you touch a file

**Examples**: `src/systems/HTMLRendererMT.cpp`, `src/systems/TemplateParser.cpp`, `include/systems/LuaUIState.h`

---

## Logging

**Library**: Quill (v7.4.0) — high-performance async logging (~12–16μs latency)

```cpp
#include "util/Logger.h"

LOG_TRACE_L1("[HTMLRendererMT] LoadHTML called ({} bytes)", html.size());
LOG_DEBUG("Loaded {} glyphs in {}ms", count, duration);
LOG_INFO("[HTMLRendererMT] Resize to {}x{}", width, height);
LOG_WARNING("Font fallback: {} not found, using default", fontName);
LOG_ERROR("Failed to load shader: {}", path);
LOG_CRITICAL("OpenGL context creation failed");
```

**Output**: `logs/imhotep.log` (also echoed to console). Initialized automatically via `Logger::GetInstance()`.

**Build-time verbosity** (default: Info):

```bash
cmake -DIMHOTEP_LOG_LEVEL=Info ..
# Values: TraceL3, TraceL2, TraceL1, Debug, Info, Warning, Error, Critical, Off
```

---

## Versioning

Format: `MAJOR.MINOR.PATCH` (Semantic Versioning 2.0.0)

**Source of truth**: the `project(imhotep VERSION X.Y.Z)` declaration in `CMakeLists.txt`

**To update version**:
1. Edit `project(imhotep VERSION X.Y.Z)` in `CMakeLists.txt`
2. Run `cmake ..` to regenerate `include/util/Version.h`
3. Add entry to `CHANGELOG.md`
4. Update any user-facing version references that actually exist (currently the `CLAUDE.md` header)

**Usage in code**:

```cpp
#include "util/Version.h"
LOG_INFO("Running {}", imhotep::Version::GetBanner());  // "Imhotep Engine v0.1.0"
```
