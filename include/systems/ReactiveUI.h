/**
 * @file ReactiveUI.h
 * @brief Reactive UI system with template rendering and state management
 * @lines ~245
 *
 * Quick-stats (Public API):
 * - RegisterTemplate() - Load HTML template (line ~65)
 * - BindLuaState() - Attach reactive Lua state (line ~75)
 * - RenderWithLua() - Evaluate + render template (line ~85)
 * - DispatchEvent() - Handle UI events (@click, etc.) (line ~95)
 * - LoadTemplateFromFiles() - Load HTML + CSS + Lua (line ~110)
 *
 * Purpose: Glue layer connecting TemplateParser, LuaUIState, and HTMLRendererMT
 * Coordinates rendering flow: dirty check → evaluate → render
 * Implementation: See src/systems/ReactiveUI.cpp (390 lines)
 */

#pragma once

#include "systems/LuaUIState.h"
#include "systems/TemplateParser.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

/**
 * @brief Reactive UI system with change detection and declarative templates
 *
 * Separates UI structure (HTML templates) from data (Lua state), only re-rendering
 * when underlying values change. Supports two rendering modes:
 *
 * **Legacy mode:** Simple {{placeholder}} string substitution
 * - RegisterTemplate() + SetValue() API
 * - Direct C++ value updates
 * - No conditional rendering or loops
 *
 * **Lua mode:** Vue.js-inspired declarative directives
 * - RegisterTemplateWithDirectives() + BindLuaState() API
 * - v-if, v-for, {{expression}} support
 * - Reactive Lua state with change detection
 *
 * **Change detection:**
 * - Tracks dirty flag per state mutation
 * - GetRenderedHTML() skips render if clean
 * - ForceRender() bypasses cache
 *
 * **Usage example (Lua mode):**
 * @code
 * auto& ui = ReactiveUI::GetInstance();
 * auto state = std::make_shared<LuaUIState>();
 * state->LoadStateFile("../res/ui/state/fps.lua");
 * ui.BindLuaState(state);
 * ui.RegisterTemplateWithDirectives("main", "<div v-if='showDebug'>FPS: {{fps}}</div>");
 *
 * // Later in game loop:
 * state->SetValue("fps", currentFPS);  // Marks dirty
 * const std::string& html = ui.GetRenderedHTML();  // Re-renders if dirty
 * @endcode
 *
 * @note Thread-safe singleton - safe to call from main thread only
 * @see docs/architecture/UI_SYSTEM.md for directive syntax and examples
 */
class ReactiveUI
{
public:
    /**
     * @brief Get singleton instance
     * @return Reference to ReactiveUI singleton
     */
    static ReactiveUI &GetInstance()
    {
        static ReactiveUI instance;
        return instance;
    }

    ReactiveUI(ReactiveUI const &) = delete;
    void operator=(ReactiveUI const &) = delete;

    // === Legacy API (simple {{placeholder}} substitution) ===

    /**
     * @brief Register HTML template with placeholder values (legacy mode)
     * @param name Template identifier (currently unused, reserved for multi-template)
     * @param htmlTemplate HTML with {{placeholder}} markers
     * @note Example: "FPS: {{fps}}, FrameTime: {{frameTime}}ms"
     * @note Switches to legacy rendering mode, disables Lua directives
     */
    void RegisterTemplate(const std::string &name, const std::string &htmlTemplate);

    /**
     * @brief Set value for placeholder (legacy mode)
     * @tparam T Value type (must support std::to_string or be std::string)
     * @param key Placeholder name (without braces)
     * @param value New value to substitute
     * @note Only marks dirty if value actually changed (prevents redundant renders)
     * @note Example: SetValue("fps", 60) replaces {{fps}} with "60"
     */
    template <typename T>
    void SetValue(const std::string &key, const T &value);

    // === New Lua-based API (v-if, v-for, {{}} directives) ===

    /**
     * @brief Bind Lua state for reactive data management
     * @param state Shared pointer to LuaUIState instance
     * @note Must be called before RegisterTemplateWithDirectives()
     * @note Switches to Lua rendering mode
     */
    void BindLuaState(std::shared_ptr<LuaUIState> state);

    /** Release the bound Lua state before the scripting VM is finalized. */
    void UnbindLuaState();

    /**
     * @brief Register template with Vue-style directives (Lua mode)
     * @param name Template identifier (currently unused)
     * @param htmlTemplate HTML with v-if, v-for, {{expression}} directives
     * @note Requires BindLuaState() called first
     * @note Parses directives immediately, caches for future renders
     * @see docs/architecture/UI_SYSTEM.md for directive syntax
     */
    void RegisterTemplateWithDirectives(const std::string &name, const std::string &htmlTemplate);

    /**
     * @brief Get reference to bound Lua state
     * @return Shared pointer to LuaUIState (nullptr if not bound)
     * @note Use to update state values: GetLuaState()->SetValue("fps", 60)
     */
    std::shared_ptr<LuaUIState> GetLuaState() { return m_luaState; }

    /**
     * @brief Get event handlers from template parser
     * @return Map of element ID → {eventType → handlerExpression}
     * @note Returns empty map if parser not initialized
     */
    const std::map<std::string, std::map<std::string, std::string>> &GetEventHandlers() const
    {
        static const std::map<std::string, std::map<std::string, std::string>> empty;
        return m_parser ? m_parser->GetEventHandlers() : empty;
    }

    // === Event handling API ===

    /**
     * @brief Event data passed to UI event handlers
     */
    struct EventData
    {
        float x, y;            ///< Mouse coordinates (window space)
        int button;            ///< Mouse button (0=left, 1=right, 2=middle)
        std::string elemId;    ///< Element ID that triggered event
        std::string eventType; ///< Event type (click, mouseover, mouseout, etc.)
    };

    /**
     * @brief Dispatch UI event to Lua handler
     * @param eventType Type of event (click, mouseover, mouseout, mousedown, mouseup)
     * @param handlerExpr Handler expression from @event directive
     * @param eventData Event data (coordinates, button, element ID)
     * @note Parses handler syntax: "method", "method(arg)", "method($event)"
     * @note Calls methods table in Lua state: methods.handlerName(self, ...)
     */
    void DispatchEvent(const std::string &eventType,
                       const std::string &handlerExpr,
                       const EventData &eventData);

    // === Template Loading API ===

    /**
     * @brief Load HTML template with CSS injection from separate files
     * @param templatePath Path to HTML template file (relative to executable)
     * @param cssPath Path to CSS stylesheet file (relative to executable)
     * @return Complete HTML document with CSS injected, or empty string on error
     * @note Replaces <!-- CSS_PLACEHOLDER --> with <style>CSS content</style>
     * @note Example: LoadTemplateFromFiles("../res/ui/templates/tetris.html", "../res/ui/styles/tetris.css")
     */
    static std::string LoadTemplateFromFiles(
        const std::string &templatePath,
        const std::string &cssPath);

    // === Common API ===

    /**
     * @brief Get current rendered HTML (lazy evaluation)
     * @return Reference to rendered HTML string
     * @note Only re-renders if dirty flag is set (change detection)
     * @note Safe to call every frame - caches result when clean
     */
    const std::string &GetRenderedHTML();

    /**
     * @brief Force immediate re-render bypassing dirty check
     * @note Useful for initial render or debugging
     * @note Clears dirty flag after render
     */
    void ForceRender();

private:
    ReactiveUI() = default;

    /**
     * @brief Load text file into string
     * @param path File path (relative to executable)
     * @return File contents, or empty string on error
     * @note Logs error if file cannot be opened
     */
    static std::string LoadTextFile(const std::string &path);

    // Legacy mode members
    std::string m_template;                                ///< HTML template with {{placeholders}}
    std::unordered_map<std::string, std::string> m_values; ///< Placeholder → value map
    std::string m_cachedHTML;                              ///< Last rendered output (shared by both modes)
    bool m_isDirty = true;                                 ///< Re-render needed flag

    // Lua-based mode members
    std::shared_ptr<LuaUIState> m_luaState;   ///< Reactive Lua state (nullptr in legacy mode)
    std::unique_ptr<TemplateParser> m_parser; ///< Directive parser (nullptr in legacy mode)
    bool m_useLuaMode = false;                ///< true = Lua directives, false = legacy placeholders
    std::map<std::string, std::map<std::string, std::string>> m_lastEventHandlers; ///< Cached event handlers to detect changes

    /**
     * @brief Render template with legacy placeholder substitution
     * @note Updates m_cachedHTML and clears m_isDirty
     */
    void RenderTemplate();

    /**
     * @brief Render template with Lua directives (v-if, v-for, {{expr}})
     * @note Updates m_cachedHTML and clears m_isDirty
     */
    void RenderWithLua();
};

// Template implementation
template <typename T>
void ReactiveUI::SetValue(const std::string &key, const T &value)
{
    std::string valueStr = std::to_string(value);

    auto it = m_values.find(key);
    if (it == m_values.end() || it->second != valueStr)
    {
        m_values[key] = valueStr;
        m_isDirty = true;
    }
}

// Specialization for string
template <>
inline void ReactiveUI::SetValue<std::string>(const std::string &key, const std::string &value)
{
    auto it = m_values.find(key);
    if (it == m_values.end() || it->second != value)
    {
        m_values[key] = value;
        m_isDirty = true;
    }
}
