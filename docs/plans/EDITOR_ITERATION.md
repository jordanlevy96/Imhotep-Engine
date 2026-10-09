# Editor iteration: current evidence and next working slice

Date: 2026-10-09. Baseline: `9d5b5d0` (PR17 documentation atop `161dd1f`).
Work branch: `feat/editor-iteration`. PR17 was verified open/draft with passing macOS CI; it is not assumed merged. This plan supersedes the phase-status table in the historical Editor architecture proposal for this iteration.

> **Priority update:** Jordan’s target is authoring Tetris through the Editor. See [TETRIS_AUTHORING.md](TETRIS_AUTHORING.md) for the source map, scene-document implementation and revised scene/play-first milestone. The template-first sequence below records the initial audit.

## Evidence and intended workflows

This pass inspected source, configured and built locally, and ran headless tests. It did **not** observe the running Editor UI. Screen coordination could not reach the source task, so display tests and interactive use remain pending. The local Codex memories directory was empty; the final documentation-task history confirmed PR17's status. No repository `.agents` directory was present.

| Workflow | Implemented in source | Gap to a dependable user workflow |
|---|---|---|
| Inspect a scene | Startup loads `scenes/EditorTest.yaml`; tree selection and viewport ID picking feed the inspector | No project/scene chooser; scene tree population is startup-oriented |
| Edit transforms | Custom focused-field handling changes position, Euler rotation and scale in the registry | No scene save, undo, edit transaction or unsaved-change protection |
| Navigate the viewport | HTML bounds drive direct OpenGL rendering and picking; panel resize handlers exist | Camera orbit/pan/gizmos from the old proposal are not established working features |
| Author UI templates | Manager lists/loads/saves HTML/CSS/Lua triples; tabs, preview, create and save callbacks exist | Template selection expects a native select change event; textareas expect input events without a complete editable-control implementation |
| Preview reactive UI | CSS injection and TemplateParser evaluation; 500 ms debounce flag | Lua can execute twice, uses a fixed temporary source file, and some Lua failures are silently cleared; preview CSS/content is embedded into the editor document |
| Save/reopen templates | Three separate writes and manager reload | No atomic multi-file save, overwrite/conflict handling, or unsaved-switch protection; storage reload defects addressed below |

File/Edit/Tools are menu labels with hover handlers, not working menus. Ctrl+S/O/Z/Shift+Z log unimplemented scene operations; macOS Command equivalents are not wired there. The existing Save button calls template persistence separately. `onCodeChange` schedules preview but does not set `isDirty`. Do not infer browser behavior from HTML markup.

## Actual architecture

See [the separate flow diagram](../architecture/diagrams/editor-current-flow.mmd).

- `src/editor/Editor.cpp` links the shared `core` engine, initializes EngineCore using editor settings and a fixed test scene, and skips ScriptSystem in its frame loop. Tween and hierarchy updates still run.
- `res/ui/editor.html` and `res/ui/state/editor.lua` describe the shell. Editor injects C++ callbacks into the Lua methods table. C++ owns engine access and custom text input; Lua owns UI state and UI-only handlers.
- `Editor::Render()` checks Lua dirty state, asks ReactiveUI for fully evaluated HTML, and calls `HTMLRendererMT::UpdateHTML()` when dirty. This is full template evaluation/document rebuilding, not fine-grained DOM patching.
- HTMLRendererMT performs litehtml layout/raster work on its worker; main-thread code uses published bounds and front-buffer APIs. Preserve those thread boundaries.
- The scene is drawn directly on the OpenGL thread with `SceneViewport::RenderToScreen()` inside the current HTML viewport rectangle. A separate framebuffer serves entity picking. The old PNG/base64 viewport proposal is not the active path.
- TemplateParser's `v-model` serialization populates input values and textarea content. It does not by itself implement focus, native dropdowns, selection or two-way editing. Inspector fields already demonstrate explicit click-to-focus handling.
- Dependencies remain OpenGL/GLFW, litehtml/Gumbo, FreeType, Lua/sol2, YAML, Quill and shared-core Python integration. No new dependencies are proposed.

## Phase 1: reliable template authoring loop

Recommended first end-to-end slice: choose an existing template, edit HTML/CSS/Lua, preview, save, reopen, and recover from a bad edit without losing the buffer.

Completed foundation in this iteration:

- Empty HTML saved by the manager can be reopened.
- Read all three files into temporary buffers; publish outputs only after every required/existing file reads successfully. Missing CSS/Lua remain valid empty companions; a directory in place of a file is an error.
- Apply the existing template-name rule to load/save/delete/lookup as well as create.
- Add headless `editor.templates` coverage using disposable files, including save/reload and failure paths.

Next implementation tasks, using existing patterns:

1. Use explicit template-selection click handlers, matching the scene tree pattern, so selection does not depend on native select widgets. Keep requested template separate from the currently loaded document until load succeeds.
2. Connect code fields to explicit focus/edit handlers with multiline input, escaped source display, dirty state and preview scheduling. Preserve inspector numeric-entry behavior. Verify keyboard selection/backspace/paste and Command+S on macOS.
3. Show preview compile/runtime errors and retain the last good preview. Remove double execution and fixed temporary-file reliance only after agreeing the small LuaUIState API change required by repository architecture rules.
4. Guard switching/closing dirty documents with Save/Discard/Cancel, then test error recovery. Failed save must keep the edited buffer and dirty marker.

Acceptance: start from a disposable template; select, change all three files, see a reactive preview, save, reopen with byte-identical content; reject invalid names; preserve the open document on missing/unreadable input and failed writes; surface invalid Lua and recover after fixing it. Add a display-backed editor click/input scenario modeled on the existing engine click test. Visual verification must include panel bounds and HiDPI picking after resizing.

## Phase 2: scene authoring, after one product decision

Decide whether the first writable document is an engine YAML scene (recommended starting scope) or a project-specific Lua scene/module. Saving arbitrary runtime ECS state must not silently overwrite hand-authored game logic or serialize generated entities.

Then implement one loop: open a supported YAML scene, select an entity, change a transform, save, reload and verify the transform. Define supported components and preserve unedited fields. Establish dirty state and save-failure behavior before adding entity creation/deletion or transform undo. No new serializer or command framework is assumed approved by this plan.

## Deferred until the basic loops work

Gizmos, play mode, file watching/hot reload, visual CSS tooling and broad component editing. These are not prerequisites to making current controls dependable.

## Verification and local setup

The existing five tests cover gameplay Lua, engine units, registry liveness, and display-backed engine smoke/click behavior. None previously exercised Editor persistence or Editor interaction. `editor.templates` adds headless persistence coverage, not UI coverage.

This Mac needs the C++ standard-header workaround already present in the primary checkout. The isolated worktree reuses cached dependency sources and copies existing submodule contents without changing third-party source:

```sh
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_QUILL=/Users/jordan/dev/cppengine/build/_deps/quill-src \
  -DFETCHCONTENT_SOURCE_DIR_GLFW=/Users/jordan/dev/cppengine/build/_deps/glfw-src \
  -DPython3_EXECUTABLE=/opt/homebrew/opt/python@3.13/bin/python3.13 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -DCMAKE_CXX_FLAGS='-nostdinc++ -isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1'
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure -E '^(engine\.smoke|engine\.click)$'
```

Run the Editor from `build/` once shared-screen access is coordinated. Storage saves still use three independent writes: a later write failure can leave earlier files changed. This iteration does not claim transactional save, filesystem containment through symlinks, or unsaved-document protection.

### Results of this pass

- Full local build passed, including `imhotep-editor`.
- All four headless tests passed: `vaporqube.lua.behavior`, `engine.unit`, `engine.registry-liveness`, `editor.templates`.
- `git diff --check` passed. Primary documentation checkout remains clean.
- Existing linker warnings mention a missing `/usr/local/opt/openssl@1.1/lib` search directory; they did not prevent linking.
- Display-backed smoke/click tests and Editor visual/save/reload interaction remain unverified pending shared-screen coordination. Manager-level save/reload and failure paths are verified.
- No commit, push, PR, merge, deployment, dependency install or toolchain modification was performed.
