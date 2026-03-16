# Testing Architecture

> Last Updated: 2026-03-04
> Status: Stable

## Overview

Imhotep uses a three-tier testing strategy via CTest. Each tier targets a different layer of the engine, balancing speed and coverage.

```
Tier 1:  engine.smoke         Full engine boot, 10 frames, exit (needs display)
Tier 1b: engine.click         Click event integration test (needs display)
Tier 2:  engine.unit          C++ subsystem tests, headless
Tier 3:  vaporqube.lua.behavior  Lua gameplay contract tests, headless
```

Run all tests:

```bash
cd build
cmake -DIMHOTEP_BUILD_TESTS=ON .. && make -j8
ctest --output-on-failure
```

Selective runs:

```bash
ctest -R lua     # Tier 3 only (fast, headless)
ctest -R unit    # Tier 2 only (fast, headless)
ctest -R smoke   # Tier 1 smoke test only (needs display)
ctest -R click   # Tier 1b click test only (needs display)
ctest -E "smoke|click"  # Headless tests only (for CI without display)
```

---

## Tier 1: Engine Smoke Test (`engine.smoke`)

**Purpose**: Validate that the full engine boots, loads a scene, runs frames, and shuts down without crashing. Catches regressions across 15+ subsystems.

**How it works**: The `imhotep` binary accepts a `--smoke-test` flag. When set, `Game::RunFrames(10)` runs 10 frames of the fixed-timestep loop then returns, instead of entering the normal interactive loop.

**What it exercises**:
- ConfigLoader + settings.yaml parsing
- Logger initialization
- WindowManager / GLFW window creation + OpenGL context
- Camera setup
- HTMLRendererMT (separate render thread + FreeType + litehtml)
- ScriptManager (Lua VM + Python interpreter + real bindings)
- Registry / ECS
- Scene loading (TetrisScene + all Lua scripts)
- TemplateParser directives + ReactiveUI event dispatch
- Frame loop (input polling, ScriptSystem, TweenSystem, HierarchySystem)
- Rendering pipeline (sky, tiled background, entities, UI composite)
- Shutdown sequence

**Requires display**: Yes (creates a real GLFW window). Works from macOS Terminal, CI with virtual framebuffer (`xvfb-run`), or any environment with a GPU context.

**Timeout**: 30 seconds.

**Files**:
- `src/main.cpp` — `--smoke-test` flag parsing
- `src/controllers/Game.cpp` — `RunFrames()` implementation
- `CMakeLists.txt` — CTest entry

### Extending the smoke test

The smoke test is intentionally simple: boot + N frames + exit. To validate specific behavior:

- **Increase frame count**: Change the `10` in `main.cpp` to run more frames (e.g., enough for gravity to trigger a piece drop).
- **Add more smoke scenarios**: Add new CLI flags (e.g., `--smoke-test-variable`) that call `SetGameMode(GameMode::VARIABLE)` before `RunFrames()`.
- **Exit code validation**: `Initialize()` already returns false on failure. Add similar checks to `RunFrames()` if specific runtime conditions need validation.

---

## Tier 1b: Click Event Test (`engine.click`)

**Purpose**: Validate that the HTML event pipeline works end-to-end — template parsing produces interactive elements with correct bounding boxes, hit-testing maps coordinates to the right element, and event dispatch triggers the correct Lua handler with observable state change.

**How it works**: The `imhotep` binary accepts a `--click-test` flag. `Game::RunClickTest()` runs through 6 phases:

1. **Warm-up**: Run frames (up to 60) until the render thread populates interactive elements
2. **Bounds check**: Query `TryFindInteractiveElementBoundsByHandler("click", "onStartGame", ...)` and verify the "START GAME" button exists with valid dimensions (width > 0, height > 0)
3. **Pre-state assertion**: Read `data.gameStarted` from Lua state — must be `false`
4. **Simulate click**: Convert framebuffer-space button center to window-space coordinates (accounting for HiDPI scale), call `HandleClickEvent()`, verify it returns `true` (click consumed)
5. **Propagation**: Run 5 more frames for Lua handler execution and state propagation
6. **Post-state assertion**: Read `data.gameStarted` from Lua state — must now be `true`

**What it catches**:
- Template `@click` directives not producing interactive elements
- Element bounding box miscalculation (wrong position, zero dimensions)
- HiDPI coordinate scaling bugs (window-to-framebuffer mismatch)
- Hit-test logic regressions (z-index ordering, point-in-rect)
- ReactiveUI event dispatch failures (handler lookup, Lua method calls)
- Lua handler not updating UI state via `SetUIValue()`

**Requires display**: Yes (same as smoke test).

**Timeout**: 30 seconds.

**Files**:
- `src/main.cpp` — `--click-test` flag parsing
- `src/controllers/Game.cpp` — `RunClickTest()` implementation
- `CMakeLists.txt` — CTest entry

### Extending with more click tests

To test a different button or event type, follow the same pattern in `RunClickTest()`:

```cpp
// Find element by handler expression
int x, y, w, h;
if (!htmlRenderer->TryFindInteractiveElementBoundsByHandler("click", "onRestart", x, y, w, h))
{
    LOG_ERROR("Button not found");
    return false;
}

// Convert framebuffer center → window coords
float centerFbX = x + w / 2.0f;
float centerFbY = y + h / 2.0f;
float windowX = centerFbX * (float)winW / (float)fbW;
float windowY = centerFbY * (float)winH / (float)fbH;

// Simulate and verify
bool handled = htmlRenderer->HandleClickEvent(windowX, windowY, 0);
```

The coordinate conversion (framebuffer → window) is needed because `TryFindInteractiveElementBoundsByHandler` returns framebuffer-space coordinates while `HandleClickEvent` expects window-space input (it does the forward conversion internally). On non-HiDPI displays these are identical; on Retina displays the scale factor is typically 2x.

---

## Tier 2: C++ Unit Tests (`engine.unit`)

**Purpose**: Test C++ subsystems in isolation without graphics. Currently covers ExpressionCache and FrameTiming.

**Binary**: `imhotep-engine-tests` (separate executable, does not link the full `core` library).

**How it works**: The test binary compiles only the specific `.cpp` files under test plus `Logger.cpp` (required by LOG macros). It links `lua`, `sol2`, and `quill::quill` — no OpenGL, no GLFW, no Python.

**Test file**: `tests/EngineUnitTests.cpp`

### Current test coverage

**ExpressionCache** (12 tests):

| Test | What it validates |
|------|-------------------|
| Initialize | Cache starts uninitialized, becomes initialized after `Initialize()` |
| CompileAndCacheHit | Same expression returns same ID; different expression returns new ID; hit/miss stats are correct |
| EvaluateInteger | `x + y` evaluates to correct integer string |
| EvaluateString | String variable evaluates to correct value |
| EvaluateBool | Comparison expressions evaluate to correct boolean |
| EvaluateDouble | Floating-point values format correctly (2 decimal places) |
| EvaluateNestedTable | `data.score` pattern works with nested Lua tables |
| InvalidExpression | Syntax errors return `UINT32_MAX` |
| InvalidExprId | Out-of-range IDs return empty string / false |
| DependencyAnalysis | `data.score` produces dependencies `{"data", "data.score"}` |
| Clear | Resets cache size and stats to zero |
| NotInitialized | Calling `GetOrCompile()` before `Initialize()` returns `UINT32_MAX` |

**FrameTiming** (11 tests):

| Test | What it validates |
|------|-------------------|
| FixedDeltaCalculation | 60 FPS -> 16.67ms, 30 FPS -> 33.33ms |
| FixedStepAccumulation | After sleeping 20ms, `ShouldUpdateFixedStep()` returns true |
| FixedStepWrongMode | `ShouldUpdateFixedStep()` returns false in VARIABLE mode |
| VariableSimAccumulation | At 1x speed, accumulated time triggers simulation step |
| PausedSimulation | Multiplier 0.0 prevents simulation updates |
| VariableRenderFrame | Accumulated render time triggers `ShouldRenderFrame()` |
| FixedAlwaysRenders | `ShouldRenderFrame()` always returns true in FIXED mode |
| SimpleMode | Delta-only timing, no fixed steps, always renders |
| Reset | Clears accumulators so no pending steps |
| SimMultiplier | Getter/setter round-trips correctly |
| VSync | Getter/setter round-trips correctly |

### Extending with new unit tests

**Adding tests for an existing subsystem** — add a new test block in `EngineUnitTests.cpp`:

```cpp
// In a new RunFooTests() function:
static void RunFooTests()
{
    std::cout << "\n--- Foo ---" << std::endl;

    // Test: Description of what's being tested
    {
        Foo foo;
        foo.Initialize();
        ASSERT_EQ(foo.GetValue(), 42);

        std::cout << "  PASS: BasicValue" << std::endl;
        g_passed++;
    }
}

// Call from main():
RunFooTests();
```

**Adding a new source file to the test binary** — update `CMakeLists.txt`:

```cmake
add_executable(imhotep-engine-tests
    "tests/EngineUnitTests.cpp"
    "src/systems/ExpressionCache.cpp"
    "src/util/FrameTiming.cpp"
    "src/util/Logger.cpp"
    "src/path/to/NewFile.cpp"       # <-- add here
)
```

Add link libraries if the new file has new dependencies.

**Key constraint**: The unit test binary must stay headless. Only add `.cpp` files that don't pull in OpenGL, GLFW, or other graphics headers. If a subsystem includes graphics transitively (e.g., via ScriptManager), it needs the smoke test instead, or a refactor to break the dependency.

### Candidates for future unit tests

| Subsystem | Testable? | Notes |
|-----------|-----------|-------|
| TemplateParser | Needs refactor | Coupled to `ScriptManager::GetInstance()` which includes graphics headers |
| ReactiveUI | Needs refactor | Same coupling as TemplateParser |
| LuaUIState | Needs refactor | Same coupling |
| ConfigLoader | Yes | Pure YAML parsing, depends only on yaml-cpp |
| TransformUtils | Yes | Pure math, depends only on GLM |
| SceneTraversal | Yes | Pure graph traversal logic |

---

## Tier 3: Lua Behavior Tests (`vaporqube.lua.behavior`)

**Purpose**: Validate Lua gameplay contracts — scoring, gravity curves, piece data, input routing, collision detection. These test the game logic layer without any C++ engine code.

**Binary**: `imhotep-game-tests`

**How it works**: Creates a standalone `sol::state`, registers mock bindings (no-op logging, stub `vec2`/`vec3`), then loads the real Lua scripts from `res/scripts/` and asserts on their behavior.

**Why mocked bindings**: These tests validate that the Lua scripts implement correct game logic. They deliberately avoid the real C++ bindings so they can run headless and fast. The real bindings are exercised by the smoke test (Tier 1).

**Test file**: `tests/GameLuaTests.cpp`

**Current tests** (8):

| Test | What it validates |
|------|-------------------|
| TestGameConstantsGravity | Gravity curve invariants across all 15 levels |
| TestGameConstantsSRSKickCompleteness | All 8 SRS kick transitions exist with 5 offsets each |
| TestTetriminoDataShapeIntegrity | All 7 pieces have 4 rotation states with 4 blocks each |
| TestGameLogicLifecycle | Start/reset/pause UI state transitions |
| TestGameLogicPiecePreview | Next/hold piece preview rendering data |
| TestGameInputKeyRouting | Input event dispatch to correct game/grid actions |
| TestGameGridCollisionDetection | Wall, floor, and block collision |
| TestGameGridScoringFormulas | Line clear, T-spin, and combo scoring math |

### Extending with new Lua tests

Add a new test function in `tests/GameLuaTests.cpp` following the existing pattern:

```cpp
bool TestNewBehavior(sol::state &lua)
{
    std::cout << "  [test] NewBehavior... ";

    // Load scripts
    lua.script_file(ScriptPath("res/scripts/MyScript.lua"));

    // Assert behavior
    sol::table result = lua["myFunction"](args);
    if (result["value"].get<int>() != expected)
    {
        std::cout << "FAIL" << std::endl;
        return false;
    }

    std::cout << "PASS" << std::endl;
    return true;
}
```

Register it in `main()`:

```cpp
RUN_TEST(TestNewBehavior);
```

**Mock bindings**: If the new script calls C++ functions not yet mocked, add stubs in `RegisterBaseBindings()`. Keep mocks minimal — the point is testing Lua logic, not C++ integration.

---

## Test Harness Notes

### Tier 2 harness

The unit tests use a minimal built-in harness (no external test framework dependency). Available macros:

| Macro | Purpose |
|-------|---------|
| `ASSERT_TRUE(expr)` | Expression must be truthy |
| `ASSERT_FALSE(expr)` | Expression must be falsy |
| `ASSERT_EQ(a, b)` | Values must be equal (numeric, uses `std::to_string` in error messages) |
| `ASSERT_STR_EQ(a, b)` | Strings must be equal |
| `ASSERT_NEAR(a, b, tol)` | Floating-point values within tolerance |

Each test is a scoped block within a `Run*Tests()` function. On assertion failure, it throws and the harness catches and reports it. The process exits with code 1 if any test fails.

### Tier 3 harness

The Lua tests use a `RUN_TEST` macro that calls a `bool`-returning function and tracks pass/fail counts. Process exits with code 1 if any test fails.

### CTest integration

All tiers report pass/fail via process exit code (0 = pass, non-zero = fail). CTest picks this up automatically. The `--output-on-failure` flag shows stdout/stderr only for failed tests.

---

## CI Considerations

- **Tiers 2 and 3** are fully headless and run anywhere.
- **Tier 1** requires a display context. Options for CI:
  - macOS: Works natively in GitHub Actions macOS runners.
  - Linux: Use `xvfb-run ctest -R smoke` for a virtual framebuffer.
  - Alternatively, skip with `ctest -E smoke` and rely on Tiers 2+3 for headless CI.
