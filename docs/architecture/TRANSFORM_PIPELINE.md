# Transform Pipeline Refactor Plan

> Last Updated: 2026-01-16

> **Status (2026-10-08): Design history with partial implementation.** `WorldTransform`, `HierarchySystem`, automatic world-component registration, and render-system consumption are implemented. The dirty-transform optimization and some cleanup ideas remain unimplemented. The original phase table below is retained as the proposal baseline and must not be read as current status.

## Implementation Status

| Phase | Name | Status |
|-------|------|--------|
| 1 | Add WorldTransform Component | Implemented |
| 2 | Create HierarchySystem | Implemented |
| 3 | Migrate RenderSystem | Implemented |
| 4 | Dirty Flag Optimization | Planned |
| 5 | Clean Up TransformUtils | Partial / not re-audited |

## Executive Summary

This document outlines a phased refactor to introduce a proper hierarchical transform pipeline. The current implementation has several architectural issues that must be addressed before supporting complex scene graphs.

---

## Architecture Analysis

### Problems Identified

**1. Hierarchy Composition in Renderer (Critical)**

Location: `src/systems/RenderSystem.cpp:32-38`

```cpp
if (hc.Parent < std::numeric_limits<size_t>::max())
{
    Transform parentTransform = registry->GetComponent<Transform>(hc.Parent);
    t.Pos += parentTransform.Pos;      // WRONG: Should use matrix multiplication
    t.Color *= parentTransform.Color;
    t.Scale *= parentTransform.Scale;  // WRONG: Only works for uniform scaling
}
```

Issues:
- **Single-level only**: Grandparents are ignored (no recursive traversal)
- **Incorrect position composition**: `+=` doesn't account for parent rotation/scale
- **Missing rotation composition**: Parent rotation doesn't affect child position
- **Scale inheritance broken**: Component-wise multiplication only works for uniform scales
- **Wrong system boundary**: Renderer should not know about hierarchy traversal

**2. No Separation of Local vs World Space**

- `Transform` is documented as "local space" but treated as world space by the renderer
- No dedicated storage for computed world matrices
- World space is recomputed every frame, every entity, in the renderer

**3. TransformUtils Mutations**

Location: `src/util/TransformUtils.cpp`

The `translate()` and `rotate()` functions recursively modify local transforms of children. This conflates "moving an entity in world space" with "changing local offset from parent."

---

## Target Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Loop                             │
├─────────────────────────────────────────────────────────────┤
│  1. Input Processing                                         │
│  2. ScriptSystem::Update()     ← Modifies LOCAL Transforms   │
│  3. TweenSystem::Update()      ← Modifies LOCAL Transforms   │
│  4. HierarchySystem::Update()  ← Computes WORLD Transforms   │ ← NEW
│  5. RenderSystem::Update()     ← Reads WORLD Transforms only │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

```
Transform (local)  ──►  HierarchySystem  ──►  WorldTransform (world mat4)
                              │
                              ▼
                    Parent's WorldTransform
```

### Matrix Composition (per entity)

```
WorldMatrix = ParentWorldMatrix * LocalMatrix
LocalMatrix = Translation * Rotation * Scale
```

For root entities (no parent):
```
WorldMatrix = LocalMatrix
```

---

## Component Changes

### Existing: Transform (no changes to structure)

```cpp
struct Transform
{
    glm::vec3 Pos;       // LOCAL position relative to parent
    glm::vec3 Scale;     // LOCAL scale
    glm::quat Rotation;  // LOCAL rotation
    glm::vec3 Color;     // Tint (not affected by hierarchy)
};
```

### New: WorldTransform

```cpp
struct WorldTransform
{
    glm::mat4 matrix = glm::mat4(1.0f);  // Computed world transformation matrix
    bool dirty = true;                    // Needs recomputation
};
```

**Design Decision: mat4 vs decomposed TRS**

| Approach | Pros | Cons |
|----------|------|------|
| `mat4` only | Simple, GPU-ready, single multiply | Harder to extract position for physics/queries |
| Decomposed TRS | Easy position queries | Redundant storage, quaternion extraction costly |
| **mat4 + cached position** | Best of both | Slightly more memory, must keep in sync |

**Recommendation**: Start with `mat4` only. Add cached world position later if needed for spatial queries.

---

## New System: HierarchySystem

### Responsibilities

1. Traverse entity hierarchy in correct order (parents before children)
2. Compute world matrices using proper composition
3. Manage dirty flags for optimization
4. **NOT** responsible for: rendering, physics, scripts

### Interface

```cpp
class HierarchySystem
{
public:
    // Called once per frame, before RenderSystem
    static void Update();

private:
    // Recursive traversal from root entities
    static void UpdateEntity(EntityID entity, const glm::mat4& parentWorld);

    // Build sorted traversal order (topological sort)
    static void BuildTraversalOrder();
};
```

### Traversal Strategy

**Option A: Recursive from roots**
- Find all root entities (no parent)
- DFS traverse children, passing parent's world matrix down
- Pros: Natural, no preprocessing
- Cons: Stack depth = hierarchy depth

**Option B: Topological sort + flat iteration**
- Pre-sort entities so parents come before children
- Single flat loop through sorted list
- Pros: Cache-friendly, constant stack depth
- Cons: Must maintain sorted list when hierarchy changes

**Recommendation**: Start with Option A (recursive). Optimize to Option B if profiling shows issues with deep hierarchies (100+ levels).

---

## Implementation Phases

### Phase 1: Add WorldTransform Component (Non-Breaking)

**Goal**: Introduce WorldTransform without changing existing behavior.

**Steps**:

1. Create `include/components/WorldTransform.h`:
   ```cpp
   struct WorldTransform
   {
       glm::mat4 matrix = glm::mat4(1.0f);
   };
   ```

2. Add to `Registry.h`:
   - Add `SparseSet<WorldTransform> WorldTransformComponents;`
   - Add template specialization for `GetComponentSet<WorldTransform>()`

3. Update `Registry::RegisterEntity()`:
   - Auto-create WorldTransform for all entities (alongside Transform)

4. Update `Registry.cpp` `GetComponentSet` specializations

**Verification**:
- Build succeeds
- All existing entities have WorldTransform component
- No behavioral changes yet

---

### Phase 2: Create HierarchySystem (Parallel Path)

**Goal**: Implement HierarchySystem that computes world matrices, running alongside existing renderer logic.

**Steps**:

1. Create `include/systems/HierarchySystem.h`:
   ```cpp
   class HierarchySystem
   {
   public:
       static void Update();
   private:
       static void UpdateEntity(EntityID entity, const glm::mat4& parentWorld);
   };
   ```

2. Create `src/systems/HierarchySystem.cpp`:
   - Iterate root entities (Parent == -1)
   - Recursively traverse children
   - Compute: `world = parentWorld * TransformUtils::calculateMatrix(localTransform)`
   - Store result in WorldTransform component

3. Call `HierarchySystem::Update()` in Game.cpp main loop:
   - After TweenSystem::Update()
   - Before RenderSystem::Update()

4. Add logging to verify world matrices are computed correctly

**Verification**:
- WorldTransform.matrix values are correct for:
  - Root entities (should equal local matrix)
  - Single-level children (parent * local)
  - Multi-level children (grandparent * parent * local)
- Existing rendering unchanged (still using old code path)

---

### Phase 3: Migrate RenderSystem to WorldTransform

**Goal**: RenderSystem reads WorldTransform instead of computing hierarchy inline.

**Steps**:

1. Modify `RenderSystem::RenderEntity<RenderComponent>()`:

   **Before**:
   ```cpp
   Transform t = registry->GetComponent<Transform>(id);
   HierarchyComponent hc = registry->GetComponent<HierarchyComponent>(id);
   if (hc.Parent < std::numeric_limits<size_t>::max()) {
       // ... manual composition (REMOVE THIS)
   }
   glm::mat4 Model = TransformUtils::calculateMatrix(t);
   ```

   **After**:
   ```cpp
   WorldTransform& wt = registry->GetComponent<WorldTransform>(id);
   Transform& t = registry->GetComponent<Transform>(id);  // Only for Color
   rc.AddUniform("objectColor", t.Color, UniformTypeMap::vec3);
   rc.AddUniform("model", wt.matrix, UniformTypeMap::mat4);
   ```

2. Remove hierarchy traversal code from RenderSystem entirely

3. Update any lighting code that uses positions to extract from WorldTransform:
   ```cpp
   glm::vec3 worldPos = glm::vec3(wt.matrix[3]);  // Extract translation
   ```

**Verification**:
- Visual output identical to before
- Entities with parents render at correct world positions
- Deeply nested hierarchies (3+ levels) render correctly
- RenderSystem no longer references HierarchyComponent

---

### Phase 4: Implement Dirty Flag Optimization (Optional)

**Goal**: Avoid recomputing world matrices for static entities.

**Steps**:

1. Add `dirty` flag to WorldTransform:
   ```cpp
   struct WorldTransform
   {
       glm::mat4 matrix = glm::mat4(1.0f);
       bool dirty = true;
   };
   ```

2. Mark dirty when local Transform changes:
   - Option A: Modify Transform setters (requires wrapper or proxy)
   - Option B: Compare previous frame values in HierarchySystem
   - Option C: Explicitly mark dirty from ScriptSystem/TweenSystem

3. Propagate dirty to children:
   - If parent is dirty, all descendants must be dirty

4. Skip recomputation for clean entities:
   ```cpp
   if (!worldTransform.dirty) return;
   // ... compute matrix ...
   worldTransform.dirty = false;
   ```

**Recommendation**: Defer to Phase 4. For small scenes (<1000 entities), the overhead of dirty checking may exceed the cost of recomputation.

**Verification**:
- Profile before/after with 1000+ entity scene
- Ensure moving a parent correctly updates all children
- Ensure static hierarchies skip computation

---

### Phase 5: Clean Up TransformUtils (Optional)

**Goal**: Clarify whether TransformUtils operates in local or world space.

**Current Issue**: `TransformUtils::translate()` modifies local positions of children, effectively treating them as world positions.

**Options**:

1. **Keep as-is**: Document that these functions "move entities in world space by modifying local transforms"

2. **Split into two APIs**:
   - `LocalTransform::translate()` - modifies entity's local offset from parent
   - `WorldTransform::translate()` - moves in world space (inverse parent * offset)

3. **Remove recursive behavior**: Only modify the target entity, let hierarchy system handle propagation

**Recommendation**: Option 1 for now. The current behavior is useful for game logic that wants to "move a group." Add Option 2 when use cases demand it.

---

## System Boundaries

| System | Reads | Writes | Purpose |
|--------|-------|--------|---------|
| ScriptSystem | Transform | Transform | Game logic modifies local space |
| TweenSystem | Transform, Tween | Transform | Animations modify local space |
| **HierarchySystem** | Transform, HierarchyComponent | WorldTransform | Computes world matrices |
| RenderSystem | WorldTransform, RenderComponent | (GPU) | Renders using world space |

---

## Hierarchy Traversal Details

### Finding Root Entities

```cpp
std::vector<EntityID> GetRootEntities()
{
    std::vector<EntityID> roots;
    for (EntityID id : registry->GetComponentSet<HierarchyComponent>().GetEntities())
    {
        HierarchyComponent& hc = registry->GetComponent<HierarchyComponent>(id);
        if (hc.Parent == static_cast<EntityID>(-1))
        {
            roots.push_back(id);
        }
    }
    return roots;
}
```

### Recursive Update

```cpp
void HierarchySystem::UpdateEntity(EntityID entity, const glm::mat4& parentWorld)
{
    Transform& local = registry->GetComponent<Transform>(entity);
    WorldTransform& world = registry->GetComponent<WorldTransform>(entity);
    HierarchyComponent& hc = registry->GetComponent<HierarchyComponent>(entity);

    // Compose world matrix
    glm::mat4 localMatrix = TransformUtils::calculateMatrix(local);
    world.matrix = parentWorld * localMatrix;

    // Recurse to children
    for (EntityID child : hc.Children)
    {
        UpdateEntity(child, world.matrix);
    }
}
```

### Update Entry Point

```cpp
void HierarchySystem::Update()
{
    glm::mat4 identity(1.0f);

    for (EntityID root : GetRootEntities())
    {
        UpdateEntity(root, identity);
    }
}
```

---

## Risks and Pitfalls

### 1. Cycle Detection

**Risk**: Malformed hierarchy with cycles causes infinite recursion.

**Mitigation**:
- Add debug-build cycle detection using visited set
- Log error and skip entity if cycle detected

### 2. Orphaned Entities

**Risk**: Child references non-existent parent after parent deletion.

**Mitigation**:
- `Registry::DestroyEntity()` should either:
  - Reparent children to grandparent, or
  - Destroy children recursively
- Document chosen behavior

### 3. Order Dependency

**Risk**: HierarchySystem runs after ScriptSystem, so scripts see stale world positions.

**Mitigation**:
- Document that scripts should use local transforms
- Provide helper: `HierarchySystem::GetWorldPosition(EntityID)` for scripts that need it
- Consider running HierarchySystem twice (before and after scripts) if needed

### 4. Scale Inheritance

**Risk**: Non-uniform parent scale + child rotation = shear (may be unexpected).

**Mitigation**:
- Document the behavior
- Artists/designers should use uniform scales on parents with rotated children
- Future: Add flag to ignore parent scale if needed

### 5. Performance with Deep Hierarchies

**Risk**: Stack overflow with very deep hierarchies (1000+ levels).

**Mitigation**:
- Phase 4 can switch to iterative topological sort
- Add max depth limit with warning

---

## Success Criteria

### Phase 1 Complete
- [ ] WorldTransform component exists
- [ ] All entities automatically receive WorldTransform
- [ ] Build succeeds, tests pass (manual)
- [ ] No behavioral changes

### Phase 2 Complete
- [ ] HierarchySystem computes world matrices
- [ ] Root entities: WorldTransform == LocalMatrix
- [ ] Child entities: WorldTransform == ParentWorld * LocalMatrix
- [ ] 3+ level hierarchies compute correctly
- [ ] Logging confirms correct values

### Phase 3 Complete
- [ ] RenderSystem uses WorldTransform.matrix directly
- [ ] RenderSystem has no hierarchy traversal code
- [ ] Visual output identical to before refactor
- [ ] Moving parent moves children correctly (visual verification)

### Phase 4 Complete (Optional)
- [ ] Static entities skip recomputation
- [ ] Moving parent marks children dirty
- [ ] Performance improvement measured with 1000+ entities

### Full Refactor Complete
- [ ] Transform is purely local space
- [ ] WorldTransform is purely world space
- [ ] Clear system boundaries documented
- [ ] No per-frame mutation of local transforms by renderer

---

## Testing Scenarios

### Manual Test Cases

1. **Root entity movement**: Move root, verify visual position changes
2. **Parent-child**: Create parent/child, move parent, verify child moves with it
3. **Grandparent chain**: Create A→B→C hierarchy, move A, verify all move
4. **Child rotation**: Rotate parent, verify child orbits (not just rotates in place)
5. **Scale inheritance**: Scale parent, verify child scales appropriately
6. **Mixed transforms**: Parent rotated + scaled, child offset, verify correct world position

### Debug Helpers

Add temporary debug logging:
```cpp
LOG_DEBUG("Entity {} WorldTransform: pos=({:.2f},{:.2f},{:.2f})",
    id, world.matrix[3][0], world.matrix[3][1], world.matrix[3][2]);
```

---

## Timeline Considerations

This document does not include time estimates per project guidelines. The phases are ordered by dependency and risk:

- **Phase 1**: Zero risk, enables future work
- **Phase 2**: Low risk, parallel path
- **Phase 3**: Medium risk, changes rendering (most critical to test)
- **Phase 4**: Low risk, optimization only

Phase 3 is the critical switchover point. Consider feature-flagging it:
```cpp
#define USE_WORLD_TRANSFORM 1  // Set to 0 to revert
```

---

## References

- `include/components/Transform.h` - Current transform definition
- `include/components/HierarchyComponent.h` - Current hierarchy definition
- `src/systems/RenderSystem.cpp` - Current (incorrect) hierarchy handling
- `src/util/TransformUtils.cpp` - Matrix calculation and recursive transforms
- GLM documentation for matrix operations
