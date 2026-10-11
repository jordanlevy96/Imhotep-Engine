/**
 * @file WindowManager.cpp
 * @brief GLFW window and input management singleton
 * @lines ~480
 *
 * Purpose: Manages application window, OpenGL context, and input routing.
 * Centralizes all GLFW interactions for window lifecycle and event callbacks.
 *
 * Key functions:
 * - Initialize() - Create GLFW window, setup OpenGL context (line 155, ~65 lines)
 * - Shutdown() - Clean up GLFW resources (line 222, ~5 lines)
 * - key_callback() - Route keyboard events to handlers (line 272, ~25 lines)
 * - click_callback() - Route mouse button events (line 300, ~50 lines)
 * - cursorPos_callback() - Route mouse movement events (line 352, ~25 lines)
 * - scroll_callback() - Route scroll wheel events (line 410, ~20 lines)
 * - char_callback() - Route text input events (line 432, ~15 lines)
 * - resize_callback() - Handle window resize (line 380, ~30 lines)
 *
 * Input handler system:
 * - RegisterInputHandler() adds listeners for keyboard/mouse events
 * - Events routed to handlers in reverse registration order (latest has priority)
 * - Handlers return true to consume event, false to pass through
 *
 * Integration: Used by all systems needing window/input access (Game, Editor, ReactiveUI)
 */

#include "controllers/WindowManager.h"
#include "controllers/ScriptManager.h"
#include "controllers/Game.h"

#include <iostream>

std::unordered_map<int, std::string> keyMap = {
    // Alphabet
    {GLFW_KEY_A, "A"},
    {GLFW_KEY_B, "B"},
    {GLFW_KEY_C, "C"},
    {GLFW_KEY_D, "D"},
    {GLFW_KEY_E, "E"},
    {GLFW_KEY_F, "F"},
    {GLFW_KEY_G, "G"},
    {GLFW_KEY_H, "H"},
    {GLFW_KEY_I, "I"},
    {GLFW_KEY_J, "J"},
    {GLFW_KEY_K, "K"},
    {GLFW_KEY_L, "L"},
    {GLFW_KEY_M, "M"},
    {GLFW_KEY_N, "N"},
    {GLFW_KEY_O, "O"},
    {GLFW_KEY_P, "P"},
    {GLFW_KEY_Q, "Q"},
    {GLFW_KEY_R, "R"},
    {GLFW_KEY_S, "S"},
    {GLFW_KEY_T, "T"},
    {GLFW_KEY_U, "U"},
    {GLFW_KEY_V, "V"},
    {GLFW_KEY_W, "W"},
    {GLFW_KEY_X, "X"},
    {GLFW_KEY_Y, "Y"},
    {GLFW_KEY_Z, "Z"},

    // Numbers
    {GLFW_KEY_0, "0"},
    {GLFW_KEY_1, "1"},
    {GLFW_KEY_2, "2"},
    {GLFW_KEY_3, "3"},
    {GLFW_KEY_4, "4"},
    {GLFW_KEY_5, "5"},
    {GLFW_KEY_6, "6"},
    {GLFW_KEY_7, "7"},
    {GLFW_KEY_8, "8"},
    {GLFW_KEY_9, "9"},

    // Function Keys
    {GLFW_KEY_F1, "F1"},
    {GLFW_KEY_F2, "F2"},
    {GLFW_KEY_F3, "F3"},
    {GLFW_KEY_F4, "F4"},
    {GLFW_KEY_F5, "F5"},
    {GLFW_KEY_F6, "F6"},
    {GLFW_KEY_F7, "F7"},
    {GLFW_KEY_F8, "F8"},
    {GLFW_KEY_F9, "F9"},
    {GLFW_KEY_F10, "F10"},
    {GLFW_KEY_F11, "F11"},
    {GLFW_KEY_F12, "F12"},
    {GLFW_KEY_F13, "F13"},
    {GLFW_KEY_F14, "F14"},
    {GLFW_KEY_F15, "F15"},
    {GLFW_KEY_F16, "F16"},
    {GLFW_KEY_F17, "F17"},
    {GLFW_KEY_F18, "F18"},
    {GLFW_KEY_F19, "F19"},
    {GLFW_KEY_F20, "F20"},
    {GLFW_KEY_F21, "F21"},
    {GLFW_KEY_F22, "F22"},
    {GLFW_KEY_F23, "F23"},
    {GLFW_KEY_F24, "F24"},
    {GLFW_KEY_F25, "F25"},

    // Navigation Keys
    {GLFW_KEY_UP, "UP"},
    {GLFW_KEY_DOWN, "DOWN"},
    {GLFW_KEY_LEFT, "LEFT"},
    {GLFW_KEY_RIGHT, "RIGHT"},
    {GLFW_KEY_PAGE_UP, "PAGE_UP"},
    {GLFW_KEY_PAGE_DOWN, "PAGE_DOWN"},
    {GLFW_KEY_HOME, "HOME"},
    {GLFW_KEY_END, "END"},

    // Modifier Keys
    {GLFW_KEY_LEFT_SHIFT, "LEFT_SHIFT"},
    {GLFW_KEY_RIGHT_SHIFT, "RIGHT_SHIFT"},
    {GLFW_KEY_LEFT_CONTROL, "LEFT_CONTROL"},
    {GLFW_KEY_RIGHT_CONTROL, "RIGHT_CONTROL"},
    {GLFW_KEY_LEFT_ALT, "LEFT_ALT"},
    {GLFW_KEY_RIGHT_ALT, "RIGHT_ALT"},
    {GLFW_KEY_LEFT_SUPER, "LEFT_SUPER"},
    {GLFW_KEY_RIGHT_SUPER, "RIGHT_SUPER"},

    // Other Keys
    {GLFW_KEY_SPACE, "SPACE"},
    {GLFW_KEY_ENTER, "ENTER"},
    {GLFW_KEY_ESCAPE, "ESCAPE"},
    {GLFW_KEY_BACKSPACE, "BACKSPACE"},
    {GLFW_KEY_TAB, "TAB"},
    {GLFW_KEY_DELETE, "DELETE"},

    // Numpad Keys
    {GLFW_KEY_KP_0, "KP_0"},
    {GLFW_KEY_KP_1, "KP_1"},
    {GLFW_KEY_KP_2, "KP_2"},
    {GLFW_KEY_KP_3, "KP_3"},
    {GLFW_KEY_KP_4, "KP_4"},
    {GLFW_KEY_KP_5, "KP_5"},
    {GLFW_KEY_KP_6, "KP_6"},
    {GLFW_KEY_KP_7, "KP_7"},
    {GLFW_KEY_KP_8, "KP_8"},
    {GLFW_KEY_KP_9, "KP_9"},
    {GLFW_KEY_KP_DIVIDE, "KP_DIVIDE"},
    {GLFW_KEY_KP_MULTIPLY, "KP_MULTIPLY"},
    {GLFW_KEY_KP_SUBTRACT, "KP_SUBTRACT"},
    {GLFW_KEY_KP_ADD, "KP_ADD"},
    {GLFW_KEY_KP_DECIMAL, "KP_DECIMAL"},
    {GLFW_KEY_KP_EQUAL, "KP_EQUAL"},

    // Special Characters and Keys
    {GLFW_KEY_APOSTROPHE, "APOSTROPHE"},
    {GLFW_KEY_COMMA, "COMMA"},
    {GLFW_KEY_MINUS, "MINUS"},
    {GLFW_KEY_PERIOD, "PERIOD"},
    {GLFW_KEY_SLASH, "SLASH"},
    {GLFW_KEY_SEMICOLON, "SEMICOLON"},
    {GLFW_KEY_EQUAL, "EQUAL"},
    {GLFW_KEY_LEFT_BRACKET, "LEFT_BRACKET"},
    {GLFW_KEY_RIGHT_BRACKET, "RIGHT_BRACKET"},
    {GLFW_KEY_BACKSLASH, "BACKSLASH"},
    {GLFW_KEY_GRAVE_ACCENT, "GRAVE_ACCENT"},

    // Number Lock and Caps Lock
    {GLFW_KEY_CAPS_LOCK, "CAPS_LOCK"},
    {GLFW_KEY_SCROLL_LOCK, "SCROLL_LOCK"},
    {GLFW_KEY_NUM_LOCK, "NUM_LOCK"},

    // Media Keys
    {GLFW_KEY_PAUSE, "PAUSE"},
    {GLFW_KEY_PRINT_SCREEN, "PRINT_SCREEN"},
    {GLFW_KEY_MENU, "MENU"},

    // Other
    {GLFW_KEY_UNKNOWN, "UNKNOWN"},
    {GLFW_KEY_WORLD_1, "WORLD_1"},
    {GLFW_KEY_WORLD_2, "WORLD_2"}};

#define GLFW_KEY(x) (keyMap.count(x) ? keyMap[x] : nullptr)

// Reverse map: string name → GLFW key code (built once from keyMap)
static std::unordered_map<std::string, int> reverseKeyMap = []()
{
    std::unordered_map<std::string, int> m;
    for (const auto &[code, name] : keyMap)
    {
        m[name] = code;
    }
    return m;
}();

void error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

bool WindowManager::Initialize(int const width, int const height)
{
    m_isShutdown = false;

    glfwSetErrorCallback(error_callback);

    // Wayland is not fully supported in GLFW
    // this will force using X11 on wayland (XWayland)
#ifdef __linux__
    std::cout << "linux detected, using x11" << std::endl;
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    std::cout << "INIT - GLFW: SUCCESS" << std::endl;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window = glfwCreateWindow(width, height, "Game", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    std::cout << "INIT - GLAD: SUCCESS" << std::endl;

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

    glfwSwapInterval(1); // Set vsync

    // Use framebuffer size for viewport (handles Retina/HiDPI displays)
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);

    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, click_callback);
    glfwSetCursorPosCallback(window, cursorPos_callback);
    glfwSetFramebufferSizeCallback(window, resize_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetCharCallback(window, char_callback);

    return true;
}

void WindowManager::Shutdown()
{
    if (m_isShutdown)
    {
        return;
    }

    m_isShutdown = true;

    EnableTestInputOverrides(false);

    // Clear input handlers so late unregistration during static teardown is a no-op.
    m_inputHandlers.clear();
    m_nextHandlerId = 0;

    if (window != nullptr)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    glfwTerminate();
}

void WindowManager::CloseWindow()
{
    if (window == nullptr)
    {
        std::cerr << "ERROR: GLFW window invalid" << std::endl;
    }
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

glm::vec2 WindowManager::GetSize()
{
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    return glm::vec2(width, height);
}

bool WindowManager::IsKeyPressed(const std::string &keyName) const
{
    if (m_testInputOverridesEnabled)
    {
        return m_testPressedKeys.find(keyName) != m_testPressedKeys.end();
    }

    auto it = reverseKeyMap.find(keyName);
    if (it == reverseKeyMap.end())
    {
        return false;
    }
    return glfwGetKey(window, it->second) == GLFW_PRESS;
}

void WindowManager::EnableTestInputOverrides(bool enabled)
{
    m_testInputOverridesEnabled = enabled;
    m_testPressedKeys.clear();
}

void WindowManager::SetTestKeyPressed(const std::string &keyName, bool pressed)
{
    if (!m_testInputOverridesEnabled)
    {
        return;
    }

    if (pressed)
    {
        m_testPressedKeys.insert(keyName);
    }
    else
    {
        m_testPressedKeys.erase(keyName);
    }
}

size_t WindowManager::RegisterInputHandler(InputHandler handler)
{
    size_t id = m_nextHandlerId++;
    m_inputHandlers.push_back({id, handler});
    LOG_INFO("[WindowManager] Registered input handler ID {}", id);
    return id;
}

void WindowManager::UnregisterInputHandler(size_t id)
{
    auto it = std::remove_if(m_inputHandlers.begin(), m_inputHandlers.end(),
                             [id](const auto &pair)
                             { return pair.first == id; });

    if (it != m_inputHandlers.end())
    {
        m_inputHandlers.erase(it, m_inputHandlers.end());
    }
    else
    {
        // During shutdown/static teardown, IDs can be stale; avoid logging here.
    }
}

// Input handling is done via Lua - converts to native Lua table for proper queue handling
#define APPEND_EVENT(event) sm.AddInputEventToQueue(event);

void WindowManager::key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    WindowManager &wm = GetInstance();
    if (wm.ShouldIgnorePhysicalInput())
    {
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        InputEvent event;
        event.type = InputTypes::Key;
        event.input = GLFW_KEY(key);
        event.mods = mods;
        event.action = action;

        LOG_TRACE_L2("[WindowManager] Key event: key={}, action={}, mods={}", GLFW_KEY(key), action, mods);

        // Try C++ handlers first
        for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
        {
            const auto &[id, handler] = *it;
            if (handler(event))
            {
                LOG_DEBUG("[WindowManager] Key input consumed by handler ID {}", id);
                return; // Event consumed, don't send to Lua
            }
        }

        // Fall through to Lua if no C++ handler consumed event
        ScriptManager &sm = ScriptManager::GetInstance();
        APPEND_EVENT(event)
    }
}

void WindowManager::click_callback(GLFWwindow *window, int button, int action, int mods)
{
    if (GetInstance().ShouldIgnorePhysicalInput())
    {
        return;
    }

    // Log all click events for debugging
    LOG_TRACE_L2("[WindowManager] click_callback triggered: button={}, action={}, mods={}", button, action, mods);

    // Get cursor position at time of click
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    LOG_TRACE_L2("[WindowManager] Mouse click detected: button={}, pos=({}, {})", button, xpos, ypos);

    // Always emit a MouseButton event (press + release) for fine-grained UI input (@mousedown/@mouseup).
    // This is handled by C++ systems (HTMLRendererMT) and is not queued to Lua by default.
    {
        InputEvent mouseButtonEvent;
        mouseButtonEvent.type = InputTypes::MouseButton;
        mouseButtonEvent.input = glm::vec4(xpos, ypos, static_cast<float>(button), static_cast<float>(action));
        mouseButtonEvent.mods = mods;

        WindowManager &wm = GetInstance();
        for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
        {
            const auto &[id, handler] = *it;
            if (handler(mouseButtonEvent))
            {
                LOG_TRACE_L2("[WindowManager] MouseButton input consumed by handler ID {}", id);
                break;
            }
        }
    }

    // Only emit Click on GLFW_PRESS to avoid double-triggering on press+release.
    if (action != GLFW_PRESS)
    {
        return;
    }

    InputEvent event;
    event.type = InputTypes::Click;
    event.input = glm::vec3(xpos, ypos, button);
    event.mods = mods;
    event.action = action;

    // Try C++ handlers first
    WindowManager &wm = GetInstance();
    LOG_TRACE_L3("[WindowManager] Trying {} C++ handlers for click event", wm.m_inputHandlers.size());
    for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
    {
        const auto &[id, handler] = *it;
        LOG_TRACE_L3("[WindowManager] Calling handler ID {}", id);
        bool consumed = handler(event);
        LOG_TRACE_L3("[WindowManager] Handler ID {} returned {}", id, consumed ? "true (consumed)" : "false (pass through)");
        if (consumed)
        {
            LOG_TRACE_L2("[WindowManager] Click input consumed by handler ID {}", id);
            return; // Event consumed, don't send to Lua
        }
    }

    // Fall through to Lua if no C++ handler consumed event
    LOG_TRACE_L3("[WindowManager] No handlers consumed click, adding to Lua queue");
    ScriptManager &sm = ScriptManager::GetInstance();
    APPEND_EVENT(event)
}

void WindowManager::cursorPos_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (GetInstance().ShouldIgnorePhysicalInput())
    {
        return;
    }

    static int cursorMoveCount = 0;
    if (cursorMoveCount++ % 10 == 0) // Log every 10th move to reduce spam
    {
        LOG_TRACE_L1("[WindowManager] Cursor moved to ({}, {})", xpos, ypos);
    }

    InputEvent event;
    event.type = InputTypes::Cursor;
    event.input = glm::vec2(xpos, ypos);

    // Try C++ handlers first
    WindowManager &wm = GetInstance();
    for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
    {
        const auto &[id, handler] = *it;
        if (handler(event))
        {
            LOG_DEBUG("[WindowManager] Cursor input consumed by handler ID {}", id);
            return; // Event consumed, don't send to Lua
        }
    }

    // Fall through to Lua if no C++ handler consumed event
    ScriptManager &sm = ScriptManager::GetInstance();
    APPEND_EVENT(event)
}

void WindowManager::resize_callback(GLFWwindow *window, int fbWidth, int fbHeight)
{
    // Update OpenGL viewport to match new framebuffer size
    glViewport(0, 0, fbWidth, fbHeight);

    // Broadcast a Resize event to C++ handlers (e.g., HTMLRendererMT, editor viewport sizing).
    {
        InputEvent event;
        event.type = InputTypes::Resize;
        event.input = glm::vec2(static_cast<float>(fbWidth), static_cast<float>(fbHeight));

        WindowManager &wm = GetInstance();
        for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
        {
            const auto &[id, handler] = *it;
            (void)id;
            handler(event);
        }
    }

    // Update camera projection if camera exists
    // Note: Camera uses window size for aspect ratio, not framebuffer size
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    // Get Game instance via user pointer if set
    void *userPtr = glfwGetWindowUserPointer(window);
    if (userPtr != nullptr)
    {
        Game *game = static_cast<Game *>(userPtr);
        if (game->cam != nullptr)
        {
            game->cam->SetPerspective(game->cam->fov, windowWidth, windowHeight);
        }
        // If cam is null, skip silently (common in editor mode).
    }
}

void WindowManager::scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    if (GetInstance().ShouldIgnorePhysicalInput())
    {
        return;
    }

    InputEvent event;
    event.type = InputTypes::Scroll;
    event.input = glm::vec2(xoffset, yoffset);

    // Try C++ handlers first
    WindowManager &wm = GetInstance();
    for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
    {
        const auto &[id, handler] = *it;
        if (handler(event))
        {
            LOG_DEBUG("[WindowManager] Scroll input consumed by handler ID {}", id);
            return; // Event consumed, don't send to Lua
        }
    }

    // Fall through to Lua if no C++ handler consumed event
    ScriptManager &sm = ScriptManager::GetInstance();
    APPEND_EVENT(event)
}

void WindowManager::char_callback(GLFWwindow *window, unsigned int codepoint)
{
    InputEvent event;
    event.type = InputTypes::Char;
    // Convert a Unicode codepoint to UTF-8.
    // GLFW gives us a numeric codepoint (e.g. 'A' = 65). We need a UTF-8 byte sequence
    // so InputEvent can store actual text in a std::string. The OS/UI will interpret
    // those bytes as a readable character when rendering or processing the text.
    char utf8[5] = {0};
    int len = 0;
    if (codepoint < 0x80)
    {
        // 1-byte UTF-8: 0xxxxxxx
        utf8[0] = static_cast<char>(codepoint);
        len = 1;
    }
    else if (codepoint < 0x800)
    {
        // 2-byte UTF-8: 110xxxxx 10xxxxxx
        utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        len = 2;
    }
    else if (codepoint < 0x10000)
    {
        // 3-byte UTF-8: 1110xxxx 10xxxxxx 10xxxxxx
        utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        len = 3;
    }
    else
    {
        // 4-byte UTF-8: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        len = 4;
    }
    event.input = std::string(utf8, len);

    // Try C++ handlers first
    WindowManager &wm = GetInstance();
    for (auto it = wm.m_inputHandlers.rbegin(); it != wm.m_inputHandlers.rend(); ++it)
    {
        const auto &[id, handler] = *it;
        if (handler(event))
        {
            return; // Event consumed
        }
    }

    // Fall through to Lua if no C++ handler consumed event
    ScriptManager &sm = ScriptManager::GetInstance();
    APPEND_EVENT(event)
}
