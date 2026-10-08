#pragma once
#include <string>
#include <map>
#include <filesystem>
#include <functional>

/**
 * Theme Manager for the browser
 * Handles loading, saving, and applying themes.
 */
class ThemeManager
{
public:
    struct Theme
    {
        std::string id;
        std::string name;
        std::string description;
        std::string author;
        std::string version;
        bool is_builtin;
        std::map<std::string, std::string> colors;
    };

    ThemeManager();
    ~ThemeManager();

    // Initialize with storage directory path
    void Initialize(const std::filesystem::path& storage_dir);

    // Get the currently active theme ID
    std::string GetActiveThemeId() const { return active_theme_id_; }

    // Set the active theme
    bool SetActiveTheme(const std::string& theme_id);

    // Get all custom themes as JSON
    std::string GetCustomThemesJSON() const;

    // Save custom themes from JSON
    bool SaveCustomThemes(const std::string& json);

    // Get a specific theme by ID (returns JSON)
    std::string GetThemeJSON(const std::string& theme_id) const;

    // Add or update a custom theme
    bool AddCustomTheme(const std::string& json);

    // Remove a custom theme
    bool RemoveCustomTheme(const std::string& theme_id);

    // Export a theme to file
    bool ExportTheme(const std::string& theme_id, const std::filesystem::path& file_path) const;

    // Import a theme from file
    std::string ImportTheme(const std::filesystem::path& file_path);

private:
    void LoadThemes();
    void SaveThemes();
    void LoadBuiltinThemes();
    Theme ParseThemeJSON(const std::string& json) const;
    std::string ThemeToJSON(const Theme& theme) const;

    std::filesystem::path storage_dir_;
    std::filesystem::path themes_file_;
    std::string active_theme_id_;
    std::map<std::string, Theme> builtin_themes_;
    std::map<std::string, Theme> custom_themes_;
};
