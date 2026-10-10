# Testing

> Verified against `develop` at `161dd1f` on 2026-10-08.

Imhotep registers six CTest targets. Tests are enabled by default through `IMHOTEP_BUILD_TESTS=ON`.

| Test | Environment | What it proves |
|---|---|---|
| `vaporqube.lua.behavior` | Headless | Real VaporQube Lua gameplay contracts with mocked engine bindings |
| `engine.unit` | Headless | Expression cache, frame timing, and scene-module contract behavior |
| `engine.scenario-input` | Headless | Test-only held-key override behavior without a GLFW window |
| `engine.registry-liveness` | Headless | Entity liveness and stale-ID behavior against the real core library |
<<<<<<< HEAD
=======
>>>>>>> 09a9fa3 (test(vaporqube): add seeded input scenario)
| `engine.smoke` | Display/OpenGL | Full engine initialization, scene load, ten frames, and shutdown |
| `engine.click` | Display/OpenGL | START GAME layout bounds, HiDPI conversion, hit-testing, Lua dispatch, and state change |

## Build and run

```sh
cmake -S . -B build -DIMHOTEP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run only headless tests:

```sh
ctest --test-dir build --output-on-failure -E '^(engine\.smoke|engine\.click)$'
```

## Seeded VaporQube input scenario

Run the bounded engine-owned scenario from `build/`:

```sh
./imhotep --scenario-test --scenario-seed 1
```

This test-mode path uses five fixed simulation ticks, seeds Lua's random generator, routes ENTER/X/SPACE through the normal Lua input handler, and overrides held RIGHT state through `WindowManager` for one tick. It checks game start, one-column movement, rotation, locked board cells, and positive score. Physical keyboard/click input is ignored only while this scenario runs. It requires the normal GLFW/OpenGL initialization and does not verify Editor controls.

To save selected rendered frames for visual inspection, create an output directory first and request one to four scenario ticks (1 through 5):

```sh
mkdir -p scenario-captures
./imhotep --scenario-test --scenario-seed 1 \
  --scenario-capture-dir ./scenario-captures \
  --scenario-capture-tick 5
```

Captures are binary PPM (P6) files read from the OpenGL `GL_BACK` buffer after game/UI rendering and before buffer swap. At each selected tick, the test validates the HUD model against game state, queues the resulting HTML with a monotonic revision, and waits up to two seconds for the render thread to publish that revision before compositing and capturing it. This bounded barrier runs only in the scenario capture path and does not change normal game timing. Filenames include the seed and tick; existing files are never overwritten. Each capture is limited to 16 MiB, and one run is limited to four captures and 64 MiB total. The engine rejects blank or color-uniform frames. Capturing frames does not use OS screenshots or UI automation.

Run the display-backed tests locally:

```sh
ctest --test-dir build --output-on-failure -R '^(engine\.smoke|engine\.click)$'
```

## CI versus local display coverage

`.github/workflows/ci.yml` builds on `macos-latest` and runs the four headless tests. It explicitly excludes `engine.smoke` and `engine.click`: hosted runners cannot create the GLFW NSGL pixel format used by the engine. This is an environment limitation, not evidence that the tests are broken.

The six configured tests (including the new scenario-input test) pass locally on the verified macOS setup. Treat display tests as a local gate until CI gains a suitable graphical environment.

## What the click test covers

`Game::RunClickTest()` waits for the HTML worker to publish interactive bounds, finds the handler `onStartGame`, validates the rectangle, converts its framebuffer-space center back to window coordinates, calls `HandleClickEvent`, advances frames, and asserts `data.gameStarted == true`. This is the strongest current end-to-end proof of the reactive UI event path.

It does not simulate a physical OS mouse device; it invokes the same renderer hit-test used by the registered C++ input handler.

## Where tests live

- `tests/GameLuaTests.cpp` — VaporQube Lua behavior
- `tests/EngineUnitTests.cpp` — headless subsystem tests
- `tests/RegistryLivenessTests.cpp` — ECS liveness regression coverage
- `src/controllers/Game.cpp` — smoke/click runtime scenarios
- `CMakeLists.txt` — authoritative target registration

## Adding coverage

- Put pure C++ logic without graphics dependencies in `engine.unit`.
- Put ECS behavior requiring `core` in a focused executable like `engine.registry-liveness`.
- Put Lua gameplay rules in `vaporqube.lua.behavior` and use minimal mocks.
- Use smoke/click-style tests only when a real window, OpenGL, scene, or HTML render is essential.

Keep the headless/display distinction explicit whenever adding CI steps or documentation.
