#include "ExtensionManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <regex>
#include <cctype>

namespace extensions {

// Simple JSON parsing helpers (avoiding external dependencies)
namespace json {
    std::string GetStringValue(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        
        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        
        return json.substr(pos + 1, end - pos - 1);
    }
    
    std::vector<std::string> GetStringArray(const std::string& json, const std::string& key) {
        std::vector<std::string> result;
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return result;
        
        pos = json.find('[', pos);
        if (pos == std::string::npos) return result;
        
        size_t end = json.find(']', pos);
        if (end == std::string::npos) return result;
        
        std::string arr = json.substr(pos + 1, end - pos - 1);
        
        size_t start = 0;
        while ((start = arr.find('"', start)) != std::string::npos) {
            size_t strEnd = arr.find('"', start + 1);
            if (strEnd == std::string::npos) break;
            result.push_back(arr.substr(start + 1, strEnd - start - 1));
            start = strEnd + 1;
        }
        
        return result;
    }
    
    bool GetBoolValue(const std::string& json, const std::string& key, bool defaultVal) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return defaultVal;
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return defaultVal;
        
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
            pos++;
        
        if (pos < json.size() && json.substr(pos, 4) == "true") return true;
        if (pos < json.size() && json.substr(pos, 5) == "false") return false;
        
        return defaultVal;
    }
}

// URL pattern matching
bool MatchURLPattern(const std::string& pattern, const std::string& url) {
    if (pattern == "*" || pattern == "<all_urls>") return true;
    if (pattern.empty()) return false;
    
    // Convert glob pattern to regex
    std::string regexPattern;
    for (char c : pattern) {
        switch (c) {
            case '*': regexPattern += ".*"; break;
            case '?': regexPattern += "."; break;
            case '.': regexPattern += "\\."; break;
            case '/': regexPattern += "\\/"; break;
            case ':': regexPattern += "\\:"; break;
            default: regexPattern += c; break;
        }
    }
    
    try {
        std::regex re(regexPattern, std::regex::icase);
        return std::regex_match(url, re);
    } catch (...) {
        return pattern == url;
    }
}

bool Extension::MatchesURL(const std::string& url) const {
    if (match_patterns.empty()) return true; // No patterns means match all
    
    for (const auto& pattern : match_patterns) {
        if (MatchURLPattern(pattern, url)) return true;
    }
    return false;
}

// Singleton instance
ExtensionManager& ExtensionManager::Instance() {
    static ExtensionManager instance;
    return instance;
}

ExtensionManager::ExtensionManager() = default;
ExtensionManager::~ExtensionManager() = default;

void ExtensionManager::Initialize(const std::filesystem::path& extensions_dir) {
    extensions_dir_ = extensions_dir;
    state_file_ = extensions_dir / "extensions_state.json";
    
    // Create extensions directory if it doesn't exist
    if (!std::filesystem::exists(extensions_dir_)) {
        std::filesystem::create_directories(extensions_dir_);
    }
    
    // Load extension states
    LoadState();
    
    // Load all extensions
    ReloadAll();
    
    initialized_ = true;
}

void ExtensionManager::ReloadAll() {
    // Save current enabled states
    std::map<std::string, bool> enabled_states;
    for (const auto& ext : extensions_) {
        enabled_states[ext.id] = ext.enabled;
    }
    
    extensions_.clear();
    
    if (!std::filesystem::exists(extensions_dir_)) return;
    
    for (const auto& entry : std::filesystem::directory_iterator(extensions_dir_)) {
        if (entry.is_directory()) {
            auto manifest_path = entry.path() / "manifest.json";
            if (std::filesystem::exists(manifest_path)) {
                LoadExtension(entry.path());
            }
        }
    }
    
    // Restore enabled states
    for (auto& ext : extensions_) {
        auto it = enabled_states.find(ext.id);
        if (it != enabled_states.end()) {
            ext.enabled = it->second;
        }
    }
    
    // Load states from disk
    LoadState();
    
    NotifyEvent("reloaded", "");
}

bool ExtensionManager::LoadExtension(const std::filesystem::path& extension_dir) {
    if (!std::filesystem::exists(extension_dir) || !std::filesystem::is_directory(extension_dir)) {
        std::cerr << "[Extensions] Invalid extension directory: " << extension_dir << std::endl;
        return false;
    }
    
    auto manifest_path = extension_dir / "manifest.json";
    if (!std::filesystem::exists(manifest_path)) {
        std::cerr << "[Extensions] No manifest.json found in: " << extension_dir << std::endl;
        return false;
    }
    
    Extension ext;
    ext.base_path = extension_dir;
    ext.id = extension_dir.filename().string();
    
    if (!ParseManifest(manifest_path, ext)) {
        std::cerr << "[Extensions] Failed to parse manifest: " << manifest_path << std::endl;
        return false;
    }
    
    if (!ValidateExtension(ext)) {
        std::cerr << "[Extensions] Invalid extension: " << ext.id << std::endl;
        return false;
    }
    
    // Set load timestamp
    ext.loaded_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Check if already loaded (replace)
    auto it = std::find_if(extensions_.begin(), extensions_.end(),
        [&ext](const Extension& e) { return e.id == ext.id; });
    
    if (it != extensions_.end()) {
        ext.enabled = it->enabled; // Preserve enabled state
        *it = std::move(ext);
        std::cout << "[Extensions] Reloaded: " << it->name << " (" << it->id << ")" << std::endl;
    } else {
        extensions_.push_back(std::move(ext));
        std::cout << "[Extensions] Loaded: " << extensions_.back().name 
                  << " (" << extensions_.back().id << ")" << std::endl;
    }
    
    NotifyEvent("loaded", ext.id);
    return true;
}

bool ExtensionManager::UnloadExtension(const std::string& id) {
    auto it = std::find_if(extensions_.begin(), extensions_.end(),
        [&id](const Extension& e) { return e.id == id; });
    
    if (it != extensions_.end()) {
        extensions_.erase(it);
        NotifyEvent("unloaded", id);
        return true;
    }
    return false;
}

bool ExtensionManager::SetExtensionEnabled(const std::string& id, bool enabled) {
    for (auto& ext : extensions_) {
        if (ext.id == id) {
            ext.enabled = enabled;
            SaveState();
            NotifyEvent(enabled ? "enabled" : "disabled", id);
            return true;
        }
    }
    return false;
}

const Extension* ExtensionManager::GetExtension(const std::string& id) const {
    for (const auto& ext : extensions_) {
        if (ext.id == id) return &ext;
    }
    return nullptr;
}

std::string ExtensionManager::GetContentScriptsForURL(const std::string& url) const {
    std::stringstream ss;
    
    // Wrapper for extension API
    ss << "(function() {\n";
    ss << "  if (window.__ultralightExtensions) return;\n";
    ss << "  window.__ultralightExtensions = {\n";
    ss << "    loaded: [],\n";
    ss << "    storage: {\n";
    ss << "      get: function(key) { return localStorage.getItem('__ext_' + key); },\n";
    ss << "      set: function(key, value) { localStorage.setItem('__ext_' + key, value); },\n";
    ss << "      remove: function(key) { localStorage.removeItem('__ext_' + key); }\n";
    ss << "    },\n";
    ss << "    log: function(msg) { console.log('[Extension]', msg); }\n";
    ss << "  };\n";
    ss << "})();\n\n";
    
    for (const auto& ext : extensions_) {
        if (!ext.enabled) continue;
        if (!ext.MatchesURL(url)) continue;
        
        for (const auto& script_name : ext.content_scripts) {
            auto script_path = ext.base_path / script_name;
            if (std::filesystem::exists(script_path)) {
                std::string content = ReadFileContents(script_path);
                if (!content.empty()) {
                    ss << "/* Extension: " << ext.name << " (" << ext.id << ") */\n";
                    ss << "(function() {\n";
                    ss << "  try {\n";
                    ss << "    window.__ultralightExtensions.loaded.push('" << EscapeJS(ext.id) << "');\n";
                    ss << content << "\n";
                    ss << "  } catch(e) {\n";
                    ss << "    console.error('[Extension " << EscapeJS(ext.id) << "]', e);\n";
                    ss << "  }\n";
                    ss << "})();\n\n";
                }
            }
        }
    }
    
    return ss.str();
}

std::string ExtensionManager::GetBackgroundScripts() const {
    std::stringstream ss;
    
    for (const auto& ext : extensions_) {
        if (!ext.enabled) continue;
        if (ext.background_script.empty()) continue;
        
        auto script_path = ext.base_path / ext.background_script;
        if (std::filesystem::exists(script_path)) {
            std::string content = ReadFileContents(script_path);
            if (!content.empty()) {
                ss << "/* Background: " << ext.name << " */\n";
                ss << "(function() {\n";
                ss << content << "\n";
                ss << "})();\n\n";
            }
        }
    }
    
    return ss.str();
}

std::string ExtensionManager::GetExtensionsJSON() const {
    std::stringstream ss;
    ss << "[";
    
    bool first = true;
    for (const auto& ext : extensions_) {
        if (!first) ss << ",";
        first = false;
        
        ss << "{";
        ss << "\"id\":\"" << EscapeJS(ext.id) << "\",";
        ss << "\"name\":\"" << EscapeJS(ext.name) << "\",";
        ss << "\"version\":\"" << EscapeJS(ext.version) << "\",";
        ss << "\"description\":\"" << EscapeJS(ext.description) << "\",";
        ss << "\"author\":\"" << EscapeJS(ext.author) << "\",";
        ss << "\"enabled\":" << (ext.enabled ? "true" : "false") << ",";
        ss << "\"hasIcon\":" << (!ext.icon_path.empty() ? "true" : "false") << ",";
        ss << "\"contentScripts\":" << ext.content_scripts.size() << ",";
        ss << "\"matchPatterns\":[";
        
        bool firstPattern = true;
        for (const auto& pattern : ext.match_patterns) {
            if (!firstPattern) ss << ",";
            firstPattern = false;
            ss << "\"" << EscapeJS(pattern) << "\"";
        }
        ss << "],";
        ss << "\"loadedAt\":" << ext.loaded_at;
        ss << "}";
    }
    
    ss << "]";
    return ss.str();
}

bool ExtensionManager::SaveState() const {
    std::ofstream file(state_file_);
    if (!file) return false;
    
    file << "{\n  \"extensions\": {\n";
    
    bool first = true;
    for (const auto& ext : extensions_) {
        if (!first) file << ",\n";
        first = false;
        file << "    \"" << ext.id << "\": { \"enabled\": " << (ext.enabled ? "true" : "false") << " }";
    }
    
    file << "\n  }\n}\n";
    return true;
}

bool ExtensionManager::LoadState() {
    if (!std::filesystem::exists(state_file_)) return false;
    
    std::string content = ReadFileContents(state_file_);
    if (content.empty()) return false;
    
    // Parse enabled states
    for (auto& ext : extensions_) {
        std::string search = "\"" + ext.id + "\"";
        size_t pos = content.find(search);
        if (pos != std::string::npos) {
            // Find "enabled" value after this
            size_t enabledPos = content.find("\"enabled\"", pos);
            if (enabledPos != std::string::npos && enabledPos < content.find('}', pos)) {
                size_t colonPos = content.find(':', enabledPos);
                if (colonPos != std::string::npos) {
                    size_t valueStart = colonPos + 1;
                    while (valueStart < content.size() && std::isspace(content[valueStart]))
                        valueStart++;
                    
                    if (content.substr(valueStart, 4) == "true") {
                        ext.enabled = true;
                    } else if (content.substr(valueStart, 5) == "false") {
                        ext.enabled = false;
                    }
                }
            }
        }
    }
    
    return true;
}

bool ExtensionManager::ImportExtension(const std::filesystem::path& source) {
    // For now, just support folder import (copy to extensions dir)
    if (std::filesystem::is_directory(source)) {
        auto dest = extensions_dir_ / source.filename();
        
        if (std::filesystem::exists(dest)) {
            std::filesystem::remove_all(dest);
        }
        
        std::filesystem::copy(source, dest, std::filesystem::copy_options::recursive);
        return LoadExtension(dest);
    }
    
    // TODO: Support .zip import
    return false;
}

bool ExtensionManager::DeleteExtension(const std::string& id) {
    auto ext = GetExtension(id);
    if (!ext) return false;
    
    auto path = ext->base_path;
    
    if (!UnloadExtension(id)) return false;
    
    if (std::filesystem::exists(path)) {
        std::filesystem::remove_all(path);
    }
    
    SaveState();
    NotifyEvent("deleted", id);
    return true;
}

bool ExtensionManager::CreateExtensionTemplate(const std::string& name) {
    // Generate ID from name
    std::string id = name;
    std::transform(id.begin(), id.end(), id.begin(), [](char c) {
        if (std::isalnum(c)) return (char)std::tolower(c);
        return '-';
    });
    
    auto ext_dir = extensions_dir_ / id;
    
    if (std::filesystem::exists(ext_dir)) {
        std::cerr << "[Extensions] Extension already exists: " << id << std::endl;
        return false;
    }
    
    std::filesystem::create_directories(ext_dir);
    
    // Create manifest.json
    std::ofstream manifest(ext_dir / "manifest.json");
    if (!manifest.is_open()) {
        std::cerr << "[Extensions] Failed to create manifest.json" << std::endl;
        return false;
    }
    manifest << "{\n";
    manifest << "  \"name\": \"" << name << "\",\n";
    manifest << "  \"version\": \"1.0.0\",\n";
    manifest << "  \"description\": \"A new extension for Ultralight Browser\",\n";
    manifest << "  \"author\": \"You\",\n";
    manifest << "  \"content_scripts\": [\"content.js\"],\n";
    manifest << "  \"match_patterns\": [\"*\"]\n";
    manifest << "}\n";
    manifest.close();
    
    // Create content.js template
    std::ofstream content(ext_dir / "content.js");
    if (!content.is_open()) {
        std::cerr << "[Extensions] Failed to create content.js" << std::endl;
        return false;
    }
    content << "// " << name << " - Content Script\n";
    content << "// This script runs on every page that matches your patterns.\n\n";
    content << "console.log('[" << name << "] Extension loaded!');\n\n";
    content << "// Example: Add a banner to every page\n";
    content << "// const banner = document.createElement('div');\n";
    content << "// banner.style.cssText = 'position:fixed;top:0;left:0;right:0;padding:5px;background:#8b7cf5;color:white;text-align:center;z-index:99999;';\n";
    content << "// banner.textContent = 'Hello from " << name << "!';\n";
    content << "// document.body.appendChild(banner);\n\n";
    content << "// Use extension storage:\n";
    content << "// __ultralightExtensions.storage.set('myKey', 'myValue');\n";
    content << "// const value = __ultralightExtensions.storage.get('myKey');\n";
    content.close();
    
    return LoadExtension(ext_dir);
}

bool ExtensionManager::ParseManifest(const std::filesystem::path& manifest_path, Extension& ext) {
    std::string content = ReadFileContents(manifest_path);
    if (content.empty()) return false;
    
    ext.name = json::GetStringValue(content, "name");
    if (ext.name.empty()) ext.name = ext.id; // Fallback to ID
    
    ext.version = json::GetStringValue(content, "version");
    if (ext.version.empty()) ext.version = "1.0.0";
    
    ext.description = json::GetStringValue(content, "description");
    ext.author = json::GetStringValue(content, "author");
    ext.icon_path = json::GetStringValue(content, "icon");
    ext.background_script = json::GetStringValue(content, "background");
    ext.content_scripts = json::GetStringArray(content, "content_scripts");
    ext.match_patterns = json::GetStringArray(content, "match_patterns");
    ext.enabled = json::GetBoolValue(content, "enabled", true);
    
    // Default to match all if no patterns specified
    if (ext.match_patterns.empty()) {
        ext.match_patterns.push_back("*");
    }
    
    return true;
}

std::string ExtensionManager::ReadFileContents(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool ExtensionManager::ValidateExtension(const Extension& ext) const {
    // Must have at least a name
    if (ext.name.empty() && ext.id.empty()) return false;
    
    // Must have content scripts or background script
    if (ext.content_scripts.empty() && ext.background_script.empty()) {
        std::cerr << "[Extensions] Extension has no scripts: " << ext.id << std::endl;
        return false;
    }
    
    // Verify content scripts exist
    for (const auto& script : ext.content_scripts) {
        auto path = ext.base_path / script;
        if (!std::filesystem::exists(path)) {
            std::cerr << "[Extensions] Content script not found: " << path << std::endl;
            return false;
        }
    }
    
    // Verify background script exists if specified
    if (!ext.background_script.empty()) {
        auto path = ext.base_path / ext.background_script;
        if (!std::filesystem::exists(path)) {
            std::cerr << "[Extensions] Background script not found: " << path << std::endl;
            return false;
        }
    }
    
    return true;
}

std::string ExtensionManager::EscapeJS(const std::string& str) const {
    std::string result;
    result.reserve(str.size() * 2);
    
    for (char c : str) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\'': result += "\\'"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    
    return result;
}

void ExtensionManager::NotifyEvent(const std::string& event, const std::string& extension_id) {
    if (event_callback_) {
        event_callback_(event, extension_id);
    }
}

} // namespace extensions
