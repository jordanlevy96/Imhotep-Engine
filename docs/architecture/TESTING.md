# Testing

CTest targets are registered in `CMakeLists.txt`. List the targets available in a configured build with `ctest --test-dir build -N`.

## Build and run

```sh
cmake -S . -B build -DIMHOTEP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CI runs headless tests. Display-backed tests require a local graphical environment:

```sh
ctest --test-dir build --output-on-failure -E '^(engine\.smoke|engine\.click)$'
ctest --test-dir build --output-on-failure -R '^(engine\.smoke|engine\.click)$'
```

The hosted macOS runner cannot create the GLFW NSGL pixel format required by `engine.smoke` and `engine.click`.

## Click test

`Game::RunClickTest()` queries the rendered bounds for `onStartGame`, converts the center to window coordinates, dispatches it through the renderer hit test, and checks that Lua state changed. It does not generate an operating-system mouse event.

## Adding tests

- Put graphics-independent C++ tests in a headless target.
- Use a display-backed test only when a real window, OpenGL context, or rendered HTML is required.
- Register targets in `CMakeLists.txt`; keep CI's headless and display requirements explicit.
