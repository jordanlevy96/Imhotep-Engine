# Editor Viewport — Architecture & Implementation

> Last Updated: January 16, 2026

> **Status (2026-10-08): Historical proposal, superseded in the current path.** The editor now obtains the viewport rectangle from laid-out HTML bounds and calls `SceneViewport::RenderToScreen(fbX, fbY, width, height)` on the OpenGL thread. The PNG/base64 pipeline described as “current” below is legacy context; data-URI support still exists in the HTML renderer but is not the active viewport presentation path.

This document covers the editor viewport system including texture rendering optimization and event handling.

## Goal

Replace the CPU-heavy PNG+base64 pipeline used to embed the `SceneViewport` into the Editor UI with a low-latency, thread-safe transfer path. Today, the viewport readback, PNG encode, base64 encode, and Lua string assignment takes ~5–10ms per frame. We want to reduce this to <2ms steady-state and eliminate large temporary allocations.

## Background & Problem Statement

### Current Pipeline

Today's flow is expensive:

1. **Scene render phase** (main thread): `SceneViewport::Render()` draws the scene into a framebuffer object (FBO) with a color texture (`m_colorTexture`).
2. **Editor render phase** (main thread): `Editor::Render()` calls `m_viewport->GetTextureAsDataURI()`, which:
   - Issues `glGetTexImage()` to read the texture back to CPU memory (~2–3 ms for a 1280×720 framebuffer).
   - Flips the image vertically (CPU work).
   - Encodes to PNG using stb_image_write (~2–3 ms).
   - Base64-encodes the PNG bytes (~1–2 ms).
   - Returns a massive `data:image/png;base64,...` string (can be 1–3 MB).
3. **Lua state update**: The string is assigned to `LuaUIState::viewportImage` and the state is marked dirty.
4. **HTML render thread**: The HTML renderer parses templates and sees `<img src="{{ viewportImage }}">`, so it treats the data-URI as an embedded image.

**Why this is problematic:**

- The readback + encode + base64 path is CPU-intensive and serializes the viewport update with the main thread.
- Large strings are allocated and stored in the Lua state, creating memory pressure and GC overhead.
- Latency is high: any stall in readback (GPU sync) or encoding directly delays frame rendering.
- We're doing the same work every frame even if the viewport hasn't changed.

### Design Goals

We want to achieve:

- **Low latency**: Transfer cost <2 ms per frame steady-state, with minimal GPU stalls.
- **Low memory**: No large temporary buffers or Lua strings.
- **Thread-safe**: Renderer and main thread can operate independently without contention.
- **MVP-focused**: Prioritize macOS support and texture sharing; PBO fallback and cross-platform support are secondary.

## Proposed Solution: Multi-Mode Viewport Transfer

Rather than always doing expensive CPU encode, we'll offer multiple transfer paths chosen at runtime based on GL context capabilities. **MVP focus**: Implement texture sharing on macOS as the primary path; PBO fallback as secondary; keep data-URI as a last resort for debugging.

### Primary Path: Texture Handle Sharing (MVP Target)

**Idea:** Export the viewport's GL texture directly and let the HTML renderer sample it.

- **Mechanism**: After `SceneViewport` finishes rendering the current frame, the main thread calls `HTMLRendererMT::SetViewportExternalTexture(textureId, width, height, frameSerial, generationId)` with the texture handle, dimensions, and version metadata.
- **Benefit**: Zero copies, minimal CPU overhead. The renderer can sample the texture immediately during UI composition.
- **MVP Scope**: Implement for macOS with shared GL contexts. Windows/Linux support deferred.
- **Synchronization**: All GL commands happen on the main thread. `SceneViewport` owns the texture; the renderer accesses it sequentially. No locks needed. The `generationId` increments when dimensions or texture identity changes, signaling the renderer to reset sampling state.

### Secondary Path: Pixel Upload with Async Readback (Post-MVP)

**Idea:** If texture sharing isn't available, perform async GPU-to-CPU readback using a small ring of Pixel Buffer Objects (PBOs), then push the raw RGBA pixels to the renderer.

- **Mechanism**:
  1. Use a ring of 4 PBOs to perform asynchronous `glReadPixels` without stalling the GPU pipeline.
  2. Map the back PBO and copy to a CPU-owned pixel buffer.
  3. Call `HTMLRendererMT::PushViewportPixels(rgba_data, width, height)` to hand pixels to the renderer.
  4. Renderer uploads via `glTexSubImage2D` on the main thread.
- **Benefit**: Avoids PNG/base64 encoding; async readback reduces CPU blocking.
- **Cost**: GPU-to-CPU transfer (~2–3 ms) + CPU-to-GPU upload (~0.5–1 ms). Faster than current pipeline.
- **When to use**: Shared GL contexts unavailable (e.g., separate renderer thread). Deferred post-MVP unless blockers arise on macOS.
- **Note**: Inherent 1-frame lag from async pipeline; ring size of 4 balances GPU memory vs latency.

### Future Path: Cooperative Rendering (Deferred)

**Idea:** Render the 3D scene directly into the HTML renderer's double-buffered back buffer, eliminating all transfers.

- **Benefit**: Zero readbacks, zero uploads. Highest performance.
- **Status**: Defer until texture sharing is stable and we have real-world perf data. Requires significant ownership restructuring.

## Key Design Decisions

### TransferMode Enum & Runtime Selection

Define a `TransferMode` enum:

```
enum TransferMode {
  TextureShare,   // Primary: texture handle sharing (macOS MVP)
  PBOPushPixels,  // Secondary: async readback + pixel upload
  DataURI         // Debug fallback: PNG+base64 (for comparison/debugging)
};
```

**MVP behavior**: Always attempt texture sharing first on macOS. If setup fails, fall back to DataURI (not PBO yet). Log the selected mode and any errors at startup.

**Post-MVP**: Implement PBO fallback path for robustness across platforms.

### Ownership & Synchronization: Frame Serial & Generation ID

To ensure the renderer doesn't sample a stale or incomplete texture, we use two metadata fields:

- **Frame Serial** (`uint64_t`): `SceneViewport` increments this after every `Render()` completes. Allows renderer to detect new content and skip redundant updates.
- **Generation ID** (`uint64_t`): Increments whenever texture identity or dimensions change (e.g., resize, context loss recovery). Signals renderer to invalidate cached sampler state.

**Synchronization model**:

- **MVP (macOS, single GL context)**: All GL operations on main thread, sequential. No locks needed. Renderer reads frame serial before sampling; if unchanged, reuses cached texture.
- **Future (multi-thread/multi-GPU)**: Use `glFenceSync` / `glWaitSync` to coordinate GPU work. Document frame-lag tolerance (e.g., up to 3 frames behind is acceptable).

### Color Format & Metadata

Standardize on:

- **Format**: `GL_RGBA8` (8 bits per channel).
- **Color space**: Assume sRGB for the texture. Renderer applies linear-to-sRGB in shader if needed.
- **Alpha**: Straight alpha (non-premultiplied). Renderer blends with standard `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`.

## Implementation Outline

### Phase 1: API Definition & macOS Texture Sharing Setup

**Deliverable**: Headers, stubs, and basic capability check for macOS shared GL contexts.

**Tasks**:

- Add `TransferMode` enum and metadata structs to `include/systems/ViewportTransfer.h`:
  - `struct ViewportTextureHandle { GLuint texture; uint32_t width; uint32_t height; uint64_t frameSerial; uint64_t generationId; }`
- Add to `SceneViewport`:
  - `GLuint GetColorTexture() const;`
  - `uint64_t GetFrameSerial() const;`
  - `uint64_t GetGenerationId() const;`
  - `void IncrementGenerationId();` (called on resize or context loss)
- Add to `HTMLRendererMT`:
  - `void SetViewportExternalTexture(const ViewportTextureHandle&);`
  - `void SetViewportTransferMode(TransferMode mode);`
- Add simple GL context sharing probe:
  - `bool CanShareGLContexts();` (macOS: check if both viewport and renderer have same GL context).
- Log selected transfer mode at initialization.

**Status**: Stubs only, no rendering logic yet.

### Phase 2: Texture Sharing Implementation (macOS MVP)

**Deliverable**: Working texture sharing on macOS; viewport renders in editor UI without visual artifacts.

**Tasks**:

- Implement `SetViewportExternalTexture()` to cache texture handle, dimensions, and metadata in `HTMLRendererMT`.
- During HTML renderer's composition phase, bind and sample the external texture when rendering the viewport element.
- Update `Editor::Render()` to call `SetViewportExternalTexture()` after `SceneViewport::Render()` completes.
- Verify frame serial changes between frames (confirms texture is being updated).
- Verify generation ID handling: on viewport resize, confirm generation ID increments and renderer adapts.
- Test on macOS: simple scene, animated object in viewport, confirm no tearing or corruption.

**Acceptance criteria**:

- Viewport renders without artifacts.
- <2 ms per-frame transfer latency (near-zero with shared context).
- No GL errors in debug output.

### Phase 3: Fallback & Error Handling (macOS MVP)

**Deliverable**: Robust fallback to DataURI if texture sharing fails; clear error logging.

**Tasks**:

- Detect texture sharing setup failures and log reason (e.g., "GL context mismatch", "unsupported on this driver").
- On failure, automatically fall back to DataURI path and log at info level.
- Add a telemetry callback to capture:
  - Selected transfer mode
  - Any errors encountered
  - Frame transfer time (microseconds)
- Do _not_ implement PBO fallback yet; save for post-MVP if needed on other platforms.

### Phase 4: Integration Testing (macOS MVP)

**Deliverable**: End-to-end validation that editor UI correctly displays viewport with new transfer path.

**Tasks**:

- Test scenarios:
  - Viewport resize: confirm generation ID increments, no flicker.
  - Rapid frame updates: confirm no dropped frames or tearing.
  - Editor minimize/restore: confirm texture handle remains valid.
  - Scene with moving objects: confirm smooth animation in embedded viewport.
- Verify GL debug output: no errors, warnings, or unsupported operations.
- Compare frame times: measure old (DataURI) vs new (TextureShare) paths side-by-side.
- Update `CLAUDE.md` or developer docs with architecture and transfer mode selection logic.

### Phase 5: Post-MVP Cross-Platform Support (Windows/Linux)

**Deliverable**: PBO fallback path for platforms without reliable shared GL contexts.

**Deferred tasks** (implement after macOS MVP is stable):

- Implement PBO ring (4 slots) for async readback on Windows/Linux.
- Implement `PushViewportPixels()` to upload raw RGBA data to internal texture.
- Update fallback logic: if texture sharing unavailable, try PBO; if PBO unavailable, fall back to DataURI.
- Test on Windows (NVIDIA, AMD drivers) and Linux (Mesa, proprietary).

**Cleanup** (post-MVP, when new path is stable):

- Deprecate `GetTextureAsDataURI()` once PBO fallback covers all platforms.
- Remove data-URI path after 2+ releases of stable texture-sharing usage.

## Future Optimizations (Post-MVP)

### Partial Updates & Dirty Rectangles

- Track which regions of viewport changed.
- Use `glCopyImageSubData()` to transfer only dirty regions, reducing bandwidth.
- Store last frame serial in Lua UI state; renderer skips updates if unchanged.

### Platform-Specific Fast Paths

- **macOS**: Investigate IOSurface and Metal interop for even lower-latency sharing.
- **Windows**: Use WGL_NV_shared_resources or similar extensions if available.
- **Linux**: Optimize GLX context sharing setup.

### Throttling & Quality Hints

- Expose `SceneViewport::SetQualityHint()` to render at half or quarter resolution during fast iteration (reduces readback/upload cost).
- Automatic throttling: if frame time exceeds budget, reduce quality hint temporarily.

### Diagnostics & Telemetry

Optional logging for performance tuning (post-MVP):

- Per-frame transfer time and GPU utilization.
- Fallback frequency and reasons.
- Frame serial change rate (detect stuck viewports).

Keep minimal for MVP; add if needed based on field experience.

## Testing & Acceptance Criteria (MVP: macOS)

### Functional

- Viewport renders correctly in editor UI.
- No visual artifacts (tearing, corruption, color mismatches).
- Viewport resizing works without flicker.
- Editor input and UI interaction unaffected.

### Performance

- **Baseline**: Current data-URI path ~5–10 ms per frame.
- **Target**: Texture sharing path achieves <2 ms per frame (or ≥60% improvement).
- **Measurement**: Frame-time profiler + per-operation timers.

### Stability

- No GL errors in debug output.
- Graceful fallback to DataURI if texture sharing unavailable.
- No crashes on context loss or resize.

### Cross-Platform (Post-MVP)

- Windows and Linux pass same functional/performance tests with PBO fallback.
- No platform-specific visual artifacts.

## Implementation Roadmap

**MVP (macOS, texture sharing)**:

1. Phase 1: API definitions and macOS capability probe.
2. Phase 2: Implement texture sharing on macOS.
3. Phase 3: Error handling and DataURI fallback.
4. Phase 4: Integration testing and validation.

**Post-MVP**:

5. Phase 5: PBO fallback path for Windows/Linux.
6. Future: Partial updates, quality hints, diagnostics, cooperative rendering.

---

## Completed: Click Event Handling Fix

**Status**: Complete (January 2026)

Fixed missing click feedback for viewport UI elements. Root cause was in handler expression evaluation during v-for template rendering.

### Problem

When template rendering processes v-for loops like:

```html
<div v-for="entity in entities" @click="selectEntity(entity.id)"></div>
```

The handler expression `selectEntity(entity.id)` was stored as-is without substituting the actual loop variable value. When the click was dispatched, ReactiveUI tried to evaluate `entity.id` but `entity` didn't exist in Lua global scope.

Error: `[ReactiveUI] Lua handler 'selectEntity' failed: stack index 1, expected number, received table`

### Solution

Modified `TemplateParser.cpp` `SerializeElementForIteration` to call `ProcessIterationInterpolations()` on event handler expressions. Handlers are now stored with concrete values (e.g., `selectEntity(1)` instead of `selectEntity(entity.id)`).

### Files Changed

| File | Change |
|------|--------|
| `src/systems/TemplateParser.cpp` | Process handler expressions during v-for iteration |
| `src/systems/HTMLRendererMT.cpp` | Enhanced click/hover logging |
| `src/systems/ReactiveUI.cpp` | Improved error messages with event/element context |

### Event Flow (After Fix)

1. GLFW receives click → WindowManager callback
2. Input event pushed to Lua EventQueue
3. Input handler calls `htmlRenderer:HandleClickEvent(x, y, button)`
4. HTMLRendererMT hit-tests elements (HiDPI-aware coordinates)
5. ReactiveUI receives pre-evaluated handler (e.g., `selectEntity(1)`)
6. Lua executes handler with concrete argument values

---

## See Also

- [EDITOR_ARCHITECTURE.md](EDITOR_ARCHITECTURE.md) - Editor design (viewport is Phase 2)
- [UI_SYSTEM.md](UI_SYSTEM.md) - HTMLRendererMT multi-threading model
