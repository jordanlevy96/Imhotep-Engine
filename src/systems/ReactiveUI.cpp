/**
 * @file ReactiveUI.cpp
 * @brief Reactive UI singleton managing HTML templates with Lua state bindings
 * @lines ~390
 *
 * Purpose: Coordinates HTML template rendering with Lua state management.
 * Acts as glue between TemplateParser (directives), LuaUIState (data), and HTMLRendererMT (display).
 *
 * Key functions:
 * - RegisterTemplate() - Load HTML template (line 11, ~30 lines)
 * - BindLuaState() - Attach Lua state for reactive updates (line 82, ~5 lines)
 * - RenderWithLua() - Evaluate template + render via HTMLRendererMT (line 113, ~25 lines)
 * - DispatchEvent() - Handle UI events (@click, etc.) (line 140, ~185 lines)
 * - LoadTemplateFromFiles() - Load HTML + CSS + Lua state (line 338, ~50 lines)
 *
 * Rendering flow:
 * 1. Check if LuaUIState is dirty (IsDirty())
 * 2. If dirty: TemplateParser::Evaluate(state) → HTML string
 * 3. Pass HTML to HTMLRendererMT for multi-threaded rendering
 * 4. Clear dirty flag after render
 *
 * Event handling:
 * - Parses @click, @keydown, etc. from HTML
 * - Executes Lua functions in response to user input
 * - Coordinates with WindowManager for mouse/keyboard state
 *
 * Integration: Central hub connecting UI components (Parser, State, Renderer)
 */

#include "systems/ReactiveUI.h"
#include "controllers/ScriptManager.h"
#include "systems/HTMLRendererMT.h"
#include "util/Logger.h"
#include <fstream>
#include <sstream>

// === Legacy API Implementation ===
// TODO: Remove legacy API?

void ReactiveUI::RegisterTemplate(const std::string &name, const std::string &htmlTemplate)
{
    m_template = htmlTemplate;
    m_isDirty = true;
    LOG_INFO("[ReactiveUI] Template registered: {}", name);
}

const std::string &ReactiveUI::GetRenderedHTML()
{
    if (m_useLuaMode)
    {
        // Lua-based rendering: check if Lua state is dirty
        if (m_luaState && m_luaState->IsDirty())
        {
            RenderWithLua();
            m_luaState->ClearDirty();
        }
    }
    else
    {
        // Legacy rendering: check if local dirty flag is set
        // TODO: Remove legacy mode?
        if (m_isDirty)
        {
            RenderTemplate();
            m_isDirty = false;
        }
    }
    return m_cachedHTML;
}

void ReactiveUI::ForceRender()
{
    if (m_useLuaMode)
    {
        RenderWithLua();
        if (m_luaState)
        {
            m_luaState->ClearDirty();
        }
    }
    else
    {
        m_isDirty = true;
        RenderTemplate();
        m_isDirty = false;
    }
}

void ReactiveUI::RenderTemplate()
{
    LOG_TRACE_L2("[ReactiveUI] Rendering template (legacy mode, dirty)");

    m_cachedHTML = m_template;

    // Replace all {{key}} placeholders with their values
    for (const auto &[key, value] : m_values)
    {
        std::string placeholder = "{{" + key + "}}";
        size_t pos = 0;

        while ((pos = m_cachedHTML.find(placeholder, pos)) != std::string::npos)
        {
            m_cachedHTML.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
}

// === Lua-based API Implementation ===

void ReactiveUI::BindLuaState(std::shared_ptr<LuaUIState> state)
{
    m_luaState = state;
    m_useLuaMode = true;
    LOG_INFO("[ReactiveUI] Lua state bound, switching to Lua mode");
}

void ReactiveUI::UnbindLuaState()
{
    m_luaState.reset();
    m_useLuaMode = false;
}

void ReactiveUI::RegisterTemplateWithDirectives(const std::string &name, const std::string &htmlTemplate)
{
    if (!m_luaState)
    {
        LOG_ERROR("[ReactiveUI] Must bind Lua state before registering template with directives");
        return;
    }

    // Initialize parser if not already created
    if (!m_parser)
    {
        m_parser = std::make_unique<TemplateParser>();
    }

    // Parse the template and cache the parsed structure
    m_parser->Parse(htmlTemplate);
    m_useLuaMode = true;

    LOG_INFO("[ReactiveUI] Template with directives registered: {}", name);

    // Force initial render
    ForceRender();
}

void ReactiveUI::RenderWithLua()
{
    if (!m_parser || !m_luaState)
    {
        LOG_ERROR("[ReactiveUI] Parser or Lua state not initialized");
        return;
    }

    LOG_TRACE_L2("[ReactiveUI] Rendering template (Lua mode, dirty)");

    // Use TemplateParser to evaluate directives with current Lua state
    m_cachedHTML = m_parser->Evaluate(*m_luaState);

    // Only update event handlers if they changed (avoids churn on data-only updates)
    const auto &newHandlers = m_parser->GetEventHandlers();
    HTMLRendererMT &htmlRenderer = HTMLRendererMT::GetInstance();

    if (newHandlers != m_lastEventHandlers)
    {
        htmlRenderer.SetEventHandlers(newHandlers);
        m_lastEventHandlers = newHandlers;
        LOG_TRACE_L2("[ReactiveUI] Updated {} event handlers after render", newHandlers.size());
    }
}

// === Event Handling Implementation ===

void ReactiveUI::DispatchEvent(const std::string &eventType,
                               const std::string &handlerExpr,
                               const EventData &eventData)
{
    if (!m_luaState || !m_luaState->IsReady())
    {
        LOG_ERROR("[ReactiveUI] Lua state not ready for event dispatch");
        return;
    }

    // Log only errors, not every event dispatch

    // Parse handler expression: "methodName" or "methodName(args)" or "methodName($event)"
    std::string handlerName;
    std::vector<std::string> args;

    size_t parenPos = handlerExpr.find('(');
    if (parenPos == std::string::npos)
    {
        // Simple handler: "methodName"
        handlerName = handlerExpr;
    }
    else
    {
        // Handler with args: "methodName(arg1, arg2)"
        handlerName = handlerExpr.substr(0, parenPos);

        size_t closePos = handlerExpr.find(')');
        if (closePos != std::string::npos)
        {
            std::string argsStr = handlerExpr.substr(parenPos + 1, closePos - parenPos - 1);

            // Simple arg parsing (handles single arg for now)
            if (!argsStr.empty())
            {
                // Trim whitespace
                size_t start = argsStr.find_first_not_of(" \t");
                size_t end = argsStr.find_last_not_of(" \t");
                if (start != std::string::npos)
                {
                    args.push_back(argsStr.substr(start, end - start + 1));
                }
            }
        }
    }

    // Get methods table from Lua state
    sol::object methodsObj = m_luaState->GetValue("methods");
    if (!methodsObj.is<sol::table>())
    {
        LOG_ERROR("[ReactiveUI] {} event dispatch failed: No 'methods' table found in Lua state (handler: '{}')",
                  eventType, handlerExpr);
        return;
    }

    sol::table methods = methodsObj.as<sol::table>();
    sol::object handlerObj = methods[handlerName];
    if (!handlerObj.valid() || handlerObj.get_type() == sol::type::nil)
    {
        LOG_ERROR("[ReactiveUI] {} event dispatch failed: Handler '{}' not found in methods table (elem: '{}')",
                  eventType, handlerName, eventData.elemId);
        return;
    }

    if (handlerObj.get_type() != sol::type::function)
    {
        LOG_ERROR("[ReactiveUI] {} event dispatch failed: Handler '{}' is not a function (type: {}) (elem: '{}')",
                  eventType, handlerName, static_cast<int>(handlerObj.get_type()), eventData.elemId);
        return;
    }

    sol::protected_function handler = handlerObj.as<sol::protected_function>();
    sol::table stateTable = m_luaState->GetStateTable();

    // Call handler with appropriate arguments
    sol::protected_function_result result;
    try
    {
        if (args.empty())
        {
            // No args: handler(self)
            result = handler(stateTable);
        }
        else if (args[0] == "$event")
        {
            // Event object: handler(self, event)
            // Create event table using ScriptManager's Lua state
            sol::state &lua = ScriptManager::GetInstance().GetLuaState();
            sol::table eventTable = lua.create_table();
            eventTable["x"] = eventData.x;
            eventTable["y"] = eventData.y;
            eventTable["button"] = eventData.button;
            eventTable["elemId"] = eventData.elemId;
            eventTable["type"] = eventData.eventType;
            result = handler(stateTable, eventTable);
        }
        else
        {
            // Literal arg: handler(self, arg)
            // Try to evaluate as Lua expression in the state table context
            sol::object argValue;
            bool evaluatedSuccessfully = false;

            // Try to evaluate as Lua expression (e.g., "entity.id", "uiEditor.newTemplateName") within the state table context
            try
            {
                // First try to get it directly from state table (for simple keys)
                sol::object result_obj = stateTable[args[0]];
                if (result_obj.valid() && result_obj.get_type() != sol::type::nil)
                {
                    argValue = result_obj;
                    evaluatedSuccessfully = true;
                    LOG_TRACE_L3("[ReactiveUI] Got arg '{}' directly from state table", args[0]);
                }
                else
                {
                    // Not a direct property, try to evaluate as Lua chunk with state table as environment
                    std::string luaCode = "return " + args[0];
                    sol::state &lua = ScriptManager::GetInstance().GetLuaState();

                    // Load and execute the expression with state table as environment
                    sol::load_result loadResult = lua.load(luaCode);
                    if (loadResult.valid())
                    {
                        // NOTE: loadResult is already a callable chunk; executing it returns the expression result.
                        // Do NOT call loadResult() here expecting a function return value.
                        sol::protected_function func = loadResult;

                        // Set state table as environment so expressions like "data.uiEditor.newTemplateName" work
                        sol::environment env(lua, sol::create, stateTable);
                        sol::set_environment(env, func);
                        
                        sol::protected_function_result scriptResult = func();
                        if (scriptResult.valid())
                        {
                            argValue = scriptResult.get<sol::object>();
                            evaluatedSuccessfully = true;
                            LOG_TRACE_L3("[ReactiveUI] Evaluated arg '{}' as Lua expression", args[0]);
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                LOG_DEBUG("[ReactiveUI] Failed to evaluate '{}' as Lua expression: {}", args[0], e.what());
            }

            if (evaluatedSuccessfully && argValue.valid())
            {
                // Use the evaluated value
                result = handler(stateTable, argValue);
            }
            else
            {
                // Fall back to parsing as number or string literal
                try
                {
                    int numArg = std::stoi(args[0]);
                    result = handler(stateTable, numArg);
                }
                catch (...)
                {
                    // Not a number, use as string (remove quotes if present)
                    std::string strArg = args[0];
                    if (!strArg.empty() && (strArg.front() == '\'' || strArg.front() == '"'))
                    {
                        strArg = strArg.substr(1, strArg.length() - 2);
                    }
                    result = handler(stateTable, strArg);
                }
            }
        }

        if (!result.valid())
        {
            sol::error err = result;
            LOG_ERROR("[ReactiveUI] Lua handler '{}' failed: {}", handlerName, err.what());
            return;
        }

        // Handler executed successfully. Mark state dirty so the next GetRenderedHTML() re-evaluates the template.
        // Special-case noop handlers used for event consumption only.
        if (handlerName != "noop")
        {
            m_luaState->MarkDirty();
        }
    }
    catch (const sol::error &e)
    {
        LOG_ERROR("[ReactiveUI] Exception calling handler '{}': {}", handlerName, e.what());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[ReactiveUI] Exception calling handler '{}': {}", handlerName, e.what());
    }
}

// === Template Loading Implementation ===

std::string ReactiveUI::LoadTextFile(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        LOG_ERROR("[ReactiveUI] Failed to open file: {}", path);
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string ReactiveUI::LoadTemplateFromFiles(
    const std::string &templatePath,
    const std::string &cssPath)
{
    // Load CSS
    std::string cssContent = LoadTextFile(cssPath);
    if (cssContent.empty())
    {
        LOG_ERROR("[ReactiveUI] Failed to load CSS from: {}", cssPath);
        return "";
    }

    // Load HTML template
    std::string htmlTemplate = LoadTextFile(templatePath);
    if (htmlTemplate.empty())
    {
        LOG_ERROR("[ReactiveUI] Failed to load template from: {}", templatePath);
        return "";
    }

    // Inject CSS into template
    const std::string placeholder = "<!-- CSS_PLACEHOLDER -->";
    size_t pos = htmlTemplate.find(placeholder);

    if (pos == std::string::npos)
    {
        LOG_WARNING("[ReactiveUI] CSS placeholder not found in template, appending to <head>");
        // Fallback: inject before </head>
        size_t headEnd = htmlTemplate.find("</head>");
        if (headEnd != std::string::npos)
        {
            std::string styleTag = "    <style>\n" + cssContent + "    </style>\n";
            htmlTemplate.insert(headEnd, styleTag);
        }
        else
        {
            LOG_ERROR("[ReactiveUI] Invalid HTML structure - no </head> tag found");
            return "";
        }
    }
    else
    {
        // Replace placeholder with CSS
        std::string styleTag = "<style>\n" + cssContent + "    </style>";
        htmlTemplate.replace(pos, placeholder.length(), styleTag);
    }

    LOG_INFO("[ReactiveUI] Loaded template ({} bytes HTML, {} bytes CSS)",
             htmlTemplate.size(), cssContent.size());

    return htmlTemplate;
}
