cmake_minimum_required(VERSION 3.12)

# ============================================================================
# CMake Policies - Suppress development warnings
# ============================================================================
# CMP0135: Use download timestamp for FetchContent archives (new behavior)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(imhotep VERSION 0.1.0)

# MSVC: conformant preprocessor + large object file support
# /Zc:preprocessor: correct __VA_ARGS__ handling (required by Quill logging macros)
# /bigobj: ScriptManager.cpp exceeds default section limit
if(MSVC)
    add_compile_options(/Zc:preprocessor /bigobj)
    # Export all symbols from core.dll so MSVC generates the import library (.lib)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(IMHOTEP_GAME_NAME "imhotep" CACHE STRING "Output binary name")
set(IMHOTEP_GAME_CONFIG "games/vaporqube/conf/settings.yaml" CACHE STRING "Game config path relative to res/")

# Log verbosity (TraceL3, TraceL2, TraceL1, Debug, Info, Warning, Error, Critical, Off)
set(IMHOTEP_LOG_LEVEL "Info" CACHE STRING "Default log verbosity for Quill")

# Generate version header from CMake version
configure_file(
    "${CMAKE_SOURCE_DIR}/include/util/Version.h.in"
    "${CMAKE_SOURCE_DIR}/include/util/Version.h"
    @ONLY
)

add_library(core SHARED
    "src/Camera.cpp"
    "src/controllers/EngineCore.cpp"
    "src/controllers/Game.cpp"
    "src/controllers/Registry.cpp"
    "src/controllers/ResourceManager.cpp"
    "src/controllers/ScriptManager.cpp"
    "src/controllers/WindowManager.cpp"
    "src/systems/HierarchySystem.cpp"
    "src/systems/RenderSystem.cpp"
    "src/systems/ScriptSystem.cpp"
    "src/systems/TweenSystem.cpp"
    "src/systems/HTMLRendererMT.cpp"
    "src/systems/ReactiveUI.cpp"
    "src/systems/LuaUIState.cpp"
    "src/systems/ExpressionCache.cpp"
    "src/systems/TemplateParser.cpp"
    "src/systems/SceneModule.cpp"
    "src/systems/SceneContext.cpp"
    "src/systems/SceneLoader.cpp"
    "src/systems/TiledBackgroundRenderer.cpp"
    "src/systems/SkyBackgroundRenderer.cpp"
    "src/systems/SplashScreen.cpp"
    "src/util/ConfigLoader.cpp"
    "src/util/FileIO.cpp"
    "src/util/PathResolver.cpp"
    "src/util/FrameTiming.cpp"
    "src/util/Logger.cpp"
    "src/util/Mesh.cpp"
    "src/util/SceneTraversal.cpp"
    "src/util/Shader.cpp"
    "src/util/TransformUtils.cpp"
    "external/glad/src/glad.c"
)

target_compile_definitions(core PUBLIC IMHOTEP_LOG_LEVEL_STR="${IMHOTEP_LOG_LEVEL}")

set(OpenGL_GL_PREFERENCE "GLVND")
find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)

# Enable PIC for all submodules (required on Linux where they link into core.so)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(external/yaml-cpp)
add_subdirectory(external/sol2)
add_subdirectory(external/litehtml)

# Quill logging library (all platforms)
include(FetchContent)
set(QUILL_DISABLE_NON_PREFIXED_MACROS ON CACHE BOOL "Disable Quill's non-prefixed LOG_* macros" FORCE)
FetchContent_Declare(
    quill
    GIT_REPOSITORY https://github.com/odygrd/quill.git
    GIT_TAG        v11.0.2
)
FetchContent_MakeAvailable(quill)

if (APPLE)
    FetchContent_Declare(
        GLFW
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(GLFW)

elseif(WIN32)
    FetchContent_Declare(
        GLFW
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(GLFW)

else() # Linux
    FetchContent_Declare(
        GLFW
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(GLFW)
    target_link_libraries(core PRIVATE dl)
endif()

set(core_INCLUDE_DIRS
    include
    external/headers
    external/glm
    external/glad/include
    external/yaml-cpp/include
    external/litehtml/include
    ${GLFW_DIR}
    ${FREETYPE_INCLUDE_DIRS}
)

set(core_LINKS
    glfw
    yaml-cpp::yaml-cpp
    litehtml
    quill::quill
    ${OPENGL_LIBRARIES}
    ${FREETYPE_LIBRARIES}
)

target_include_directories(core PUBLIC ${core_INCLUDE_DIRS})
target_link_libraries(core PUBLIC ${core_LINKS})

# ============================================================================
# Lua Scripting - Game logic and UI
# ============================================================================
message(STATUS "Configuring Lua scripting")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build as static library" FORCE)
add_subdirectory(external/lua-cmake)
target_include_directories(core PUBLIC
    external/lua-cmake
    external/lua-cmake/upstream
    external/sol2/include/sol
)
target_link_libraries(core PUBLIC lua sol2)
target_compile_definitions(core PUBLIC USE_LUA_SCRIPTING)

# ============================================================================
# Python Scripting - Data analytics, etc.
# ============================================================================
option(IMHOTEP_ENABLE_PYTHON "Enable Python scripting" ON)

if(IMHOTEP_ENABLE_PYTHON)
# NOTE: You may see linker warnings about '/usr/local/opt/openssl@1.1/lib' not found.
# This is harmless - it comes from the Python library's embedded link flags and
# the path may not exist on all systems. The warning can be ignored.
#
message(STATUS "")
message(STATUS "=== Configuring Python Scripting ===")

# Detect target architecture
if(APPLE)
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE IMHOTEP_HOST_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "Host architecture: ${IMHOTEP_HOST_ARCH}")
endif()

# Find Python - priority order:
# 1. Explicitly specified Python3_EXECUTABLE (cmake -DPython3_EXECUTABLE=...)
# 2. Conda environment (if CONDA_PREFIX set)
# 3. Homebrew ARM64 Python (on Apple Silicon)
# 4. System Python (find_package fallback)

set(CONDA_PREFIX $ENV{CONDA_PREFIX})
set(IMHOTEP_PYTHON_SOURCE "unknown")

if(Python3_EXECUTABLE)
    # User explicitly specified Python - use it
    message(STATUS "Using user-specified Python: ${Python3_EXECUTABLE}")
    set(IMHOTEP_PYTHON_SOURCE "user-specified")

elseif(CONDA_PREFIX)
    # Conda environment active
    message(STATUS "Conda environment detected: ${CONDA_PREFIX}")
    set(Python3_ROOT_DIR "${CONDA_PREFIX}")
    set(Python3_EXECUTABLE "${CONDA_PREFIX}/bin/python")
    set(IMHOTEP_PYTHON_SOURCE "conda")

elseif(APPLE AND IMHOTEP_HOST_ARCH STREQUAL "arm64")
    # Apple Silicon without conda - look for Homebrew ARM64 Python
    message(STATUS "Apple Silicon detected, searching for ARM64 Python...")

    # Common Homebrew ARM64 Python locations
    set(HOMEBREW_PYTHON_VERSIONS "3.13" "3.12" "3.11")
    set(HOMEBREW_PYTHON_FOUND FALSE)

    foreach(PYVER IN LISTS HOMEBREW_PYTHON_VERSIONS)
        set(HOMEBREW_PYTHON_PATH "/opt/homebrew/opt/python@${PYVER}")
        set(HOMEBREW_PYTHON_EXE "${HOMEBREW_PYTHON_PATH}/bin/python${PYVER}")
        set(HOMEBREW_PYTHON_FW "${HOMEBREW_PYTHON_PATH}/Frameworks/Python.framework/Versions/${PYVER}")

        if(EXISTS "${HOMEBREW_PYTHON_EXE}" AND EXISTS "${HOMEBREW_PYTHON_FW}/include/python${PYVER}")
            message(STATUS "Found Homebrew Python ${PYVER} at: ${HOMEBREW_PYTHON_PATH}")

            # Verify it's actually ARM64
            execute_process(
                COMMAND file "${HOMEBREW_PYTHON_FW}/Python"
                OUTPUT_VARIABLE PYTHON_ARCH_CHECK
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            if(PYTHON_ARCH_CHECK MATCHES "arm64")
                message(STATUS "Verified: Python ${PYVER} is ARM64")
                set(Python3_EXECUTABLE "${HOMEBREW_PYTHON_EXE}")
                set(Python3_LIBRARY "${HOMEBREW_PYTHON_FW}/lib/libpython${PYVER}.dylib")
                set(Python3_INCLUDE_DIR "${HOMEBREW_PYTHON_FW}/include/python${PYVER}")
                set(Python3_ROOT_DIR "${HOMEBREW_PYTHON_PATH}")
                set(HOMEBREW_PYTHON_FOUND TRUE)
                set(IMHOTEP_PYTHON_SOURCE "homebrew-arm64")
                break()
            else()
                message(STATUS "Skipping: Python ${PYVER} at ${HOMEBREW_PYTHON_PATH} is not ARM64")
            endif()
        endif()
    endforeach()

    if(NOT HOMEBREW_PYTHON_FOUND)
        message(WARNING "")
        message(WARNING "=== ARM64 Python Not Found ===")
        message(WARNING "On Apple Silicon, you need ARM64 Python. Install with:")
        message(WARNING "  brew install python@3.13")
        message(WARNING "")
        message(WARNING "Or specify Python explicitly:")
        message(WARNING "  cmake -DPython3_EXECUTABLE=/path/to/python3 ..")
        message(WARNING "")
        message(WARNING "Falling back to system Python (may cause architecture mismatch)...")
        set(IMHOTEP_PYTHON_SOURCE "system-fallback")
    endif()

else()
    message(STATUS "Using system Python (find_package)")
    set(IMHOTEP_PYTHON_SOURCE "system")
endif()

# Now find_package will use the hints we've set above
find_package(Python3 COMPONENTS Interpreter Development Development.Embed REQUIRED)

# Verify architecture match on Apple Silicon
if(APPLE AND IMHOTEP_HOST_ARCH STREQUAL "arm64")
    execute_process(
        COMMAND file "${Python3_LIBRARIES}"
        OUTPUT_VARIABLE PYTHON_LIB_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(PYTHON_LIB_ARCH MATCHES "x86_64" AND NOT PYTHON_LIB_ARCH MATCHES "arm64")
        message(FATAL_ERROR "")
        message(FATAL_ERROR "=== ARCHITECTURE MISMATCH ===")
        message(FATAL_ERROR "Found x86_64 Python but building for ARM64!")
        message(FATAL_ERROR "Python library: ${Python3_LIBRARIES}")
        message(FATAL_ERROR "")
        message(FATAL_ERROR "Solutions:")
        message(FATAL_ERROR "1. Install ARM64 Python: brew install python@3.13")
        message(FATAL_ERROR "2. Specify ARM64 Python explicitly:")
        message(FATAL_ERROR "   cmake -DPython3_EXECUTABLE=/opt/homebrew/bin/python3.13 \\")
        message(FATAL_ERROR "         -DPython3_LIBRARY=/opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/lib/libpython3.13.dylib \\")
        message(FATAL_ERROR "         -DPython3_INCLUDE_DIR=/opt/homebrew/opt/python@3.13/Frameworks/Python.framework/Versions/3.13/include/python3.13 \\")
        message(FATAL_ERROR "         ..")
        message(FATAL_ERROR "3. Use a conda environment with ARM64 Python")
        message(FATAL_ERROR "")
    endif()
endif()

# Try to find pybind11 as a system package first, fall back to submodule
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
    message(STATUS "pybind11: using submodule (not found in system)")
    add_subdirectory(external/pybind11)
else()
    message(STATUS "pybind11: using system installation at ${pybind11_DIR}")
endif()

# Summary
message(STATUS "")
message(STATUS "Python Configuration Summary:")
message(STATUS "  Source:       ${IMHOTEP_PYTHON_SOURCE}")
message(STATUS "  Executable:   ${Python3_EXECUTABLE}")
message(STATUS "  Version:      ${Python3_VERSION}")
message(STATUS "  Include dirs: ${Python3_INCLUDE_DIRS}")
message(STATUS "  Libraries:    ${Python3_LIBRARIES}")
message(STATUS "")

# pybind11's add_subdirectory already sets up include paths
target_include_directories(core PUBLIC ${Python3_INCLUDE_DIRS})
target_link_libraries(core PUBLIC pybind11::embed ${Python3_LIBRARIES})
target_compile_definitions(core PUBLIC USE_PYTHON_SCRIPTING)

# Create minimal Python module for data export
pybind11_add_module(engine src/controllers/python/engine_bindings.cpp)
target_link_libraries(engine PUBLIC core)

endif() # IMHOTEP_ENABLE_PYTHON

add_executable(imhotep src/main.cpp)
set_target_properties(imhotep PROPERTIES OUTPUT_NAME "${IMHOTEP_GAME_NAME}")
target_compile_definitions(imhotep PRIVATE IMHOTEP_GAME_CONFIG="${IMHOTEP_GAME_CONFIG}")
target_link_libraries(imhotep PRIVATE core)

# Editor executable
add_executable(imhotep-editor
    "src/editor/main.cpp"
    "src/editor/Editor.cpp"
    "src/editor/SceneViewport.cpp"
    "src/editor/UITemplateManager.cpp"
)
target_link_libraries(imhotep-editor PRIVATE core)
target_include_directories(imhotep-editor PRIVATE ${core_INCLUDE_DIRS})

# ============================================================================
# Automated Tests
# ============================================================================
option(IMHOTEP_BUILD_TESTS "Build automated tests (CTests)" ON)

if(IMHOTEP_BUILD_TESTS)
    enable_testing()

    add_executable(imhotep-game-tests
        "tests/GameLuaTests.cpp"
    )
    target_include_directories(imhotep-game-tests PRIVATE
        external/sol2/include
        external/lua-cmake/upstream
    )
    target_link_libraries(imhotep-game-tests PRIVATE
        lua
        sol2
    )
    target_compile_definitions(imhotep-game-tests PRIVATE
        IMHOTEP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    add_test(
        NAME vaporqube.lua.behavior
        COMMAND imhotep-game-tests
    )

    # Engine C++ unit tests (headless — ExpressionCache + FrameTiming)
    add_executable(imhotep-engine-tests
        "tests/EngineUnitTests.cpp"
        "src/systems/ExpressionCache.cpp"
        "src/systems/SceneModule.cpp"
        "src/util/FrameTiming.cpp"
        "src/util/Logger.cpp"
    )
    target_include_directories(imhotep-engine-tests PRIVATE
        include
        external/sol2/include
        external/lua-cmake/upstream
    )
    target_link_libraries(imhotep-engine-tests PRIVATE
        lua
        sol2
        quill::quill
    )
    target_compile_definitions(imhotep-engine-tests PRIVATE
        IMHOTEP_LOG_LEVEL_STR="${IMHOTEP_LOG_LEVEL}"
        IMHOTEP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    add_test(
        NAME engine.unit
        COMMAND imhotep-engine-tests
    )

    add_executable(imhotep-registry-liveness-tests
        "tests/RegistryLivenessTests.cpp"
    )
    target_link_libraries(imhotep-registry-liveness-tests PRIVATE core)

    add_test(
        NAME engine.registry-liveness
        COMMAND imhotep-registry-liveness-tests
    )

    # Engine smoke test: boots full engine, runs 10 frames, exits
    add_test(
        NAME engine.smoke
        COMMAND imhotep --smoke-test
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(engine.smoke PROPERTIES TIMEOUT 30)

    # Engine click test: simulates START GAME button click, verifies state change
    add_test(
        NAME engine.click
        COMMAND imhotep --click-test
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(engine.click PROPERTIES TIMEOUT 30)
endif()

# ============================================================================
# Installation Configuration (for distributable builds)
# ============================================================================
# CMake install() commands define where files go when running:
#   cmake --install . --prefix /path/to/install
#
# For macOS app bundles, the structure is:
#   Imhotep.app/Contents/MacOS/imhotep      (executable)
#   Imhotep.app/Contents/Frameworks/        (dylibs)
#   Imhotep.app/Contents/Resources/         (assets, bundled Python)

# --- macOS App Bundle Properties ---
# Only enable bundle mode when building for distribution (not during development)
option(IMHOTEP_BUILD_BUNDLE "Build macOS app bundle (for distribution)" OFF)

if(APPLE AND IMHOTEP_BUILD_BUNDLE)
    set_target_properties(imhotep PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${IMHOTEP_GAME_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # INSTALL_RPATH tells the executable where to find dylibs at runtime
        # @executable_path = directory containing the executable
        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

    set_target_properties(core PROPERTIES
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

    # Engine Python module needs rpath for finding core library
    if(IMHOTEP_ENABLE_PYTHON)
        set_target_properties(engine PROPERTIES
            INSTALL_RPATH "@loader_path"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endif()

# Linux RPATH
if(NOT APPLE AND NOT WIN32)
    set_target_properties(imhotep PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH TRUE)
    set_target_properties(core PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# --- Install Targets ---
if(APPLE AND IMHOTEP_BUILD_BUNDLE)
    # macOS .app bundle layout
    install(TARGETS imhotep
        BUNDLE DESTINATION .
    )

    install(TARGETS core
        LIBRARY DESTINATION ${IMHOTEP_GAME_NAME}.app/Contents/Frameworks
    )

    if(IMHOTEP_ENABLE_PYTHON)
        install(TARGETS engine
            LIBRARY DESTINATION ${IMHOTEP_GAME_NAME}.app/Contents/Frameworks
        )
    endif()

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/res/
        DESTINATION ${IMHOTEP_GAME_NAME}.app/Contents/Resources/res
        PATTERN ".DS_Store" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
    )
else()
    # Windows/Linux flat directory layout
    install(TARGETS imhotep
        RUNTIME DESTINATION ${IMHOTEP_GAME_NAME}
    )

    install(TARGETS core
        LIBRARY DESTINATION ${IMHOTEP_GAME_NAME}
        RUNTIME DESTINATION ${IMHOTEP_GAME_NAME}
    )

    if(IMHOTEP_ENABLE_PYTHON)
        install(TARGETS engine
            LIBRARY DESTINATION ${IMHOTEP_GAME_NAME}
            RUNTIME DESTINATION ${IMHOTEP_GAME_NAME}
        )
    endif()

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/res/
        DESTINATION ${IMHOTEP_GAME_NAME}/res
        PATTERN ".DS_Store" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
    )
endif()

# --- Optional: Bundled Python ---
option(IMHOTEP_BUNDLE_PYTHON "Bundle Python runtime for distribution" OFF)
set(IMHOTEP_PYTHON_BUNDLE_PATH "" CACHE PATH "Path to portable Python installation")

if(IMHOTEP_ENABLE_PYTHON AND IMHOTEP_BUNDLE_PYTHON AND EXISTS "${IMHOTEP_PYTHON_BUNDLE_PATH}")
    message(STATUS "Bundling Python from: ${IMHOTEP_PYTHON_BUNDLE_PATH}")
    if(APPLE AND IMHOTEP_BUILD_BUNDLE)
        install(DIRECTORY ${IMHOTEP_PYTHON_BUNDLE_PATH}/
            DESTINATION ${IMHOTEP_GAME_NAME}.app/Contents/Resources/python
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
            PATTERN "test" EXCLUDE
            PATTERN "tests" EXCLUDE
            PATTERN "pip*" EXCLUDE
        )
    else()
        install(DIRECTORY ${IMHOTEP_PYTHON_BUNDLE_PATH}/
            DESTINATION ${IMHOTEP_GAME_NAME}/python
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
            PATTERN "test" EXCLUDE
            PATTERN "tests" EXCLUDE
            PATTERN "pip*" EXCLUDE
        )
    endif()
endif()
