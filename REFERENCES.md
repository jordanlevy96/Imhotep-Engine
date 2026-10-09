# References & Acknowledgements

This document credits the resources, libraries, and tools that made Imhotep possible.

---

## Learning Resources

### Primary References

- **[LearnOpenGL](https://learnopengl.com/)** - Comprehensive OpenGL tutorials

  - 3D rendering fundamentals
  - Camera systems, lighting, texture mapping
  - Shader programming

- **[Game Programming Patterns](https://gameprogrammingpatterns.com/)** - Design patterns for game development

  - Entity-Component System architecture
  - Object pooling and optimization techniques
  - Game loop patterns

- **[The Cherno (YouTube)](https://www.youtube.com/@TheCherno)** - C++ and game engine development
  - OpenGL series
  - C++ best practices
  - Game engine architecture

---

## External Libraries

### Core Dependencies

- **[GLFW](https://www.glfw.org/)** - Window management and input handling

  - Cross-platform windowing
  - Keyboard/mouse/gamepad input
  - OpenGL context creation

- **[GLAD](https://glad.dav1d.de/)** - OpenGL function loader

  - Multi-platform OpenGL loading
  - Extension management

- **[GLM](https://github.com/g-truc/glm)** - Mathematics library

  - Vector and matrix operations
  - Quaternions for rotations
  - Graphics-oriented math utilities

- **[FreeType](https://freetype.org/)** - Font rendering
  - TrueType/OpenType font loading
  - Glyph rasterization
  - Text rendering backend

### UI & Rendering

- **[litehtml](https://github.com/litehtml/litehtml)** - Lightweight HTML/CSS renderer

  - HTML/CSS parsing and layout
  - Subset of CSS2/CSS3 support
  - Software rendering backend

### Scripting

- **[Lua](https://www.lua.org/)** - Embedded scripting language

  - Game logic scripting
  - UI state management
  - Lightweight (~200KB)

- **[Sol2](https://github.com/ThePhD/sol2)** - C++/Lua binding library

  - Modern C++17 Lua bindings
  - Type-safe function calls
  - Table manipulation

- **[Python](https://www.python.org/)** + **[pybind11](https://github.com/pybind/pybind11)** - Python integration
  - Data export and analysis
  - External tooling support

- **[python-build-standalone](https://github.com/indygreg/python-build-standalone)** - Portable Python distribution
  - Self-contained Python runtime for app bundles
  - Used by `scripts/package-macos.sh` for distribution builds

### Utilities

- **[MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk)** - Official Model Context Protocol SDK, used by the local read-only stdio server (MIT License; pinned in `tools/mcp/requirements.txt`).

- **[yaml-cpp](https://github.com/jbeder/yaml-cpp)** - YAML parsing

  - Configuration file loading
  - Scene serialization

- **[Quill](https://github.com/odygrd/quill)** - High-performance logging
  - Asynchronous logging (12-16μs latency)
  - Multi-threaded safety
  - File and console sinks

---

## AI-Assisted Development

### Claude Code

Portions of this project were developed with assistance from **[Claude Code](https://claude.com/claude-code)** (Anthropic):

- Architecture design and documentation
- Quill logging system integration
- Build system optimization (CMake fixes)
- Documentation structure (CLAUDE.md, CHANGELOG.md, architecture docs)
- Code refactoring and modernization

**Generated code attribution**: While AI-assisted, all code was reviewed, tested, and integrated by the project maintainer. AI suggestions were used as a starting point for implementation and learning.

---

## Inspirational Projects

### UI System Design

- **[Vue.js](https://vuejs.org/)** - Reactive UI patterns

  - v-if, v-for directive inspiration
  - Template interpolation syntax
  - Reactive data binding concepts

- **[Paradox Interactive Games](https://www.paradoxinteractive.com/)** - Target UI complexity
  - Crusader Kings III
  - Europa Universalis V
  - Victoria 3
  - Data-driven, moddable UI systems (Jomini GUI)

---

## Build System & Tooling

- **[CMake](https://cmake.org/)** - Cross-platform build system
- **[Git](https://git-scm.com/)** - Version control
- **[Homebrew](https://brew.sh/)** (macOS) - Package management

---

## Platform & Compiler Support

**Tested Platforms**:

- macOS (Apple Silicon & Intel)
- Ubuntu/Debian Linux
- Windows (limited testing)

**Compilers**:

- Clang (macOS Xcode, LLVM)
- GCC (Linux)
- MSVC (Windows)

---

## Additional Credits

### Community Resources

- **OpenGL documentation** - [docs.gl](https://docs.gl/)
- **C++ reference** - [cppreference.com](https://cppreference.com/)
- **Stack Overflow** - Troubleshooting and best practices

---

## License Information

This project uses external libraries under their respective licenses:

- **GLFW**: Zlib/libpng license
- **GLAD**: MIT License (generated loader)
- **GLM**: MIT License
- **FreeType**: FreeType License (BSD-style)
- **litehtml**: BSD 3-Clause License
- **Lua**: MIT License
- **Sol2**: MIT License
- **pybind11**: BSD 3-Clause License
- **yaml-cpp**: MIT License
- **Quill**: MIT License

- **python-build-standalone**: Zero-Clause BSD License

See individual library repositories for full license texts.

---

_Last Updated: February 13, 2026_
