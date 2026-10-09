# Development workflows

> Current examples use VaporQube and APIs present at `161dd1f`.

## Configure, build, run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cd build
./imhotep
```

Run from `build/` in development mode. `PathResolver::GetResourcePath()` returns `../res/` outside an installed bundle, so launching `build/imhotep` from the repository root will resolve resources incorrectly.

To use an explicit config:

```sh
cd build
./imhotep --config ../res/games/vaporqube/conf/settings.yaml
```

## Change the UI

The working VaporQube example is split into:

- `res/games/vaporqube/ui/templates/game.html`
- `res/games/vaporqube/ui/styles/game.css`
- `res/games/vaporqube/ui/state/game.lua`

Add state under `data`, reference it with `{{ data.name }}` or `v-if`, and add handlers under `methods`. A handler can call the generic bindings:

```lua
SetUIValue("data.gameStarted", true)
RefreshUI()
```

`SetUIValue` writes supported scalar values and marks state dirty when changed. `RefreshUI` evaluates dirty state and always queues the resulting HTML string. See [UI system](../architecture/UI_SYSTEM.md) before assuming browser-like DOM behavior.

## Add a scene module

1. Add a Lua file beneath `res/games/<game>/scripts/` that returns a table.
2. Declare `_contract.role`, optional `requires`, and optional engine capability `needs`.
3. Add its resource-relative path to the scene YAML `scripts:` list.
4. Implement `init(ctx)` when the module needs resolved dependencies.

```lua
local module = {
    _contract = {
        role = "input",
        requires = {"game"},
        needs = {"ui"}
    }
}

function module:init(ctx)
    self.game = ctx.game
end

return module
```

Use a role only once per scene. Unknown names are accepted as custom roles but log a warning. The loader validates capabilities, topologically orders role dependencies, and supports legacy scripts. See [API and scene contracts](../architecture/API_CONTRACTS.md).

## Add an ECS component in C++

1. Define a plain component under `include/components/`.
2. Add its `SparseSet<T>` and `GetComponentSet<T>()` specialization to `Registry`.
3. Register a value with the existing API:

```cpp
EntityID entity = Registry::GetInstance().RegisterEntity("Example");
MyComponent component{/* fields */};
Registry::GetInstance().RegisterComponent<MyComponent>(entity, component);
```

4. Extend YAML scene loading or scripting bindings only if content authors need to create it.
5. Add headless tests when the component behavior does not require OpenGL.

There is no `entity.emplace(...)` API in the current engine.

## Add a system

Systems are mostly static update passes under `src/systems/`. Query component sets from `Registry`, skip non-live entities, and place the pass deliberately in the frame order in `Game.cpp` or `Editor.cpp`. Hierarchy must update before render consumers read `WorldTransform`.

## Test a change

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure -E '^(engine\.smoke|engine\.click)$'
ctest --test-dir build --output-on-failure -R '^(engine\.smoke|engine\.click)$'
```

The second command needs a display. See [Testing](../architecture/TESTING.md).

## Add a dependency

Prefer an existing dependency first. If a new one is necessary, decide explicitly whether it is:

- a required system package (`find_package`),
- a pinned source download (`FetchContent`), or
- a vendored submodule under `external/`.

Document platform prerequisites and keep dependency initialization in top-level `CMakeLists.txt`. Do not edit vendored sources for ordinary engine work.

## Update documentation

- Put current user/developer guidance in `README.md`, `docs/guides/`, or a current architecture document.
- Put speculative designs in a document clearly labeled **Design history / roadmap**.
- Cite real file paths and symbols; avoid line numbers when the symbol is sufficient and lines are likely to drift.
- Update `docs/INDEX.md` and validate relative links.
- Keep Mermaid source beside rendered SVG/PNG when diagrams change.
