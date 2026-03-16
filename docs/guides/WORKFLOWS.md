# Common Workflows

> Last Updated: 2026-03-16

How-to recipes for frequent development tasks. For system architecture, see `docs/architecture/`.

---

## 1. Adding a New UI Screen

1. Create HTML template: `res/ui/templates/my_screen.html`
2. Create CSS styles: `res/ui/styles/my_screen.css`
3. Create Lua state file: `res/ui/state/my_screen.lua`
4. Load via ReactiveUI (see `docs/architecture/UI_SYSTEM.md` for details)

**Example Lua state** (`res/ui/state/my_screen.lua`):

```lua
return {
    data = {
        title = "My Screen",
        showPanel = true,
        items = {
            {name = "Item 1", value = 10},
            {name = "Item 2", value = 20}
        }
    }
}
```

**Example HTML template** (`res/ui/templates/my_screen.html`):

```html
<div v-if="showPanel">
  <h1>{{ title }}</h1>
  <div v-for="item in items">{{ item.name }}: {{ item.value }}</div>
</div>
```

**To trigger updates**: Mark Lua state as dirty from C++:

```cpp
m_luaState->MarkDirty();  // Next frame will re-render
```

For full directive syntax and event handling, see `docs/architecture/UI_SYSTEM.md`.

---

## 2. Adding a New Shader

1. Create `res/shaders/MyShader.shader` with `#shader vertex` and `#shader fragment` sections
2. Load in C++: `auto shader = new Shader("../res/shaders/MyShader.shader");`
3. Use: `shader->Use(); shader->SetMat4("projection", projMatrix);`

**Existing shaders** (`res/shaders/`):

- `Basic.shader` - Basic unlit mesh rendering
- `Composite.shader` - HTMLRendererMT UI overlay
- `Lighting.shader` - Lit mesh rendering
- `OutlinedCube.shader` - Selection wireframe overlay
- `Picking.shader` - Editor viewport entity picking
- `SkyBackground.shader` - Sky/background rendering
- `TiledBackground.shader` - Procedural tiled backdrop

---

## 3. Debugging the Multi-Threaded UI Renderer

`HTMLRendererMT` runs litehtml on a **separate thread**. See `docs/architecture/UI_SYSTEM.md` for the full threading model.

**Common issues**:

- **UI not updating**: Check `m_luaState->IsDirty()` flag
- **Crashes in FreeType**: Race condition — check mutex locks
- **Texture not uploading**: Check `m_frontBuffer.frameNumber` vs `m_lastFrameNumber`

**Useful breakpoints** (verify line numbers against current quick-stats header):

- `HTMLRendererMT::RenderThreadLoop()` — render thread entry (~line 1143 in `src/systems/HTMLRendererMT.cpp`)
- `HTMLRendererMT::UpdateTextureFromPixelBuffer()` — texture upload (~line 1035)
- `ReactiveUI::GetRenderedHTML()` — dirty check (~line 47 in `src/systems/ReactiveUI.cpp`)

---

## 4. Working with Lua Scripts

**Lua is used for**:

- UI state (`res/ui/state/`, `res/games/<name>/ui/*.lua`)
- Game logic (`res/games/<name>/scripts/`)

**Executing Lua from C++**:

```cpp
sol::state& lua = scriptManager->GetLuaState();
lua.script_file("../res/ui/state/fps.lua");
sol::table data = lua["data"];
```

**Calling C++ from Lua** (bindings registered in `ScriptManager.cpp`):

```cpp
lua.set_function("CreateEntity", &Registry::CreateEntity);
```

---

## 5. Adding ECS Components

1. Create header: `include/components/MyComponent.h`
2. Define struct: `struct MyComponent { float value; };`
3. Register in `Registry::LoadScene()`: `entity.emplace<MyComponent>(...)`
4. Create a system to iterate the component (optional)

**Existing components** (`include/components/`):

- `Transform` — Position, rotation, scale, color
- `WorldTransform` — Computed world-space transform (hierarchy support)
- `RenderComponent` — Mesh, shader, texture references
- `ScriptComponent` — Lua script reference
- `HierarchyComponent` — Parent/child relationships
- `Lighting` — Light source properties
- `Tween` — Animation interpolation

---

## 6. Adding New External Dependencies

When adding ANY new library, update BOTH:

1. **README.md** — External Dependencies section (category + install instructions)
2. **REFERENCES.md** — External Libraries section (description + usage) and License Information section

**CMake patterns**:

```cmake
# System package (like FreeType)
find_package(NewLibrary REQUIRED)
target_link_libraries(core PUBLIC ${NEWLIBRARY_LIBRARIES})

# FetchContent (like Quill)
FetchContent_Declare(newlib
    GIT_REPOSITORY https://github.com/author/newlib.git
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(newlib)
target_link_libraries(core PUBLIC newlib::newlib)

# Git submodule (like yaml-cpp)
add_subdirectory(external/newlib)
target_link_libraries(core PUBLIC newlib)
```

---

## 7. Build & Run Reference

```bash
# Full rebuild
rm -rf build && mkdir build && cd build && cmake .. && make -j8

# Incremental build
cd build && make -j8

# Run (from project root)
./build/imhotep
./build/imhotep --config res/games/vaporqube/conf/settings.yaml

# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j8
lldb ./imhotep   # or gdb on Linux

# Run tests
cd build && cmake -DIMHOTEP_BUILD_TESTS=ON .. && make -j8
ctest --output-on-failure
ctest -R lua     # Tier 3 only (headless, fast)
ctest -R unit    # Tier 2 only (headless, fast)

# Initialize submodules (fresh clone)
cd external && git submodule update --init --recursive

# Export for distribution
./scripts/export.sh --game vaporqube           # macOS .dmg / Linux .tar.gz / Windows .zip
./scripts/export.sh --game vaporqube --skip-python
```

---

## 8. macOS Apple Silicon — ARM64 Python Fix

If CMake finds x86_64 Python on M1/M2:

```bash
cd build && rm -rf *
cmake \
  -DPython3_EXECUTABLE=/opt/homebrew/bin/python3.13 \
  -DPython3_LIBRARY=/opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/lib/libpython3.13.dylib \
  -DPython3_INCLUDE_DIR=/opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/include/python3.13 \
  ..
make -j8
```

Verify: `file /opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/Python` should show `arm64`.
