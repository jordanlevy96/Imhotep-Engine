# Handoff: Bundled Python Distribution for Imhotep Engine

**Date**: 2026-02-05
**Previous Developer**: Claude Opus 4.5

> **Historical implementation handoff (reviewed 2026-10-08).** PathResolver and packaging support exist, but this document does not certify that every platform packaging path has been exercised recently. Use `README.md` for current build/run instructions.

---

## 1. Problem Statement

- **What**: Enable distributing Imhotep games with a bundled Python runtime
- **Why**: Allow features like in-game pandas/matplotlib charts without requiring users to install Python
- **Trigger**: User requested implementation of a detailed plan for portable Python distribution in macOS app bundles

---

## 2. Original Requirements

### Core Requirements
- Detect runtime environment (development vs installed .app bundle)
- Configure PYTHONHOME/PYTHONPATH before interpreter initialization
- Support bundling a portable Python distribution with packages (pandas, matplotlib, numpy)
- CMake install targets for creating distributable macOS app bundles
- Expose path utilities to Python scripts via engine bindings

### Discovered Requirements
- Handle Apple Silicon (ARM64) vs Intel (x86_64) architecture detection
- Auto-detect Homebrew ARM64 Python when conda isn't active
- Validate Python architecture matches build target to prevent cryptic linker errors
- Clean up CMake warnings (FetchContent timestamps, duplicate libraries)

---

## 3. Current Status

### Completed
- [x] PathResolver utility (`include/util/PathResolver.h`, `src/util/PathResolver.cpp`)
- [x] Config struct Python fields (`include/util/Config.h`)
- [x] ConfigLoader YAML parsing for Python section (`src/util/ConfigLoader.cpp`)
- [x] PreInitializePython() in ScriptManager (`src/controllers/ScriptManager.cpp`)
- [x] Engine bindings for path utilities (`src/controllers/python/engine_bindings.cpp`)
- [x] Updated init.py to use engine bindings (`res/scripts/init.py`)
- [x] CMake install targets and bundle options (`CMakeLists.txt`)
- [x] ARM64 Python auto-detection for Apple Silicon
- [x] CMake warning cleanup (CMP0135, duplicate library warnings)

### In Progress
- None

### Not Started
- [ ] Testing actual distribution build (`scripts/package-macos.sh`)
- [ ] Windows/Linux bundled Python support
- [ ] Automated verification of bundled Python with pandas/matplotlib

---

## 4. Technical Context

### Relevant Files

| File | Purpose |
|------|---------|
| `include/util/PathResolver.h` | Runtime environment detection API |
| `src/util/PathResolver.cpp` | macOS-specific path resolution using `_NSGetExecutablePath` |
| `include/util/Config.h:21-23` | Python config fields (`PythonHome`, `PythonPath`, `BundledPython`) |
| `src/util/ConfigLoader.cpp:~45-60` | YAML parsing for `python:` section |
| `src/controllers/ScriptManager.cpp:~890-930` | `PreInitializePython()` function |
| `src/controllers/ScriptManager.cpp:~998` | Python interpreter initialization |
| `src/controllers/python/engine_bindings.cpp:77-87` | `getResourcePath()`, `isInstalledBundle()`, `getExecutableDir()` |
| `res/scripts/init.py` | Python initialization using engine bindings |
| `CMakeLists.txt:177-290` | Python detection and install targets |
| `scripts/package-macos.sh` | Distribution packaging script (not yet tested) |

### Key Functions/Classes

- `PathResolver::GetExecutableDir()` - Returns absolute path to executable's directory
- `PathResolver::IsInstalledBundle()` - Checks for `.app/Contents/MacOS` pattern
- `PathResolver::GetBundledPythonHome()` - Returns Python home for bundled mode
- `ScriptManager::PreInitializePython()` - Sets PYTHONHOME/PYTHONPATH before interpreter
- `engine.getResourcePath()` - Python binding for resource path
- `engine.isInstalledBundle()` - Python binding for bundle detection

### Architecture Notes

- Python environment variables MUST be set before `py::scoped_interpreter()` is created
- Development mode uses `PYTHONPATH=../res/scripts` only
- Bundle mode sets `PYTHONHOME` and complete `PYTHONPATH` with site-packages
- Bundle detection: path contains `.app/Contents/MacOS` AND `../Resources/res` exists

### Dependencies

- pybind11 (for Python bindings)
- Python 3.13 (Homebrew ARM64 or conda)
- python-build-standalone (for bundled distribution - not yet integrated)

---

## 5. Validation Criteria

### Automated Verification
```bash
# Development build works without conda
cd build && rm -rf * && env -u CONDA_PREFIX cmake .. && make -j8
./imhotep  # Should show "Python development mode: PYTHONPATH=../res/scripts"
```

### Manual Verification Needed
- [ ] App bundle launches without crash after `cmake --install`
- [ ] `import pandas` works in bundled mode
- [ ] `engine.getResourcePath()` returns correct path in bundle
- [ ] `engine.isInstalledBundle()` returns True in bundle, False in dev

### Edge Cases
- x86_64 Python on ARM64 Mac should fail with clear error message
- Missing Homebrew Python should fall back gracefully
- Empty `python:` section in YAML should use defaults

---

## 6. Constraints & Considerations

### Technical Constraints
- PYTHONHOME must be set BEFORE interpreter initialization (C++ limitation)
- macOS app bundles have specific structure: `Contents/{MacOS,Frameworks,Resources}`
- ARM64 and x86_64 Python libraries are incompatible

### Compatibility
- Primary target: macOS Apple Silicon (ARM64)
- Secondary: macOS Intel, Linux (not tested)
- Windows: Not implemented

### Known Warnings (Harmless)
- `ld: warning: search path '/usr/local/opt/openssl@1.1/lib' not found` - From Python's embedded link flags
- CMake deprecation warnings from quill and glad submodules

---

## 7. Next Steps

1. **Test Distribution Build**
   - Run `scripts/package-macos.sh`
   - Verify the created `.app` bundle launches
   - Test Python imports work in bundled mode

2. **Verify Python Packages**
   - Ensure pandas/matplotlib/numpy work in bundled mode
   - Check package sizes and strip unnecessary files

3. **Code Signing (Optional)**
   - Add codesign steps to packaging script
   - Consider notarization for distribution

4. **Documentation**
   - Update README with distribution instructions
   - Document Python package requirements

---

## 8. Open Questions

- **Python version pinning**: Should bundle always use 3.13 or detect from build?
- **Package stripping**: How aggressively to strip bundled Python (tests, pip, etc.)?
- **Linux support**: Different path detection needed (no `.app` bundles)
- **Windows support**: Different executable path detection (`GetModuleFileName`)

---

## 9. Useful Commands

### Development Build (without conda)
```bash
cd build
rm -rf *
env -u CONDA_PREFIX cmake ..
make -j8
./imhotep
```

### Force Specific Python
```bash
cmake -DPython3_EXECUTABLE=/opt/homebrew/bin/python3.13 \
      -DPython3_LIBRARY=/opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/lib/libpython3.13.dylib \
      -DPython3_INCLUDE_DIR=/opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/include/python3.13 \
      ..
```

### Distribution Build (not yet tested)
```bash
chmod +x scripts/package-macos.sh
./scripts/package-macos.sh
open build-release/dist/Imhotep.app
```

### Check Python Architecture
```bash
file /opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/Python
# Should show: arm64
```

### View CMake Python Configuration
```bash
cmake .. 2>&1 | grep -A20 "=== Configuring Python"
```

---

## Related Documentation

- Plan file: `~/.claude/plans/stateful-baking-melody.md`
- Project docs: `CLAUDE.md` (see "macOS Apple Silicon - ARM64 Python Issue" section)
