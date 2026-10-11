/**
 * @file UITemplateManager.h
 * @brief Template management system for UI Editor
 * @lines ~132
 *
 * Quick-stats (Public API):
 * - ListTemplates() / TemplateExists() - Query available templates
 * - LoadTemplate() / SaveTemplate() - Read and persist HTML, CSS, and Lua files
 * - CreateTemplate() / DeleteTemplate() - Manage template files
 *
 * Handles loading, saving, creating, and listing UI templates.
 * Templates consist of three files: HTML, CSS, and Lua state.
 */

#pragma once

#include <string>
#include <vector>

/**
 * @brief Template information structure
 */
struct TemplateInfo
{
    std::string name;      ///< Template name (without extension)
    std::string htmlPath;  ///< Path to HTML file
    std::string cssPath;   ///< Path to CSS file
    std::string luaPath;   ///< Path to Lua state file
    bool hasHTML;          ///< HTML file exists
    bool hasCSS;           ///< CSS file exists
    bool hasLua;           ///< Lua file exists
};

/**
 * @brief UI Template Manager
 *
 * Manages UI templates stored in res/ui/templates/, res/ui/styles/, and res/ui/state/.
 * Provides operations to list, load, save, create, and delete templates.
 */
class UITemplateManager
{
public:
    /**
     * @brief Get singleton instance
     * @return Reference to UITemplateManager singleton
     */
    static UITemplateManager &GetInstance()
    {
        static UITemplateManager instance;
        return instance;
    }

    UITemplateManager(UITemplateManager const &) = delete;
    void operator=(UITemplateManager const &) = delete;

    /**
     * @brief Initialize template manager
     * @param templatesDir Directory containing HTML templates (e.g., "../res/ui/templates/")
     * @param stylesDir Directory containing CSS files (e.g., "../res/ui/styles/")
     * @param stateDir Directory containing Lua state files (e.g., "../res/ui/state/")
     * @return true if initialization succeeded
     */
    bool Initialize(const std::string &templatesDir, const std::string &stylesDir, const std::string &stateDir);

    /**
     * @brief List all available templates
     * @return Vector of template info structures
     * @note Scans directories and matches HTML/CSS/Lua files by name
     */
    std::vector<TemplateInfo> ListTemplates();

    /**
     * @brief Load template files into strings
     * @param name Template name (without extension)
     * @param html Output HTML content
     * @param css Output CSS content
     * @param lua Output Lua content
     * @return true if HTML and every existing companion file were read successfully
     * @note Empty HTML is valid. Missing CSS or Lua files produce empty strings.
     *       On failure, all output strings retain their original values.
     */
    bool LoadTemplate(const std::string &name, std::string &html, std::string &css, std::string &lua);

    /**
     * @brief Save template files to disk
     * @param name Template name (without extension)
     * @param html HTML content to save
     * @param css CSS content to save
     * @param lua Lua content to save
     * @return true if all files were saved successfully
     */
    bool SaveTemplate(const std::string &name, const std::string &html, const std::string &css, const std::string &lua);

    /**
     * @brief Create new template with default content
     * @param name Template name (without extension)
     * @return true if template was created successfully
     */
    bool CreateTemplate(const std::string &name);

    /**
     * @brief Delete template files
     * @param name Template name (without extension)
     * @return true if template was deleted (or didn't exist)
     */
    bool DeleteTemplate(const std::string &name);

    /**
     * @brief Check if template exists
     * @param name Template name (without extension)
     * @return true if at least HTML file exists
     */
    bool TemplateExists(const std::string &name);

private:
    UITemplateManager() = default;
    ~UITemplateManager() = default;

    std::string m_templatesDir;
    std::string m_stylesDir;
    std::string m_stateDir;
    bool m_initialized = false;

    /**
     * @brief Get full path to template HTML file
     */
    std::string GetHTMLPath(const std::string &name) const;

    /**
     * @brief Get full path to template CSS file
     */
    std::string GetCSSPath(const std::string &name) const;

    /**
     * @brief Get full path to template Lua file
     */
    std::string GetLuaPath(const std::string &name) const;
};
