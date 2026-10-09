/**
 * @file UITemplateManager.cpp
 * @brief UI template management (HTML/CSS/Lua) for the editor
 * @lines ~260
 *
 * Purpose: Manage UI templates on disk for the in-engine UI editor.
 *
 * Key functions:
 * - ListTemplates() - Enumerate templates from filesystem (line ~35)
 * - LoadTemplate() - Read template files into memory (line ~69)
 * - SaveTemplate() - Write template files to disk (line ~92)
 * - CreateTemplate() - Create new template skeleton (line ~132)
 */

#include "editor/UITemplateManager.h"
#include "util/FileIO.h"
#include "util/Logger.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>

static bool IsValidTemplateName(const std::string &name)
{
    if (name.empty())
    {
        return false;
    }

    for (unsigned char c : name)
    {
        if (!(std::isalnum(c) || c == '_' || c == '-'))
        {
            return false;
        }
    }

    return true;
}

bool UITemplateManager::Initialize(const std::string &templatesDir, const std::string &stylesDir, const std::string &stateDir)
{
    m_templatesDir = templatesDir;
    m_stylesDir = stylesDir;
    m_stateDir = stateDir;
    m_initialized = true;

    LOG_INFO("[UITemplateManager] Initialized with directories:");
    LOG_INFO("  Templates: {}", m_templatesDir);
    LOG_INFO("  Styles: {}", m_stylesDir);
    LOG_INFO("  State: {}", m_stateDir);

    return true;
}

std::vector<TemplateInfo> UITemplateManager::ListTemplates()
{
    std::vector<TemplateInfo> templates;

    if (!m_initialized)
    {
        LOG_WARNING("[UITemplateManager] Not initialized, cannot list templates");
        return templates;
    }

    // Get all HTML files from templates directory
    std::vector<std::string> htmlFiles = FileIO::ListFiles(m_templatesDir, ".html");

    for (const auto &htmlFile : htmlFiles)
    {
        std::string name = FileIO::GetBaseName(htmlFile);

        TemplateInfo info;
        info.name = name;
        info.htmlPath = FileIO::JoinPath(m_templatesDir, htmlFile);
        info.cssPath = FileIO::JoinPath(m_stylesDir, name + ".css");
        info.luaPath = FileIO::JoinPath(m_stateDir, name + ".lua");

        info.hasHTML = FileIO::FileExists(info.htmlPath);
        info.hasCSS = FileIO::FileExists(info.cssPath);
        info.hasLua = FileIO::FileExists(info.luaPath);

        templates.push_back(info);
    }

    LOG_INFO("[UITemplateManager] Found {} templates", templates.size());
    return templates;
}

bool UITemplateManager::LoadTemplate(const std::string &name, std::string &html, std::string &css, std::string &lua)
{
    if (!m_initialized)
    {
        LOG_ERROR("[UITemplateManager] Not initialized, cannot load template");
        return false;
    }

    if (!IsValidTemplateName(name))
    {
        LOG_ERROR("[UITemplateManager] Invalid template name: '{}'", name);
        return false;
    }

    // Read into temporary buffers so a failed reload preserves the open document.
    auto readFile = [](const std::string &path, bool required, std::string &content)
    {
        std::error_code error;
        bool exists = std::filesystem::exists(path, error);
        if (error || (!exists && required))
        {
            return false;
        }
        if (!exists)
        {
            return true;
        }
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            return false;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }
        char buffer[4096];
        while (file)
        {
            file.read(buffer, sizeof(buffer));
            content.append(buffer, static_cast<size_t>(file.gcount()));
        }
        return file.eof() && !file.bad();
    };

    std::string loadedHTML, loadedCSS, loadedLua;
    if (!readFile(GetHTMLPath(name), true, loadedHTML) ||
        !readFile(GetCSSPath(name), false, loadedCSS) ||
        !readFile(GetLuaPath(name), false, loadedLua))
    {
        LOG_ERROR("[UITemplateManager] Failed to read template '{}'", name);
        return false;
    }

    html = std::move(loadedHTML);
    css = std::move(loadedCSS);
    lua = std::move(loadedLua);
    LOG_INFO("[UITemplateManager] Loaded template '{}' (HTML: {} bytes, CSS: {} bytes, Lua: {} bytes)",
             name, html.size(), css.size(), lua.size());
    return true;
}

bool UITemplateManager::SaveTemplate(const std::string &name, const std::string &html, const std::string &css, const std::string &lua)
{
    if (!m_initialized)
    {
        LOG_ERROR("[UITemplateManager] Not initialized, cannot save template");
        return false;
    }

    if (!IsValidTemplateName(name))
    {
        return false;
    }

    std::string htmlPath = GetHTMLPath(name);
    std::string cssPath = GetCSSPath(name);
    std::string luaPath = GetLuaPath(name);

    bool htmlOk = FileIO::WriteTextFile(htmlPath, html);
    bool cssOk = FileIO::WriteTextFile(cssPath, css);
    bool luaOk = FileIO::WriteTextFile(luaPath, lua);

    if (htmlOk && cssOk && luaOk)
    {
        LOG_INFO("[UITemplateManager] Saved template '{}' (HTML: {} bytes, CSS: {} bytes, Lua: {} bytes)",
                 name, html.size(), css.size(), lua.size());
        return true;
    }
    else
    {
        LOG_ERROR("[UITemplateManager] Failed to save template '{}' (HTML: {}, CSS: {}, Lua: {})",
                  name, htmlOk ? "OK" : "FAIL", cssOk ? "OK" : "FAIL", luaOk ? "OK" : "FAIL");
        return false;
    }
}

bool UITemplateManager::CreateTemplate(const std::string &name)
{
    if (!m_initialized)
    {
        LOG_ERROR("[UITemplateManager] Not initialized, cannot create template");
        return false;
    }

    if (!IsValidTemplateName(name))
    {
        LOG_ERROR("[UITemplateManager] Invalid template name: '{}' (allowed: [A-Za-z0-9_-])", name);
        return false;
    }

    if (TemplateExists(name))
    {
        LOG_WARNING("[UITemplateManager] Template '{}' already exists", name);
        return false;
    }

    // Default HTML template
    std::string defaultHTML = R"(<!DOCTYPE html>
<html>
<head>
    <style>
        body {
            margin: 0;
            padding: 20px;
            background: #1e1e1e;
            color: #cccccc;
            font-family: Arial, sans-serif;
        }
    </style>
</head>
<body>
    <h1>Lorem ipsum</h1>
    <p>Lorem ipsum dolor sit amet.</p>
</body>
</html>
)";

    // Default CSS (empty or minimal)
    std::string defaultCSS = R"(/* CSS styles for )" + name + R"( */
)";

    // Default Lua state
    std::string defaultLua = R"(-- UI state for )" + name + R"(
return {
    data = {
        -- Add your data here
    },
    methods = {
        -- Add your event handlers here
    }
}
)";

    return SaveTemplate(name, defaultHTML, defaultCSS, defaultLua);
}

bool UITemplateManager::DeleteTemplate(const std::string &name)
{
    if (!m_initialized)
    {
        LOG_ERROR("[UITemplateManager] Not initialized, cannot delete template");
        return false;
    }

    if (!IsValidTemplateName(name))
    {
        return false;
    }

    std::string htmlPath = GetHTMLPath(name);
    std::string cssPath = GetCSSPath(name);
    std::string luaPath = GetLuaPath(name);

    bool allDeleted = true;

    try
    {
        if (FileIO::FileExists(htmlPath))
        {
            std::filesystem::remove(htmlPath);
            LOG_INFO("[UITemplateManager] Deleted HTML file: {}", htmlPath);
        }

        if (FileIO::FileExists(cssPath))
        {
            std::filesystem::remove(cssPath);
            LOG_INFO("[UITemplateManager] Deleted CSS file: {}", cssPath);
        }

        if (FileIO::FileExists(luaPath))
        {
            std::filesystem::remove(luaPath);
            LOG_INFO("[UITemplateManager] Deleted Lua file: {}", luaPath);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("[UITemplateManager] Exception while deleting template '{}': {}", name, e.what());
        allDeleted = false;
    }

    return allDeleted;
}

bool UITemplateManager::TemplateExists(const std::string &name)
{
    if (!m_initialized)
    {
        return false;
    }

    if (!IsValidTemplateName(name))
    {
        return false;
    }

    std::string htmlPath = GetHTMLPath(name);
    return FileIO::FileExists(htmlPath);
}

std::string UITemplateManager::GetHTMLPath(const std::string &name) const
{
    return FileIO::JoinPath(m_templatesDir, name + ".html");
}

std::string UITemplateManager::GetCSSPath(const std::string &name) const
{
    return FileIO::JoinPath(m_stylesDir, name + ".css");
}

std::string UITemplateManager::GetLuaPath(const std::string &name) const
{
    return FileIO::JoinPath(m_stateDir, name + ".lua");
}
