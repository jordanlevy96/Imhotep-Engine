# Imhotep Engine

Imhotep is an experimental C++ game engine focused on a data-driven ECS, Lua-authored gameplay, and declarative HTML/CSS UI. The included game is **VaporQube**.

## Start here

- [Documentation index](docs/INDEX.md)
- [Current architecture](docs/architecture/CURRENT_ARCHITECTURE.md)
- [Editor](docs/architecture/EDITOR.md)
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

For the read-only milestone, build the headless scene inspection helper and start the dedicated stdio profile from the repository root. It exposes only `inspect_scene` and `list_resources`. `inspect_scene` reads saved YAML through the existing C++ `SceneDocument`; it does not inspect unsaved Editor state, execute Lua, access the Registry, or write files. `--project-root` bounds scene paths, and `--resource-root` must be inside that project. Both roots are resolved before server startup, and per-call paths reject traversal and symlink escapes.

```sh
cmake --build build --target imhotep-scene-inspect --parallel 1
uv sync --project tools/mcp
uv run --project tools/mcp python tools/mcp/imhotep_server.py \
  --project-root "$PWD" --resource-root "$PWD/res" \
  --scene-helper "$PWD/build/imhotep-scene-inspect" \
  --read-only
```

The optional full server mode also requires the Python-enabled CMake configuration and matching interpreter ABI for the C++ session binding. Check the selected interpreter with `uv python find --project tools/mcp` and pass it as `-DPython3_EXECUTABLE=...` when configuring CMake if needed. Launch that mode without `--read-only` and with `--session-module-dir "$PWD/build"` to expose the existing headless in-memory session tools. The Editor snapshot tool is separately opt-in through `--editor-snapshot-socket`; it is not enabled in read-only mode. The server uses stdio only and reserves stdout for protocol messages; diagnostics go to stderr. The official Python SDK is pinned in `tools/mcp/uv.lock`.

The transport and tool discovery/call behavior follow the [official MCP stdio and tool specifications](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports) and use the [official Python SDK](https://github.com/modelcontextprotocol/python-sdk).

Verify initialization, discovery, saved-scene/resource calls, malformed input, missing/invalid scenes, and path boundaries with the SDK client:

```sh
uv run --project tools/mcp python tests/MCPReadOnlyProtocolTests.py build/imhotep-scene-inspect
```

### Optional live Editor snapshot socket

The Editor can expose one read-only snapshot request over a same-user Unix-domain socket. It is disabled unless explicitly started with `--mcp-snapshot-socket`; it only returns the currently authored scene snapshot and does not execute Lua or mutate scene state. The socket requires an existing directory owned by the current user with mode `0700`, and an absolute socket path in that directory. The Editor removes its own socket on shutdown. Windows is not supported yet.

From the `build/` directory, create a private endpoint directory and launch the Editor:

```sh
snapshot_dir="$(mktemp -d "${TMPDIR:-/tmp}/imhotep-snapshot.XXXXXX")"
chmod 700 "$snapshot_dir"
socket_path="$(cd "$snapshot_dir" && pwd -P)/editor.sock"
./imhotep-editor --mcp-snapshot-socket "$socket_path"
```

Keep the `pwd -P` canonicalization: on macOS, `/tmp` can be a symlink to `/private/tmp`, and the Editor rejects socket paths whose parent traverses a symlink. The socket directory remains mode `0700`; do not loosen its permissions to work around a bind failure. In a restricted execution sandbox, the Editor launch and MCP client may each need the host's approved local-socket access route if bind/connect returns `Operation not permitted`. XPC service warnings alone do not establish that Editor startup failed; check for the Editor main-loop and snapshot IPC startup messages or a specific initialization error.

The local socket protocol accepts exactly `snapshot\n`, followed by a client write-half-close. Replies begin with `IMHOTEP_SNAPSHOT/1 OK` plus byte lengths for the document identity, path, and YAML body, or `IMHOTEP_SNAPSHOT/1 ERROR` plus an error-body length. Requests are queued and read by the Editor owner thread; a request times out if the Editor loop does not service it within 1.5 seconds. After the Editor is running and has an authored scene open, launch the stdio server with `--editor-snapshot-socket "$socket_path"` added:

```sh
uv run --project tools/mcp python tools/mcp/imhotep_server.py \
  --project-root "$PWD" --resource-root "$PWD/res" \
  --scene-helper "$PWD/build/imhotep-scene-inspect" \
  --session-module-dir "$PWD/build" \
  --editor-snapshot-socket "$socket_path"
```

`inspect_editor_scene` is registered only when this option is present; its result labels itself as a live, potentially unsaved authored snapshot. `inspect_scene` remains saved-file inspection, and headless edit sessions remain separate. The MCP server never launches the Editor.

## Test

```sh
ctest --test-dir build --output-on-failure
```

The SDK-backed stdio protocol test can be run after syncing the isolated MCP environment:

```sh
uv sync --project tools/mcp
cmake --build build --target imhotep-scene-inspect imhotep_scene_session --parallel 1
uv run --project tools/mcp python tests/MCPProtocolTests.py build/imhotep-scene-inspect
uv run --project tools/mcp python tests/MCPActiveEditorProtocolTests.py build/imhotep-scene-inspect
```

The headless suite is suitable for CI:

```sh
ctest --test-dir build --output-on-failure -E '^(engine\.smoke|engine\.click)$'
```

This headless suite includes the required Unix snapshot IPC lifecycle test, which binds a temporary owner-only socket. CI runs it on macOS and fails if the bind or lifecycle assertions fail; the separate endpoint-path validation test does not require socket access.

`engine.smoke` and `engine.click` require a display. They pass locally on the verified macOS setup but are deliberately excluded on GitHub-hosted macOS runners because GLFW cannot create the required NSGL pixel format there. See [Testing](docs/architecture/TESTING.md).

## Packaging

```sh
./scripts/export.sh --game vaporqube
```

Use `--skip-python` for a package without embedded Python. The script selects platform-specific packaging options and writes artifacts under `dist/`.

## Content policy

AI tools assist development and technical design. Project assets should be human-created or permissively licensed and attributed in [REFERENCES.md](REFERENCES.md).
