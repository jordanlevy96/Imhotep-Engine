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

### Local MCP server

Build the scene inspection helper and C++ session binding, then launch the stdio MCP server from the repository root. The C++ binding target requires the normal Python-enabled CMake configuration, and its Python ABI must match the interpreter used by the uv environment. Check the selected interpreter with `uv python find --project tools/mcp` and pass it as `-DPython3_EXECUTABLE=...` when configuring CMake if needed. `--project-root` bounds scene paths; `--resource-root` must be inside that project. Saved-file inspection and headless edit sessions both use `SceneDocument`/`SceneSession`; neither sees unsaved live Editor state, executes Lua, accesses the Registry, or writes the scene file.

```sh
cmake --build build --target imhotep-scene-inspect imhotep_scene_session --parallel 1
uv sync --project tools/mcp
uv run --project tools/mcp python tools/mcp/imhotep_server.py \
  --project-root "$PWD" --resource-root "$PWD/res" \
  --scene-helper "$PWD/build/imhotep-scene-inspect" \
  --session-module-dir "$PWD/build"
```

Saved-file tools: `inspect_scene` and `list_resources`. Headless session tools: `open_edit_session`, `inspect_edit_session`, `set_scene_position`, `undo_scene_position`, and `close_edit_session`. Open returns an ephemeral session ID and opaque object handles. Position edits require the current revision. A server lock serializes concurrent calls; competing writes at one revision result in one commit and a stale-revision error for the other. Sessions and edits exist only in the MCP process: closing or restarting it discards them. There is one undoable position edit per session; another successful edit replaces that undo entry. These sessions are separate from the live Editor. Tool annotations distinguish file/session inspection from in-memory edits and session closure. The server uses stdio only and writes protocol messages to stdout; diagnostics go to stderr. `tools/mcp/uv.lock` pins the SDK and its resolved dependencies.

### Optional live Editor snapshot socket

The Editor can expose one read-only snapshot request over a same-user Unix-domain socket. It is disabled unless explicitly started with `--mcp-snapshot-socket`; it only returns the currently authored scene snapshot and does not execute Lua or mutate scene state. The socket requires an existing directory owned by the current user with mode `0700`, and an absolute socket path in that directory. The Editor removes its own socket on shutdown. Windows is not supported yet.

From the `build/` directory, create a private endpoint directory and launch the Editor:

```sh
snapshot_dir="$(mktemp -d "${TMPDIR:-/tmp}/imhotep-snapshot.XXXXXX")"
chmod 700 "$snapshot_dir"
socket_path="$(cd "$snapshot_dir" && pwd -P)/editor.sock"
./imhotep-editor --mcp-snapshot-socket "$socket_path"
```

The local socket protocol accepts exactly `snapshot\n`, followed by a client write-half-close. Replies begin with `IMHOTEP_SNAPSHOT/1 OK` plus byte lengths for the document identity, saved path, and YAML body, or `IMHOTEP_SNAPSHOT/1 ERROR` plus an error-body length. Requests are queued and read by the Editor owner thread; a request times out if the Editor loop does not service it within 1.5 seconds. This socket is a narrow IPC bridge, not an MCP client/server transport; the stdio MCP server above remains a separate saved-file/headless-session process.

## Test

```sh
ctest --test-dir build --output-on-failure
```

The SDK-backed stdio protocol test can be run after syncing the isolated MCP environment:

```sh
uv sync --project tools/mcp
cmake --build build --target imhotep-scene-inspect imhotep_scene_session --parallel 1
uv run --project tools/mcp python tests/MCPProtocolTests.py build/imhotep-scene-inspect
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
