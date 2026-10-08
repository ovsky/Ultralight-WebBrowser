#include "ThemeManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

// Simple JSON helpers (reusing pattern from BookmarkStore)
namespace {
    std::string EscapeJSON(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }

    std::string UnescapeJSON(const std::string& s) {
        std::string result;
        for (size_t i = 0; i < s.length(); ++i) {
            if (s[i] == '\\' && i + 1 < s.length()) {
                switch (s[i + 1]) {
                    case '"':  result += '"'; ++i; break;
                    case '\\': result += '\\'; ++i; break;
                    case 'n':  result += '\n'; ++i; break;
                    case 'r':  result += '\r'; ++i; break;
                    case 't':  result += '\t'; ++i; break;
                    default: result += s[i]; break;
                }
            } else {
                result += s[i];
            }
        }
        return result;
    }

    std::string ExtractJSONString(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        
        size_t end = pos + 1;
        while (end < json.length()) {
            if (json[end] == '"' && json[end - 1] != '\\') break;
            ++end;
        }
        
        return UnescapeJSON(json.substr(pos + 1, end - pos - 1));
    }

    bool ExtractJSONBool(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return false;
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return false;
        
        // Skip whitespace
        while (pos < json.length() && (json[pos] == ':' || json[pos] == ' ')) ++pos;
        
        return json.substr(pos, 4) == "true";
    }

    std::map<std::string, std::string> ExtractJSONColorMap(const std::string& json) {
        std::map<std::string, std::string> colors;
        
        size_t colorsPos = json.find("\"colors\"");
        if (colorsPos == std::string::npos) return colors;
        
        size_t start = json.find('{', colorsPos);
        if (start == std::string::npos) return colors;
        
        int depth = 1;
        size_t end = start + 1;
        while (end < json.length() && depth > 0) {
            if (json[end] == '{') ++depth;
            else if (json[end] == '}') --depth;
            ++end;
        }
        
        std::string colorsJson = json.substr(start, end - start);
        
        // Parse key-value pairs
        size_t pos = 0;
        while (pos < colorsJson.length()) {
            size_t keyStart = colorsJson.find('"', pos);
            if (keyStart == std::string::npos) break;
            
            size_t keyEnd = colorsJson.find('"', keyStart + 1);
            if (keyEnd == std::string::npos) break;
            
            std::string key = colorsJson.substr(keyStart + 1, keyEnd - keyStart - 1);
            
            size_t valueStart = colorsJson.find('"', keyEnd + 1);
            if (valueStart == std::string::npos) break;
            
            size_t valueEnd = valueStart + 1;
            while (valueEnd < colorsJson.length()) {
                if (colorsJson[valueEnd] == '"' && colorsJson[valueEnd - 1] != '\\') break;
                ++valueEnd;
            }
            
            std::string value = colorsJson.substr(valueStart + 1, valueEnd - valueStart - 1);
            colors[key] = value;
            
            pos = valueEnd + 1;
        }
        
        return colors;
    }
}

ThemeManager::ThemeManager()
    : active_theme_id_("dark")
{
}

ThemeManager::~ThemeManager()
{
}

void ThemeManager::Initialize(const std::filesystem::path& storage_dir)
{
    storage_dir_ = storage_dir;
    themes_file_ = storage_dir / "custom_themes.json";
    
    LoadBuiltinThemes();
    LoadThemes();
}

void ThemeManager::LoadBuiltinThemes()
{
    // Dark theme (default)
    Theme dark;
    dark.id = "dark";
    dark.name = "Dark (Default)";
    dark.description = "The default dark purple theme";
    dark.author = "Ultralight Team";
    dark.version = "1.0.0";
    dark.is_builtin = true;
    dark.colors = {
        {"color-bg-primary", "#16151d"},
        {"color-bg-secondary", "#1e1e2e"},
        {"color-bg-tertiary", "#232330"},
        {"color-bg-elevated", "#282839"},
        {"color-bg-hover", "#343446"},
        {"color-text-primary", "#e4e4ef"},
        {"color-text-secondary", "#c4c2d0"},
        {"color-text-tertiary", "#9999b3"},
        {"color-text-muted", "#71718a"},
        {"color-border-primary", "#313146"},
        {"color-border-secondary", "#252532"},
        {"color-accent-primary", "#6C63FF"},
        {"color-accent-secondary", "#7c6aef"},
        {"color-accent-hover", "#8a83ff"},
        {"color-success", "#6aef8a"},
        {"color-warning", "#f0b866"},
        {"color-danger", "#ef6a6a"},
        {"color-info", "#6ac0ef"},
        {"menu-bg", "#2b2b38"},
        {"card-bg", "#282839"},
        {"btn-primary-bg", "#6C63FF"},
        {"input-bg", "#32324a"}
    };
    builtin_themes_["dark"] = dark;

    // Light theme
    Theme light;
    light.id = "light";
    light.name = "Light";
    light.description = "Clean light theme for daytime use";
    light.author = "Ultralight Team";
    light.version = "1.0.0";
    light.is_builtin = true;
    light.colors = {
        {"color-bg-primary", "#ffffff"},
        {"color-bg-secondary", "#f6f8fa"},
        {"color-bg-tertiary", "#eaeef2"},
        {"color-bg-elevated", "#ffffff"},
        {"color-bg-hover", "#e8ebef"},
        {"color-text-primary", "#1f2328"},
        {"color-text-secondary", "#424a53"},
        {"color-text-tertiary", "#656d76"},
        {"color-text-muted", "#8c959f"},
        {"color-border-primary", "#d0d7de"},
        {"color-border-secondary", "#e1e4e8"},
        {"color-accent-primary", "#0969da"},
        {"color-accent-secondary", "#218bff"},
        {"color-accent-hover", "#54aeff"},
        {"color-success", "#1a7f37"},
        {"color-warning", "#9a6700"},
        {"color-danger", "#cf222e"},
        {"color-info", "#0969da"},
        {"menu-bg", "#ffffff"},
        {"card-bg", "#ffffff"},
        {"btn-primary-bg", "#0969da"},
        {"input-bg", "#ffffff"}
    };
    builtin_themes_["light"] = light;

    // Midnight theme
    Theme midnight;
    midnight.id = "midnight";
    midnight.name = "Midnight Blue";
    midnight.description = "Deep blue night theme";
    midnight.author = "Ultralight Team";
    midnight.version = "1.0.0";
    midnight.is_builtin = true;
    midnight.colors = {
        {"color-bg-primary", "#0d1117"},
        {"color-bg-secondary", "#161b22"},
        {"color-bg-tertiary", "#21262d"},
        {"color-bg-elevated", "#30363d"},
        {"color-bg-hover", "#3d444d"},
        {"color-text-primary", "#e6edf3"},
        {"color-text-secondary", "#c9d1d9"},
        {"color-text-tertiary", "#8b949e"},
        {"color-text-muted", "#6e7681"},
        {"color-border-primary", "#30363d"},
        {"color-border-secondary", "#21262d"},
        {"color-accent-primary", "#58a6ff"},
        {"color-accent-secondary", "#79c0ff"},
        {"color-accent-hover", "#a5d6ff"},
        {"color-success", "#3fb950"},
        {"color-warning", "#d29922"},
        {"color-danger", "#f85149"},
        {"color-info", "#58a6ff"},
        {"menu-bg", "#21262d"},
        {"card-bg", "#21262d"},
        {"btn-primary-bg", "#238636"},
        {"input-bg", "#0d1117"}
    };
    builtin_themes_["midnight"] = midnight;

    // Nord theme
    Theme nord;
    nord.id = "nord";
    nord.name = "Nord";
    nord.description = "Arctic, north-bluish color palette";
    nord.author = "Ultralight Team";
    nord.version = "1.0.0";
    nord.is_builtin = true;
    nord.colors = {
        {"color-bg-primary", "#2e3440"},
        {"color-bg-secondary", "#3b4252"},
        {"color-bg-tertiary", "#434c5e"},
        {"color-bg-elevated", "#4c566a"},
        {"color-bg-hover", "#5e6779"},
        {"color-text-primary", "#eceff4"},
        {"color-text-secondary", "#e5e9f0"},
        {"color-text-tertiary", "#d8dee9"},
        {"color-text-muted", "#a5adba"},
        {"color-border-primary", "#4c566a"},
        {"color-border-secondary", "#3b4252"},
        {"color-accent-primary", "#88c0d0"},
        {"color-accent-secondary", "#81a1c1"},
        {"color-accent-hover", "#5e81ac"},
        {"color-success", "#a3be8c"},
        {"color-warning", "#ebcb8b"},
        {"color-danger", "#bf616a"},
        {"color-info", "#88c0d0"},
        {"menu-bg", "#3b4252"},
        {"card-bg", "#3b4252"},
        {"btn-primary-bg", "#5e81ac"},
        {"input-bg", "#2e3440"}
    };
    builtin_themes_["nord"] = nord;

    // Monokai theme
    Theme monokai;
    monokai.id = "monokai";
    monokai.name = "Monokai Pro";
    monokai.description = "Classic Monokai color scheme";
    monokai.author = "Ultralight Team";
    monokai.version = "1.0.0";
    monokai.is_builtin = true;
    monokai.colors = {
        {"color-bg-primary", "#2d2a2e"},
        {"color-bg-secondary", "#353236"},
        {"color-bg-tertiary", "#403e41"},
        {"color-bg-elevated", "#4a474c"},
        {"color-bg-hover", "#555158"},
        {"color-text-primary", "#fcfcfa"},
        {"color-text-secondary", "#c1c0c0"},
        {"color-text-tertiary", "#939293"},
        {"color-text-muted", "#727072"},
        {"color-border-primary", "#4a474c"},
        {"color-border-secondary", "#353236"},
        {"color-accent-primary", "#ffd866"},
        {"color-accent-secondary", "#ff6188"},
        {"color-accent-hover", "#a9dc76"},
        {"color-success", "#a9dc76"},
        {"color-warning", "#ffd866"},
        {"color-danger", "#ff6188"},
        {"color-info", "#78dce8"},
        {"menu-bg", "#353236"},
        {"card-bg", "#353236"},
        {"btn-primary-bg", "#ffd866"},
        {"input-bg", "#2d2a2e"}
    };
    builtin_themes_["monokai"] = monokai;
}

void ThemeManager::LoadThemes()
{
    if (!std::filesystem::exists(themes_file_))
        return;

    std::ifstream file(themes_file_);
    if (!file.is_open())
        return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Parse custom themes JSON array
    // Simple parsing for array of theme objects
    size_t pos = json.find('[');
    if (pos == std::string::npos)
        return;

    size_t end = json.rfind(']');
    if (end == std::string::npos || end <= pos)
        return;

    // Find each theme object
    size_t objStart = pos;
    while ((objStart = json.find('{', objStart)) != std::string::npos && objStart < end)
    {
        int depth = 1;
        size_t objEnd = objStart + 1;
        while (objEnd < json.length() && depth > 0)
        {
            if (json[objEnd] == '{') ++depth;
            else if (json[objEnd] == '}') --depth;
            ++objEnd;
        }

        std::string themeJson = json.substr(objStart, objEnd - objStart);
        Theme theme = ParseThemeJSON(themeJson);
        if (!theme.id.empty() && !theme.is_builtin)
        {
            custom_themes_[theme.id] = theme;
        }

        objStart = objEnd;
    }

    // Load active theme preference
    std::filesystem::path settingsFile = storage_dir_ / "theme_settings.json";
    if (std::filesystem::exists(settingsFile))
    {
        std::ifstream sf(settingsFile);
        if (sf.is_open())
        {
            std::stringstream sb;
            sb << sf.rdbuf();
            active_theme_id_ = ExtractJSONString(sb.str(), "active_theme");
            if (active_theme_id_.empty())
                active_theme_id_ = "dark";
        }
    }
}

void ThemeManager::SaveThemes()
{
    // Ensure directory exists
    std::filesystem::create_directories(storage_dir_);

    // Save custom themes
    std::ofstream file(themes_file_);
    if (!file.is_open())
        return;

    file << "[\n";
    bool first = true;
    for (const auto& [id, theme] : custom_themes_)
    {
        if (!first) file << ",\n";
        first = false;
        file << ThemeToJSON(theme);
    }
    file << "\n]";
    file.close();

    // Save active theme preference
    std::filesystem::path settingsFile = storage_dir_ / "theme_settings.json";
    std::ofstream sf(settingsFile);
    if (sf.is_open())
    {
        sf << "{\n  \"active_theme\": \"" << EscapeJSON(active_theme_id_) << "\"\n}";
    }
}

ThemeManager::Theme ThemeManager::ParseThemeJSON(const std::string& json) const
{
    Theme theme;
    theme.id = ExtractJSONString(json, "id");
    theme.name = ExtractJSONString(json, "name");
    theme.description = ExtractJSONString(json, "description");
    theme.author = ExtractJSONString(json, "author");
    theme.version = ExtractJSONString(json, "version");
    theme.is_builtin = ExtractJSONBool(json, "isBuiltIn");
    theme.colors = ExtractJSONColorMap(json);
    return theme;
}

std::string ThemeManager::ThemeToJSON(const Theme& theme) const
{
    std::ostringstream ss;
    ss << "  {\n";
    ss << "    \"id\": \"" << EscapeJSON(theme.id) << "\",\n";
    ss << "    \"name\": \"" << EscapeJSON(theme.name) << "\",\n";
    ss << "    \"description\": \"" << EscapeJSON(theme.description) << "\",\n";
    ss << "    \"author\": \"" << EscapeJSON(theme.author) << "\",\n";
    ss << "    \"version\": \"" << EscapeJSON(theme.version) << "\",\n";
    ss << "    \"isBuiltIn\": " << (theme.is_builtin ? "true" : "false") << ",\n";
    ss << "    \"colors\": {\n";
    
    bool first = true;
    for (const auto& [key, value] : theme.colors)
    {
        if (!first) ss << ",\n";
        first = false;
        ss << "      \"" << EscapeJSON(key) << "\": \"" << EscapeJSON(value) << "\"";
    }
    
    ss << "\n    }\n";
    ss << "  }";
    return ss.str();
}

bool ThemeManager::SetActiveTheme(const std::string& theme_id)
{
    // Check if theme exists
    if (builtin_themes_.find(theme_id) == builtin_themes_.end() &&
        custom_themes_.find(theme_id) == custom_themes_.end())
    {
        return false;
    }

    active_theme_id_ = theme_id;
    SaveThemes();
    return true;
}

std::string ThemeManager::GetCustomThemesJSON() const
{
    std::ostringstream ss;
    ss << "{";
    
    bool first = true;
    for (const auto& [id, theme] : custom_themes_)
    {
        if (!first) ss << ",";
        first = false;
        ss << "\n  \"" << EscapeJSON(id) << "\": " << ThemeToJSON(theme);
    }
    
    ss << "\n}";
    return ss.str();
}

bool ThemeManager::SaveCustomThemes(const std::string& json)
{
    // Clear and repopulate from JSON
    custom_themes_.clear();
    
    // Parse the JSON object of themes
    size_t pos = json.find('{');
    if (pos == std::string::npos)
        return false;

    // Find theme objects
    size_t objStart = pos;
    while ((objStart = json.find('{', objStart + 1)) != std::string::npos)
    {
        int depth = 1;
        size_t objEnd = objStart + 1;
        while (objEnd < json.length() && depth > 0)
        {
            if (json[objEnd] == '{') ++depth;
            else if (json[objEnd] == '}') --depth;
            ++objEnd;
        }

        std::string themeJson = json.substr(objStart, objEnd - objStart);
        Theme theme = ParseThemeJSON(themeJson);
        if (!theme.id.empty() && !theme.is_builtin)
        {
            custom_themes_[theme.id] = theme;
        }

        objStart = objEnd;
    }

    SaveThemes();
    return true;
}

std::string ThemeManager::GetThemeJSON(const std::string& theme_id) const
{
    auto it = builtin_themes_.find(theme_id);
    if (it != builtin_themes_.end())
    {
        return ThemeToJSON(it->second);
    }

    auto cit = custom_themes_.find(theme_id);
    if (cit != custom_themes_.end())
    {
        return ThemeToJSON(cit->second);
    }

    return "{}";
}

bool ThemeManager::AddCustomTheme(const std::string& json)
{
    Theme theme = ParseThemeJSON(json);
    if (theme.id.empty())
        return false;

    theme.is_builtin = false;
    custom_themes_[theme.id] = theme;
    SaveThemes();
    return true;
}

bool ThemeManager::RemoveCustomTheme(const std::string& theme_id)
{
    auto it = custom_themes_.find(theme_id);
    if (it == custom_themes_.end())
        return false;

    custom_themes_.erase(it);

    // Reset to default if active theme was deleted
    if (active_theme_id_ == theme_id)
    {
        active_theme_id_ = "dark";
    }

    SaveThemes();
    return true;
}

bool ThemeManager::ExportTheme(const std::string& theme_id, const std::filesystem::path& file_path) const
{
    std::string json = GetThemeJSON(theme_id);
    if (json == "{}")
        return false;

    std::ofstream file(file_path);
    if (!file.is_open())
        return false;

    file << json;
    return true;
}

std::string ThemeManager::ImportTheme(const std::filesystem::path& file_path)
{
    if (!std::filesystem::exists(file_path))
        return "";

    std::ifstream file(file_path);
    if (!file.is_open())
        return "";

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    Theme theme = ParseThemeJSON(json);
    if (theme.id.empty())
        return "";

    // Generate new ID for imported theme
    theme.id = "imported_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    theme.is_builtin = false;

    custom_themes_[theme.id] = theme;
    SaveThemes();

    return theme.id;
}
