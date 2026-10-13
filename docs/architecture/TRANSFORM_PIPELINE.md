# Entity transforms

Each entity has a local `Transform` and a computed `WorldTransform`. `HierarchySystem::Update` walks from parent entities to their children and computes:

```text
world matrix = parent world matrix × local matrix
local matrix = translation × rotation × scale
```

Root entities use the identity matrix as their parent matrix. `RenderSystem` reads `WorldTransform`; call `HierarchySystem::Update` before render consumers each frame. The registry creates and removes both transform components with an entity.

`Transform.Pos` and `Transform.Scale` are local values; rotation is a quaternion. Do not write `WorldTransform` directly: the hierarchy pass replaces its matrix. See `include/components/Transform.h`, `include/components/WorldTransform.h`, and `src/systems/HierarchySystem.cpp` for the implementation.
