# Game Decoupling Architecture

> Last Updated: 2026-10-08
> Status: **Design history — core decoupling implemented; examples and checklists below are archival**

Current reality: VaporQube is resource-scoped under `res/games/vaporqube`, scene YAML loads ordered Lua modules, modules declare roles/dependencies/capabilities, and generic `SetUIValue`/`RefreshUI` bindings exist. Use [API contracts](API_CONTRACTS.md) for the implemented model. The original Tetris names, file layouts, line references, and unchecked migration tasks below preserve the rationale but are not current instructions.

## Overview

This document describes the architecture for decoupling game-specific logic from the Imhotep engine, enabling the engine to support multiple games as pluggable modules.

## Goal

Transform Imhotep from a Tetris-specific engine into a generic game engine where games are pluggable modules loaded via scene YAML files.

---

## Current State (Before Refactor)

### Game-Specific Code in C++

| Location | Issue |
|----------|-------|
| `Game.cpp:275-341` | `StartGame()`, `ResetGame()`, `ReturnToMainMenu()` methods |
| `ScriptManager.cpp:166-168` | Game methods exposed to Lua |
| `ScriptManager.cpp:203-223` | `UpdateGameUI()`, `UpdateGameOver()` Tetris-specific bindings |

### Hardcoded Loading

| Location | Issue |
|----------|-------|
| `init.lua:12-18` | Hardcoded Tetris module imports |
| `MainScene.yaml` | Direct TetrisGrid.lua reference |

---

## Target Architecture

### Separation of Concerns

```
┌─────────────────────────────────────────────────────────────┐
│ C++ Engine Layer (Game-Agnostic)                            │
│  ├─ Window/Input management                                 │
│  ├─ Rendering pipeline                                      │
│  ├─ ECS (Registry, Components, Systems)                     │
│  ├─ Script execution (Lua/Python VMs)                       │
│  └─ Generic Lua bindings (SetUIValue, RefreshUI, etc.)      │
└──────────────────────┬──────────────────────────────────────┘
                       │
         Scene YAML defines which game to load
                       │
┌──────────────────────▼──────────────────────────────────────┐
│ Lua Game Layer (Game-Specific)                              │
│  ├─ Game controller (TetrisGame.lua)                        │
│  ├─ Game logic (TetrisGrid.lua, Tetrimino.lua)              │
│  ├─ Input handling (TetrisInput.lua)                        │
│  └─ Game constants and data                                 │
└─────────────────────────────────────────────────────────────┘
```

### Scene-Driven Loading

Games are loaded via the `scripts:` section in scene YAML files:

```yaml
name: "TetrisScene"

scripts:
  - "games/tetris/TetrisConstants.lua"
  - "games/tetris/TetriminoData.lua"
  - "games/tetris/Tetrimino.lua"
  - "games/tetris/TetrisGame.lua"
  - "games/tetris/TetrisInput.lua"

scene:
  objects:
    - name: "TetrisGrid"
      components:
        - type: "LuaScript"
          script: "games/tetris/TetrisGrid.lua"
```

---

## Generic Lua API

### UI State Management

Games update UI state using generic bindings:

```lua
-- Set any UI value
SetUIValue("data.score", 100)
SetUIValue("data.gameOver", true)
SetUIValue("data.playerName", "Alice")

-- Force UI re-render
RefreshUI()
```

### Game Callbacks

Games implement these callbacks via ScriptComponent:

| Callback | When Called | Purpose |
|----------|-------------|---------|
| `ready()` | On entity creation | Initialize game state |
| `process(delta)` | Every fixed update | Game logic tick |
| `shutdown()` | On entity destruction | Cleanup (future) |

---

## File Organization

### Directory Structure

```
res/scripts/
├── init.lua                     # Engine-only initialization
└── games/
    └── tetris/
        ├── TetrisConstants.lua  # Game configuration
        ├── TetriminoData.lua    # Piece definitions
        ├── Tetrimino.lua        # Piece class
        ├── TetrisGrid.lua       # Main game logic
        ├── TetrisGame.lua       # Game controller
        └── TetrisInput.lua      # Input handling

res/scenes/
└── TetrisScene.yaml             # Scene with scripts section

res/ui/
├── templates/game.html          # Game UI template
├── styles/game.css              # Game UI styles
└── state/game.lua               # Game UI state
```

### Adding a New Game

1. Create `res/scripts/games/newgame/` folder with game scripts
2. Create scene YAML with `scripts:` section listing modules
3. Create UI templates/state in `res/ui/`
4. Update `settings.yaml` to point to new scene

---

## Implementation Phases

### Phase 1: Generic UI API
- Add `SetUIValue()` and `RefreshUI()` Lua bindings
- Remove game-specific `UpdateGameUI()` and `UpdateGameOver()`

### Phase 2: Scene-Driven Loading
- Extend `Registry::LoadScene()` to handle `scripts:` section
- Make `init.lua` game-agnostic

### Phase 3: Lua Game Controller
- Create `TetrisGame.lua` to replace C++ game methods
- Update `TetrisGrid.lua` and input handling

### Phase 4: File Reorganization
- Move Tetris files to `games/tetris/` folder
- Create `TetrisScene.yaml`

### Phase 5: C++ Cleanup
- Remove `StartGame()`, `ResetGame()`, `ReturnToMainMenu()` from Game class

---

## C++ Changes Summary

### ScriptManager.cpp

**Remove:**
```cpp
lua.set_function("UpdateGameUI", [...]);
lua.set_function("UpdateGameOver", [...]);
```

**Add:**
```cpp
lua.set_function("SetUIValue", [](const std::string& key, sol::object value) {
    ReactiveUI& reactiveUI = ReactiveUI::GetInstance();
    auto luaState = reactiveUI.GetLuaState();
    if (luaState) {
        if (value.is<int>()) luaState->SetValue(key, value.as<int>());
        else if (value.is<double>()) luaState->SetValue(key, value.as<double>());
        else if (value.is<std::string>()) luaState->SetValue(key, value.as<std::string>());
        else if (value.is<bool>()) luaState->SetValue(key, value.as<bool>());
    }
});

lua.set_function("RefreshUI", []() {
    ReactiveUI& reactiveUI = ReactiveUI::GetInstance();
    HTMLRendererMT& htmlRenderer = HTMLRendererMT::GetInstance();
    htmlRenderer.UpdateHTML(reactiveUI.GetRenderedHTML());
});
```

### Registry.cpp

**Add to LoadScene():**
```cpp
// Load game scripts first (in order)
if (yaml["scripts"]) {
    ScriptManager &sm = ScriptManager::GetInstance();
    for (const auto& scriptPath : yaml["scripts"]) {
        std::string fullPath = res + "scripts/" + scriptPath.as<std::string>();
        LOG_INFO("Loading game script: {}", fullPath);
        sm.Run(fullPath);
    }
}
```

### Game.h / Game.cpp

**Remove:**
- `void StartGame();`
- `void ResetGame();`
- `void ReturnToMainMenu();`

---

## Migration Notes

### Backward Compatibility

During migration, both old and new APIs can coexist. Old functions log deprecation warnings before removal.

### Testing Checklist

After implementation, verify:
- [ ] Startup screen displays correctly
- [ ] ENTER starts game
- [ ] Tetris gameplay works (movement, rotation, line clearing)
- [ ] Score/lines/level update in UI
- [ ] Game over screen appears on loss
- [ ] R to restart works
- [ ] M to return to menu works
- [ ] ESC to quit works

---

## Related Documents

- [UI_SYSTEM.md](UI_SYSTEM.md) - ReactiveUI and template system
- [TRANSFORM_PIPELINE.md](TRANSFORM_PIPELINE.md) - Entity hierarchy

---

_This document describes the planned architecture. Implementation status tracked in project issues._
