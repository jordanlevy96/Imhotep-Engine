/** @file UITemplateManager.h UI template file management. */
#pragma once

#include <string>
#include <vector>

struct TemplateInfo
{
    std::string name;
    std::string htmlPath;
    std::string cssPath;
    std::string luaPath;
    bool hasHTML;
    bool hasCSS;
    bool hasLua;
};

/** Stores an HTML template with optional CSS and Lua companions. */
class UITemplateManager
{
public:
    static UITemplateManager &GetInstance()
    {
        static UITemplateManager instance;
        return instance;
    }

    UITemplateManager(UITemplateManager const &) = delete;
    void operator=(UITemplateManager const &) = delete;

    bool Initialize(const std::string &templatesDir, const std::string &stylesDir, const std::string &stateDir);
    std::vector<TemplateInfo> ListTemplates();

    /// Missing companions are empty; failure leaves all output strings unchanged.
    bool LoadTemplate(const std::string &name, std::string &html, std::string &css, std::string &lua);
    bool SaveTemplate(const std::string &name, const std::string &html, const std::string &css, const std::string &lua);
    bool CreateTemplate(const std::string &name);

    /// Returns true when the template was already absent.
    bool DeleteTemplate(const std::string &name);
    /// Checks for the HTML file; CSS and Lua companions are optional.
    bool TemplateExists(const std::string &name);

private:
    UITemplateManager() = default;
    ~UITemplateManager() = default;

    std::string m_templatesDir;
    std::string m_stylesDir;
    std::string m_stateDir;
    bool m_initialized = false;

    std::string GetHTMLPath(const std::string &name) const;

    std::string GetCSSPath(const std::string &name) const;

    std::string GetLuaPath(const std::string &name) const;
};
