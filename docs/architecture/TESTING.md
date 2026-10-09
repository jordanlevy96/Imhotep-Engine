# Testing

> Verified against `develop` at `161dd1f` on 2026-10-08.

Imhotep registers five CTest targets. Tests are enabled by default through `IMHOTEP_BUILD_TESTS=ON`.

| Test | Environment | What it proves |
|---|---|---|
| `vaporqube.lua.behavior` | Headless | Real VaporQube Lua gameplay contracts with mocked engine bindings |
| `engine.unit` | Headless | Expression cache, frame timing, and scene-module contract behavior |
| `engine.registry-liveness` | Headless | Entity liveness and stale-ID behavior against the real core library |
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

Run the display-backed tests locally:

```sh
ctest --test-dir build --output-on-failure -R '^(engine\.smoke|engine\.click)$'
```

## CI versus local display coverage

`.github/workflows/ci.yml` builds on `macos-latest` and runs the three headless tests. It explicitly excludes `engine.smoke` and `engine.click`: hosted runners cannot create the GLFW NSGL pixel format used by the engine. This is an environment limitation, not evidence that the tests are broken.

On the verified local macOS setup, all five tests passed on 2026-10-07. Treat display tests as a local gate until CI gains a suitable graphical environment.

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
