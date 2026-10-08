#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include <functional>

/**
 * Simple Extension System for Ultralight WebBrowser
 * 
 * Extensions are JavaScript files that get injected into web pages.
 * Each extension lives in its own folder under assets/extensions/
 * with a manifest.json describing its metadata.
 * 
 * Example extension structure:
 *   assets/extensions/my-extension/
 *     manifest.json
 *     content.js
 *     (optional) background.js
 *     (optional) icon.png
 */

namespace extensions {

/**
 * Represents a single browser extension
 */
struct Extension {
    std::string id;           // Unique identifier (folder name)
    std::string name;         // Display name
    std::string version;      // Version string
    std::string description;  // Short description
    std::string author;       // Author name
    std::string icon_path;    // Path to icon file (optional)
    
    // Content scripts to inject into pages
    std::vector<std::string> content_scripts;
    
    // Background script (runs once on browser start)
    std::string background_script;
    
    // URL patterns where content scripts should be injected
    // "*" means all pages, or use patterns like "https://*.google.com/*"
    std::vector<std::string> match_patterns;
    
    // Whether extension is currently enabled
    bool enabled = true;
    
    // Full path to extension directory
    std::filesystem::path base_path;
    
    // Load timestamp
    uint64_t loaded_at = 0;
    
    // Check if URL matches any of the match patterns
    bool MatchesURL(const std::string& url) const;
};

/**
 * Extension Manager - handles loading, enabling, and injecting extensions
 */
class ExtensionManager {
public:
    // Singleton accessor
    static ExtensionManager& Instance();
    
    // Delete copy and move constructors for singleton
    ExtensionManager(const ExtensionManager&) = delete;
    ExtensionManager& operator=(const ExtensionManager&) = delete;
    
    // Initialize and load all extensions from the extensions directory
    void Initialize(const std::filesystem::path& extensions_dir);
    
    // Reload all extensions from disk
    void ReloadAll();
    
    // Load a single extension from a directory
    bool LoadExtension(const std::filesystem::path& extension_dir);
    
    // Unload an extension by ID
    bool UnloadExtension(const std::string& id);
    
    // Enable/disable an extension
    bool SetExtensionEnabled(const std::string& id, bool enabled);
    
    // Get all loaded extensions
    const std::vector<Extension>& GetExtensions() const { return extensions_; }
    
    // Get extension by ID
    const Extension* GetExtension(const std::string& id) const;
    
    // Get combined content scripts for a given URL
    // Returns JavaScript code to inject
    std::string GetContentScriptsForURL(const std::string& url) const;
    
    // Get background scripts for all enabled extensions
    std::string GetBackgroundScripts() const;
    
    // Get JSON representation of all extensions (for UI)
    std::string GetExtensionsJSON() const;
    
    // Save extension states (enabled/disabled) to disk
    bool SaveState() const;
    
    // Delete an extension
    bool DeleteExtension(const std::string& id);

private:
    ExtensionManager();
    ~ExtensionManager();
    
    // Load extension states from disk
    bool LoadState();
    
    // Import an extension from a .zip or folder
    bool ImportExtension(const std::filesystem::path& source);
    
    // Create a new empty extension with template files
    bool CreateExtensionTemplate(const std::string& name);
    
    // Get the extensions directory path
    const std::filesystem::path& GetExtensionsDirectory() const { return extensions_dir_; }
    
    // Callback for extension events
    using ExtensionEventCallback = std::function<void(const std::string& event, const std::string& extension_id)>;
    void SetEventCallback(ExtensionEventCallback callback) { event_callback_ = callback; }

private:
    // Parse manifest.json for an extension
    bool ParseManifest(const std::filesystem::path& manifest_path, Extension& ext);
    
    // Read file contents as string
    std::string ReadFileContents(const std::filesystem::path& path) const;
    
    // Validate extension structure
    bool ValidateExtension(const Extension& ext) const;
    
    // Escape JavaScript string for injection
    std::string EscapeJS(const std::string& str) const;
    
    // Notify event callback
    void NotifyEvent(const std::string& event, const std::string& extension_id);
    
    std::vector<Extension> extensions_;
    std::filesystem::path extensions_dir_;
    std::filesystem::path state_file_;
    ExtensionEventCallback event_callback_;
    bool initialized_ = false;
};

// URL pattern matching helper
bool MatchURLPattern(const std::string& pattern, const std::string& url);

} // namespace extensions
