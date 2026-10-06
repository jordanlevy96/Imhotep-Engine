/**
 * @file ScriptManager.h
 * @brief Lua and Python scripting engine integration
 */

#pragma once

#include "util/InputEvent.h"
#include "util/debug.h"
#include "util/Logger.h"
#include <sol/sol.hpp>
#ifdef USE_PYTHON_SCRIPTING
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#endif
#include <iostream>

#ifdef USE_PYTHON_SCRIPTING
namespace py = pybind11;
#endif

static const std::string &EVENT_QUEUE = "EventQueue";     ///< Lua global for input queue
static const std::string &HANDLE_INPUT_F = "HandleInput"; ///< Lua input handler function name

/**
 * @brief Lua C++ bindings registration
 */
namespace LuaBindings
{
    void RegisterEnums(sol::state &lua);     ///< Register InputTypes, CameraDirections, etc.
    void RegisterTypes(sol::state &lua);     ///< Register vec2, vec3, Transform, etc.
    void RegisterFunctions(sol::state &lua); ///< Register Game, Registry, Camera functions
}

/**
 * @brief Dual scripting engine manager (Lua + Python)
 *
 * Manages Lua (Sol2) and Python (pybind11) virtual machines.
 * Both can run simultaneously for different purposes:
 * - **Lua**: Game logic, UI state (primary, ~200KB overhead)
 * - **Python**: Data analysis, tooling (secondary, heavier)
 *
 * **Input handling:**
 * WindowManager → InputEvent queue → ScriptManager::ProcessInput() → Lua/Python handlers
 *
 * **Lua state access:**
 * UI systems use GetLuaState() for reactive state management.
 */
class ScriptManager
{
public:
    /**
     * @brief Get singleton instance
     * @return Reference to ScriptManager singleton
     */
    static ScriptManager &GetInstance()
    {
        static ScriptManager instance;
        return instance;
    };

    ScriptManager(ScriptManager const &) = delete;
    void operator=(ScriptManager const &) = delete;

    /**
     * @brief Initialize Lua and Python VMs with engine bindings
     * @note Registers all C++ types/functions to both languages
     */
    void Initialize();

    /**
     * @brief Shutdown scripting VMs and cleanup
     */
    void Shutdown();

    /**
     * @brief Execute Lua script file
     * @param scriptSrc Path to .lua file
     */
    bool Run(const std::string &scriptSrc);

    /**
     * @brief Create empty Lua table (for event queues, etc.)
     * @param key Global table name
     */
    void CreateList(const std::string &key);

    // Lua-specific methods
    template <typename T>
    void AddToTable(const std::string &tableName, const T &value)
    {
        sol::table table = lua[tableName];
        if (!table.valid())
        {
            LOG_ERROR("Table {} not found in Lua", tableName);
            return;
        }
        LOG_DEBUG("Adding value to Lua table {}", tableName);
        table.add(value);
    };

    template <typename T>
    void SetScriptVar(const std::string &key, const T &value)
    {
        lua[key] = value;
    };

    sol::table GetLuaTable(const std::string &className)
    {
        return lua[className];
    };

#ifdef USE_PYTHON_SCRIPTING
    // Python-specific methods
    template <typename T>
    void AddToList(const std::string &name, const T &value)
    {
        try
        {
            py::object global_namespace = py::globals();
            py::object py_obj = global_namespace[name.c_str()];

            if (py_obj.is_none())
            {
                throw std::runtime_error("List not found in the global namespace");
            }
            if (!py::isinstance<py::list>(py_obj))
            {
                throw std::runtime_error("Object is not a list");
            }

            py::list py_list = py_obj.cast<py::list>();
            py_list.append(value);
        }
        catch (const py::error_already_set &e)
        {
            std::cerr << "Error in accessing or modifying Python list '" << name << "': " << e.what() << std::endl;
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "Runtime error: " << e.what() << std::endl;
        }
    }

    template <typename T>
    void AddToList(const std::string &name, const T &value, const std::string &module)
    {
        try
        {
            py::object module_namespace = ImportModule(module.c_str());
            py::object py_obj = module_namespace.attr(name.c_str());

            if (py_obj.is_none())
            {
                throw std::runtime_error("List not found in the global namespace");
            }
            if (!py::isinstance<py::list>(py_obj))
            {
                throw std::runtime_error("Object is not a list");
            }

            py::list py_list = py_obj.cast<py::list>();
            py_list.append(value);
        }
        catch (const py::error_already_set &e)
        {
            std::cerr << "Error in accessing or modifying Python list '" << name << "': " << e.what() << std::endl;
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "Runtime error: " << e.what() << std::endl;
        }
    }

    py::object ImportModule(const std::string &moduleName);

    void SetClassAttribute(const std::string &className, const std::string &attribute, const std::string &value)
    {
        try
        {
            py::object scriptClass = ImportModule(className);
            scriptClass.attr(attribute.c_str()) = value;
        }
        catch (const py::error_already_set &e)
        {
            std::cerr << "Python error: " << e.what() << std::endl;
        }
    }
#endif

    void ProcessInput();

    /**
     * @brief Add input event to Lua EventQueue as native table
     * @param event The input event to add
     * @note Converts C++ InputEvent to Lua table to avoid Sol2/table.remove issues
     */
    void AddInputEventToQueue(const InputEvent& event);

    // Get reference to Lua state for UI system
    sol::state &GetLuaState() { return lua; }

private:
    ScriptManager() {};

#ifdef USE_PYTHON_SCRIPTING
    /**
     * @brief Configure Python environment before interpreter starts
     * @note MUST be called before py::scoped_interpreter is created
     *       Sets PYTHONHOME/PYTHONPATH for bundled distribution support
     */
    void PreInitializePython();
#endif

    sol::state lua;
#ifdef USE_PYTHON_SCRIPTING
    std::unique_ptr<py::scoped_interpreter> guard;
#endif
};
