# Imhotep Engine

Imhotep is an experimental C++ game engine focused on a data-driven ECS, Lua-authored gameplay, and declarative HTML/CSS UI. The included game is **VaporQube**.

## Start here

- [Documentation index](docs/INDEX.md)
- [Current architecture](docs/architecture/CURRENT_ARCHITECTURE.md)
- [Build, run, and common workflows](docs/guides/WORKFLOWS.md)
- [Runtime API and scene contracts](docs/architecture/API_CONTRACTS.md)
- [Testing](docs/architecture/TESTING.md)

## Prerequisites

- CMake 3.12+
- A C++17 compiler
- OpenGL development support
- FreeType
- Python 3 development files when `IMHOTEP_ENABLE_PYTHON=ON`
- Git submodules initialized recursively

Vendored dependencies provide GLAD plus Git submodules for GLM, litehtml, Lua/sol2, pybind11, ImGui, and yaml-cpp. CMake downloads Quill and GLFW with `FetchContent`; OpenGL and FreeType are system packages.

macOS with Homebrew:

```sh
brew install cmake freetype
git submodule update --init --recursive
```

Ubuntu/Debian package names vary by release; the minimum set normally includes `cmake`, a C++ compiler, `libgl1-mesa-dev`, `libfreetype6-dev`, and Python development headers.

## Configure and build

Run these commands from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Tests are built by default. Useful options:

| CMake variable | Default | Purpose |
|---|---:|---|
| `IMHOTEP_GAME_NAME` | `imhotep` | Game executable target name |
| `IMHOTEP_GAME_CONFIG` | `games/vaporqube/conf/settings.yaml` | Config path relative to `res/` |
| `IMHOTEP_ENABLE_PYTHON` | `ON` | Enable embedded Python |
| `IMHOTEP_BUILD_TESTS` | `ON` | Build CTest targets |
| `IMHOTEP_LOG_LEVEL` | `Info` | Compile-time log level |

For a smaller Lua-only build:

```sh
cmake -S . -B build -DIMHOTEP_ENABLE_PYTHON=OFF
cmake --build build --parallel
```

## Run

The development path resolver expects the executable to run from `build/` so that resources are found at `../res/`:

```sh
cd build
./imhotep
```

Use another game config with:

```sh
cd build
./imhotep --config ../res/games/vaporqube/conf/settings.yaml
```

The editor executable is `build/imhotep-editor`. Both game and editor require a working display/OpenGL context.

## Test

```sh
ctest --test-dir build --output-on-failure
```

The headless suite is suitable for CI:

```sh
ctest --test-dir build --output-on-failure -E '^(engine\.smoke|engine\.click)$'
```

`engine.smoke` and `engine.click` require a display. They pass locally on the verified macOS setup but are deliberately excluded on GitHub-hosted macOS runners because GLFW cannot create the required NSGL pixel format there. See [Testing](docs/architecture/TESTING.md).

## Packaging

```sh
./scripts/export.sh --game vaporqube
```

Use `--skip-python` for a package without embedded Python. Packaging behavior and platform caveats are documented in [the bundled Python handoff](docs/handoff.bundled-python.md); that handoff is historical implementation context, not a guarantee that every distribution path has been recently exercised.

## Content policy

AI tools assist development and technical design. Project assets should be human-created or permissively licensed and attributed in [REFERENCES.md](REFERENCES.md).
