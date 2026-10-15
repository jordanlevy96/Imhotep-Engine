# Runtime API and scene contracts

> Current contract reference at `161dd1f`. C++ headers and binding registration remain authoritative.

## Configuration contract

The executable defaults to `res/games/vaporqube/conf/settings.yaml`. `--config <path>` overrides it. `ConfigLoader` reads `window`, `game`, and optional `python` sections; `EngineCore` then replaces configured resource/log roots with `PathResolver` results appropriate to development or installed mode. The existing VaporQube `input:` block and `game.debug` field are not parsed by `ConfigLoader` and should not be treated as active engine configuration.

Paths inside game settings and scene YAML are resource-relative unless an API explicitly says otherwise.

## Scene YAML

VaporQube’s `res/games/vaporqube/scenes/Scene.yaml` is the canonical example:

```yaml
name: "VaporQubeScene"
capabilities:
  ui: true
  input: true
scripts:
  - "games/vaporqube/scripts/GameConstants.lua"
  - "games/vaporqube/scripts/GameLogic.lua"
scene:
  objects:
    - name: "GameGrid"
      components:
        - type: "LuaScript"
          script: "games/vaporqube/scripts/GameGrid.lua"
```

`strict: true` makes duplicate-role and missing-dependency validation fatal. Capability failures and script load/execution failures are always fatal. Contract-extraction warnings such as an unknown custom role are logged but are not promoted by strict mode.

## Lua scene-module contract

A scene script may return a module table with `_contract`:

```lua
return {
    _contract = {
        role = "game",
        requires = {"constants", "data"},
        needs = {"ui", "input"}
    },
    init = function(self, ctx)
        self.constants = ctx.constants
    end
}
```

- `role` identifies a provider in the scene context. Known roles include `game`, `constants`, `data`, `input`, `grid`, and `entity`; custom roles are supported.
- `requires` names roles that must be initialized first. `SceneLoader` topologically sorts these dependencies and rejects cycles.
- `needs` names engine capabilities declared by the scene.
- `init(ctx)` is optional and runs after validation in dependency order.
- Scripts without `_contract` remain supported as legacy modules and do not warn merely for omitting the contract. Nil/non-table returns, added globals, and other contract issues can log warnings.
- A script load or execution failure makes scene loading fail.

Resolved modules are exposed through `SceneModules` and the scene context.

## Generic Lua UI bindings

- `SetUIValue(path, value)` — writes supported scalar `int`, `double`, `string`, or `bool` values into bound `LuaUIState`; changed values mark it dirty.
- `RefreshUI()` — gets evaluated/cached HTML and queues it for HTML rendering.

See [Reactive HTML UI](UI_SYSTEM.md) for scheduling and event behavior.

## Generic entity bindings

The Lua VM exposes entity/component helpers including `RegisterEntity`, `DestroyEntity`, `AddChild`, `GetParent`, `RemoveChild`, transform access, and specialized component setters registered in `ScriptManager::RegisterFunctions`.

The C++ registry API uses stable `EntityID` values and sparse component sets:

```cpp
Registry &registry = Registry::GetInstance();
EntityID entity = registry.RegisterEntity("Example");
Transform transform;
registry.RegisterComponent<Transform>(entity, transform);
```

Always check liveness when retaining IDs across destruction. Duplicate registration upserts the component for the live entity.

## Frame contract

The game loop processes input and scripts, updates tweens, computes hierarchy/world transforms, renders the world, composites HTML UI, and polls/swaps through `EngineCore`. `HierarchySystem::Update` must precede render consumers because `RenderSystem` reads `WorldTransform` rather than rebuilding parent transforms itself.

## Contract boundaries

- C++ owns platform integration, rendering, ECS storage, resource lifetime, and bindings.
- Lua owns game modules, gameplay state/transitions, and UI-facing game methods.
- HTML/CSS/Lua UI state owns presentation structure and bindings.
- Python is optional analytics/extension support; it is not required for VaporQube gameplay.

For implementation symbols, start with `EngineCore`, `Registry`, `ScriptManager`, `SceneLoader`, `SceneModule`, `SceneContext`, and the current architecture guide.
