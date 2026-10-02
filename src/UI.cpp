#include "UI.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <Ultralight/Renderer.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <ctime>
#include <array>
#include <vector>
#include <cstdlib>
#include <exception>
#include "DownloadManager.h"
#include "PasswordManager.h"
#include "Settings.h"
#include "Utils.h"
#include "AdBlocker.h"
#include "drm/DRMWebViewManager.h"
#include "drm/DRMWebViewTab.h"
#ifdef _WIN32
#include <direct.h> // _mkdir, _getcwd
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h> // GetModuleFileNameW
#include <dwmapi.h>  // DwmSetWindowAttribute for window theming
#pragma comment(lib, "dwmapi.lib")
#else
#include <sys/stat.h> // mkdir
#include <unistd.h>   // getcwd
#endif

static UI *g_ui = 0;

// History and session were written to a path relative to the working directory
// ("data/history.json"), while settings and themes used the absolute
// SettingsDirectory(). That split meant launching the browser from anywhere
// other than the folder holding "data" silently wrote a fresh, empty history and
// session -- the same working-directory fragility that made the ICU lookup fail.
//
// These helpers resolve both files through SettingsDirectory(), with the old
// relative location kept as a read fallback so an existing install keeps its
// history instead of appearing to lose it on upgrade.
namespace
{
  std::filesystem::path DataFilePath(const char *name)
  {
    return UI::SettingsDirectory() / name;
  }

  std::filesystem::path LegacyDataFilePath(const char *name)
  {
    return std::filesystem::path("data") / name;
  }

  // Opens a data file for reading, transparently falling back to the legacy
  // working-directory-relative copy. Returns an unopened stream if neither
  // exists, so callers keep their existing "no file yet" behaviour.
  std::ifstream OpenDataFileForRead(const char *name)
  {
    std::ifstream in(DataFilePath(name), std::ios::in | std::ios::binary);
    if (in.is_open())
      return in;

    std::ifstream legacy(LegacyDataFilePath(name), std::ios::in | std::ios::binary);
    if (legacy.is_open())
      return legacy;

    return std::ifstream();
  }
} // namespace

// The browser's own pages, shared by LoadCachedInternalPages (which preloads a
// subset) and IsInternalPageURL (which decides scroll behaviour). Kept at
// namespace scope so both functions provably agree; a per-function copy is how
// "newtab.html" ended up in one list while the real file was new_tab_page.html.
static const char *const kInternalPageNames[] = {
    "ui.html",
    "settings.html",
    "new_tab_page.html",
    "history.html",
    "bookmarks.html",
    "downloads.html",
    "downloads-panel.html",
    "passwords.html",
    "themes.html",
    "extensions.html",
    "about.html",
    "release_notes.html",
    "suggestions.html",
    "menu.html",
    "contextmenu.html",
    "drm_loading.html",
    "quick-inspector.html",
    "tab_search.html",
};

#define UI_HEIGHT 80
#define UI_HEIGHT_COMPACT 60 // Reduced height when compact tabs mode is enabled

namespace
{
  constexpr int kDownloadsOverlaySpacing = 8;

  // Input-dispatch diagnostic, active only with ULTRALIGHT_VERBOSE=1.
  //
  // Reports how many events the window listener actually receives and how long
  // the handler spends. This is what separates the two failure modes that look
  // identical from the outside: a handler that is genuinely slow, versus an
  // engine that is delivering the events late because it is not presenting
  // frames. The latter shows up as few events with a fast handler.
  class InputDiagScope
  {
  public:
    explicit InputDiagScope(const char *label) : label_(label)
    {
      if (!UI::VerboseLogging())
        return;
      enabled_ = true;
      start_ = std::chrono::steady_clock::now();

      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              start_.time_since_epoch())
                              .count();
      if (now_ms - last_report_ms_ >= 2000)
      {
        if (count_ > 0)
        {
          std::fprintf(stderr, "[diag] %s: %llu events, %.2f ms avg in handler\n",
                       label_, (unsigned long long)count_, total_ms_ / (double)count_);
        }
        count_ = 0;
        total_ms_ = 0.0;
        last_report_ms_ = now_ms;
      }
    }

    ~InputDiagScope()
    {
      if (!enabled_)
        return;
      const auto end = std::chrono::steady_clock::now();
      total_ms_ += std::chrono::duration<double, std::milli>(end - start_).count();
      count_++;
    }

  private:
    const char *label_;
    bool enabled_ = false;
    std::chrono::steady_clock::time_point start_;
    // Shared across instances of this label only in spirit; static per-type is
    // good enough for a diagnostic and avoids allocating a map on the hot path.
    static inline uint64_t count_ = 0;
    static inline double total_ms_ = 0.0;
    static inline uint64_t last_report_ms_ = 0;
  };

  // ultralight::JSString only offers an implicit conversion to ultralight::String,
  // and ultralight::String is the type that owns the UTF-8 buffer. String::utf8()
  // hands back a reference into the receiver, so the intermediate String has to be
  // copied rather than referenced or the result would dangle.
  inline ultralight::String8 JSStringToUtf8(ultralight::JSString value)
  {
    return ultralight::String(value).utf8();
  }

  struct SettingDescriptor
  {
    const char *key;
    const char *name;
    const char *description;
    const char *category;
    const char *note;
    bool reload_page;
    bool UI::BrowserSettings::*member;
    bool default_value;

    // Integer settings. When is_number is true the trailing five fields are
    // authoritative and `member`/`default_value` are ignored.
    bool is_number = false;
    int UI::BrowserSettings::*number_member = nullptr;
    int number_default = 0;
    int min_value = 0;
    int max_value = 0;
  };

  constexpr std::array<SettingDescriptor, 35> kFallbackSettingsCatalog = {
      // Appearance
      SettingDescriptor{"launch_dark_theme", "Launch in dark theme",
                        "Start Ultralight with dark chrome, toolbars, and tabs by default.",
                        "appearance", nullptr, false, &UI::BrowserSettings::launch_dark_theme, false},
      SettingDescriptor{"vibrant_window_theme", "Vibrant window theme",
                        "Apply a subtle color wash to the window frame for a livelier finish.",
                        "appearance", nullptr, false, &UI::BrowserSettings::vibrant_window_theme, false},
      SettingDescriptor{"experimental_transparent_toolbar", "Transparent toolbar",
                        "Blend the toolbar into page content with a translucent, glass-like surface.",
                        "appearance", "Experimental", false, &UI::BrowserSettings::experimental_transparent_toolbar, false},
      SettingDescriptor{"experimental_compact_tabs", "Compact tabs",
                        "Reduce tab height and spacing so more tabs stay visible without scrolling.",
                        "appearance", "Experimental", true, &UI::BrowserSettings::experimental_compact_tabs, false},

      // Privacy & Security
      SettingDescriptor{"enable_adblock", "Enable ad blocking",
                        "Filter network requests using bundled block lists to hide intrusive ads.",
                        "privacy", nullptr, false, &UI::BrowserSettings::enable_adblock, true},
      SettingDescriptor{"log_blocked_requests", "Log blocked requests",
                        "Write each blocked network request to the console for debugging rules.",
                        "privacy", nullptr, false, &UI::BrowserSettings::log_blocked_requests, false},
      SettingDescriptor{"clear_history_on_exit", "Clear history on exit",
                        "Remove browsing history when Ultralight closes and skip saving new visits.",
                        "privacy", nullptr, false, &UI::BrowserSettings::clear_history_on_exit, true},
      SettingDescriptor{"enable_javascript", "Enable JavaScript",
                        "Allow websites to run JavaScript code for interactive features and dynamic content.",
                        "privacy", nullptr, false, &UI::BrowserSettings::enable_javascript, true},
      SettingDescriptor{"enable_web_security", "Enable web security",
                        "Enforce same-origin policy and other web security restrictions.",
                        "privacy", nullptr, false, &UI::BrowserSettings::enable_web_security, true},
      SettingDescriptor{"block_third_party_cookies", "Block third-party cookies",
                        "Prevent websites from setting cookies that track you across different sites.",
                        "privacy", nullptr, false, &UI::BrowserSettings::block_third_party_cookies, false},
      SettingDescriptor{"do_not_track", "Send Do Not Track header",
                        "Request that websites not track your browsing activity.",
                        "privacy", nullptr, false, &UI::BrowserSettings::do_not_track, true},

      // Address Bar & Suggestions
      SettingDescriptor{"enable_suggestions", "Show address bar suggestions",
                        "Surface history matches and popular sites while typing in the address bar.",
                        "suggestions", nullptr, false, &UI::BrowserSettings::enable_suggestions, true},
      SettingDescriptor{"enable_suggestion_favicons", "Show favicons in suggestions",
                        "Display site icons next to suggestion rows whenever an icon is available.",
                        "suggestions", nullptr, false, &UI::BrowserSettings::enable_suggestion_favicons, true},

      // Downloads
      SettingDescriptor{"show_download_badge", "Show download badge",
                        "Highlight the toolbar downloads button whenever transfers are active.",
                        "downloads", nullptr, false, &UI::BrowserSettings::show_download_badge, true},
      SettingDescriptor{"auto_open_download_panel", "Open downloads panel automatically",
                        "Pop open the quick downloads overlay as soon as a new download begins.",
                        "downloads", nullptr, false, &UI::BrowserSettings::auto_open_download_panel, true},
      SettingDescriptor{"ask_download_location", "Ask where to save downloads",
                        "Show a file picker dialog for each download instead of using default location.",
                        "downloads", nullptr, false, &UI::BrowserSettings::ask_download_location, false},
      SettingDescriptor{"convert_webp_to_png", "Convert WebP to PNG",
                        "Automatically convert downloaded WebP images to PNG format for better compatibility.",
                        "downloads", nullptr, false, &UI::BrowserSettings::convert_webp_to_png, false},

      // Performance
      SettingDescriptor{"smooth_scrolling", "Smooth scrolling",
                        "Enable smooth animated scrolling for a more fluid browsing experience.",
                        "performance", nullptr, false, &UI::BrowserSettings::smooth_scrolling, true},
      SettingDescriptor{"hardware_acceleration", "Hardware acceleration",
                        "Use GPU to accelerate graphics rendering for better performance.",
                        "performance", nullptr, false, &UI::BrowserSettings::hardware_acceleration, true},
      SettingDescriptor{"enable_local_storage", "Enable local storage",
                        "Allow websites to store data locally for offline functionality.",
                        "performance", nullptr, false, &UI::BrowserSettings::enable_local_storage, true},
      SettingDescriptor{"enable_database", "Enable database storage",
                        "Allow websites to use IndexedDB and Web SQL for data storage.",
                        "performance", nullptr, false, &UI::BrowserSettings::enable_database, true},
      SettingDescriptor{"low_ram_mode", "Low-RAM mode",
                        "Watch the process working set and reclaim memory from background tabs when it grows past your budget.",
                        "performance", nullptr, false, &UI::BrowserSettings::low_ram_mode, false},
      SettingDescriptor{"memory_budget_mb", "Memory budget (MB)",
                        "Working-set budget in megabytes. Once usage stays above it for a few seconds, low-RAM mode kicks in.",
                        "performance", "Used by low-RAM mode", false, nullptr, false,
                        true, &UI::BrowserSettings::memory_budget_mb, 512, 64, 16384},

      // Accessibility
      SettingDescriptor{"reduce_motion", "Reduce motion effects",
                        "Limit animated transitions and parallax flourishes for a calmer experience.",
                        "accessibility", nullptr, false, &UI::BrowserSettings::reduce_motion, false},
      SettingDescriptor{"high_contrast_ui", "High contrast UI",
                        "Boost contrast for overlays, menus, and dialogs to improve readability.",
                        "accessibility", nullptr, false, &UI::BrowserSettings::high_contrast_ui, false},
      SettingDescriptor{"enable_caret_browsing", "Enable caret browsing",
                        "Navigate web pages using keyboard cursor like in a text editor.",
                        "accessibility", nullptr, false, &UI::BrowserSettings::enable_caret_browsing, false},

      // Developer
      SettingDescriptor{"enable_remote_inspector", "Enable remote inspector",
                        "Allow remote debugging via Chrome DevTools Protocol.",
                        "developer", nullptr, false, &UI::BrowserSettings::enable_remote_inspector, false},
      SettingDescriptor{"show_performance_overlay", "Show performance overlay",
                        "Display FPS counter and rendering statistics on screen.",
                        "developer", nullptr, false, &UI::BrowserSettings::show_performance_overlay, false},

      // General behavior
      SettingDescriptor{"auto_save_settings", "Auto save settings",
                        "Automatically save changes to settings as soon as you toggle options.",
                        "general", nullptr, false, &UI::BrowserSettings::auto_save_settings, true},

      // Session restore
      SettingDescriptor{"restore_session_on_startup", "Restore previous session",
                        "Reopen tabs from your last browsing session when starting the browser.",
                        "general", nullptr, false, &UI::BrowserSettings::restore_session_on_startup, true},
      SettingDescriptor{"save_session_continuously", "Enable crash recovery",
                        "Continuously save session state so tabs can be restored after crashes or unexpected closures.",
                        "general", nullptr, false, &UI::BrowserSettings::save_session_continuously, true},

      // DRM subsystem
      SettingDescriptor{"enable_drm_webview", "Enable DRM WebView",
                        "Automatically switch Widevine-protected sites to a native DRM-capable WebView.",
                        "drm", "Requires native runtime", false, &UI::BrowserSettings::enable_drm_webview, false},
      SettingDescriptor{"media_fallback_enabled", "Media Fallback",
                        "Open known video sites in the native OS WebView. The bundled engine has no video codecs, so playback shows an empty box otherwise.",
                        "drm", "Requires native runtime", false, &UI::BrowserSettings::media_fallback_enabled, false},

      // Networking / User Agent
      SettingDescriptor{"use_custom_user_agent", "Use custom user agent",
                        "When enabled, send a user agent string that you specify instead of the automatic Chromium-like default.",
                        "privacy", nullptr, false, &UI::BrowserSettings::use_custom_user_agent, false},

      // Location Spoofing
      SettingDescriptor{"enable_location_spoofing", "Location Spoofing",
                        "Override navigator.geolocation to report custom GPS coordinates instead of your real location.",
                        "privacy", nullptr, false, &UI::BrowserSettings::enable_location_spoofing, false}};

  struct ParsedCatalogEntry
  {
    std::string key;
    std::string name;
    std::string description;
    std::string category;
    std::string note;
    bool has_note = false;
    bool has_default = false;
    bool default_value = false;
    bool has_reload_page = false;
    bool reload_page = false;
  };

  // RuntimeSettingDescriptor is now declared in UI.h and used throughout the project.

  std::vector<RuntimeSettingDescriptor> g_settings_catalog;
  std::unordered_map<std::string, size_t> g_settings_index;
  bool g_settings_initialized = false;

  std::string::size_type FindMatchingBrace(const std::string &text, std::string::size_type open_pos)
  {
    size_t depth = 0;
    for (size_t i = open_pos; i < text.size(); ++i)
    {
      char c = text[i];
      if (c == '{')
        ++depth;
      else if (c == '}')
      {
        if (depth == 0)
          return std::string::npos;
        --depth;
        if (depth == 0)
          return i;
      }
      else if (c == '"')
      {
        // Skip quoted strings entirely (handle escapes)
        ++i;
        bool escape = false;
        for (; i < text.size(); ++i)
        {
          char qc = text[i];
          if (escape)
          {
            escape = false;
            continue;
          }
          if (qc == '\\')
          {
            escape = true;
            continue;
          }
          if (qc == '"')
            break;
        }
      }
    }
    return std::string::npos;
  }

  bool ExtractJsonStringField(const std::string &object, const char *field, std::string &out)
  {
    if (!field)
      return false;
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = object.find(needle);
    if (pos == std::string::npos)
      return false;
    pos = object.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return false;
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos])))
      ++pos;
    if (pos >= object.size())
      return false;
    if (object[pos] == 'n' || object[pos] == 'N')
    {
      // Treat explicit null as absence
      if (object.compare(pos, 4, "null") == 0 || object.compare(pos, 4, "NULL") == 0)
        return false;
    }
    if (object[pos] != '"')
      return false;
    ++pos;
    std::string value;
    bool escape = false;
    while (pos < object.size())
    {
      char c = object[pos++];
      if (escape)
      {
        escape = false;
        switch (c)
        {
        case '"':
          value.push_back('"');
          break;
        case '\\':
          value.push_back('\\');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          value.push_back(c);
          break;
        }
        continue;
      }
      if (c == '\\')
      {
        escape = true;
        continue;
      }
      if (c == '"')
        break;
      value.push_back(c);
    }
    out = std::move(value);
    return true;
  }

  bool ExtractJsonBoolField(const std::string &object, const char *field, bool &out)
  {
    if (!field)
      return false;
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = object.find(needle);
    if (pos == std::string::npos)
      return false;
    pos = object.find(':', pos + needle.size());
    if (pos == std::string::npos)
      return false;
    ++pos;
    while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos])))
      ++pos;
    if (pos >= object.size())
      return false;
    if (object.compare(pos, 4, "true") == 0 || object[pos] == '1')
    {
      out = true;
      return true;
    }
    if (object.compare(pos, 5, "false") == 0 || object[pos] == '0')
    {
      out = false;
      return true;
    }
    return false;
  }

  void LoadSettingsCatalogFromFile(std::unordered_map<std::string, ParsedCatalogEntry> &out)
  {
    std::ifstream in("assets/settings_catalog.json", std::ios::in | std::ios::binary);
    if (!in.is_open())
      return;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string json = ss.str();
    in.close();

    size_t pos = 0;
    while (pos < json.size())
    {
      size_t open = json.find('{', pos);
      if (open == std::string::npos)
        break;
      size_t close = FindMatchingBrace(json, open);
      if (close == std::string::npos)
        break;
      std::string object = json.substr(open, close - open + 1);
      pos = close + 1;

      ParsedCatalogEntry entry;
      if (!ExtractJsonStringField(object, "key", entry.key) || entry.key.empty())
        continue;
      ExtractJsonStringField(object, "name", entry.name);
      ExtractJsonStringField(object, "description", entry.description);
      ExtractJsonStringField(object, "category", entry.category);
      std::string noteValue;
      if (ExtractJsonStringField(object, "note", noteValue))
      {
        entry.note = std::move(noteValue);
        entry.has_note = true;
      }
      else if (object.find("\"note\"") != std::string::npos)
      {
        // note was present but null
        entry.note.clear();
        entry.has_note = true;
      }
      bool defValue = false;
      if (ExtractJsonBoolField(object, "default", defValue))
      {
        entry.has_default = true;
        entry.default_value = defValue;
      }
      bool reloadValue = false;
      if (ExtractJsonBoolField(object, "reload_page", reloadValue))
      {
        entry.has_reload_page = true;
        entry.reload_page = reloadValue;
      }

      out[entry.key] = std::move(entry);
    }
  }

  void EnsureSettingsCatalogInitialized()
  {
    if (g_settings_initialized)
      return;
    g_settings_initialized = true;

    std::unordered_map<std::string, ParsedCatalogEntry> parsed_entries;
    LoadSettingsCatalogFromFile(parsed_entries);

    g_settings_catalog.clear();
    g_settings_index.clear();
    g_settings_catalog.reserve(kFallbackSettingsCatalog.size());

    for (const auto &fallback : kFallbackSettingsCatalog)
    {
      RuntimeSettingDescriptor runtime;
      runtime.key = fallback.key ? fallback.key : "";
      runtime.name = fallback.name ? fallback.name : runtime.key;
      runtime.description = fallback.description ? fallback.description : "";
      runtime.category = fallback.category ? fallback.category : "";
      runtime.note = fallback.note ? fallback.note : "";
      runtime.member = fallback.member;
      runtime.default_value = fallback.default_value;
      runtime.reload_page = fallback.reload_page;
      runtime.is_number = fallback.is_number;
      runtime.number_member = fallback.number_member;
      runtime.number_default = fallback.number_default;
      runtime.min_value = fallback.min_value;
      runtime.max_value = fallback.max_value;

      auto it = parsed_entries.find(runtime.key);
      if (it != parsed_entries.end())
      {
        const ParsedCatalogEntry &meta = it->second;
        if (!meta.name.empty())
          runtime.name = meta.name;
        if (!meta.description.empty())
          runtime.description = meta.description;
        if (!meta.category.empty())
          runtime.category = meta.category;
        if (meta.has_note)
          runtime.note = meta.note;
        if (meta.has_default)
          runtime.default_value = meta.default_value;
        if (meta.has_reload_page)
          runtime.reload_page = meta.reload_page;
      }

      g_settings_index[runtime.key] = g_settings_catalog.size();
      g_settings_catalog.push_back(std::move(runtime));
    }
  }

  // The following functions are declared in UI.h and defined in global namespace below

  // Use util:: helpers for escaping and time operations

}

// Global definitions of the catalog accessors declared in UI.h
const std::vector<RuntimeSettingDescriptor> &GetSettingsCatalog()
{
  EnsureSettingsCatalogInitialized();
  return g_settings_catalog;
}

const RuntimeSettingDescriptor *FindSettingDescriptor(const std::string &key)
{
  EnsureSettingsCatalogInitialized();
  auto it = g_settings_index.find(key);
  if (it == g_settings_index.end())
    return nullptr;
  return &g_settings_catalog[it->second];
}

UI::UI(RefPtr<Window> window)
    : window_(window), cur_cursor_(Cursor::kCursor_Pointer),
      is_resizing_inspector_(false), is_over_inspector_resize_drag_handle_(false),
    drm_settings_(SettingsDirectory() / "drm_settings.json"),
    media_fallback_("assets/media_sites.json")
  {
  uint32_t window_width = window_->width();
  ui_height_ = (uint32_t)std::round(UI_HEIGHT * window_->scale());
  base_ui_height_ = ui_height_;
  overlay_ = Overlay::Create(window_, window_width, ui_height_, 0, 0);
  g_ui = this;

  // Prepare settings and state BEFORE loading the main UI document so the first snapshot reflects persisted values.
  LoadSuggestionsFaviconsFlag();
  EnsureDataDirectoryExists();
  settings_storage_path_ = SettingsFilePath().string();
  LoadSettingsFromDisk();

  // No adblock-specific hooks here; UA overrides are applied in Tab
  // when new views are created.

  // Hook listeners and then load UI document
  view()->set_load_listener(this);
  view()->set_view_listener(this);
  view()->LoadURL("file:///ui.html");

  download_manager_ = std::make_unique<DownloadManager>();
  download_manager_->SetOnChangeCallback([this]()
                                         { NotifyDownloadsChanged(); });
  download_manager_->SetWebPConversionCallback([this]()
                                               { return settings_.convert_webp_to_png; });

  // Initialize password manager
  password_manager_ = std::make_unique<password::PasswordManager>();
  password_manager_->Initialize(SettingsDirectory());

  // Initialize bookmark store
  bookmark_store_ = std::make_unique<BookmarkStore>();
  bookmark_store_->Initialize(SettingsDirectory());

  // Initialize the low-RAM memory monitor and apply the persisted budget.
  memory_monitor_ = std::make_unique<memory::MemoryMonitor>();
  memory_monitor_->SetLowRamModeEnabled(settings_.low_ram_mode);
  memory_monitor_->SetBudgetBytes(
      static_cast<uint64_t>(settings_.memory_budget_mb) * 1024ull * 1024ull);
  StartMemoryWatchdog();

  // Apply runtime toggles (visual sync happens on DOMReady via SyncSettingsStateToUI)
  ApplySettings(true, true);

  // Pre-load start page HTML for instant new tab creation
  LoadCachedStartPage();
  // Pre-load internal browser pages for instant loading
  LoadCachedInternalPages();

  // Load keyboard shortcuts mapping
  LoadShortcuts();

  // Load popular sites for suggestions
  LoadPopularSites();
  // Load favicon disk cache
  LoadFaviconDiskCache();

  // Load history from disk
  LoadHistoryFromDisk();

  // Load session data for crash recovery
  LoadSessionFromDisk();

  // Restore the reopen-closed-tab stack. Independent of continuous session
  // saving, so Ctrl+Shift+T still works across restarts when that is off.
  LoadClosedTabs();
}

// Compatibility overload: accepts optional ad/tracker blockers (ignored if not used)
UI::UI(RefPtr<Window> window, AdBlocker *adblock, AdBlocker *tracker)
    : window_(window), cur_cursor_(Cursor::kCursor_Pointer),
      is_resizing_inspector_(false), is_over_inspector_resize_drag_handle_(false),
      adblock_(adblock), trackerblock_(tracker),
    drm_settings_(SettingsDirectory() / "drm_settings.json"),
    media_fallback_("assets/media_sites.json")
  {
  uint32_t window_width = window_->width();
  ui_height_ = (uint32_t)std::round(UI_HEIGHT * window_->scale());
  base_ui_height_ = ui_height_;
  overlay_ = Overlay::Create(window_, window_width, ui_height_, 0, 0);
  g_ui = this;
   
  LoadSuggestionsFaviconsFlag();
  EnsureDataDirectoryExists();
  settings_storage_path_ = SettingsFilePath().string();
  LoadSettingsFromDisk();
  // UA overrides are applied in Tab when views are created.

  view()->set_load_listener(this);
  view()->set_view_listener(this);
  view()->LoadURL("file:///ui.html");

  download_manager_ = std::make_unique<DownloadManager>();
  download_manager_->SetOnChangeCallback([this]()
                                         { NotifyDownloadsChanged(); });
  download_manager_->SetWebPConversionCallback([this]()
                                               { return settings_.convert_webp_to_png; });

  // Initialize password manager
  password_manager_ = std::make_unique<password::PasswordManager>();
  password_manager_->Initialize(SettingsDirectory());

  // Initialize bookmark store
  bookmark_store_ = std::make_unique<BookmarkStore>();
  bookmark_store_->Initialize(SettingsDirectory());

  // Initialize the low-RAM memory monitor and apply the persisted budget.
  memory_monitor_ = std::make_unique<memory::MemoryMonitor>();
  memory_monitor_->SetLowRamModeEnabled(settings_.low_ram_mode);
  memory_monitor_->SetBudgetBytes(
      static_cast<uint64_t>(settings_.memory_budget_mb) * 1024ull * 1024ull);
  StartMemoryWatchdog();

  // Apply runtime toggles (visual sync happens on DOMReady via SyncSettingsStateToUI)
  ApplySettings(true, true);

  // Pre-load start page HTML for instant new tab creation
  LoadCachedStartPage();
  // Pre-load internal browser pages for instant loading
  LoadCachedInternalPages();

  // Load keyboard shortcuts mapping
  LoadShortcuts();

  // Load popular sites for suggestions
  LoadPopularSites();
  // Load favicon disk cache
  LoadFaviconDiskCache();

  // Load history from disk
  LoadHistoryFromDisk();

  // Load session data for crash recovery
  LoadSessionFromDisk();

  // Initialize extension system
  InitializeExtensions();

  adblock_enabled_cached_ = adblock_ ? adblock_->enabled() : adblock_enabled_cached_;

  // Pre-warm WebView2 environment in background for faster DRM tab creation
  if (settings_.enable_drm_webview)
    drm::PrewarmWebViewEnvironment();
}

Tab *UI::active_tab()
{
  auto it = tabs_.find(active_tab_id_);
  if (it == tabs_.end())
    return nullptr;
  return it->second.get();
}

drm::DRMWebViewTab *UI::active_drm_tab()
{
  auto it = drm_tabs_.find(active_tab_id_);
  if (it == drm_tabs_.end())
    return nullptr;
  return it->second.get();
}

bool UI::ActiveTabIsDRM() const
{
  auto it = drm_tabs_.find(active_tab_id_);
  if (it == drm_tabs_.end())
    return false;
  return it->second != nullptr;
}

Tab *UI::GetUltralightTab(uint64_t id)
{
  auto it = tabs_.find(id);
  if (it == tabs_.end())
    return nullptr;
  return it->second.get();
}

drm::DRMWebViewTab *UI::GetDrmTab(uint64_t id)
{
  auto it = drm_tabs_.find(id);
  if (it == drm_tabs_.end())
    return nullptr;
  return it->second.get();
}

void UI::HideDrmTab(uint64_t id)
{
  auto it = drm_tabs_.find(id);
  if (it == drm_tabs_.end() || !it->second)
    return;
  it->second->Blur(); // Release focus before hiding
  it->second->Hide();
  if (id == active_tab_id_)
  {
    auto tab_it = tabs_.find(id);
    if (tab_it != tabs_.end() && tab_it->second)
    {
      tab_it->second->Show();
      tab_it->second->view()->Focus(); // Give focus back to Ultralight tab
    }
  }
  drm_tab_titles_.erase(id);
  drm_tab_urls_.erase(id);
  UpdateDrmBadge(id, false);
}

void UI::HideAllDrmTabs()
{
  // Hide ALL DRM tabs to ensure none interfere with input
  for (auto &entry : drm_tabs_)
  {
    if (entry.second)
    {
      entry.second->Blur();
      entry.second->Hide();
    }
  }
}

void UI::UpdateDrmBadge(uint64_t id, bool is_drm)
{
  if (!setTabDrmState)
    return;
  RefPtr<JSContext> lock(view()->LockJSContext());
  setTabDrmState({static_cast<double>(id), is_drm ? 1.0 : 0.0});
}

void UI::EnsureDrmManager()
{
  if (drm_manager_)
    return;
  void *native = window_ ? window_->native_handle() : nullptr;
  drm_manager_ = std::make_unique<drm::DRMWebViewManager>(native);
  // Pre-warm the WebView environment to reduce first-load lag
  drm::PrewarmWebViewEnvironment();
}

bool UI::MaybeOpenMediaTab(uint64_t tab_id, const std::string &url)
{
  if (!media_fallback_enabled_)
    return false;

  // DRM takes precedence: those sites are already routed, they show the DRM
  // prompt, and re-routing them here would skip that.
  if (drm_settings_.IsDrmSite(url))
    return false;

  if (!media_fallback_.IsMediaSite(url))
    return false;

  AppendDrmLog("Media fallback: opening in the system webview: " + url);
  return OpenSystemWebViewTab(tab_id, url, "Loading Media Player...");
}

bool UI::MaybeOpenDrmTab(uint64_t tab_id, const std::string &url, bool user_initiated)
{
  // Check if URL matches a DRM site (ignores DRMSettings enabled_ flag)
  if (!drm_settings_.IsDrmSite(url))
  {
    // URL is not a DRM site
    return false;
  }

  if (!settings_.enable_drm_webview)
  {
    // DRM webview is disabled in browser settings - show prompt to user
    ShowDrmPrompt(url, tab_id);
    return false;
  }

  // URL matched a DRM site - open it in the system webview.
  AppendDrmLog("Opening DRM tab for: " + url);
  return OpenSystemWebViewTab(tab_id, url, "Loading DRM System...");
}

bool UI::OpenSystemWebViewTab(uint64_t tab_id, const std::string &url, const std::string &loading_title)
{
  EnsureDrmManager();
  if (!drm_manager_)
    return false;

  auto *dependency_manager = drm_manager_->dependency_manager();
  if (dependency_manager && !dependency_manager->IsInstalled())
  {
    AppendDrmLog("Cannot open a system webview tab because " + dependency_manager->GetName() + " is not installed.");
    return false;
  }

  auto it = tabs_.find(tab_id);
  if (it == tabs_.end())
    return false;
  auto *ultra_tab = it->second.get();

  drm::DRMWebViewConfig config;
  config.parent_window = window_ ? window_->native_handle() : nullptr;
  config.width = window_ ? window_->width() : 0;
  uint32_t height = window_ ? window_->height() : 0;
  uint32_t ui_height = ui_height_ > 0 ? static_cast<uint32_t>(ui_height_) : 0;
  config.height = height > ui_height ? height - ui_height : height;
  config.offset_x = 0;
  config.offset_y = ui_height;

  drm::DRMWebViewCallbacks callbacks;
  callbacks.on_title_changed = [this](uint64_t id, const std::string &title)
  { HandleDrmTitleChanged(id, title); };
  callbacks.on_url_changed = [this](uint64_t id, const std::string &new_url)
  { HandleDrmUrlChanged(id, new_url); };
  callbacks.on_loading_state = [this](uint64_t id, bool loading)
  { HandleDrmLoading(id, loading); };
  callbacks.on_navigation_state = [this](uint64_t id, bool can_back, bool can_forward)
  {
    HandleDrmNavigationState(id, can_back, can_forward);
  };

  auto drm_it = drm_tabs_.find(tab_id);
  if (drm_it == drm_tabs_.end() || !drm_it->second)
  {
    // Create new DRM tab
    drm_tabs_[tab_id] = drm_manager_->CreateTab(tab_id, config, callbacks);
    drm_it = drm_tabs_.find(tab_id);
  }
  else
  {
    // DRM tab already exists - just resize and navigate
    drm_it->second->Resize(config.width, config.height, config.offset_x, config.offset_y);
  }

  if (drm_it == drm_tabs_.end() || !drm_it->second)
  {
    AppendDrmLog("Failed to create the system webview tab. Verify the native runtime is installed (WebView2 on Windows).");
    return false;
  }

  drm_tab_urls_[tab_id] = url;
  drm_tab_titles_[tab_id] = loading_title;

  // Show loading page in Ultralight tab while the system webview initializes
  if (ultra_tab)
  {
    ultra_tab->view()->LoadURL("file:///drm_loading.html");
    ultra_tab->Show(); // Keep showing the loading page
  }
  // DON'T show the system webview yet - it will be shown when it starts loading.
  // This ensures the loading page is visible while it initializes.
  drm_it->second->Hide();
  UpdateDrmBadge(tab_id, true);

  // Update tab UI to show loading state
  {
    RefPtr<JSContext> lock(view()->LockJSContext());
    if (updateTab)
    {
      ultralight::String title_str(loading_title.c_str());
      ultralight::String url_str(url.c_str());
      updateTab({tab_id, title_str, GetFaviconURL(url_str), true}); // true = loading
    }
    // Update URL bar
    if (tab_id == active_tab_id_)
    {
      ultralight::String url_str(url.c_str());
      SetURL(url_str);
    }
  }

  // Set loading state immediately
  if (tab_id == active_tab_id_)
    SetLoading(true);

  // Navigate to URL (this handles pending URL if WebView isn't ready yet)
  drm_it->second->LoadURL(url);
  return true;
}

void UI::HandleDrmTitleChanged(uint64_t tab_id, const std::string &title)
{
  drm_tab_titles_[tab_id] = title;
  RefPtr<JSContext> lock(view()->LockJSContext());
  if (updateTab)
  {
    ultralight::String title_str(title.c_str());
    const auto &url_ref = drm_tab_urls_[tab_id];
    ultralight::String url = url_ref.empty() ? ultralight::String("") : ultralight::String(url_ref.c_str());
    updateTab({tab_id, title_str, GetFaviconURL(url), false});
  }
  if (tab_id == active_tab_id_)
  {
    ultralight::String title_str(title.c_str());
    updateURL({title_str});
  }
}

void UI::HandleDrmUrlChanged(uint64_t tab_id, const std::string &url)
{
  drm_tab_urls_[tab_id] = url;
  ultralight::String url_string(url.c_str());
  if (tab_id == active_tab_id_)
  {
    SetURL(url_string);
  }
}

void UI::HandleDrmLoading(uint64_t tab_id, bool is_loading)
{
  if (tab_id == active_tab_id_)
    SetLoading(is_loading);

  // When WebView2 starts loading content, show it and hide the Ultralight loading page
  if (is_loading)
  {
    // Show the DRM tab now that it's actually loading, but only if no overlays are open
    // Note: suggestions_overlay_ is excluded because it doesn't hide the DRM tab
    auto drm_it = drm_tabs_.find(tab_id);
    if (drm_it != drm_tabs_.end() && drm_it->second)
    {
      // Only show if this is the active tab and no overlays are covering the content
      if (tab_id == active_tab_id_ && !menu_overlay_ && !downloads_overlay_ && !context_menu_overlay_)
        drm_it->second->Show();
    }

    // Pre-load solid background in Ultralight tab so it's ready when overlays open (no lag)
    // The background has a fast fade-in animation for smooth visual transition
    auto tab_it = tabs_.find(tab_id);
    if (tab_it != tabs_.end() && tab_it->second)
    {
      tab_it->second->view()->LoadHTML(R"(<!DOCTYPE html><html><head><style>
html,body{margin:0;padding:0;width:100%;height:100%;overflow:hidden}
body{background:#1a1a2e;animation:fadeIn 0.15s ease-out}
@keyframes fadeIn{from{opacity:0}to{opacity:1}}
</style></head><body></body></html>)");
      tab_it->second->Hide();
    }
  }
}

void UI::HandleDrmNavigationState(uint64_t tab_id, bool can_back, bool can_forward)
{
  if (tab_id == active_tab_id_)
  {
    SetCanGoBack(can_back);
    SetCanGoForward(can_forward);
  }
  RefPtr<JSContext> lock(view()->LockJSContext());
  if (updateTab)
  {
    const auto &title_ref = drm_tab_titles_[tab_id];
    const auto &url_ref = drm_tab_urls_[tab_id];
    ultralight::String title = title_ref.empty() ? ultralight::String("DRM Tab") : ultralight::String(title_ref.c_str());
    ultralight::String url = url_ref.empty() ? ultralight::String("") : ultralight::String(url_ref.c_str());
    updateTab({tab_id, title, GetFaviconURL(url), false});
  }
}

UI::~UI()
{
  // Stop the sampler first so it cannot outlive the monitor it polls.
  StopMemoryWatchdog();

  // Save session one final time with clean_exit flag
  // This preserves tabs for restoration while indicating it was a normal shutdown
  SaveSessionToDiskWithCleanExit();

  // Persist or clear history on shutdown based on settings
  if (clear_history_on_exit_)
  {
    history_.clear();
    std::remove(DataFilePath("history.json").string().c_str());
  }
  else
  {
    SaveHistoryToDisk();
  }
  if (settings_dirty_)
    SaveSettingsToDisk();

  if (download_manager_)
    download_manager_->SetOnChangeCallback(nullptr);

  HideMenuOverlay();
  HideDownloadsOverlay();
  HideContextMenuOverlay();
  HideSuggestionsOverlay();

  view()->set_load_listener(nullptr);
  view()->set_view_listener(nullptr);
  g_ui = nullptr;
}

void UI::OnAddressBarBlur(const JSObject &obj, const JSArgs &args)
{
  address_bar_is_focused_ = false;
}

void UI::OnAddressBarFocus(const JSObject &obj, const JSArgs &args)
{
  address_bar_is_focused_ = true;
}

bool UI::VerboseLogging()
{
  static const bool enabled = []()
  {
    const char *env = std::getenv("ULTRALIGHT_VERBOSE");
    return env && std::strcmp(env, "0") != 0 && std::string(env) != "false";
  }();
  return enabled;
}

// Window exposes is_fullscreen() but no setter in this SDK, so fullscreen is
  // toggled through the platform window directly.
static void ToggleWindowFullscreen(ultralight::Window *window)
{
  if (!window)
    return;
#if defined(_WIN32)
  HWND hwnd = (HWND)window->native_handle();
  if (!hwnd)
    return;
  if (IsZoomed(hwnd))
  {
    // Restore the pre-fullscreen rect rather than an arbitrary size.
    RECT r;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0))
      SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwnd, SW_RESTORE);
  }
  else
  {
    ShowWindow(hwnd, SW_MAXIMIZE);
  }
#else
  (void)window;
#endif
}

bool UI::OnKeyEvent(const ultralight::KeyEvent &evt)
{
  // Tab switcher owns the keyboard while open, and is checked first so arrow keys
  // move the selection instead of scrolling the page behind it. Ctrl+Shift+A must
  // still toggle it closed, so that combo is handled before the capture.
  if (evt.type == KeyEvent::kType_RawKeyDown &&
      (evt.modifiers & KeyEvent::kMod_CtrlKey) &&
      (evt.modifiers & KeyEvent::kMod_ShiftKey) &&
      static_cast<uint32_t>(evt.virtual_key_code) == 'A')
  {
    if (tab_search_overlay_)
      HideTabSearchOverlay();
    else
      ShowTabSearchOverlay();
    return false;
  }

  if (tab_search_overlay_ && tab_search_overlay_->view())
  {
    tab_search_overlay_->view()->FireKeyEvent(evt);
    return false;
  }

  // If menu overlay is active, route all key events to it and consume
  if (menu_overlay_ && menu_overlay_->view())
  {
    menu_overlay_->view()->FireKeyEvent(evt);
    return false;
  }
  // If context menu overlay is active, route keys (eg ESC) to it and consume
  if (context_menu_overlay_ && context_menu_overlay_->view())
  {
    context_menu_overlay_->view()->FireKeyEvent(evt);
    return false;
  }
  // If downloads overlay is active, route key events to it and consume
  if (downloads_overlay_ && downloads_overlay_->view())
  {
    downloads_overlay_->view()->FireKeyEvent(evt);
    return false;
  }
  // If suggestions overlay is open, route navigation keys to it, others to UI overlay (address bar)
  if (suggestions_overlay_ && suggestions_overlay_->view())
  {
    // Virtual key codes: use evt.virtual_key_code values for arrows/enter/escape
    int vk = evt.virtual_key_code;
    bool isNavKey = (vk == 0x26 /*UP*/ || vk == 0x28 /*DOWN*/ || vk == 0x0D /*ENTER*/ || vk == 0x1B /*ESC*/ || vk == 0x09 /*TAB*/);
    if (isNavKey)
    {
      suggestions_overlay_->view()->FireKeyEvent(evt);
      return false;
    }
    // Otherwise send to UI overlay so address bar receives typing
    view()->FireKeyEvent(evt);
    return false;
  }

// Modifier-free shortcuts that are not part of the Ctrl+/Alt+ table. F5 and F12
  // carry no modifier, so NormalizeShortcutKey (which requires one) cannot see
  // them.
  if (evt.type == KeyEvent::kType_RawKeyDown &&
      !(evt.modifiers & (KeyEvent::kMod_CtrlKey | KeyEvent::kMod_AltKey |
                         KeyEvent::kMod_MetaKey)))
  {
    switch (static_cast<uint32_t>(evt.virtual_key_code))
    {
    case 0x74: // F5, reload
      if (RunShortcutAction("reload"))
        return false;
      break;
    case 0x7A: // F11, fullscreen
      if (window_)
      {
        ToggleWindowFullscreen(window_.get());
        return false;
      }
      break;
    case 0x7B: // F12, developer tools
      if (active_tab())
      {
        active_tab()->ToggleInspector();
        return false;
      }
      break;
    default:
      break;
    }
  }

if (evt.type == KeyEvent::kType_RawKeyDown &&
      (evt.modifiers & (KeyEvent::kMod_CtrlKey | KeyEvent::kMod_AltKey)))
  {
    const std::string key = NormalizeShortcutKey(evt);
    if (!key.empty())
    {
      auto it = shortcuts_.find(key);
      if (it != shortcuts_.end())
      {
        if (RunShortcutAction(it->second))
          return false;
      }
    }
  }

  if (address_bar_is_focused_)
  {
    view()->FireKeyEvent(evt);
    return false; // Consume the event
  }

  if (active_tab())
  {
    active_tab()->view()->FireKeyEvent(evt);
    return false; // Consume to avoid double-dispatch to focused view
  }

  return true;
}

// --- Shortcuts helpers ---
static void trim(std::string &s)
{
  size_t a = s.find_first_not_of(" \t\n\r");
  size_t b = s.find_last_not_of(" \t\n\r");
  if (a == std::string::npos)
  {
    s.clear();
    return;
  }
  s = s.substr(a, b - a + 1);
}

void UI::LoadCachedStartPage()
{
  // Pre-load the start page HTML into memory for instant tab creation
  // This eliminates file I/O delay when opening new tabs
  
  // Get current working directory to construct absolute path
  char cwd_buf[1024] = {0};
#ifdef _WIN32
  _getcwd(cwd_buf, sizeof(cwd_buf));
#else
  getcwd(cwd_buf, sizeof(cwd_buf));
#endif
  
  std::string cwd(cwd_buf);
  std::string file_path = cwd + "/assets/static-sties/google-static.html";
  
  std::ifstream in(file_path, std::ios::in | std::ios::binary);
  if (!in.is_open())
  {
    // Try alternative path (in case assets folder is in a different location)
    std::string alt_path = cwd + "/../assets/static-sties/google-static.html";
    in.open(alt_path, std::ios::in | std::ios::binary);
    
    if (!in.is_open())
    {
      // Last resort: try new_tab_page.html
      std::string fallback_path = cwd + "/assets/new_tab_page.html";
      in.open(fallback_path, std::ios::in | std::ios::binary);
      
      if (!in.is_open())
      {
        // Final fallback: minimal dark page if file not found
        cached_start_page_html_ = R"(<!DOCTYPE html><html><head><title>New Tab</title>
          <style>body,html{margin:0;padding:0;height:100%;background:#202124;}</style>
          </head><body></body></html>)";
        return;
      }
    }
  }
  
  std::ostringstream ss;
  ss << in.rdbuf();
  cached_start_page_html_ = ss.str();
  in.close();
}

void UI::LoadCachedInternalPages()
{
  // Single source of truth for the browser's own pages.
  //
  // This replaces two index-aligned arrays (one of "assets/x.html" paths, one of
  // "file:///x.html" URLs) plus a third copy inside IsInternalPageURL. The arrays
  // only worked while both were edited together, and the earlier divergence is
  // why IsInternalPageURL briefly listed "newtab.html" while the real file is
  // new_tab_page.html. One table means a new page is registered in one place.
  //
  // preload is true only for the pages opened often enough to be worth holding in
  // memory; every page still needs to be listed so IsInternalPageURL recognises
  // it and gives it instant (non-animated) scrolling.
  static const InternalPage kInternalPages[] = {
      {"ui.html", false},
      {"settings.html", true},
      {"new_tab_page.html", true},
      {"history.html", true},
      {"bookmarks.html", true},
      {"downloads.html", true},
      {"downloads-panel.html", false},
      {"passwords.html", true},
      {"themes.html", true},
      {"extensions.html", true},
      {"about.html", true},
      {"release_notes.html", false},
      {"suggestions.html", false},
      {"menu.html", false},
      {"contextmenu.html", false},
      {"drm_loading.html", false},
      {"quick-inspector.html", false},
  };

  // Get current working directory to construct absolute paths
  char cwd_buf[1024] = {0};
#ifdef _WIN32
  _getcwd(cwd_buf, sizeof(cwd_buf));
#else
  getcwd(cwd_buf, sizeof(cwd_buf));
#endif

  std::string cwd(cwd_buf);

  for (const InternalPage &page : kInternalPages)
  {
    if (!page.preload)
      continue;

    const std::string relative = std::string("assets/") + page.name;
    std::ifstream in(cwd + "/" + relative, std::ios::in | std::ios::binary);

    // If not found, try alternative path
    if (!in.is_open())
    {
      in.open(cwd + "/../" + relative, std::ios::in | std::ios::binary);
    }
    
    if (in.is_open())
    {
      std::ostringstream ss;
      ss << in.rdbuf();
      cached_internal_pages_["file:///" + std::string(page.name)] = ss.str();
      in.close();
    }
  }
}

const std::string &UI::GetCachedPageHTML(const std::string &url) const
{
  static const std::string empty;
  auto it = cached_internal_pages_.find(url);
  if (it != cached_internal_pages_.end())
    return it->second;
  return empty;
}

void UI::LoadShortcuts()
{
  // Defaults first, so a missing or unreadable assets/shortcuts.json degrades to
  // the built-in set instead of leaving the browser with only four bindings.
  // Previously this list held 4 of the 26 shipped bindings, so losing the JSON
  // silently disabled tab switching, tab-by-number and hard reload.
  static const std::pair<const char *, const char *> kDefaults[] = {
      {"Ctrl+T", "new-tab"},
      {"Ctrl+R", "reload"},
      {"Alt+Left", "nav-back"},
      {"Alt+Right", "nav-forward"},
      {"Ctrl+N", "new-window"},
      {"Ctrl+W", "close-tab"},
      {"Ctrl+H", "open-history"},
      {"Ctrl+B", "open-bookmarks"},
      {"Ctrl+J", "open-downloads"},
      {"Ctrl+E", "open-extensions"},
      {"Ctrl+P", "open-passwords"},
      {"Ctrl+L", "focus-address"},
      {"Ctrl+,", "open-settings"},
      {"Ctrl+Shift+T", "reopen-closed-tab"},
      {"Ctrl+Shift+A", "open-tab-search"},
      {"Ctrl+Tab", "next-tab"},
      {"Ctrl+Shift+Tab", "prev-tab"},
      {"Ctrl+1", "select-tab-1"},
      {"Ctrl+2", "select-tab-2"},
      {"Ctrl+3", "select-tab-3"},
      {"Ctrl+4", "select-tab-4"},
      {"Ctrl+5", "select-tab-5"},
      {"Ctrl+6", "select-tab-6"},
      {"Ctrl+7", "select-tab-7"},
      {"Ctrl+8", "select-tab-8"},
      {"Ctrl+9", "select-tab-last"},
      {"Ctrl+Shift+R", "reload-hard"},
      {"Ctrl+F", "find-in-page"},
      {"Ctrl+Plus", "zoom-in"},
      {"Ctrl+Minus", "zoom-out"},
      {"Ctrl+0", "zoom-reset"},
  };

  shortcuts_.clear();
  for (const auto &entry : kDefaults)
    shortcuts_[entry.first] = entry.second;

  // F11 is handled directly in OnKeyEvent rather than through this table,
  // because it carries no Ctrl modifier.

  // Try load from assets/shortcuts.json
  std::ifstream in("assets/shortcuts.json", std::ios::in | std::ios::binary);
  if (!in.is_open())
    return;
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string txt = ss.str();
  in.close();

  // Very lenient JSON key-value parser for flat object { "Ctrl+T": "new-tab", ... }
  size_t i = 0, n = txt.size();
  auto next_quote = [&](size_t pos)
  {
    return txt.find('"', pos);
  };
  while (i < n)
  {
    size_t k1 = next_quote(i);
    if (k1 == std::string::npos)
      break;
    size_t k2 = next_quote(k1 + 1);
    if (k2 == std::string::npos)
      break;
    std::string key = txt.substr(k1 + 1, k2 - (k1 + 1));
    size_t colon = txt.find(':', k2 + 1);
    if (colon == std::string::npos)
      break;
    size_t v1 = next_quote(colon + 1);
    if (v1 == std::string::npos)
      break;
    size_t v2 = next_quote(v1 + 1);
    if (v2 == std::string::npos)
      break;
    std::string val = txt.substr(v1 + 1, v2 - (v1 + 1));
    trim(key);
    trim(val);
    if (!key.empty() && !val.empty())
      shortcuts_[key] = val;
    i = v2 + 1;
  }
}

// Maps a key event onto the identifier form used as the key in shortcuts_
// ("Ctrl+T", "Ctrl+Shift+T", "Ctrl+,", "Ctrl+Tab", "Ctrl+1").
//
// The previous matcher only handled Ctrl plus an A-Z virtual key code, so every
// other entry in assets/shortcuts.json was unreachable in practice: Ctrl+,
// (open settings) and Ctrl+Shift+T (open themes) were both dead. Keys like Tab
// also have virtual key codes far outside the ASCII range, so casting the code
// to char produced a control character rather than a name.
//
// Returns an empty string when the event is not a Ctrl combination this browser
// binds, so the caller can fall through to normal key routing.
std::string UI::NormalizeShortcutKey(const ultralight::KeyEvent &evt)
{
  // Chrome's navigation keys are Alt+Left / Alt+Right rather than Ctrl, so both
  // modifiers are accepted here.
  const bool ctrl = (evt.modifiers & KeyEvent::kMod_CtrlKey) != 0;
  const bool alt = (evt.modifiers & KeyEvent::kMod_AltKey) != 0;
  if (!ctrl && !alt)
    return std::string();
  if (ctrl && alt)
    return std::string(); // Ctrl+Alt combos are not bound; let the page see them.

  // Virtual key codes for keys that are not printable ASCII.
  enum : uint32_t
  {
    kVK_Tab = 0x09,
    kVK_Return = 0x0D,
    kVK_PageUp = 0x21,
    kVK_PageDown = 0x22,
    kVK_End = 0x23,
    kVK_Home = 0x24,
    kVK_Left = 0x25,
    kVK_Up = 0x26,
    kVK_Right = 0x27,
    kVK_Down = 0x28,
  };

  const uint32_t vk = static_cast<uint32_t>(evt.virtual_key_code);
  std::string name;
  // Set for keys where Shift is part of the same binding (Ctrl+= and Ctrl+Shift+=
  // are both zoom-in), so the identifier does not need a Shift variant.
  bool suppress_shift = false;

  switch (vk)
  {
  case kVK_Tab:      name = "Tab"; break;
  case kVK_Return:   name = "Enter"; break;
  case kVK_PageUp:   name = "PageUp"; break;
  case kVK_PageDown: name = "PageDown"; break;
  case kVK_End:      name = "End"; break;
  case kVK_Home:     name = "Home"; break;
  case kVK_Left:     name = "Left"; break;
  case kVK_Up:       name = "Up"; break;
  case kVK_Right:    name = "Right"; break;
  case kVK_Down:     name = "Down"; break;
default:
      if (vk >= '0' && vk <= '9')
      {
        name = std::string(1, static_cast<char>(vk));
      }
      else if (vk >= 'A' && vk <= 'Z')
      {
        name = std::string(1, static_cast<char>(vk));
      }
      else if (vk >= 'a' && vk <= 'z')
      {
        name = std::string(1, static_cast<char>(vk - 'a' + 'A'));
      }
      else
      {
        switch (vk)
        {
        // Zoom keys. Windows reports the unshifted key for these, so Ctrl+Shift+=
        // arrives as '=' too. Both spellings mean zoom-in in Chrome, so the
        // Shift flag is dropped for these (see below). Numeric keypad variants
        // are included because Ctrl+'+' on the keypad is the other common way to
        // zoom in.
        case 0xBB: /* '='  */
        case 0x6B: /* KP+ */
          name = "Plus";
          suppress_shift = true;
          break;
        case 0xBD: /* '-'  */
        case 0x6D: /* KP- */
        case 0x6F: /* KP_ */
          name = "Minus";
          suppress_shift = true;
          break;
        default:
        {
          // Punctuation such as Ctrl+, relies on the platform reporting it directly.
          const int ch = static_cast<int>(vk);
          if (ch >= 0x20 && ch <= 0x7E)
            name = std::string(1, static_cast<char>(ch));
          break;
        }
        }
      }
      break;
  }

  if (name.empty())
    return std::string();

  std::string key = alt ? "Alt+" : "Ctrl+";
  // Shift is recorded in the identifier because it is what distinguishes
  // Ctrl+T (new tab) from Ctrl+Shift+T, not the shifted character itself. For a
  // letter, Shift+T also reports 'T' as the key, so the two would otherwise
  // collide.
  if ((evt.modifiers & KeyEvent::kMod_ShiftKey) && !suppress_shift)
    key += "Shift+";
  key += name;
  return key;
}

bool UI::RunShortcutAction(const std::string &action)
{
  if (action == "new-tab")
  {
    CreateNewTab();
    return true;
  }
  if (action == "new-window")
  {
    OnRequestNewWindow({}, {});
    return true;
  }
  if (action == "close-tab")
  {
    if (active_tab())
    {
      OnRequestTabClose({}, {active_tab_id_});
      return true;
    }
    return false;
  }
  if (action == "reopen-closed-tab")
  {
    return ReopenLastClosedTab();
  }
  if (action == "open-history")
  {
    // Open History in a NEW tab instead of replacing current
    CreateNewTabForChildView(String("file:///history.html"));
    return true;
  }
  if (action == "open-bookmarks")
  {
    // Open Bookmarks in a NEW tab
    CreateNewTabForChildView(String("file:///bookmarks.html"));
    return true;
  }
  if (action == "focus-address")
  {
    if (focusAddressBar)
    {
      RefPtr<JSContext> lock(view()->LockJSContext());
      focusAddressBar({});
      address_bar_is_focused_ = true;
      return true;
    }
    return false;
  }
  if (action == "open-downloads")
  {
    // Toggle downloads overlay
    ShowDownloadsOverlay();
    return true;
  }
  if (action == "open-extensions")
  {
    // Open Extensions in a new tab
    CreateNewTabForChildView(String("file:///extensions.html"));
    return true;
  }
  if (action == "open-passwords")
  {
    // Open Passwords in a new tab
    CreateNewTabForChildView(String("file:///passwords.html"));
    return true;
  }
  if (action == "open-settings")
  {
    // Open Settings in a NEW tab (like Ctrl+H opens history)
    CreateNewTabForChildView(String("file:///settings.html"));
    return true;
  }
  if (action == "open-themes")
  {
    // Open Themes in a new tab
    CreateNewTabForChildView(String("file:///themes.html"));
    return true;
  }

  if (action == "open-tab-search")
  {
    ShowTabSearchOverlay();
    return true;
  }

  // --- Tab navigation (Chrome parity) ---

  if (action == "next-tab" || action == "prev-tab")
  {
    return CycleActiveTab(action == "next-tab" ? 1 : -1);
  }

  // Ctrl+1..9 selects the Nth tab; Ctrl+9 also selects the last one, as in Chrome.
  if (action.rfind("select-tab-", 0) == 0)
  {
    const std::string n = action.substr(std::string("select-tab-").size());
    if (n == "last")
      return SelectTabByIndex(-1);
    char *end = nullptr;
    const long idx = std::strtol(n.c_str(), &end, 10);
    if (end && *end == '\0' && idx >= 1 && idx <= 9)
      return SelectTabByIndex(static_cast<int>(idx) - 1);
    return false;
  }

  if (action == "reload" || action == "reload-hard")
  {
    if (active_tab())
    {
      active_tab()->view()->Reload();
      return true;
    }
    if (active_drm_tab())
    {
      active_drm_tab()->Reload();
      return true;
    }
    return false;
  }

  // Chrome binds Alt+Left / Alt+Right for history navigation. Routed through the
  // same handlers as the toolbar buttons so DRM tabs are covered too.
  if (action == "nav-back")
  {
    OnBack({}, {});
    return true;
  }
  if (action == "nav-forward")
  {
    OnForward({}, {});
    return true;
  }

  if (action == "toggle-fullscreen")
  {
    ToggleWindowFullscreen(window_.get());
    return true;
  }

  if (action == "find-in-page")
  {
    // window.find() is a Chromium Web API, so the engine provides the find bar.
    // Done via EvaluateScript because JSObject in this SDK exposes no way to test
    // for undefined, so calling it defensively is not possible through the
    // object API.
    if (active_tab())
    {
      active_tab()->view()->EvaluateScript("try{window.find&&window.find();}catch(e){}", nullptr);
      return true;
    }
    return false;
  }

  // --- Page zoom (Chrome parity) ---
  // Zoom applies to the active tab only, matching Chrome's per-tab behaviour.
  if (action == "zoom-in" || action == "zoom-out" || action == "zoom-reset")
  {
    auto tab = active_tab();
    if (!tab)
      return false;
    if (action == "zoom-reset")
      tab->SetZoom(1.0);
    else
      tab->SetZoom(tab->zoom() + (action == "zoom-in" ? 0.1 : -0.1));
    SyncZoomToUI(tab->zoom());
    return true;
  }

  return false;
}

void UI::SyncZoomToUI(double zoom)
{
  // The chrome view has no zoom badge today, so this only keeps the value
  // observable for the address bar / status area. Best effort: a missing JS
  // binding must never revert the zoom, which is already applied to the tab.
  RefPtr<View> ui_view = view();
  if (!ui_view)
    return;

  const std::string script =
      "try{if(window.OnZoomChanged)window.OnZoomChanged(" +
      std::to_string(static_cast<int>(std::lround(zoom * 100.0))) + ");}catch(e){}";
  ui_view->EvaluateScript(String(script.c_str()), nullptr);
}

std::string UI::BuildTabSearchJSON()
{
  std::string out = "[";
  bool first = true;

  for (const auto &entry : tabs_)
  {
    Tab *tab = entry.second.get();
    if (!tab)
      continue;

    View *v = tab->view().get();
    auto title_u8 = v->title().utf8();
    auto url_u8 = v->url().utf8();
    // GetFaviconURL fills and reads the favicon caches, so it is not const.
    auto favicon_str = GetFaviconURL(v->url());
    auto fav_u8 = favicon_str.utf8();

    const std::string title = title_u8.data() ? title_u8.data() : "";
    const std::string url = url_u8.data() ? url_u8.data() : "";
    const std::string fav = fav_u8.data() ? fav_u8.data() : "";

    if (!first)
      out += ",";
    first = false;

    // Titles and URLs are attacker-influenced, so every value goes through the
    // escaper. Without it a page titled with a quote would break the JSON and
    // could inject markup into the switcher.
    out += "{\"id\":";
    out += std::to_string(entry.first);
    out += ",\"title\":\"";
    out += util::EscapeJsonString(title);
    out += "\",\"url\":\"";
    out += util::EscapeJsonString(url);
    out += "\",\"favicon\":\"";
    out += util::EscapeJsonString(fav);
    out += "\",\"current\":";
    out += (entry.first == active_tab_id_) ? "true" : "false";
    out += "}";
  }

  out += "]";
  return out;
}

void UI::ShowTabSearchOverlay()
{
  HideTabSearchOverlay();

  if (tabs_.size() < 2)
    return; // Nothing to switch between, so do not flash an empty panel.

  ultralight::ViewConfig cfg;
  cfg.is_transparent = true;
  cfg.initial_device_scale = window_->scale();
  if (overlay_ && overlay_->view())
  {
    cfg.is_accelerated = overlay_->view()->is_accelerated();
    cfg.display_id = overlay_->view()->display_id();
  }

  // The list is content-height and capped in CSS, so reserving a generous height
  // and letting the overlay clip avoids a second round trip to measure the rows.
  int panel_height = static_cast<int>(window_->height() / 2);
  if (panel_height < 1)
    panel_height = 1;
  auto view = App::instance()->renderer()->CreateView(window_->width(),
                                                      static_cast<uint32_t>(panel_height), cfg, nullptr);

  tab_search_overlay_ = Overlay::Create(window_, view, 0, ui_height_);
  tab_search_overlay_->Show();
  view->set_load_listener(this);
  view->set_view_listener(this);
  view->LoadURL("file:///tab_search.html");

  // The page asks for the list once it is ready; the JSON is built at that point
  // rather than captured now so the titles are current.
  pending_tab_search_json_ = BuildTabSearchJSON();
}

void UI::HideTabSearchOverlay()
{
  if (!tab_search_overlay_)
    return;
  tab_search_overlay_->Hide();
  tab_search_overlay_->Unfocus();
  tab_search_overlay_->view()->set_load_listener(nullptr);
  tab_search_overlay_->view()->set_view_listener(nullptr);
  tab_search_overlay_ = nullptr;
  pending_tab_search_json_ = "";
}

void UI::OnTabSearchPick(const JSObject &, const JSArgs &args)
{
  if (args.size() < 1 || !args[0].IsString())
    return;

  auto id_u8 = JSStringToUtf8(args[0].ToString());
  const std::string id_str = id_u8.data() ? id_u8.data() : "";
  if (id_str.empty())
    return;

  HideTabSearchOverlay();

  // The id arrives as a string so large tab ids survive; parse it rather than
  // casting, and ignore anything that is not a plain number.
  for (char c : id_str)
  {
    if (!std::isdigit(static_cast<unsigned char>(c)))
      return;
  }

  const uint64_t id = std::strtoull(id_str.c_str(), nullptr, 10);
  if (tabs_.count(id) == 0)
    return;

  OnActiveTabChange({}, {static_cast<ultralight::JSValue>(static_cast<double>(id))});
}

bool UI::CycleActiveTab(int direction)
{
  if (tabs_.size() < 2)
    return false;

  // Walk the id-ordered map so cycling is stable and matches tab-strip order,
  // wrapping at both ends the way Chrome does.
  std::vector<uint64_t> ids;
  ids.reserve(tabs_.size());
  for (const auto &entry : tabs_)
    ids.push_back(entry.first);

  auto pos = std::find(ids.begin(), ids.end(), active_tab_id_);
  if (pos == ids.end())
  {
    // Active tab is not in the Ultralight map (e.g. a DRM tab). Select the first
    // tab in the direction of travel.
    pos = ids.begin();
    if (direction < 0)
      pos = ids.end() - 1;
  }
  else
  {
    pos += direction;
    if (pos >= ids.end())
      pos = ids.begin();
    else if (pos < ids.begin())
      pos = ids.end() - 1;
  }

  const uint64_t next = *pos;
  if (next == active_tab_id_)
    return false;

  OnActiveTabChange({}, {static_cast<ultralight::JSValue>(static_cast<double>(next))});
  return true;
}

bool UI::SelectTabByIndex(int index)
{
  std::vector<uint64_t> ids;
  ids.reserve(tabs_.size());
  for (const auto &entry : tabs_)
    ids.push_back(entry.first);
  if (ids.empty())
    return false;

  if (index < 0)
    index = static_cast<int>(ids.size()) - 1;
  if (index < 0 || index >= static_cast<int>(ids.size()))
    return false;

  const uint64_t target = ids[static_cast<size_t>(index)];
  if (target == active_tab_id_)
    return true;
  OnActiveTabChange({}, {static_cast<ultralight::JSValue>(static_cast<double>(target))});
  return true;
}

bool UI::OnMouseEvent(const ultralight::MouseEvent &evt)
{
  InputDiagScope diag("mouse");
  // CRITICAL: If clicking in UI area (toolbar) on a DRM tab, detach WebView2 immediately
  // This prevents WebView2 from intercepting keyboard input to address bar
  if (evt.type == MouseEvent::kType_MouseDown && evt.y <= ui_height_)
  {
    auto drm_it = drm_tabs_.find(active_tab_id_);
    if (drm_it != drm_tabs_.end() && drm_it->second)
    {
      drm_it->second->DetachFromParent();
      // Show solid background
      auto tab_it = tabs_.find(active_tab_id_);
      if (tab_it != tabs_.end() && tab_it->second)
        tab_it->second->Show();
    }
  }

  // If menu overlay is active, route mouse events to it and consume
  if (menu_overlay_ && menu_overlay_->view())
  {
    menu_overlay_->view()->FireMouseEvent(evt);
    return false;
  }
  // If context menu overlay is active, route mouse events to it and consume
  if (context_menu_overlay_ && context_menu_overlay_->view())
  {
    context_menu_overlay_->view()->FireMouseEvent(evt);
    return false;
  }
  // Tab switcher: clicks inside the panel select a tab, clicks anywhere else
  // dismiss it and fall through to the page below, matching Chrome.
  if (tab_search_overlay_ && tab_search_overlay_->view())
  {
    const int ox = tab_search_overlay_->x();
    const int oy = tab_search_overlay_->y();
    const int ow = static_cast<int>(tab_search_overlay_->width());
    const int oh = static_cast<int>(tab_search_overlay_->height());

    if (evt.x >= ox && evt.x < ox + ow && evt.y >= oy && evt.y < oy + oh)
    {
      tab_search_overlay_->view()->FireMouseEvent(evt);
      return false;
    }

    HideTabSearchOverlay();
  }
  // If suggestions overlay is active, route mouse to it and consume
  if (suggestions_overlay_ && suggestions_overlay_->view())
  {
    suggestions_overlay_->view()->FireMouseEvent(evt);
    return false;
  }
  if (downloads_overlay_ && downloads_overlay_->view())
  {
    int overlay_x = downloads_overlay_->x();
    int overlay_y = downloads_overlay_->y();
    uint32_t overlay_w = downloads_overlay_->width();
    uint32_t overlay_h = downloads_overlay_->height();

    if (evt.x >= overlay_x && evt.x < overlay_x + static_cast<int>(overlay_w) &&
        evt.y >= overlay_y && evt.y < overlay_y + static_cast<int>(overlay_h))
    {
      ultralight::MouseEvent adjusted = evt;
      adjusted.x -= overlay_x;
      adjusted.y -= overlay_y;
      downloads_overlay_->view()->FireMouseEvent(adjusted);
      return false;
    }
  }

  // Handle right-clicks globally (both navbar/UI and page content)
  if (evt.type == MouseEvent::kType_MouseDown && evt.button == MouseEvent::kButton_Right)
  {
    bool on_ui = evt.y <= ui_height_;
    // Build a small script to collect context info at the click point
    char script_buf[512];
    std::snprintf(script_buf, sizeof(script_buf),
                  "(function(x,y){try{var t=document.elementFromPoint(x,y);var a=t&&t.closest?t.closest('a[href]'):null;var img=t&&t.closest?t.closest('img[src]'):null;var sel='';try{sel=String(window.getSelection?window.getSelection():'');}catch(_){ }var info={linkURL:a&&a.href?a.href:'',imageURL:img&&img.src?img.src:'',selectionText:sel||'',isEditable:!!(t&&(t.isContentEditable||(t.tagName==='INPUT'||t.tagName==='TEXTAREA')))};return JSON.stringify(info);}catch(e){return '{}';}})(%d,%d)",
                  on_ui ? evt.x : evt.x,
                  on_ui ? evt.y : (evt.y - ui_height_ < 0 ? 0 : evt.y - ui_height_));

    ultralight::String json;
    if (on_ui)
    {
      RefPtr<View> uiView = view();
      json = uiView->EvaluateScript(script_buf, nullptr);
      ctx_target_ = 1;
    }
    else if (active_tab())
    {
      json = active_tab()->view()->EvaluateScript(script_buf, nullptr);
      ctx_target_ = 2;
    }
    else
    {
      ctx_target_ = 0;
      json = "{}";
    }

    ShowContextMenuOverlay(evt.x, evt.y, json);
    return false; // consume right-click
  }

  // If mouse is interacting within the UI overlay region, forward to UI view and consume
  if (evt.y <= ui_height_)
  {
    // Ensure UI overlay has focus for typing in address bar
    if (evt.type == MouseEvent::kType_MouseDown)
    {
      address_bar_is_focused_ = true;
      view()->Focus();
      // If a DRM tab is active, blur it so keyboard input goes to Ultralight UI
      if (auto drm_tab = active_drm_tab())
        drm_tab->Blur();
    }
    view()->FireMouseEvent(evt);
    return false;
  }

  if (evt.type == MouseEvent::kType_MouseDown)
  {
    // Click occurred outside the UI overlay (handled above), switch focus to page.
    // Do NOT auto-close the downloads overlay here; let the user dismiss it explicitly
    // via the Close button, clicking the overlay background, or pressing Escape.
    address_bar_is_focused_ = false;
    if (active_tab())
    {
      active_tab()->view()->Focus();
    }
    // If DRM tab is active, focus it when clicking in the content area
    else if (auto drm_tab = active_drm_tab())
    {
      drm_tab->Focus();
    }
  }
  if (active_tab() && active_tab()->IsInspectorShowing())
  {
    int x_px = static_cast<int>(std::lround(evt.x * window()->scale()));
    int y_px = static_cast<int>(std::lround(evt.y * window()->scale()));

    if (is_resizing_inspector_)
    {
      int resize_delta = inspector_resize_begin_mouse_y_ - y_px;
      int new_inspector_height = inspector_resize_begin_height_ + resize_delta;
      active_tab()->SetInspectorHeight(new_inspector_height);

      if (evt.type == MouseEvent::kType_MouseUp)
      {
        is_resizing_inspector_ = false;
      }

      return false;
    }

    IntRect drag_handle = active_tab()->GetInspectorResizeDragHandle();

    bool over_drag_handle = drag_handle.Contains(Point(static_cast<float>(x_px), static_cast<float>(y_px)));

    if (over_drag_handle && !is_over_inspector_resize_drag_handle_)
    {
      // We entered the drag area
      window()->SetCursor(Cursor::kCursor_NorthSouthResize);
      is_over_inspector_resize_drag_handle_ = true;
    }
    else if (!over_drag_handle && is_over_inspector_resize_drag_handle_)
    {
      // We left the drag area, restore previous cursor
      window()->SetCursor(cur_cursor_);
      is_over_inspector_resize_drag_handle_ = false;
    }

    if (over_drag_handle && evt.type == MouseEvent::kType_MouseDown && !is_resizing_inspector_)
    {
      is_resizing_inspector_ = true;
      inspector_resize_begin_mouse_y_ = y_px;
      inspector_resize_begin_height_ = active_tab()->GetInspectorHeight();
    }

    return !over_drag_handle;
  }

  return true;
}

bool UI::OnScrollEvent(const ultralight::ScrollEvent &evt)
{
  InputDiagScope diag("scroll");
  // The mouse wheel is delivered as a ScrollEvent, so it never passes through
  // OnMouseEvent and therefore never reaches the modal overlays that
  // unconditionally consume mouse input. Without this, opening the menu and then
  // scrolling scrolls the page behind the menu.
  //
  // ScrollEvent carries only deltas, no cursor position, so hit-testing against
  // an overlay's rectangle is not possible. These three overlays are modal and
  // consume every mouse event regardless of position (see OnMouseEvent), so
  // routing the wheel to them is exactly consistent with their mouse behaviour.
  // The overlay view receives the event directly, so a long menu or suggestion
  // list can still scroll itself instead of the wheel being swallowed.
  //
  // The downloads panel is deliberately excluded: it is a non-modal panel that
  // only consumes mouse events inside its own bounds, and without cursor
  // coordinates we cannot tell where the wheel was, so leaving it alone
  // preserves the existing page-scrolling behaviour.
  if (menu_overlay_ && menu_overlay_->view())
  {
    menu_overlay_->view()->FireScrollEvent(evt);
    return false;
  }
  if (context_menu_overlay_ && context_menu_overlay_->view())
  {
    context_menu_overlay_->view()->FireScrollEvent(evt);
    return false;
  }
  if (suggestions_overlay_ && suggestions_overlay_->view())
  {
    suggestions_overlay_->view()->FireScrollEvent(evt);
    return false;
  }

  // Not over an overlay: let AppCore forward the scroll to the focused view,
  // which is the page for any content-area scrolling.
  return true;
}

void UI::OnClose(ultralight::Window *window)
{
  App::instance()->Quit();
}

void UI::OnResize(ultralight::Window *window, uint32_t width, uint32_t height)
{
  int tab_height = window->height() - ui_height_;

  if (tab_height < 1)
    tab_height = 1;

  overlay_->Resize(window->width(), ui_height_);

  if (menu_overlay_)
    menu_overlay_->Resize(window->width(), window->height());
  if (context_menu_overlay_)
    context_menu_overlay_->Resize(window->width(), window->height());
  if (downloads_overlay_)
    LayoutDownloadsOverlay();

  for (auto &entry : tabs_)
  {
    if (entry.second)
      entry.second->Reposition(0, (uint32_t)ui_height_);
  }
  for (auto &entry : tabs_)
  {
    if (entry.second)
      entry.second->Resize(window->width(), (uint32_t)tab_height);
  }
  for (auto &entry : drm_tabs_)
  {
    if (entry.second)
      entry.second->Resize(window->width(), (uint32_t)tab_height, 0, ui_height_);
  }
}

void UI::OnDOMReady(View *caller, uint64_t frame_id, bool is_main_frame, const String &url)
{
  // Set the context for all subsequent JS* calls for THIS caller view
  RefPtr<JSContext> locked_context = caller->LockJSContext();
  SetJSContext(locked_context->ctx());

  JSObject global = JSGlobalObject();
  auto url_utf8 = url.utf8();
  bool is_menu_view = url_utf8.data() && std::strstr(url_utf8.data(), "menu.html") != nullptr;
  bool is_ctx_view = url_utf8.data() && std::strstr(url_utf8.data(), "contextmenu.html") != nullptr;
  bool is_sugg_view = url_utf8.data() && std::strstr(url_utf8.data(), "suggestions.html") != nullptr;
  bool is_downloads_overlay_view = url_utf8.data() && std::strstr(url_utf8.data(), "downloads-panel.html") != nullptr;
  bool is_settings_page_view = url_utf8.data() && std::strstr(url_utf8.data(), "settings.html") != nullptr;
  bool is_extensions_page_view = url_utf8.data() && std::strstr(url_utf8.data(), "extensions.html") != nullptr;
  bool is_tab_search_view = url_utf8.data() && std::strstr(url_utf8.data(), "tab_search.html") != nullptr;

  // The tab switcher page is inert data; it owns its own keyboard handling and
  // only needs the list, so it is excluded from the chrome binding pass below.
  if (is_tab_search_view && !pending_tab_search_json_.empty())
  {
    // The payload is already-escaped JSON, so it is injected as a quoted JS
    // string literal with EscapeJsStringLiteral rather than re-escaped as JSON.
    const std::string script =
        "try{if(window.OnTabSearchShow)window.OnTabSearchShow(" +
        util::EscapeJsStringLiteral(pending_tab_search_json_) + ");}catch(e){}";
    caller->EvaluateScript(String(script.c_str()), nullptr);
  }

  if (!is_menu_view && !is_ctx_view && !is_sugg_view && !is_downloads_overlay_view &&
      !is_settings_page_view && !is_extensions_page_view && !is_tab_search_view)
  {
    // Only main UI view has these functions
    updateBack = global["updateBack"];
    updateForward = global["updateForward"];
    updateLoading = global["updateLoading"];
    updateURL = global["updateURL"];
    addTab = global["addTab"];
    updateTab = global["updateTab"];
    closeTab = global["closeTab"];
    focusAddressBar = global["focusAddressBar"];
    isAddressBarFocused = global["isAddressBarFocused"];
    updateAdblockEnabled = global["updateAdblockEnabled"];
    setTabDrmState = global["setTabDrmState"];
    applySettings = global["applySettings"];
  }

  global["OnBack"] = BindJSCallback(&UI::OnBack);
  global["OnForward"] = BindJSCallback(&UI::OnForward);
  global["OnRefresh"] = BindJSCallback(&UI::OnRefresh);
  global["OnStop"] = BindJSCallback(&UI::OnStop);
  global["OnToggleTools"] = BindJSCallback(&UI::OnToggleTools);
  global["OnMenuOpen"] = BindJSCallback(&UI::OnMenuOpen);
  global["OnMenuClose"] = BindJSCallback(&UI::OnMenuClose);
  global["OnDownloadsOverlayToggle"] = BindJSCallback(&UI::OnDownloadsOverlayToggle);
  global["OnDownloadsOverlayClose"] = BindJSCallback(&UI::OnDownloadsOverlayClose);
  global["OnToggleDarkMode"] = BindJSCallback(&UI::OnToggleDarkMode);
  global["GetDarkModeEnabled"] = BindJSCallbackWithRetval(&UI::OnGetDarkModeEnabled);
  global["OnToggleAdblock"] = BindJSCallback(&UI::OnToggleAdblock);
  global["GetAdblockEnabled"] = BindJSCallbackWithRetval(&UI::OnGetAdblockEnabled);
  global["OnToggleBookmark"] = BindJSCallback(&UI::OnToggleBookmark);
  global["OnOpenSettingsPanel"] = BindJSCallback(&UI::OnOpenSettingsPanel);
  global["OnCloseSettingsPanel"] = BindJSCallback(&UI::OnCloseSettingsPanel);
  // Password save bar callback
  global["OnPasswordSaveBarResponse"] = BindJSCallback(&UI::OnPasswordSaveBarResponse);
  // DRM prompt bar callback
  global["OnDrmPromptResponse"] = BindJSCallback(&UI::OnDrmPromptResponse);
  // Session restore bar callbacks
  global["OnRestoreSession"] = BindJSCallback(&UI::OnRestoreSession);
  global["OnDismissSession"] = BindJSCallback(&UI::OnDismissSession);
  // Allow UI documents (including settings) to request a chrome overlay reload.
  global["OnReloadChromeUI"] = BindJSCallback(&UI::OnReloadChromeUI);
  // Allow UI documents to request reloading the active non-settings tab.
  global["OnReloadActiveNonSettingsTab"] = BindJSCallback(&UI::OnReloadActiveNonSettingsTab);

  // Bind settings bridge functions to ALL views (not just UI overlay)
  // This ensures settings.html can call GetSettingsSnapshot when loaded in a tab
  global["GetSettingsSnapshot"] = BindJSCallbackWithRetval(&UI::OnGetSettings);
  global["OnUpdateSetting"] = BindJSCallback(&UI::OnUpdateSetting);
  global["GetMemorySnapshot"] = BindJSCallbackWithRetval(&UI::OnGetMemorySnapshot);
  global["OnRestoreSettingsDefaults"] = BindJSCallbackWithRetval(&UI::OnRestoreSettingsDefaults);
  global["OnSaveSettings"] = BindJSCallback(&UI::OnSaveSettings);

  // Native theme persistence. Bound to every UI-owned view because
  // assets/themes/theme.js is loaded by nine different pages (ui, settings,
  // passwords, themes, history, downloads, bookmarks, extensions, new tab) and
  // each of them checks for these globals independently. Binding them only on
  // the chrome overlay would leave every internal page on the localStorage
  // fallback, and the active theme would then differ between pages.
  global["NativeGetSetting"] = BindJSCallbackWithRetval(&UI::OnNativeGetThemeSetting);
  global["NativeSetSetting"] = BindJSCallback(&UI::OnNativeSetThemeSetting);
  global["NativeGetThemes"] = BindJSCallbackWithRetval(&UI::OnNativeGetCustomThemes);
  global["NativeSaveThemes"] = BindJSCallback(&UI::OnNativeSaveCustomThemes);
  global["NativeGetThemeOverrides"] = BindJSCallbackWithRetval(&UI::OnNativeGetThemeOverrides);
  global["NativeSaveThemeOverrides"] = BindJSCallback(&UI::OnNativeSaveThemeOverrides);
  global["NativeGetSeedThemes"] = BindJSCallbackWithRetval(&UI::OnNativeGetSeedThemes);

  if (is_ctx_view)
  {
    // context menu overlay actions
    global["OnContextMenuAction"] = BindJSCallback(&UI::OnContextMenuAction);
    setupContextMenu = global["setupContextMenu"];
    // Initialize menu now (use '{}' if pending JSON is empty)
    if (setupContextMenu)
    {
      ultralight::String info = pending_ctx_info_json_.empty() ? ultralight::String("{}") : pending_ctx_info_json_;
      setupContextMenu({(double)pending_ctx_position_.first, (double)pending_ctx_position_.second, info});
    }
  }
  if (is_sugg_view)
  {
    // suggestions overlay actions
    global["OnSuggestionPick"] = BindJSCallback(&UI::OnSuggestionPick);
    global["OnSuggestionPaste"] = BindJSCallback(&UI::OnSuggestionPaste);
    global["OnFaviconReady"] = BindJSCallback(&UI::OnFaviconReady);
    setupSuggestions = global["setupSuggestions"];
    if (setupSuggestions)
    {
      ultralight::String items = pending_sugg_json_.empty() ? ultralight::String("[]") : pending_sugg_json_;
      setupSuggestions({(double)pending_sugg_position_.first, (double)pending_sugg_position_.second, (double)pending_sugg_width_, items});
    }
  }
  global["OnRequestNewTab"] = BindJSCallback(&UI::OnRequestNewTab);
  global["OnResetZoom"] = BindJSCallback(&UI::OnResetZoom);
  global["OnTabSearchPick"] = BindJSCallback(&UI::OnTabSearchPick);
  global["CloseTabSearchOverlay"] = BindJSCallbackWithRetval(&UI::OnHideTabSearchOverlay);
  global["OnRequestNewWindow"] = BindJSCallback(&UI::OnRequestNewWindow);
  global["OnRequestTabClose"] = BindJSCallback(&UI::OnRequestTabClose);
  global["OnActiveTabChange"] = BindJSCallback(&UI::OnActiveTabChange);
  global["OnRequestChangeURL"] = BindJSCallback(&UI::OnRequestChangeURL);
  global["OnAddressBarNavigate"] = BindJSCallback(&UI::OnAddressBarNavigate);
  global["OnOpenHistoryNewTab"] = BindJSCallback(&UI::OnOpenHistoryNewTab);
  global["OnOpenBookmarksNewTab"] = BindJSCallback(&UI::OnOpenBookmarksNewTab);
  global["OnOpenDownloadsNewTab"] = BindJSCallback(&UI::OnOpenDownloadsNewTab);
  global["OnOpenPasswordsNewTab"] = BindJSCallback(&UI::OnOpenPasswordsNewTab);
  global["OnOpenExtensionsNewTab"] = BindJSCallback(&UI::OnOpenExtensionsNewTab);
  global["OnOpenThemesNewTab"] = BindJSCallback(&UI::OnOpenThemesNewTab);
  global["OnOpenThemesDirectory"] = BindJSCallback(&UI::OnOpenThemesDirectory);
  global["GetDownloadsSnapshot"] = BindJSCallbackWithRetval(&UI::OnDownloadsOverlayGet);
  global["ClearDownloadsSnapshot"] = BindJSCallback(&UI::OnDownloadsOverlayClear);
  global["OnAddressBarBlur"] = BindJSCallback(&UI::OnAddressBarBlur);
  global["OnAddressBarFocus"] = BindJSCallback(&UI::OnAddressBarFocus);
  global["GetSuggestions"] = BindJSCallbackWithRetval(&UI::OnGetSuggestions);
  global["OpenSuggestionsOverlay"] = BindJSCallback(&UI::OnOpenSuggestionsOverlay);
  global["CloseSuggestionsOverlay"] = BindJSCallback(&UI::OnCloseSuggestionsOverlay);
  global["OnSuggestOpen"] = BindJSCallback(&UI::OnSuggestOpen);
  global["OnSuggestClose"] = BindJSCallback(&UI::OnSuggestClose);

  if (is_downloads_overlay_view)
  {
    global["NativeGetDownloads"] = BindJSCallbackWithRetval(&UI::OnDownloadsOverlayGet);
    global["NativeClearDownloads"] = BindJSCallback(&UI::OnDownloadsOverlayClear);
    global["NativeOpenDownload"] = BindJSCallback(&UI::OnDownloadsOverlayOpenItem);
    global["NativeRevealDownload"] = BindJSCallback(&UI::OnDownloadsOverlayRevealItem);
    global["NativePauseDownload"] = BindJSCallback(&UI::OnDownloadsOverlayPauseItem);
    global["NativeRemoveDownload"] = BindJSCallback(&UI::OnDownloadsOverlayRemoveItem);
  }

  if (is_settings_page_view)
  {
    // Settings page is loaded - hydrate it with current settings immediately
    applySettingsPanel = global["applySettingsState"];
    global["GetDrmStatus"] = BindJSCallbackWithRetval(&UI::OnGetDrmStatus);
    global["InstallDrmDependencies"] = BindJSCallbackWithRetval(&UI::OnInstallDrmDependencies);
    if (applySettingsPanel)
    {
      std::string payload = BuildSettingsPayload(true);
      // Settings page loaded Ă˘â‚¬â€ť apply settings
      applySettingsPanel({String(payload.c_str())});
    }
    else
    {
      // Settings page loaded but applySettingsState not yet bound
    }
  }

  if (is_extensions_page_view)
  {
    // Extensions page is loaded - bind extension management callbacks
    global["GetExtensions"] = BindJSCallbackWithRetval(&UI::OnGetExtensions);
    global["OnToggleExtension"] = BindJSCallback(&UI::OnToggleExtension);
    global["OnReloadExtension"] = BindJSCallback(&UI::OnReloadExtension);
    global["OnReloadAllExtensions"] = BindJSCallback(&UI::OnReloadAllExtensions);
    global["OnDeleteExtension"] = BindJSCallback(&UI::OnDeleteExtension);
    global["OnLoadExtension"] = BindJSCallbackWithRetval(&UI::OnLoadExtension);
    global["OnCreateExtension"] = BindJSCallback(&UI::OnCreateExtension);
    global["OnOpenExtensionsFolder"] = BindJSCallback(&UI::OnOpenExtensionsFolder);
  }

  // Passwords page bindings
  bool is_passwords_page_view = url_utf8.data() && std::strstr(url_utf8.data(), "passwords.html") != nullptr;
  if (is_passwords_page_view)
  {
    // Password management callbacks - bind directly to global object
    global["getPasswords"] = BindJSCallbackWithRetval(&UI::OnGetPasswords);
    global["getPasswordStats"] = BindJSCallbackWithRetval(&UI::OnGetPasswordStats);
    global["savePassword"] = BindJSCallback(&UI::OnSavePassword);
    global["deletePassword"] = BindJSCallback(&UI::OnDeletePassword);
    global["getDecryptedPassword"] = BindJSCallbackWithRetval(&UI::OnGetDecryptedPassword);
    global["savePasswordSettings"] = BindJSCallback(&UI::OnSavePasswordSettings);
    global["exportPasswords"] = BindJSCallback(&UI::OnExportPasswords);
    global["importPasswords"] = BindJSCallback(&UI::OnImportPasswords);
    global["isDarkModeEnabled"] = BindJSCallbackWithRetval(&UI::OnIsDarkModeEnabled);
  }

  if (!is_menu_view && !is_ctx_view && !is_sugg_view && !is_downloads_overlay_view && !is_settings_page_view && !is_extensions_page_view)
  {
    SyncAdblockStateToUI();
    SyncSettingsStateToUI(true);
    // Rehydrate tab strip from existing C++ tabs if present. This avoids
    // creating a spurious new tab when the chrome overlay reloads and
    // preserves the user's open tabs (including settings tab) and active
    // selection.
    RefPtr<JSContext> lock(view()->LockJSContext());
    if (tabs_.empty())
    {
      // Check if we should restore a previous session
      // Only show restore bar if there are meaningful (non-internal) tabs to restore
      if (settings_.restore_session_on_startup && session_restore_pending_ && HasSavedSession() && GetMeaningfulSavedTabCount() > 0)
      {
        // IMPORTANT: Set this flag BEFORE creating the tab to prevent session saving
        // from overwriting the saved session while the restore bar is visible
        session_restore_bar_visible_ = true;

        // Create a blank tab first, then show restore bar
        CreateNewTab();
        // Show the session restore bar to ask user
        ShowSessionRestoreBar();
      }
      else
      {
        // No meaningful session to restore, create a new tab
        CreateNewTab();
        // Clear restore pending since there's nothing meaningful to restore
        session_restore_pending_ = false;
      }
    }
    else
    {
      for (auto &entry : tabs_)
      {
        if (!entry.second)
          continue;
        // addTab expects: id, title, favicon, is_loading
        addTab({entry.first, entry.second->view()->title(), GetFaviconURL(entry.second->view()->url()), entry.second->view()->is_loading()});
        if (setTabDrmState)
        {
          bool is_drm = false;
          auto drm_it = drm_tabs_.find(entry.first);
          if (drm_it != drm_tabs_.end() && drm_it->second)
            is_drm = true;
          setTabDrmState({entry.first, is_drm ? 1.0 : 0.0});
        }
      }

      // Ensure the active tab state is reflected in the chrome UI
      if (tabs_.count(active_tab_id_) && tabs_[active_tab_id_])
      {
        auto tab_view = tabs_[active_tab_id_]->view();
        SetLoading(tab_view->is_loading());
        SetCanGoBack(tab_view->CanGoBack());
        // Was CanGoBack(), which made the forward arrow mirror the back arrow:
        // a tab with history ahead but none behind showed forward as disabled.
        SetCanGoForward(tab_view->CanGoForward());
        SetURL(tab_view->url());
      }
    }
  }

  // Apply the scroll mode to the view that just loaded. Settings and the other
  // chrome pages must scroll instantly even when smooth scrolling is on, and a
  // view can navigate from a web page to a chrome page (or back) without any
  // settings change to piggyback on.
  SyncScrollModeForView(RefPtr<View>(caller));
}

void UI::OnBack(const JSObject &obj, const JSArgs &args)
{
  if (ActiveTabIsDRM())
  {
    if (auto tab = active_drm_tab())
      tab->GoBack();
    return;
  }
  if (active_tab())
    active_tab()->view()->GoBack();
}

void UI::OnForward(const JSObject &obj, const JSArgs &args)
{
  if (ActiveTabIsDRM())
  {
    if (auto tab = active_drm_tab())
      tab->GoForward();
    return;
  }
  if (active_tab())
    active_tab()->view()->GoForward();
}

void UI::OnRefresh(const JSObject &obj, const JSArgs &args)
{
  if (ActiveTabIsDRM())
  {
    if (auto tab = active_drm_tab())
      tab->Reload();
    return;
  }
  if (active_tab())
    active_tab()->view()->Reload();
}

void UI::OnStop(const JSObject &obj, const JSArgs &args)
{
  if (ActiveTabIsDRM())
  {
    if (auto tab = active_drm_tab())
      tab->Stop();
    return;
  }
  if (active_tab())
    active_tab()->view()->Stop();
}

void UI::OnToggleTools(const JSObject &obj, const JSArgs &args)
{
  if (ActiveTabIsDRM())
    return;
  if (active_tab())
    active_tab()->ToggleInspector();
}

void UI::OnRequestNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTab();
}

void UI::OnResetZoom(const JSObject &, const JSArgs &)
{
  // Clicking the zoom badge resets the active tab to 100%, matching Chrome's
  // click-the-zoom-indicator behaviour.
  auto tab = active_tab();
  if (!tab)
    return;
  tab->SetZoom(1.0);
  SyncZoomToUI(tab->zoom());
}

ultralight::JSValue UI::OnHideTabSearchOverlay(const JSObject &, const JSArgs &)
{
  HideTabSearchOverlay();
  // Returned to JS because the page calls it as a plain function; an undefined
  // return value would surface as an exception in the page's try/catch.
  return JSValue();
}

void UI::OnRequestNewWindow(const JSObject &obj, const JSArgs &args)
{
#if defined(_WIN32)
  // Get the executable path
  wchar_t exePath[MAX_PATH];
  GetModuleFileNameW(NULL, exePath, MAX_PATH);

  // Launch new instance
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
#else
  // For non-Windows platforms, spawn a new process
  std::string exePath = std::filesystem::read_symlink("/proc/self/exe").string();
  if (fork() == 0)
  {
    execl(exePath.c_str(), exePath.c_str(), nullptr);
    exit(0);
  }
#endif
}

// Records a tab so Ctrl+Shift+T can bring it back, matching Chrome. Called
// before the tab is torn down, while its URL and title are still readable.
//
// The URL is captured here rather than reconstructed later because a DRM tab's
// address only exists in the drm_tab_urls_ map, which is erased during close.
void UI::RememberClosedTab(uint64_t tab_id)
{
  std::string url;
  std::string title;

  auto drm_url = drm_tab_urls_.find(tab_id);
  if (drm_url != drm_tab_urls_.end())
  {
    url = drm_url->second;
    auto drm_title = drm_tab_titles_.find(tab_id);
    if (drm_title != drm_tab_titles_.end())
      title = drm_title->second;
  }
  else
  {
    auto it = tabs_.find(tab_id);
    if (it != tabs_.end() && it->second)
    {
      View *v = it->second->view().get();
      if (v)
      {
        url = util::ToStdString(v->url());
        title = util::ToStdString(v->title());
      }
    }
  }

  // Nothing worth restoring: a blank tab, or one that never navigated.
  if (url.empty())
    return;

  // Closing the same page repeatedly should not fill the stack with copies.
  if (!closed_tabs_.empty() && closed_tabs_.front().url == url)
    return;

  closed_tabs_.insert(closed_tabs_.begin(), ClosedTabEntry{url, title});

  // Chrome keeps a bounded list; the cap also bounds what gets persisted.
  constexpr size_t kMaxClosedTabs = 25;
  if (closed_tabs_.size() > kMaxClosedTabs)
    closed_tabs_.resize(kMaxClosedTabs);

  SaveClosedTabs();
}

bool UI::ReopenLastClosedTab()
{
  if (closed_tabs_.empty())
    return false;

  const ClosedTabEntry entry = closed_tabs_.front();
  closed_tabs_.erase(closed_tabs_.begin());
  SaveClosedTabs();

  const String url(entry.url.c_str());
  RefPtr<View> created = CreateNewTabForChildView(url);

  // The page reports its own title once it loads, so nothing is seeded here.
  return created.get() != nullptr;
}

void UI::OnRequestTabClose(const JSObject &obj, const JSArgs &args)
{
  if (args.size() == 1)
  {
    uint64_t id = args[0];

    // find(), not operator[]: an unknown id would default-insert a null entry
    // and the !tab check below would then return, leaving a phantom tab that
    // inflates tabs_.size() (breaking the "last tab quits" check and any code
    // that iterates the map).
    auto tab_it = tabs_.find(id);
    if (tab_it == tabs_.end() || !tab_it->second)
      return;

    // Capture the address while the tab still exists, so it can be reopened.
    RememberClosedTab(id);

    if (tabs_.size() == 1 && App::instance())
      App::instance()->Quit();

    if (drm_tabs_.count(id))
    {
      drm_tabs_[id]->Close();
      drm_tabs_.erase(id);
      drm_tab_titles_.erase(id);
      drm_tab_urls_.erase(id);
    }

    if (id != active_tab_id_)
    {
      tabs_[id].reset();
      tabs_.erase(id);
    }
    else
    {
      tab_it->second->set_ready_to_close(true);
    }

    RefPtr<JSContext> lock(view()->LockJSContext());
    closeTab({id});

    tab_last_active_seq_.erase(id);
    // The switcher would otherwise keep listing a tab that no longer exists.
    if (tab_search_overlay_)
      HideTabSearchOverlay();
    if (memory_monitor_)
      memory_monitor_->NoteTabClosed();

    // Closing a tab is a natural moment to top up: the user just made room.
    ReclaimMemoryIfNeeded();

    // Save session after tab close for crash recovery
    SaveSessionToDisk();
  }
}

void UI::OnActiveTabChange(const JSObject &obj, const JSArgs &args)
{
  if (args.size() == 1)
  {
    uint64_t id = args[0];

    if (id == active_tab_id_)
      return;

    // Use find() rather than operator[]. `tabs_[id]` default-inserts a null
    // entry for an unknown id, which then sits in the map as a live-looking but
    // empty tab and is dereferenced unguarded further down.
    auto incoming_it = tabs_.find(id);
    if (incoming_it == tabs_.end() || !incoming_it->second)
      return;

    NoteTabActivated(id);

    // Always hide all DRM tabs first to ensure clean state
    HideAllDrmTabs();

    // Hide the previous Ultralight tab if it wasn't DRM. This used to guard with
    // count() but then dereferenced tabs_[active_tab_id_] again on the very next
    // line without any check, so an unknown active id crashed the process.
    auto previous_it = tabs_.find(active_tab_id_);
    if (previous_it != tabs_.end() && previous_it->second)
    {
      previous_it->second->Hide();

      if (previous_it->second->ready_to_close())
      {
        tabs_.erase(previous_it);
      }
    }

    active_tab_id_ = id;
    auto active_it = tabs_.find(active_tab_id_);
    if (active_it == tabs_.end() || !active_it->second)
      return;

    // If the newly active tab is NOT the settings page, mark it as the
    // last non-settings active tab so reloads requested from the settings
    // page will target a meaningful browsing tab.
    auto tabView = active_it->second->view();
    if (tabView)
    {
      auto tab_url = tabView->url().utf8();
      const char *tab_u = tab_url.data() ? tab_url.data() : "";
      if (std::strstr(tab_u, "settings.html") == nullptr)
        last_non_settings_tab_id_ = active_tab_id_;
    }
    auto drm_tab = GetDrmTab(active_tab_id_);
    if (drm_tab)
    {
      drm_tab->Show();
      drm_tab->Focus(); // Give focus to DRM tab
      auto title_it = drm_tab_titles_.find(active_tab_id_);
      auto url_it = drm_tab_urls_.find(active_tab_id_);
      if (url_it != drm_tab_urls_.end())
        SetURL(ultralight::String(url_it->second.c_str()));
      SetLoading(false);
      SetCanGoBack(drm_tab->CanGoBack());
      SetCanGoForward(drm_tab->CanGoForward());
      // Zoom is per Ultralight tab; a DRM tab has none of its own.
      SyncZoomToUI(1.0);
    }
    else
    {
      active_it->second->Show();
      active_it->second->view()->Focus(); // Give focus to Ultralight tab
      auto tab_view = active_it->second->view();
      SetLoading(tab_view->is_loading());
      SetCanGoBack(tab_view->CanGoBack());
      // Was CanGoBack(), so the forward arrow always mirrored the back arrow.
      SetCanGoForward(tab_view->CanGoForward());
      SetURL(tab_view->url());
      // Zoom is per tab, so the badge has to follow the newly active tab.
      SyncZoomToUI(active_it->second->zoom());
    }

    // Update bookmark button state for the newly active tab
    UpdateBookmarkButtonState();
  }
}

void UI::OnRequestChangeURL(const JSObject &obj, const JSArgs &args)
{
  if (args.size() == 1)
  {
    ultralight::String url = args[0];
    std::string url_utf8;
    auto url_data = url.utf8();
    if (url_data.data())
      url_utf8 = url_data.data();

    // Check if this is a DRM site
    if (MaybeOpenDrmTab(active_tab_id_, url_utf8, true))
      return;

    // Not DRM, but it may be a video host the engine cannot decode.
    if (MaybeOpenMediaTab(active_tab_id_, url_utf8))
      return;

    // Not a DRM site - close any existing DRM tab and show Ultralight tab
    HideAllDrmTabs();
    // find(), not operator[]: the active tab can legitimately be absent from
    // tabs_ (a DRM tab lives in drm_tabs_), and indexing would insert a null
    // entry that then counts toward tabs_.size().
    if (!tabs_.empty())
    {
      auto tab_it = tabs_.find(active_tab_id_);
      if (tab_it != tabs_.end() && tab_it->second)
      {
        tab_it->second->Show();
        tab_it->second->view()->Focus(); // Ensure focus returns to Ultralight
        tab_it->second->view()->LoadURL(url);
      }
    }

    // Reclaim only now that active_tab_id_ reflects the tab the user chose.
    // Doing this earlier would let the monitor treat the incoming tab as
    // background and evict the page the user just switched to.
    ReclaimMemoryIfNeeded();
  }
}

void UI::OnAddressBarNavigate(const JSObject &obj, const JSArgs &args)
{
  if (args.size() == 1)
  {
    ultralight::String url = args[0];
    std::string url_utf8;
    auto url_data = url.utf8();
    if (url_data.data())
      url_utf8 = url_data.data();

    // Record immediately so History UI updates quickly (dedup inside RecordHistory)
    RecordHistory(url, String(""));

    // Check if the new URL is a DRM site
    bool new_url_is_drm = drm_settings_.IsDRMRequired(url_utf8);

    // Check if currently on a DRM site
    auto drm_it = drm_tabs_.find(active_tab_id_);
    bool is_on_drm = (drm_it != drm_tabs_.end() && drm_it->second);

    if (is_on_drm && new_url_is_drm)
    {
      // DRM -> DRM: Simple navigation within WebView2
      drm_it->second->LoadURL(url_utf8);
      drm_tab_urls_[active_tab_id_] = url_utf8;
      SetURL(url);
      return;
    }

    if (is_on_drm && !new_url_is_drm)
    {
      // DRM -> Non-DRM: Close DRM tab, convert to standard Ultralight tab
      uint64_t tab_id = active_tab_id_;

      // Close and remove DRM WebView2
      drm_it->second->Close();
      drm_tabs_.erase(tab_id);
      drm_tab_urls_.erase(tab_id);
      drm_tab_titles_.erase(tab_id);

      // Update UI to remove DRM badge
      UpdateDrmBadge(tab_id, false);

      // Navigate the Ultralight tab to the new URL
      auto tab_it = tabs_.find(tab_id);
      if (tab_it != tabs_.end() && tab_it->second)
      {
        tab_it->second->Show();
        tab_it->second->view()->Focus();
        tab_it->second->view()->LoadURL(url);

        // Update UI immediately
        SetURL(url);
        SetLoading(true);
      }
      return;
    }

    if (!is_on_drm && new_url_is_drm)
    {
      // Non-DRM -> DRM: Convert existing tab to DRM tab
      uint64_t tab_id = active_tab_id_;

      // Create DRM tab (this will handle loading page display)
      if (MaybeOpenDrmTab(tab_id, url_utf8, true))
      {
        // Update UI to add DRM badge
        UpdateDrmBadge(tab_id, true);
      }
      return;
    }

    // Non-DRM -> Non-DRM: Standard navigation
    auto tab_it = tabs_.find(active_tab_id_);
    if (tab_it != tabs_.end() && tab_it->second)
    {
      tab_it->second->view()->LoadURL(url);
      tab_it->second->Show();
      tab_it->second->view()->Focus();
    }
  }
}

void UI::OnOpenHistoryNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTabForChildView(String("file:///history.html"));
}

void UI::OnOpenBookmarksNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTabForChildView(String("file:///bookmarks.html"));
}

void UI::OnOpenDownloadsNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTabForChildView(String("file:///downloads.html"));
}

void UI::OnOpenPasswordsNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTabForChildView(String("file:///passwords.html"));
}

void UI::OnOpenExtensionsNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTabForChildView(String("file:///extensions.html"));
}

void UI::OnOpenThemesNewTab(const JSObject &obj, const JSArgs &args)
{
  CreateNewTabForChildView(String("file:///themes.html"));
}

void UI::OnOpenThemesDirectory(const JSObject &obj, const JSArgs &args)
{
  // Open themes directory in system file explorer
  std::filesystem::path themes_dir = std::filesystem::current_path() / "data" / "themes";

  // Create directory if it doesn't exist
  if (!std::filesystem::exists(themes_dir)) {
    std::filesystem::create_directories(themes_dir);
  }

  std::string path_str = themes_dir.string();
#ifdef _WIN32
  std::wstring wide_path(path_str.begin(), path_str.end());
  ShellExecuteW(NULL, L"explore", wide_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::string cmd = "open " + util::EscapeShellArg(path_str);
  system(cmd.c_str());
#else
  std::string cmd = "xdg-open " + util::EscapeShellArg(path_str);
  system(cmd.c_str());
#endif
}

// ============================================================================
// Extension System Implementation
// ============================================================================

void UI::InitializeExtensions()
{
  std::string ext_dir = GetExtensionsDirectory();
  extensions::ExtensionManager::Instance().Initialize(ext_dir);
}

std::string UI::GetExtensionsDirectory() const
{
  namespace fs = std::filesystem;
  fs::path dir = SettingsDirectory() / "extensions";
  if (!fs::exists(dir))
  {
    std::error_code ec;
    fs::create_directories(dir, ec);
  }
  return dir.string();
}

std::string UI::BuildExtensionsPayload() const
{
  const auto &extensions = extensions::ExtensionManager::Instance().GetExtensions();
  std::string ext_dir = GetExtensionsDirectory();

  std::ostringstream ss;
  ss << "{\"extensions\":[";
  bool first = true;
  for (const auto &ext : extensions)
  {
    if (!first)
      ss << ",";
    first = false;

    ss << "{";
    ss << "\"id\":\"" << ext.id << "\",";
    ss << "\"name\":\"";
    // Escape name for JSON
    for (char c : ext.name)
    {
      if (c == '"')
        ss << "\\\"";
      else if (c == '\\')
        ss << "\\\\";
      else if (c == '\n')
        ss << "\\n";
      else
        ss << c;
    }
    ss << "\",";
    ss << "\"description\":\"";
    for (char c : ext.description)
    {
      if (c == '"')
        ss << "\\\"";
      else if (c == '\\')
        ss << "\\\\";
      else if (c == '\n')
        ss << "\\n";
      else
        ss << c;
    }
    ss << "\",";
    ss << "\"version\":\"" << ext.version << "\",";
    ss << "\"author\":\"";
    for (char c : ext.author)
    {
      if (c == '"')
        ss << "\\\"";
      else if (c == '\\')
        ss << "\\\\";
      else
        ss << c;
    }
    ss << "\",";
    ss << "\"enabled\":" << (ext.enabled ? "true" : "false") << ",";
    ss << "\"manifest_path\":\"";
    std::string base_path_str = ext.base_path.string();
    for (char c : base_path_str)
    {
      if (c == '"')
        ss << "\\\"";
      else if (c == '\\')
        ss << "\\\\";
      else
        ss << c;
    }
    ss << "\",";

    // Match patterns
    ss << "\"match_patterns\":[";
    bool first_pattern = true;
    for (const auto &p : ext.match_patterns)
    {
      if (!first_pattern)
        ss << ",";
      first_pattern = false;
      ss << "\"" << p << "\"";
    }
    ss << "],";

    // Permissions (empty for now since Extension struct doesn't have permissions)
    ss << "\"permissions\":[]";

    ss << "}";
  }
  ss << "],\"extensions_path\":\"";
  for (char c : ext_dir)
  {
    if (c == '\\')
      ss << "\\\\";
    else if (c == '"')
      ss << "\\\"";
    else
      ss << c;
  }
  ss << "\"}";
  return ss.str();
}

ultralight::JSValue UI::OnGetExtensions(const JSObject &obj, const JSArgs &args)
{
  std::string payload = BuildExtensionsPayload();
  return JSValue(String(payload.c_str()));
}

void UI::OnToggleExtension(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 2)
    return;
  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";
  bool enabled = args[1].ToBoolean();

  extensions::ExtensionManager::Instance().SetExtensionEnabled(id, enabled);
}

void UI::OnReloadExtension(const JSObject &obj, const JSArgs &args)
{
  if (args.empty())
    return;
  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";
  // Reload a specific extension by unloading and reloading it
  auto *ext = extensions::ExtensionManager::Instance().GetExtension(id);
  if (ext)
  {
    std::filesystem::path ext_path = ext->base_path;
    extensions::ExtensionManager::Instance().UnloadExtension(id);
    extensions::ExtensionManager::Instance().LoadExtension(ext_path);
  }
}

void UI::OnReloadAllExtensions(const JSObject &obj, const JSArgs &args)
{
  extensions::ExtensionManager::Instance().ReloadAll();
}

void UI::OnDeleteExtension(const JSObject &obj, const JSArgs &args)
{
  if (args.empty())
    return;
  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";
  extensions::ExtensionManager::Instance().DeleteExtension(id);
}

ultralight::JSValue UI::OnLoadExtension(const JSObject &obj, const JSArgs &args)
{
  if (args.empty())
    return JSValue(false);
  ultralight::String path_ul = args[0].ToString();
  auto path_str = path_ul.utf8();
  std::string path = path_str.data() ? path_str.data() : "";
  if (path.empty())
    return JSValue(false);

  // ImportExtension copies into the managed extensions directory, so the extension
  // is still present after ReloadAll()/restart. Loading it in place would make it
  // disappear on the next scan.
  auto &manager = extensions::ExtensionManager::Instance();
  return JSValue(manager.ImportExtension(path));
}

void UI::OnCreateExtension(const JSObject &obj, const JSArgs &args)
{
  if (args.empty())
    return;
  ultralight::String json_ul = args[0].ToString();
  auto json_str = json_ul.utf8();
  std::string json_data = json_str.data() ? json_str.data() : "";

  // Parse the JSON to extract id, name, description, match_pattern, script
  // Simple parsing (not full JSON parser)
  auto extract_value = [&json_data](const std::string &key) -> std::string
  {
    std::string search_key = "\"" + key + "\":\"";
    size_t pos = json_data.find(search_key);
    if (pos == std::string::npos)
      return "";
    pos += search_key.length();
    std::string result;
    while (pos < json_data.length() && json_data[pos] != '"')
    {
      if (json_data[pos] == '\\' && pos + 1 < json_data.length())
      {
        pos++;
        if (json_data[pos] == 'n')
          result += '\n';
        else if (json_data[pos] == 't')
          result += '\t';
        else
          result += json_data[pos];
      }
      else
      {
        result += json_data[pos];
      }
      pos++;
    }
    return result;
  };

  std::string id = extract_value("id");
  std::string name = extract_value("name");
  std::string description = extract_value("description");
  std::string match_pattern = extract_value("match_pattern");
  std::string script = extract_value("script");

  if (id.empty() || script.empty())
    return;

  // Create extension directory and files manually
  namespace fs = std::filesystem;
  fs::path ext_base = fs::path(GetExtensionsDirectory()) / id;
  if (!fs::exists(ext_base))
  {
    std::error_code ec;
    fs::create_directories(ext_base, ec);
    if (ec)
      return;
  }

  // Create manifest.json
  std::ofstream manifest_file(ext_base / "manifest.json");
  if (manifest_file.is_open())
  {
    manifest_file << "{\n";
    manifest_file << "  \"id\": \"" << id << "\",\n";
    manifest_file << "  \"name\": \"" << (name.empty() ? id : name) << "\",\n";
    manifest_file << "  \"description\": \"" << description << "\",\n";
    manifest_file << "  \"version\": \"1.0.0\",\n";
    manifest_file << "  \"content_scripts\": [\n";
    manifest_file << "    {\n";
    manifest_file << "      \"matches\": [\"" << (match_pattern.empty() ? "*://*/*" : match_pattern) << "\"],\n";
    manifest_file << "      \"js\": [\"content.js\"]\n";
    manifest_file << "    }\n";
    manifest_file << "  ]\n";
    manifest_file << "}\n";
    manifest_file.close();
  }

  // Create content.js with the script
  std::ofstream script_file(ext_base / "content.js");
  if (script_file.is_open())
  {
    script_file << script;
    script_file.close();
  }

  // Load the newly created extension
  extensions::ExtensionManager::Instance().LoadExtension(ext_base);
}

void UI::OnOpenExtensionsFolder(const JSObject &obj, const JSArgs &args)
{
  std::string ext_dir = GetExtensionsDirectory();
#ifdef _WIN32
  // Use ShellExecute to open folder
  std::wstring wide_path(ext_dir.begin(), ext_dir.end());
  ShellExecuteW(NULL, L"explore", wide_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::string cmd = "open " + util::EscapeShellArg(ext_dir);
  system(cmd.c_str());
#else
  std::string cmd = "xdg-open " + util::EscapeShellArg(ext_dir);
  system(cmd.c_str());
#endif
}

void UI::CreateNewTab()
{
  // Hide all DRM tabs when creating a new standard tab
  HideAllDrmTabs();

  uint64_t id = tab_id_counter_++;
  RefPtr<Window> window = window_;
  int tab_height = window->height() - ui_height_;
  if (tab_height < 1)
    tab_height = 1;

  // Build view settings from current browser settings
  TabViewSettings view_settings;
  view_settings.enable_javascript = settings_.enable_javascript;
  view_settings.hardware_acceleration = settings_.hardware_acceleration;

  tabs_[id] = std::make_unique<Tab>(this, id, window->width(), (uint32_t)tab_height, 0, ui_height_, active_user_agent_, view_settings);

  // Use cached HTML for instant page display (no file I/O delay)
  // This eliminates the white flash before page content loads
  const char *kStartPageURL = "file:///static-sties/google-static.html";
  if (!cached_start_page_html_.empty())
  {
    tabs_[id]->view()->LoadHTML(String(cached_start_page_html_.c_str()), String(kStartPageURL));
  }
  else
  {
    tabs_[id]->view()->LoadURL(kStartPageURL);
  }

  {
    RefPtr<JSContext> lock(view()->LockJSContext());
    addTab({id, "New Tab", GetFaviconURL(kStartPageURL), tabs_[id]->view()->is_loading()});
  }
  UpdateDrmBadge(id, false);

  // Track tab load for the low-RAM monitor. A brand-new tab is not activated
  // until the chrome UI reports the selection, so it is intentionally left with
  // no recency entry and sorts as the coldest candidate.
  if (memory_monitor_)
    memory_monitor_->NoteTabOpened();

  // Opening a tab is the point where pressure most often matters. Reclaim before
  // adding load, not after.
  ReclaimMemoryIfNeeded();

  // Save session after new tab for crash recovery
  SaveSessionToDisk();
}

RefPtr<View> UI::CreateNewTabForChildView(const String &url)
{
  // Hide all DRM tabs when creating a new standard tab
  HideAllDrmTabs();

  uint64_t id = tab_id_counter_++;
  RefPtr<Window> window = window_;
  int tab_height = window->height() - ui_height_;
  if (tab_height < 1)
    tab_height = 1;

  // Build view settings from current browser settings
  TabViewSettings view_settings;
  view_settings.enable_javascript = settings_.enable_javascript;
  view_settings.hardware_acceleration = settings_.hardware_acceleration;

  tabs_[id] = std::make_unique<Tab>(this, id, window->width(), (uint32_t)tab_height, 0, ui_height_, active_user_agent_, view_settings);

  // Try to use cached HTML for instant loading of internal pages
  auto url_utf8 = url.utf8();
  std::string url_str(url_utf8.data() ? url_utf8.data() : "");
  const std::string &cached_html = GetCachedPageHTML(url_str);
  if (!cached_html.empty())
  {
    // Use cached HTML for instant display
    tabs_[id]->view()->LoadHTML(String(cached_html.c_str()), url);
  }
  else
  {
    // Fall back to regular URL loading for non-cached pages
    tabs_[id]->view()->LoadURL(url);
  }

  {
    RefPtr<JSContext> lock(view()->LockJSContext());
    addTab({id, "", GetFaviconURL(url), tabs_[id]->view()->is_loading()});
  }
  UpdateDrmBadge(id, false);

  return tabs_[id]->view();
}

void UI::UpdateTabTitle(uint64_t id, const ultralight::String &title)
{
  RefPtr<JSContext> lock(view()->LockJSContext());
  // Title changed; pass current page URL-derived favicon
  updateTab({id, title, GetFaviconURL(tabs_[id]->view()->url()), tabs_[id]->view()->is_loading()});

  // If active tab is a local file, reflect title in the address bar instead of file URL
  if (id == active_tab_id_)
  {
    auto url_u = tabs_[id]->view()->url().utf8();
    const char *cur = url_u.data();
    std::string_view cur_view(cur ? cur : "");
    if (cur && cur_view.size() >= 7 && cur_view.substr(0, 7) == "file://")
    {
      updateURL({title});
    }
  }
}

void UI::UpdateTabURL(uint64_t id, const ultralight::String &url)
{
  // If this tab already has an active DRM view, ignore URL updates from the Ultralight tab
  // (they may come from about:blank or other intermediate states)
  if (GetDrmTab(id) != nullptr)
    return;

  std::string url_utf8;
  auto utf8 = url.utf8();
  if (utf8.data())
    url_utf8 = utf8.data();

  if (!url_utf8.empty())
  {
    if (MaybeOpenDrmTab(id, url_utf8, false))
      return;
    // Video hosts also go to the system webview when media fallback is on.
    if (MaybeOpenMediaTab(id, url_utf8))
      return;
    // Only hide DRM tab if we're navigating away from a DRM site
    // (this shouldn't happen since we check above, but keep for safety)
  }

  if (id == active_tab_id_ && !tabs_.empty())
  {
    SetURL(url);
    UpdateBookmarkButtonState();
  }
}

void UI::UpdateTabNavigation(uint64_t id, bool is_loading, bool can_go_back, bool can_go_forward)
{
  if (tabs_.empty())
    return;

  RefPtr<JSContext> lock(view()->LockJSContext());
  // Loading/nav state; update favicon based on current URL
  updateTab({id, tabs_[id]->view()->title(), GetFaviconURL(tabs_[id]->view()->url()), tabs_[id]->view()->is_loading()});

  if (id == active_tab_id_)
  {
    SetLoading(is_loading);
    SetCanGoBack(can_go_back);
    SetCanGoForward(can_go_forward);
  }

  // Save session when navigation completes (not during loading to reduce disk I/O)
  if (!is_loading)
  {
    SaveSessionToDisk();
  }
}

void UI::UpdateTabFavicon(uint64_t id, const String &favicon_data_url)
{
  if (tabs_.empty() || tabs_.find(id) == tabs_.end())
    return;

  RefPtr<JSContext> lock(view()->LockJSContext());
  // Update tab with the new favicon data URL
  updateTab({id, tabs_[id]->view()->title(), favicon_data_url, tabs_[id]->view()->is_loading()});
}

void UI::SetLoading(bool is_loading)
{
  RefPtr<JSContext> lock(view()->LockJSContext());
  updateLoading({is_loading});
}

void UI::SetCanGoBack(bool can_go_back)
{
  RefPtr<JSContext> lock(view()->LockJSContext());
  updateBack({can_go_back});
}

void UI::SetCanGoForward(bool can_go_forward)
{
  RefPtr<JSContext> lock(view()->LockJSContext());
  updateForward({can_go_forward});
}

void UI::SetURL(const ultralight::String &url)
{
  // For local static pages (file:///...), prefer showing the page title in the address bar
  ultralight::String display = url;
  auto u8 = url.utf8();
  const char *c_url = u8.data();
  bool is_file = (c_url && strncmp(c_url, "file://", 7) == 0);
  if (is_file && !tabs_.empty())
  {
    auto it = tabs_.find(active_tab_id_);
    if (it != tabs_.end() && it->second && it->second->view())
    {
      auto title = it->second->view()->title();
      auto t8 = title.utf8();
      if (t8.data() && *t8.data())
        display = title;
    }
  }

  RefPtr<JSContext> lock(view()->LockJSContext());
  updateURL({display});
}

void UI::SetCursor(ultralight::Cursor cursor)
{
  if (App::instance())
    window_->SetCursor(cursor);
}

void UI::UpdateBookmarkButtonState()
{
  if (!bookmark_store_ || !active_tab() || !active_tab()->view())
    return;

  auto url = active_tab()->view()->url();
  auto url_str = url.utf8();
  std::string url_string = url_str.data() ? url_str.data() : "";

  bool is_bookmarked = bookmark_store_->IsBookmarked(url_string);

  RefPtr<JSContext> lock(view()->LockJSContext());
  JSContextRef ctx = lock->ctx();
  ultralight::String js = ultralight::String("if(typeof updateBookmarkButton === 'function') updateBookmarkButton(") +
                          ultralight::String(is_bookmarked ? "true" : "false") +
                          ultralight::String(");");
  view()->EvaluateScript(js, nullptr);
}

String UI::GetFaviconURL(const String &page_url)
{
  // Best-effort: use origin + "/favicon.ico" for http/https URLs.
  // For browser internal pages, return custom favicons.
  // Cache by origin so multiple tabs/pages from the same site reuse it.
  auto utf8 = page_url.utf8();
  const char *url = utf8.data();
  if (!url)
    return String("");

  std::string_view url_view(url);

  // Handle browser internal pages with custom favicons (base64-encoded SVGs for CSS compatibility)
  if (url_view.find("file:///") == 0)
  {
    // Start page / Google static page - home icon
    if (url_view.find("static-sties/") != std::string_view::npos ||
        url_view.find("google-static") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEwIDIwdi02aDR2Nmg1di04aDNMMTIgMyAyIDEyaDN2OHonLz48L3N2Zz4=");

    // Settings page - gear icon
    if (url_view.find("settings.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTE5LjE0IDEyLjk0Yy4wNC0uMzEuMDYtLjYzLjA2LS45NCAwLS4zMS0uMDItLjYzLS4wNi0uOTRsMi4wMy0xLjU4YS40OS40OSAwIDAwLjEyLS42MWwtMS45Mi0zLjMyYS40OS40OSAwIDAwLS41OS0uMjJsLTIuMzkuOTZjLS41LS4zOC0xLjAzLS43LTEuNjItLjk0bC0uMzYtMi41NGEuNDg0LjQ4NCAwIDAwLS40OC0uNDFoLTMuODRjLS4yNCAwLS40My4xNy0uNDcuNDFsLS4zNiAyLjU0Yy0uNTkuMjQtMS4xMy41Ny0xLjYyLjk0bC0yLjM5LS45NmEuNDkuNDkgMCAwMC0uNTkuMjJMMi43NCA4Ljg3Yy0uMTIuMjEtLjA4LjQ3LjEyLjYxbDIuMDMgMS41OGMtLjA0LjMxLS4wNi42My0uMDYuOTRzLjAyLjYzLjA2Ljk0bC0yLjAzIDEuNThhLjQ5LjQ5IDAgMDAtLjEyLjYxbDEuOTIgMy4zMmMuMTIuMjIuMzcuMjkuNTkuMjJsMi4zOS0uOTZjLjUuMzggMS4wMy43IDEuNjIuOTRsLjM2IDIuNTRjLjA1LjI0LjI0LjQxLjQ4LjQxaDMuODRjLjI0IDAgLjQ0LS4xNy40Ny0uNDFsLjM2LTIuNTRjLjU5LS4yNCAxLjEzLS41NiAxLjYyLS45NGwyLjM5Ljk2Yy4yMi4wOC40NyAwIC41OS0uMjJsMS45Mi0zLjMyYy4xMi0uMjIuMDctLjQ3LS4xMi0uNjFsLTIuMDEtMS41OHpNMTIgMTUuNmMtMS45OCAwLTMuNi0xLjYyLTMuNi0zLjZzMS42Mi0zLjYgMy42LTMuNiAzLjYgMS42MiAzLjYgMy42LTEuNjIgMy42LTMuNiAzLjZ6Jy8+PC9zdmc+");

    // History page - clock icon
    if (url_view.find("history.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEzIDNhOSA5IDAgMDAtOSA5SDFsMy44OSAzLjg5LjA3LjE0TDkgMTJINmMwLTMuODcgMy4xMy03IDctN3M3IDMuMTMgNyA3LTMuMTMgNy03IDdjLTEuOTMgMC0zLjY4LS43OS00Ljk0LTIuMDZsLTEuNDIgMS40MkE4Ljk1NCA4Ljk1NCAwIDAwMTMgMjFhOSA5IDAgMDAwLTE4em0tMSA1djVsNC4yOCAyLjU0LjcyLTEuMjEtMy41LTIuMDhWOEgxMnonLz48L3N2Zz4=");

    // Downloads page - download icon
    if (url_view.find("downloads.html") != std::string_view::npos ||
        url_view.find("downloads-panel.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTE5IDloLTRWM0g5djZINWw3IDcgNy03ek01IDE4djJoMTR2LTJINXonLz48L3N2Zz4=");

    // Passwords page - key icon
    if (url_view.find("passwords.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEyLjY1IDEwQTUuOTkgNS45OSAwIDAwNyA2Yy0zLjMxIDAtNiAyLjY5LTYgNnMyLjY5IDYgNiA2YTUuOTkgNS45OSAwIDAwNS42NS00SDE3djRoNHYtNGgydi00SDEyLjY1ek03IDE0Yy0xLjEgMC0yLS45LTItMnMuOS0yIDItMiAyIC45IDIgMi0uOSAyLTIgMnonLz48L3N2Zz4=");

    // Extensions page - puzzle piece icon
    if (url_view.find("extensions.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTIwLjUgMTFIMTlWN2MwLTEuMS0uOS0yLTItMmgtNFYzLjVDMTMgMi4xMiAxMS44OCAxIDEwLjUgMVM4IDIuMTIgOCAzLjVWNUg0Yy0xLjEgMC0xLjk5LjktMS45OSAydjMuOEgzLjVjMS40OSAwIDIuNyAxLjIxIDIuNyAyLjdzLTEuMjEgMi43LTIuNyAyLjdIMlYyMGMwIDEuMS45IDIgMiAyaDMuOHYtMS41YzAtMS40OSAxLjIxLTIuNyAyLjctMi43IDEuNDkgMCAyLjcgMS4yMSAyLjcgMi43VjIySDE3YzEuMSAwIDItLjkgMi0ydi00aDEuNWMxLjM4IDAgMi41LTEuMTIgMi41LTIuNVMyMS44OCAxMSAyMC41IDExeicvPjwvc3ZnPg==");

    // About page - info icon
    if (url_view.find("about.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEyIDJDNi40OCAyIDIgNi40OCAyIDEyczQuNDggMTAgMTAgMTAgMTAtNC40OCAxMC0xMFMxNy41MiAyIDEyIDJ6bTEgMTVoLTJ2LTZoMnY2em0wLThoLTJWN2gydjJ6Jy8+PC9zdmc+");

    // New tab page - home icon
    if (url_view.find("new_tab_page.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEwIDIwdi02aDR2Nmg1di04aDNMMTIgMyAyIDEyaDN2OHonLz48L3N2Zz4=");

    // Release notes - document icon
    if (url_view.find("release_notes.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTE0IDJINMM0LjkgMCA0LjAxLjkgNC4wMSAyTDQgMjBjMCAxLjEuODkgMiAxLjk5IDJIMTHJMS4xIDAgMi0uOSAyLTJWOGwtNi02em0yIDE2SDh2LTJoOHYyem0wLTRIOHYtMmg4djJ6bS0zLTVWMy41TDE4LjUgOUgxM3onLz48L3N2Zz4=");

    // Themes page - theme icon
    if (url_view.find("themes.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEyIDNjLTQuOTcgMC05IDQuMDMtOSA5czQuMDMgOSA5IDljLjgzIDAgMS41LS42NyAxLjUtMS41IDAtLjM5LS4xNS0uNzQtLjM5LTEuMDEtLjIzLS4yNi0uMzgtLjYxLS4zOC0uOTkgMC0uODMuNjctMS41IDEuNS0xLjVIMTZjMi43NiAwIDUtMi4yNCA1LTUgMC00LjQyLTQuMDMtOC05LTh6bS01LjUgOWMtLjgzIDAtMS41LS42Ny0xLjUtMS41UzUuNjcgOSA2LjUgOSA4IDkuNjcgOCA5LjY3IDcuMzMgMTIgNi41IDEyem0zLTRDOC42NyA4IDggNy4zMyA4IDYuNXM4LjY3IDUgOS41IDVzMS41LjY3IDEuNSAxLjVTMTAuMzMgOCA5LjUgej01IDBjLS44MyAwLTEuNS0uNjctMS41LTEuNVMxMy42NyA1IDE0LjUgNXMxLjUuNjcgMS41IDEuNVMxNS4zMyA4IDE0LjUgej0zIDRjLS44MyAwLTEuNS0uNjctMS41LTEuNVMxNi42NyA5IDE3LjUgOXMxLjUuNjcgMS41IDEuNS0uNjcgMS41LTEuNSAxLjV6Jy8+PC9zdmc+");

    // Bookmarks page - folder icon
    if (url_view.find("bookmarks.html") != std::string_view::npos)
      return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCc+PHJlY3QgeD0iMiIgeT0iNCIgd2lkdGg9IjIwIiBoZWlnaHQ9IjE2IiBmaWxsPSIjYzJiY2U4Ii8+PHJlY3QgeD0iMiIgeT0iMiIgd2lkdGg9IjEyIiBoZWlnaHQ9IjQiIGZpbGw9IiNjMmJjZTgiLz48L3N2Zz4=");

    // Default for other file:// URLs - globe icon
    return String("data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAyNCAyNCcgZmlsbD0nI2MyYmNlOCc+PHBhdGggZD0nTTEyIDJDNi40OCAyIDIgNi40OCAyIDEyczQuNDggMTAgMTAgMTAgMTAtNC40OCAxMC0xMFMxNy41MiAyIDEyIDJ6bS0xIDE3LjkzYy0zLjk1LS40OS03LTMuODUtNy03LjkzIDAtLjYyLjA4LTEuMjEuMjEtMS43OUw5IDE1djFjMCAxLjEuOSAyIDIgMnYxLjkzem02LjktMi41NGMtLjI2LS44MS0xLTEuMzktMS45LTEuMzloLTF2LTNjMC0uNTUtLjQ1LTEtMS0xSDh2LTJoMmMuNTUgMCAxLS40NSAxLTFWN2gyYzEuMSAwIDItLjkgMi0ydi0uNDFjMi45MyAxLjE5IDUgNC4wNiA1IDcuNDEgMCAyLjA4LS44IDMuOTctMi4xIDUuMzl6Jy8+PC9zdmc+");
  }

  if (url_view.size() < 7 ||
      (url_view.substr(0, 7) != "http://" &&
       (url_view.size() < 8 || url_view.substr(0, 8) != "https://")))
    return String("");

  const char *scheme_sep = strstr(url, "://");
  if (!scheme_sep)
    return String("");
  const char *host_start = scheme_sep + 3;
  const char *slash_after_host = strchr(host_start, '/');

  // Compute origin as a std::string for cache key
  std::string origin_str;
  if (!slash_after_host)
  {
    origin_str.assign(url);
  }
  else
  {
    origin_str.assign(url, (size_t)(slash_after_host - url));
  }

  // Check disk cache first (contains data URIs that actually work)
  auto it_file = favicon_file_cache_.find(origin_str);
  if (it_file != favicon_file_cache_.end() && !it_file->second.empty())
  {
    return String(it_file->second.c_str());
  }

  // Check memory cache
  auto it = favicon_cache_.find(origin_str);
  if (it != favicon_cache_.end())
  {
    return String(it->second.c_str());
  }

  // Fall back to the conventional path. Returning "" here (the old behaviour)
  // meant an origin could only ever get a favicon if the suggestions page had
  // already cached it, so any site visited without typing it into the address
  // bar showed no icon at all. The tab chrome renders this in an <img>, which
  // loads an http(s) URL directly, and the async probe in Tab.cpp replaces it
  // with the real <link rel="icon"> when the page publishes one.
  return String((origin_str + "/favicon.ico").c_str());
}

// --- History helpers ---
void UI::RecordHistory(const String &url, const String &title)
{
  auto url_u = url.utf8();
  const char *c_url = url_u.data();
  if (!c_url)
    return;

  // Only record http(s)
  std::string_view url_view(c_url);
  if (url_view.size() < 7 ||
      (url_view.substr(0, 7) != "http://" &&
       (url_view.size() < 8 || url_view.substr(0, 8) != "https://")))
    return;

  // Basic cap to avoid unbounded growth later (we'll prune oldest when exceeding)
  auto title_u = title.utf8();
  std::string t = title_u.data() ? title_u.data() : "";
  std::string u = c_url;
  uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

  // Find existing entry by URL and update; otherwise push new
  //
  // A single navigation reaches RecordHistory twice when it starts from the
  // address bar: UI::OnAddressBarNavigate records immediately so the History
  // page updates promptly, and Tab::OnChangeURL records again once the view's
  // URL actually changes. Link navigations only hit the second path. Counting
  // both made visit_count roughly double for address-bar navigations while
  // link navigations counted once, so the numbers depended on how you got to a
  // page. A repeat inside this window is treated as the same visit: the title is
  // refreshed (the first call carries an empty one) but the count is not
  // incremented again.
  constexpr uint64_t kSameVisitWindowMs = 1500;

  bool found = false;
  for (auto &e : history_)
  {
    if (e.url == u)
    {
      if (!t.empty())
        e.title = t;
      const bool same_visit = (now_ms - e.timestamp_ms) < kSameVisitWindowMs;
      e.timestamp_ms = now_ms;
      if (!same_visit)
        e.visit_count = (e.visit_count + 1);
      found = true;
      break;
    }
  }
  if (!found)
  {
    history_.push_back({u, t, now_ms, 1});
  }

  // Cap to 500 by removing oldest (by timestamp)
  if (history_.size() > 500)
  {
    // Find oldest
    size_t idx = 0;
    uint64_t oldest = UINT64_MAX;
    size_t oldest_i = 0;
    for (idx = 0; idx < history_.size(); ++idx)
    {
      if (history_[idx].timestamp_ms < oldest)
      {
        oldest = history_[idx].timestamp_ms;
        oldest_i = idx;
      }
    }
    history_.erase(history_.begin() + oldest_i);
  }

  // Save history to disk after recording
  SaveHistoryToDisk();

  // If any tab is showing the History page, ask it to refresh now
  for (auto &it : tabs_)
  {
    auto &tabPtr = it.second;
    if (!tabPtr)
      continue;
    RefPtr<View> v = tabPtr->view();
    if (!v)
      continue;
    auto vurl_u = v->url().utf8();
    const char *vurl = vurl_u.data();
    if (vurl && std::strstr(vurl, "history.html"))
    {
      v->EvaluateScript("(function(){ if (window.refresh) window.refresh(); })();", nullptr);
    }
  }
}

static std::string jsonEscape(const std::string &s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s)
  {
    switch (c)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

String UI::GetHistoryJSON()
{
  // Serialize as { items: [ {url,title,time}, ... ] }
  std::string json = std::string("{\"items\":[");
  // Newest first
  for (size_t i = 0; i < history_.size(); ++i)
  {
    const auto &e = history_[history_.size() - 1 - i];
    if (i)
      json += ",";
    json += "{\"url\":\"" + jsonEscape(e.url) + "\",\"title\":\"" + jsonEscape(e.title) + "\",\"time\":" + std::to_string(e.timestamp_ms) + "}";
  }
  json += "]}";
  return String(json.c_str());
}

void UI::ClearHistory()
{
  history_.clear();
}

String UI::GetDownloadsJSON()
{
  if (!download_manager_)
    return String("{\"items\":[]}");
  std::string json = download_manager_->GetDownloadsJSON();
  return String(json.c_str());
}

void UI::ClearCompletedDownloads()
{
  if (!download_manager_)
    return;
  download_manager_->ClearFinishedDownloads();
  NotifyDownloadsChanged();
}

bool UI::OpenDownloadItem(uint64_t id)
{
  if (!download_manager_)
    return false;
  return download_manager_->OpenDownload(static_cast<DownloadManager::DownloadId>(id));
}

bool UI::RevealDownloadItem(uint64_t id)
{
  if (!download_manager_)
    return false;
  return download_manager_->RevealDownload(static_cast<DownloadManager::DownloadId>(id));
}

bool UI::PauseDownloadItem(uint64_t id)
{
  if (!download_manager_)
    return false;
  return download_manager_->CancelDownload(static_cast<DownloadManager::DownloadId>(id));
}

bool UI::RemoveDownloadItem(uint64_t id)
{
  if (!download_manager_)
    return false;
  return download_manager_->RemoveDownload(static_cast<DownloadManager::DownloadId>(id));
}

void UI::NotifyDownloadsChanged()
{
  if (download_manager_)
  {
    // Do not aggressively prune pending download placeholders here; keep
    // completed downloads visible until the user explicitly clears them.
    // Only notify about new downloads by sequence change.
    uint64_t latest_sequence = download_manager_->last_started_sequence();
    if (latest_sequence != 0 && latest_sequence != downloads_last_sequence_seen_)
    {
      downloads_last_sequence_seen_ = latest_sequence;
      OnNewDownloadStarted();
    }
  }

  bool has_active = download_manager_ ? download_manager_->HasActiveDownloads() : false;
  if (auto_open_download_panel_ && has_active && !downloads_overlay_had_active_)
  {
    downloads_overlay_user_dismissed_ = false;
    ShowDownloadsOverlay();
  }
  downloads_overlay_had_active_ = has_active;

  for (auto &entry : tabs_)
  {
    auto &tab = entry.second;
    if (!tab)
      continue;
    RefPtr<View> view = tab->view();
    if (!view)
      continue;
    auto url_u = view->url().utf8();
    const char *url = url_u.data();
    if (url && std::strstr(url, "downloads.html"))
    {
      view->EvaluateScript("(function(){ if(window.__ul_downloads_ready) window.__ul_downloads_ready(); })();", nullptr);
    }
  }

  if (downloads_overlay_ && downloads_overlay_->view())
  {
    downloads_overlay_->view()->EvaluateScript("(function(){ if(window.__ul_downloads_panel_refresh) window.__ul_downloads_panel_refresh(); })();", nullptr);
  }

  if (overlay_ && overlay_->view())
  {
    overlay_->view()->EvaluateScript("(function(){ if(window.__ul_update_downloads_badge) window.__ul_update_downloads_badge(); })();", nullptr);
  }
}

ultralight::JSValue UI::OnDownloadsOverlayGet(const JSObject &, const JSArgs &)
{
  return ultralight::JSValue(GetDownloadsJSON());
}

void UI::OnDownloadsOverlayClear(const JSObject &, const JSArgs &)
{
  ClearCompletedDownloads();
}

void UI::OnDownloadsOverlayOpenItem(const JSObject &, const JSArgs &args)
{
  if (args.empty())
    return;
  uint64_t id = static_cast<uint64_t>((double)args[0]);
  OpenDownloadItem(id);
}

void UI::OnDownloadsOverlayRevealItem(const JSObject &, const JSArgs &args)
{
  if (args.empty())
    return;
  uint64_t id = static_cast<uint64_t>((double)args[0]);
  RevealDownloadItem(id);
}

void UI::OnDownloadsOverlayPauseItem(const JSObject &, const JSArgs &args)
{
  if (args.empty())
    return;
  uint64_t id = static_cast<uint64_t>((double)args[0]);
  PauseDownloadItem(id);
}

void UI::OnDownloadsOverlayRemoveItem(const JSObject &, const JSArgs &args)
{
  if (args.empty())
    return;
  uint64_t id = static_cast<uint64_t>((double)args[0]);
  RemoveDownloadItem(id);
}

void UI::OnDownloadsOverlayToggle(const JSObject &, const JSArgs &)
{
  if (downloads_overlay_)
  {
    downloads_overlay_user_dismissed_ = true;
    HideDownloadsOverlay();
  }
  else
  {
    downloads_overlay_user_dismissed_ = false;
    ShowDownloadsOverlay();
  }
}

void UI::OnDownloadsOverlayClose(const JSObject &, const JSArgs &)
{
  downloads_overlay_user_dismissed_ = true;
  HideDownloadsOverlay();
}

void UI::ShowDownloadsOverlay()
{
  if (downloads_overlay_)
    return;

  downloads_overlay_user_dismissed_ = false;

  // Hide active DRM WebView2 tab so overlay appears on top
  auto drm_it = drm_tabs_.find(active_tab_id_);
  if (drm_it != drm_tabs_.end() && drm_it->second)
  {
    drm_it->second->Hide();
    // Show the pre-loaded solid background Ultralight tab
    auto tab_it = tabs_.find(active_tab_id_);
    if (tab_it != tabs_.end() && tab_it->second)
      tab_it->second->Show();
  }

  ultralight::ViewConfig cfg;
  cfg.is_transparent = true;
  cfg.initial_device_scale = window_->scale();
  if (overlay_ && overlay_->view())
  {
    cfg.is_accelerated = overlay_->view()->is_accelerated();
    cfg.display_id = overlay_->view()->display_id();
  }

  int overlay_top = ui_height_ + kDownloadsOverlaySpacing;
  if (overlay_top < 0)
    overlay_top = 0;
  uint32_t overlay_height = window_->height();
  if (overlay_height > (uint32_t)overlay_top)
    overlay_height -= static_cast<uint32_t>(overlay_top);
  else
    overlay_height = 1;

  auto view = App::instance()->renderer()->CreateView(window_->width(), overlay_height, cfg, nullptr);
  downloads_overlay_ = Overlay::Create(window_, view, 0, overlay_top);
  LayoutDownloadsOverlay();
  downloads_overlay_->Show();
  downloads_overlay_->Focus();
  view->set_load_listener(this);
  view->set_view_listener(this);
  view->LoadURL("file:///downloads-panel.html");
}

void UI::HideDownloadsOverlay()
{
  if (!downloads_overlay_)
    return;

  downloads_overlay_->Hide();
  downloads_overlay_->Unfocus();
  if (overlay_)
    overlay_->Focus();

  auto view = downloads_overlay_->view();
  if (view)
  {
    view->set_load_listener(nullptr);
    view->set_view_listener(nullptr);
  }

  downloads_overlay_ = nullptr;

  // Restore active DRM WebView2 tab if no other overlays are open
  // Note: suggestions_overlay_ is excluded because it doesn't hide the DRM tab
  if (!menu_overlay_ && !context_menu_overlay_)
  {
    auto drm_it = drm_tabs_.find(active_tab_id_);
    if (drm_it != drm_tabs_.end() && drm_it->second)
      drm_it->second->Show();
  }
}

void UI::LayoutDownloadsOverlay()
{
  if (!downloads_overlay_)
    return;

  int overlay_top = ui_height_ + kDownloadsOverlaySpacing;
  if (overlay_top < 0)
    overlay_top = 0;

  uint32_t overlay_width = window_->width();
  uint32_t overlay_height = window_->height();
  if (overlay_height > static_cast<uint32_t>(overlay_top))
    overlay_height -= static_cast<uint32_t>(overlay_top);
  else
    overlay_height = 1;

  downloads_overlay_->MoveTo(0, overlay_top);
  downloads_overlay_->Resize(overlay_width, overlay_height);

  if (auto view = downloads_overlay_->view())
    view->Resize(overlay_width, overlay_height);
}

void UI::OnMenuOpen(const JSObject &obj, const JSArgs &args)
{
  ShowMenuOverlay();
}

void UI::OnMenuClose(const JSObject &obj, const JSArgs &args)
{
  HideMenuOverlay();
}

void UI::OnToggleDarkMode(const JSObject &obj, const JSArgs &args)
{
  HandleSettingMutation("launch_dark_theme", !dark_mode_enabled_);
}

void UI::OnOpenSettingsPanel(const JSObject &, const JSArgs &)
{
  HideMenuOverlay();
  CreateNewTabForChildView(String("file:///settings.html"));
}

void UI::OnCloseSettingsPanel(const JSObject &, const JSArgs &)
{
  // Legacy no-op: settings now open in a dedicated tab.
}

void UI::OnReloadChromeUI(const JSObject &, const JSArgs &)
{
  ReloadChromeUI();
}

void UI::OnReloadActiveNonSettingsTab(const JSObject &, const JSArgs &)
{
  ReloadActiveNonSettingsTab();
}

void UI::ReloadActiveNonSettingsTab()
{
  if (VerboseLogging())
  std::fprintf(stderr, "[UI] ReloadActiveNonSettingsTab invoked: active_tab_id=%llu last_non_settings_tab_id=%llu\n", (unsigned long long)active_tab_id_, (unsigned long long)last_non_settings_tab_id_);
  // Prefer reloading the active browsing tab if it is NOT the settings page.
  if (active_tab() && active_tab()->view())
  {
    auto v = active_tab()->view();
    auto url = v->url().utf8();
    const char *u = url.data() ? url.data() : "";
    if (std::strstr(u, "settings.html") == nullptr)
    {
      if (VerboseLogging())
      std::fprintf(stderr, "[UI] Reloading active tab id=%llu url=%s\n", (unsigned long long)active_tab_id_, u);
      v->Reload();
      return;
    }
  }

  // Otherwise, prefer the most-recently active non-settings tab and recreate it.
  if (last_non_settings_tab_id_ != 0)
  {
    auto it = tabs_.find(last_non_settings_tab_id_);
    if (it != tabs_.end() && it->second && it->second->view())
    {
      auto v = it->second->view();
      auto url = v->url().utf8();
      const char *u = url.data() ? url.data() : "";
      if (std::strstr(u, "settings.html") == nullptr)
      {
        std::string urlstr = u;
        RefPtr<View> newView = CreateNewTabForChildView(String(urlstr.c_str()));
        if (newView)
        {
          if (VerboseLogging())
          std::fprintf(stderr, "[UI] Recreated tab for last_non_settings_tab_id=%llu, new view created\n", (unsigned long long)last_non_settings_tab_id_);
          newView->LoadURL(String(urlstr.c_str()));
          uint64_t new_id = 0;
          for (auto &e : tabs_)
          {
            if (e.second && e.second->view() == newView)
            {
              new_id = e.first;
              break;
            }
          }
          uint64_t old_id = last_non_settings_tab_id_;
          bool was_active = (old_id == active_tab_id_);
          if (was_active && new_id != 0)
          {
            if (tabs_.count(old_id) && tabs_[old_id])
              tabs_[old_id]->Hide();
            active_tab_id_ = new_id;
            if (tabs_.count(active_tab_id_) && tabs_[active_tab_id_])
            {
              tabs_[active_tab_id_]->Show();
              auto tab_view = tabs_[active_tab_id_]->view();
              SetLoading(tab_view->is_loading());
              SetCanGoBack(tab_view->CanGoBack());
              SetCanGoForward(tab_view->CanGoBack());
              SetURL(tab_view->url());
            }
          }
          if (tabs_.count(old_id))
          {
            if (VerboseLogging())
            std::fprintf(stderr, "[UI] Closing old tab id=%llu (replaced by id=%llu)\n", (unsigned long long)old_id, (unsigned long long)new_id);
            tabs_[old_id].reset();
            tabs_.erase(old_id);
            RefPtr<JSContext> lock(view()->LockJSContext());
            closeTab({old_id});
          }
        }
        return;
      }
    }
  }

  for (auto &entry : tabs_)
  {
    if (!entry.second)
      continue;
    auto v = entry.second->view();
    if (!v)
      continue;
    auto url = v->url().utf8();
    const char *u = url.data() ? url.data() : "";
    if (std::strstr(u, "settings.html") == nullptr)
    {
      std::string urlstr = u;
      RefPtr<View> newView = CreateNewTabForChildView(String(urlstr.c_str()));
      if (newView)
      {
        if (VerboseLogging())
        std::fprintf(stderr, "[UI] Recreated fallback tab id=%llu new view created\n", (unsigned long long)entry.first);
        newView->LoadURL(String(urlstr.c_str()));
        uint64_t new_id = 0;
        uint64_t old_id = entry.first;
        for (auto &e : tabs_)
        {
          if (e.second && e.second->view() == newView)
          {
            new_id = e.first;
            break;
          }
        }
        bool was_active = (old_id == active_tab_id_);
        if (was_active && new_id != 0)
        {
          if (tabs_.count(old_id) && tabs_[old_id])
            tabs_[old_id]->Hide();
          active_tab_id_ = new_id;
          if (tabs_.count(active_tab_id_) && tabs_[active_tab_id_])
          {
            tabs_[active_tab_id_]->Show();
            auto tab_view = tabs_[active_tab_id_]->view();
            SetLoading(tab_view->is_loading());
            SetCanGoBack(tab_view->CanGoBack());
            SetCanGoForward(tab_view->CanGoBack());
            SetURL(tab_view->url());
          }
        }
        if (tabs_.count(old_id))
        {
          if (VerboseLogging())
          std::fprintf(stderr, "[UI] Closing old tab id=%llu (replaced by id=%llu)\n", (unsigned long long)old_id, (unsigned long long)new_id);
          tabs_[old_id].reset();
          tabs_.erase(old_id);
          RefPtr<JSContext> lock(view()->LockJSContext());
          closeTab({old_id});
        }
      }
      return;
    }
  }
}

ultralight::JSValue UI::OnGetSettings(const JSObject &, const JSArgs &)
{
  // Build a fresh snapshot of current settings state
  std::string payload = BuildSettingsPayload(false);

  // Payload returned to settings UI

  // Convert std::string to ultralight::String for proper JSValue conversion
  ultralight::String ul_payload(payload.c_str());
  return ultralight::JSValue(ul_payload);
}

ultralight::JSValue UI::OnGetDarkModeEnabled(const JSObject &, const JSArgs &)
{
  return ultralight::JSValue(dark_mode_enabled_ ? 1.0 : 0.0);
}

// ---------------------------------------------------------------------------
// Native theme persistence
//
// assets/themes/theme.js prefers these bridges and falls back to localStorage
// when they are absent. They were never bound, so the theme engine ran entirely
// on localStorage: the selection was scoped to the file:// origin of whichever
// internal page saved it, was invisible to the settings system, and was lost
// whenever site data was cleared. Binding them puts the active theme and any
// custom themes next to the rest of the browser's persistent state.
// ---------------------------------------------------------------------------

themes::ThemeManager *UI::theme_store()
{
  if (!theme_manager_)
  {
    theme_manager_ = std::make_unique<themes::ThemeManager>(SettingsDirectory());
    theme_manager_->Initialize();
  }
  return theme_manager_.get();
}

// ---------------------------------------------------------------------------
// Native theme persistence
//
// assets/themes/theme.js owns the theme engine. These bridges give it disk
// access with validation, delegated to themes::ThemeManager. They are bound on
// every UI-owned view because nine separate pages load theme.js and each one
// checks for the globals independently; binding them only on the chrome overlay
// would leave every internal page on the localStorage fallback and the theme
// would differ between pages.
// ---------------------------------------------------------------------------

ultralight::JSValue UI::OnNativeGetThemeSetting(const JSObject &, const JSArgs &args)
{
  if (args.size() >= 1 && args[0].IsString())
  {
    auto key = JSStringToUtf8(args[0].ToString());
    const std::string key_str = key.data() ? key.data() : "";

    if (key_str == "theme")
      return ultralight::JSValue(String(theme_store()->GetActiveThemeId().c_str()));
  }

  return ultralight::JSValue(String(""));
}

void UI::OnNativeSetThemeSetting(const JSObject &, const JSArgs &args)
{
  if (args.size() < 2 || !args[0].IsString() || !args[1].IsString())
    return;

  auto key = JSStringToUtf8(args[0].ToString());
  const std::string key_str = key.data() ? key.data() : "";
  if (key_str != "theme")
    return;

  auto value = JSStringToUtf8(args[1].ToString());
  const std::string theme_id = value.data() ? value.data() : "";

  // The manager logs why a value was refused. It also keeps a localStorage copy
  // via the engine, so a refused write is not a lost selection.
  if (!theme_store()->SetActiveThemeId(theme_id))
    return;

  // Only push when the manager actually accepted the id, so a refused write does
  // not leave views disagreeing with the persisted theme.
  BroadcastThemeToViews(theme_store()->GetActiveThemeId());
}

void UI::BroadcastThemeToViews(const std::string &theme_id)
{
  if (theme_id.empty())
    return;

  // The id is a validated theme key (alphanumeric plus '_' and '-'), so it needs
  // no escaping here. The guard is deliberate: never build a script from an
  // unvalidated string.
  const std::string script =
      "if (window.__ulApplyTheme) window.__ulApplyTheme(\"" + theme_id + "\");";

  // theme.js only defines the hook once its IIFE has run, so a view that is
  // mid-load simply misses this push and picks the theme up from storage when it
  // initializes. That is why no error handling is needed here.
  auto push = [&script](const RefPtr<View> &v) {
    if (v)
      v->EvaluateScript(script.c_str(), nullptr);
  };

  push(view());

  for (const auto &entry : tabs_)
    if (entry.second)
      push(entry.second->view());

  // DRM tabs are native WebView2 surfaces rather than Ultralight views, so they
  // have no script context to push into and are deliberately skipped.

  push(menu_overlay_ ? menu_overlay_->view() : RefPtr<View>());
  push(context_menu_overlay_ ? context_menu_overlay_->view() : RefPtr<View>());
  push(suggestions_overlay_ ? suggestions_overlay_->view() : RefPtr<View>());
  push(downloads_overlay_ ? downloads_overlay_->view() : RefPtr<View>());
}

ultralight::JSValue UI::OnNativeGetCustomThemes(const JSObject &, const JSArgs &)
{
  return ultralight::JSValue(String(theme_store()->GetCustomThemesJSON().c_str()));
}

void UI::OnNativeSaveCustomThemes(const JSObject &, const JSArgs &args)
{
  if (args.size() < 1 || !args[0].IsString())
    return;

  auto json = JSStringToUtf8(args[0].ToString());
  theme_store()->SaveCustomThemesJSON(json.data() ? json.data() : "");
}

ultralight::JSValue UI::OnNativeGetThemeOverrides(const JSObject &, const JSArgs &)
{
  return ultralight::JSValue(String(theme_store()->GetOverridesJSON().c_str()));
}

void UI::OnNativeSaveThemeOverrides(const JSObject &, const JSArgs &args)
{
  if (args.size() < 1 || !args[0].IsString())
    return;

  auto json = JSStringToUtf8(args[0].ToString());
  theme_store()->SaveOverridesJSON(json.data() ? json.data() : "");
}

ultralight::JSValue UI::OnNativeGetSeedThemes(const JSObject &, const JSArgs &)
{
  return ultralight::JSValue(String(theme_store()->GetSeedThemesJSON("assets").c_str()));
}
void UI::OnToggleAdblock(const JSObject &, const JSArgs &)
{
  HandleSettingMutation("enable_adblock", !settings_.enable_adblock);
}

ultralight::JSValue UI::OnGetAdblockEnabled(const JSObject &, const JSArgs &)
{
  return ultralight::JSValue(settings_.enable_adblock ? 1.0 : 0.0);
}

void UI::OnUpdateSetting(const JSObject &, const JSArgs &args)
{
  if (args.size() < 2 || !args[0].IsString())
    return;

  ultralight::String key_ul = args[0].ToString();
  auto key_str = key_ul.utf8();
  std::string key = key_str.data() ? key_str.data() : "";
  if (key.empty())
    return;

  // Special-case: target_user_agent is a string value that maps to
  // settings_.custom_user_agent. It is always accepted, even when
  // use_custom_user_agent is currently false, so the user can prefill
  // a custom UA before turning the toggle on.
  if (key == "target_user_agent")
  {
    if (!args[1].IsString())
      return;
    ultralight::String ua_ul = args[1].ToString();
    auto ua_str = ua_ul.utf8();
    std::string ua = ua_str.data() ? ua_str.data() : "";
    settings_.custom_user_agent = ua;
    UpdateSettingsDirtyFlag();
    ApplySettings(false, false);
    UpdateSettingsDirtyFlag();
    return;
  }

  // Special-case: dark_theme_excluded_sites is a string value containing
  // newline-separated URL patterns for sites where dark theme should be disabled.
  if (key == "dark_theme_excluded_sites")
  {
    if (!args[1].IsString())
      return;
    ultralight::String sites_ul = args[1].ToString();
    auto sites_str = sites_ul.utf8();
    std::string sites = sites_str.data() ? sites_str.data() : "";
    settings_.dark_theme_excluded_sites = sites;
    UpdateSettingsDirtyFlag();
    ApplySettings(false, false);
    UpdateSettingsDirtyFlag();
    return;
  }

  // Special-case: spoofed_latitude is a numeric value for location spoofing
  if (key == "spoofed_latitude")
  {
    double val = 0.0;
    if (args[1].IsNumber())
    {
      val = args[1].ToNumber();
    }
    else if (args[1].IsString())
    {
      ultralight::String str_ul = args[1].ToString();
      auto str_data = str_ul.utf8();
      std::string str = str_data.data() ? str_data.data() : "";
      try
      {
        val = std::stod(str);
      }
      catch (...)
      {
        val = 0.0;
      }
    }
    // Clamp to valid latitude range
    val = (std::max)(-90.0, (std::min)(90.0, val));
    settings_.spoofed_latitude = val;
    UpdateSettingsDirtyFlag();
    ApplySettings(false, false);
    UpdateSettingsDirtyFlag();
    return;
  }

  // Special-case: spoofed_longitude is a numeric value for location spoofing
  if (key == "spoofed_longitude")
  {
    double val = 0.0;
    if (args[1].IsNumber())
    {
      val = args[1].ToNumber();
    }
    else if (args[1].IsString())
    {
      ultralight::String str_ul = args[1].ToString();
      auto str_data = str_ul.utf8();
      std::string str = str_data.data() ? str_data.data() : "";
      try
      {
        val = std::stod(str);
      }
      catch (...)
      {
        val = 0.0;
      }
    }
    // Clamp to valid longitude range
    val = (std::max)(-180.0, (std::min)(180.0, val));
    settings_.spoofed_longitude = val;
    UpdateSettingsDirtyFlag();
    ApplySettings(false, false);
    UpdateSettingsDirtyFlag();
    return;
  }

  // Integer settings declared in the catalog (currently memory_budget_mb).
  // The descriptor owns the bounds, so the value is clamped in one place.
  if (const RuntimeSettingDescriptor *numeric = FindSettingDescriptor(key))
  {
    if (!numeric->is_number || !numeric->number_member)
    {
      // Not numeric - fall through to the boolean path below.
    }
    else
    {
      long long requested = numeric->number_default;
      if (args[1].IsNumber())
      {
        requested = static_cast<long long>(args[1].ToNumber());
      }
      else if (args[1].IsString())
      {
        ultralight::String str_ul = args[1].ToString();
        auto str_data = str_ul.utf8();
        std::string str = str_data.data() ? str_data.data() : "";
        try
        {
          size_t consumed = 0;
          requested = std::stoll(str, &consumed);

          // Reject trailing garbage. Without this check stoll silently accepts
          // "512abc" and "512.5" as 512, so a partially-typed value could be
          // saved from the bridge even though the settings loader rejects it.
          // Surrounding whitespace is tolerated, matching strtol semantics.
          while (consumed < str.size() && std::isspace(static_cast<unsigned char>(str[consumed])))
          {
            ++consumed;
          }
          if (consumed != str.size())
          {
            return;
          }
        }
        catch (const std::exception &)
        {
          // Ignore unparseable input and keep the previous value.
          return;
        }
      }
      else
      {
        return;
      }

      const int clamped = static_cast<int>((std::max)(
          static_cast<long long>(numeric->min_value),
          (std::min)(static_cast<long long>(numeric->max_value), requested)));

      int &field = settings_.*(numeric->number_member);
      if (field == clamped)
        return;

      field = clamped;
      UpdateSettingsDirtyFlag();
      ApplySettings(false, false);
      UpdateSettingsDirtyFlag();
      return;
    }
  }

  bool value = false;
  if (args[1].IsBoolean())
  {
    value = args[1].ToBoolean();
  }
  else if (args[1].IsNumber())
  {
    value = args[1].ToInteger() != 0;
  }
  else if (args[1].IsString())
  {
    ultralight::String val_ul = args[1].ToString();
    auto val_str = val_ul.utf8();
    std::string text = val_str.data() ? val_str.data() : "";
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    value = (text == "true" || text == "1" || text == "on" || text == "yes");
  }
  else
  {
    return;
  }

  HandleSettingMutation(key, value);
}

ultralight::JSValue UI::OnRestoreSettingsDefaults(const JSObject &, const JSArgs &)
{
  RestoreSettingsToDefaults();
  UpdateSettingsDirtyFlag();
  ApplySettings(false, false);
  UpdateSettingsDirtyFlag();

  std::string payload = BuildSettingsPayload(false);
  ultralight::String ul_payload(payload.c_str());
  return ultralight::JSValue(ul_payload);
}

void UI::OnSaveSettings(const JSObject &, const JSArgs &)
{
  bool saved = SaveSettingsToDisk();
  SyncSettingsStateToUI(saved);
  updateAdblockEnabled({adblock_enabled_cached_ ? 1.0 : 0.0});
}

ultralight::JSValue UI::OnGetDrmStatus(const JSObject &, const JSArgs &)
{
  std::string payload = BuildDrmStatusPayload();
  return ultralight::JSValue(String(payload.c_str()));
}

ultralight::JSValue UI::OnInstallDrmDependencies(const JSObject &, const JSArgs &)
{
  EnsureDrmManager();
  auto *dependency_manager = drm_manager_ ? drm_manager_->dependency_manager() : nullptr;
  if (!dependency_manager)
  {
    AppendDrmLog("No DRM dependency manager is available for this platform.");
    return ultralight::JSValue(0.0);
  }
  if (drm_install_running_)
  {
    AppendDrmLog("An installation is already in progress.");
    return ultralight::JSValue(0.0);
  }

  drm_install_running_ = true;
  AppendDrmLog("Starting installation for " + dependency_manager->GetName() + "...");
  auto sink = [this](const std::string &line)
  {
    AppendDrmLog(line);
  };
  bool success = dependency_manager->Install(sink);
  drm_install_running_ = false;
  drm_last_install_result_ = success;
  if (success)
    AppendDrmLog("Installation completed successfully.");
  else
    AppendDrmLog("Installation failed. Review the log above for details.");
  drm_dependencies_installed_cached_ = dependency_manager->IsInstalled();
  return ultralight::JSValue(success ? 1.0 : 0.0);
}

std::string UI::BuildDrmStatusPayload()
{
  EnsureDrmManager();
  auto *dependency_manager = drm_manager_ ? drm_manager_->dependency_manager() : nullptr;
  std::string dependency_name = dependency_manager ? dependency_manager->GetName() : "Unavailable";
  bool installed = dependency_manager ? dependency_manager->IsInstalled() : false;
  drm_dependencies_installed_cached_ = installed;

  if (drm_log_lines_.empty())
  {
    AppendDrmLog("DRM subsystem ready. Dependency: " + dependency_name + ".");
  }

  std::ostringstream ss;
  ss << "{";
  ss << "\"enabled\": " << (settings_.enable_drm_webview ? "true" : "false") << ",";
  ss << "\"dependency_name\": \"" << util::EscapeJsonString(dependency_name) << "\",";
  ss << "\"installed\": " << (installed ? "true" : "false") << ",";
  ss << "\"installing\": " << (drm_install_running_ ? "true" : "false") << ",";
  if (drm_last_install_result_.has_value())
    ss << "\"last_install_success\": " << (*drm_last_install_result_ ? "true" : "false") << ",";
  else
    ss << "\"last_install_success\": null,";
  std::string settings_path = drm_settings_.storage_path().empty() ? (SettingsDirectory() / "drm_settings.json").string() : drm_settings_.storage_path().string();
  ss << "\"settings_file\": \"" << util::EscapeJsonString(settings_path) << "\",";
  ss << "\"site_rules\": [";
  bool first = true;
  for (const auto &entry : drm_settings_.site_rules())
  {
    if (!first)
      ss << ",";
    ss << "{\"host\": \"" << util::EscapeJsonString(entry.first) << "\",";
    ss << "\"force\": " << (entry.second.force ? "true" : "false") << "}";
    first = false;
  }
  ss << "],";
  ss << "\"log\": [";
  first = true;
  for (const auto &line : drm_log_lines_)
  {
    if (!first)
      ss << ",";
    ss << "\"" << util::EscapeJsonString(line) << "\"";
    first = false;
  }
  ss << "]";
  ss << "}";
  return ss.str();
}

void UI::AppendDrmLog(const std::string &line)
{
  constexpr size_t kMaxDrmLogLines = 200;
  std::string timestamp = util::ToIso8601UTC(std::chrono::system_clock::now());
  drm_log_lines_.emplace_back(timestamp + "  " + line);
  if (drm_log_lines_.size() > kMaxDrmLogLines)
    drm_log_lines_.pop_front();
}

void UI::SyncSettingsStateToUI(bool snapshot_is_baseline)
{
  UpdateSettingsDirtyFlag();
  std::string json = BuildSettingsJSON();
  ultralight::String json_str(json.c_str());

  if (applySettings)
  {
    RefPtr<JSContext> lock(view()->LockJSContext());
    applySettings({json_str});
  }

  if (applySettingsPanel)
  {
    RefPtr<View> settings_view;
    for (auto &entry : tabs_)
    {
      if (!entry.second)
        continue;
      auto v = entry.second->view();
      if (!v)
        continue;
      auto url = v->url().utf8();
      if (url.data() && std::strstr(url.data(), "settings.html"))
      {
        settings_view = v;
        break;
      }
    }

    if (settings_view)
    {
      std::string payload = BuildSettingsPayload(snapshot_is_baseline);
      RefPtr<JSContext> lock(settings_view->LockJSContext());
      applySettingsPanel({String(payload.c_str())});
    }
    else
    {
      applySettingsPanel = JSFunction();
    }
  }
}

void UI::ApplySettings(bool initial, bool snapshot_is_baseline)
{
  // Appearance
  SetDarkModeEnabled(settings_.launch_dark_theme);

  // Vibrant window theme - changes title bar color
  bool was_vibrant = vibrant_window_theme_enabled_;
  vibrant_window_theme_enabled_ = settings_.vibrant_window_theme;
  if (was_vibrant != vibrant_window_theme_enabled_ || initial)
  {
    ApplyVibrantWindowTheme(vibrant_window_theme_enabled_);
  }

  // Transparent toolbar - applies CSS to UI overlay
  bool was_transparent = experimental_transparent_toolbar_enabled_;
  experimental_transparent_toolbar_enabled_ = settings_.experimental_transparent_toolbar;
  if (was_transparent != experimental_transparent_toolbar_enabled_ || initial)
  {
    ApplyTransparentToolbar(experimental_transparent_toolbar_enabled_);
  }

  // Handle compact tabs mode - adjust UI height and trigger resize
  bool was_compact = experimental_compact_tabs_enabled_;
  experimental_compact_tabs_enabled_ = settings_.experimental_compact_tabs;

  if (was_compact != experimental_compact_tabs_enabled_)
  {
    // Calculate new UI height based on compact mode
    double scale = window_ ? window_->scale() : 1.0;
    uint32_t target_height = experimental_compact_tabs_enabled_ ? (uint32_t)std::round(UI_HEIGHT_COMPACT * scale) : (uint32_t)std::round(UI_HEIGHT * scale);

    if (ui_height_ != target_height)
    {
      ui_height_ = target_height;
      // Trigger resize to reposition all tabs
      if (window_)
        OnResize(window_.get(), window_->width(), window_->height());
    }
  }

  // Privacy & Security
  if (adblock_)
  {
    adblock_->set_enabled(settings_.enable_adblock);
    adblock_->set_log_blocked(settings_.log_blocked_requests);
  }
  if (trackerblock_)
  {
    trackerblock_->set_enabled(settings_.enable_adblock);
    trackerblock_->set_log_blocked(settings_.log_blocked_requests);
  }
  adblock_enabled_cached_ = settings_.enable_adblock;
  clear_history_on_exit_ = settings_.clear_history_on_exit;

  // Low-RAM mode: keep the monitor's budget in sync with the settings so a
  // change takes effect immediately rather than at the next launch. Setting the
  // budget resets the pressure latch, so toggling it never leaves the browser
  // stuck reporting memory pressure.
  if (memory_monitor_)
  {
    memory_monitor_->SetLowRamModeEnabled(settings_.low_ram_mode);
    memory_monitor_->SetBudgetBytes(
        static_cast<uint64_t>(settings_.memory_budget_mb) * 1024ull * 1024ull);
  }

  // Note: enable_javascript and hardware_acceleration are applied to NEW tabs via TabViewSettings.
  // Existing tabs keep their original settings since ViewConfig is immutable after creation.
  //
  // Privacy settings implementation:
  // - do_not_track: Implemented via JavaScript injection (sets navigator.doNotTrack = '1')
  // - block_third_party_cookies: Implemented via JavaScript injection (blocks cross-origin cookie access)
  // - enable_web_security: Not directly supported by Ultralight ViewConfig. XHR/Fetch credentials
  //   are handled via the existing polyfills. Full CORS enforcement would require Ultralight API changes.

  // Address Bar & Suggestions
  suggestions_enabled_ = settings_.enable_suggestions;
  suggestion_favicons_enabled_ = settings_.enable_suggestion_favicons;
  if (!suggestions_enabled_)
    HideSuggestionsOverlay();

  // Downloads
  show_download_badge_ = settings_.show_download_badge;
  auto_open_download_panel_ = settings_.auto_open_download_panel;
  // ask_download_location would be checked when download starts

  // Media fallback. The host list is loaded once at startup; only the toggle is
  // re-applied here, so toggling the setting does not re-read the asset.
  media_fallback_enabled_ = settings_.media_fallback_enabled;
  if (!media_fallback_.loaded())
    media_fallback_.Load();

  // Performance
  // enable_javascript and hardware_acceleration are applied during Tab creation (see CreateNewTab)
  smooth_scrolling_enabled_ = settings_.smooth_scrolling;

  // Accessibility flags must be resolved before the smooth-scrolling decision
  // below, because reduce-motion overrides the smooth-scrolling toggle.
  reduce_motion_enabled_ = settings_.reduce_motion;
  high_contrast_ui_enabled_ = settings_.high_contrast_ui;

// Accessibility applies to every view, but smooth scrolling only to web
  // content -- see SyncScrollModeForView. Chrome does not animate its own UI
  // surfaces, and Settings is a long scrolling form where an animated scroller
  // reads as sluggish.
  auto apply_scroll_and_accessibility = [&](RefPtr<View> v)
  {
    if (!v)
      return;
    SyncScrollModeForView(v);
    if (reduce_motion_enabled_)
      ApplyReduceMotionToView(v);
    else
      RemoveReduceMotionFromView(v);
    if (high_contrast_ui_enabled_)
      ApplyHighContrastToView(v);
    else
      RemoveHighContrastFromView(v);
  };

  apply_scroll_and_accessibility(view());
  for (auto &entry : tabs_)
  {
    if (entry.second)
      apply_scroll_and_accessibility(entry.second->view());
  }
  // enable_caret_browsing would require page-level script injection

  // Developer
  // enable_remote_inspector, show_performance_overlay
  // These would require additional implementation

  // Networking / User Agent
  // Compute the active user agent string whenever settings change.
  if (settings_.use_custom_user_agent)
  {
    // If user toggles custom UA on but custom_user_agent is empty, fall back to default.
    if (settings_.custom_user_agent.empty())
    {
      active_user_agent_ = BuildDefaultChromiumUserAgent();
    }
    else
    {
      active_user_agent_ = settings_.custom_user_agent;
    }
  }
  else
  {
    active_user_agent_ = BuildDefaultChromiumUserAgent();
  }

  SyncAdblockStateToUI();
  UpdateSettingsDirtyFlag();
  SyncSettingsStateToUI(snapshot_is_baseline);
}

std::string UI::BuildDefaultChromiumUserAgent() const
{
  // Best-effort modern Chromium UA approximation. We avoid querying the
  // actual OS version in detail to keep this simple and deterministic.
#if defined(_WIN64) || defined(_WIN32)
  const char *platform = "Windows NT 10.0; Win64; x64";
#elif defined(__APPLE__)
  const char *platform = "Macintosh; Intel Mac OS X 10_15_7";
#else
  const char *platform = "X11; Linux x86_64";
#endif

  // Pretend to be the latest stable Chromium build; this string should
  // be bumped periodically as Chromium versions advance.
  // Note: Using Chrome 142 which is the latest version.
  std::string ua = "Mozilla/5.0 (";
  ua += platform;
  ua += ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36";
  return ua;
}

void UI::SetDarkModeEnabled(bool enabled)
{
  if (dark_mode_enabled_ == enabled)
  {
    settings_.launch_dark_theme = enabled;
    return;
  }

  dark_mode_enabled_ = enabled;
  settings_.launch_dark_theme = enabled;

  auto apply_to = [&](RefPtr<View> v)
  {
    if (!v)
      return;
    if (enabled)
      ApplyDarkModeToView(v);
    else
      RemoveDarkModeFromView(v);
  };

  apply_to(view());
  for (auto &entry : tabs_)
  {
    if (entry.second)
      apply_to(entry.second->view());
  }
  if (menu_overlay_)
    apply_to(menu_overlay_->view());
  if (downloads_overlay_)
    apply_to(downloads_overlay_->view());
  if (context_menu_overlay_)
    apply_to(context_menu_overlay_->view());
  if (suggestions_overlay_)
    apply_to(suggestions_overlay_->view());
}

void UI::EnsureDataDirectoryExists()
{
  SettingsManager::EnsureDataDirectoryExists();
}

void UI::RestoreSettingsToDefaults()
{
  SettingsManager::RestoreSettingsToDefaults(*this);
}

bool UI::ParseSettingsBool(const std::string &buffer, const char *key, bool fallback) const
{
  if (!key)
    return fallback;
  std::string needle = std::string("\"") + key + "\"";
  size_t pos = buffer.find(needle);
  if (pos == std::string::npos)
    return fallback;
  pos = buffer.find(':', pos);
  if (pos == std::string::npos)
    return fallback;
  ++pos;
  while (pos < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[pos])))
    ++pos;
  if (pos >= buffer.size())
    return fallback;
  if (buffer.compare(pos, 4, "true") == 0)
    return true;
  if (buffer.compare(pos, 5, "false") == 0)
    return false;
  if (buffer[pos] == '1')
    return true;
  if (buffer[pos] == '0')
    return false;
  return fallback;
}

void UI::LoadSettingsFromDisk()
{
  SettingsManager::LoadSettingsFromDisk(*this);
}

bool UI::SaveSettingsToDisk()
{
  return SettingsManager::SaveSettingsToDisk(*this);
}

void UI::UpdateSettingsDirtyFlag()
{
  settings_dirty_ = (settings_ != saved_settings_);
}

std::string UI::BuildSettingsPayload(bool snapshot_is_baseline) const
{
  const auto &catalog = GetSettingsCatalog();
  std::ostringstream ss;
  ss << "{";
  // Base boolean settings map.
  ss << "\"values\": " << BuildSettingsJSON() << ",";
  // Expose the effective user agent string as a separate field so the
  // Settings page can always display the UA that will actually be used.
  // Also expose the raw custom_user_agent for the input field when use_custom_user_agent is enabled.
  ss << "\"target_user_agent\": \"" << util::EscapeJsonString(settings_.custom_user_agent.empty() ? active_user_agent_ : settings_.custom_user_agent) << "\",";
  // Expose dark_theme_excluded_sites as a separate field for the text input in settings UI
  ss << "\"dark_theme_excluded_sites\": \"" << util::EscapeJsonString(settings_.dark_theme_excluded_sites) << "\",";
  // Expose location spoofing coordinates
  ss << "\"spoofed_latitude\": " << settings_.spoofed_latitude << ",";
  ss << "\"spoofed_longitude\": " << settings_.spoofed_longitude << ",";
  ss << "\"meta\": {";
  ss << "\"updated_at\": \"" << util::ToIso8601UTC(std::chrono::system_clock::now()) << "\",";
  ss << "\"dirty\": " << (settings_dirty_ ? "true" : "false") << ",";
  ss << "\"storage_path\": \"" << util::EscapeJsonString(settings_storage_path_) << "\",";
  ss << "\"settings\": [";
  bool first = true;
  for (const auto &desc : catalog)
  {
    // A descriptor must map to exactly one concrete field; skip any that do not.
    const bool is_bool = !desc.is_number && desc.member != nullptr;
    const bool is_int = desc.is_number && desc.number_member != nullptr;
    if (!is_bool && !is_int)
      continue;
    if (!first)
      ss << ",";
    ss << "{";
    ss << "\"key\":\"" << util::EscapeJsonString(desc.key) << "\",";
    ss << "\"name\":\"" << util::EscapeJsonString(desc.name) << "\",";
    ss << "\"description\":\"" << util::EscapeJsonString(desc.description) << "\",";
    ss << "\"category\":\"" << util::EscapeJsonString(desc.category) << "\",";
    if (is_int)
    {
      const int current = settings_.*(desc.number_member);
      const int saved = saved_settings_.*(desc.number_member);
      ss << "\"type\": \"number\",";
      ss << "\"value\": " << current << ",";
      ss << "\"default\": " << desc.number_default << ",";
      ss << "\"saved\": " << saved << ",";
      ss << "\"min\": " << desc.min_value << ",";
      ss << "\"max\": " << desc.max_value << ",";
      ss << "\"step\": " << desc.step;
    }
    else
    {
      const bool current = settings_.*(desc.member);
      const bool saved = saved_settings_.*(desc.member);
      ss << "\"type\": \"bool\",";
      ss << "\"value\": " << (current ? "true" : "false") << ",";
      ss << "\"default\": " << (desc.default_value ? "true" : "false") << ",";
      ss << "\"saved\": " << (saved ? "true" : "false");
    }
    if (!desc.note.empty())
      ss << ",\"note\":\"" << util::EscapeJsonString(desc.note) << "\"";
    ss << ",\"reload_page\": " << (desc.reload_page ? "true" : "false");
    ss << "}";
    first = false;
  }
  ss << "]";
  ss << "}";
  ss << "}";
  return ss.str();
}

void UI::HandleSettingMutation(const std::string &key, bool value)
{
  const auto *descriptor = FindSettingDescriptor(key);
  if (!descriptor || !descriptor->member)
    return;

  bool &field = settings_.*(descriptor->member);
  bool old_value = field;
  if (VerboseLogging())
  std::fprintf(stderr, "[UI] HandleSettingMutation invoked: key='%s' old=%s new=%s\n", key.c_str(), (old_value ? "true" : "false"), (value ? "true" : "false"));
  if (field == value)
    return;

  field = value;
  UpdateSettingsDirtyFlag();
  ApplySettings(false, false);
  UpdateSettingsDirtyFlag();

  if (key == "enable_drm_webview")
  {
    drm_settings_.SetEnabled(value);
    drm_settings_.Save();
  }

  // If compact tabs changed, ensure the chrome UI and browsing tab update
  // immediately regardless of whether the settings page's JS requested it.
  if (key == "experimental_compact_tabs")
  {
    if (VerboseLogging())
    std::fprintf(stderr, "[UI] experimental_compact_tabs changed -> ReloadChromeUI + ReloadActiveNonSettingsTab\n");
    ReloadChromeUI();
    ReloadActiveNonSettingsTab();
  }

  if (key == "clear_history_on_exit")
  {
    if (value)
      std::remove(DataFilePath("history.json").string().c_str());
    else
      SaveHistoryToDisk();
  }
}

void UI::AdjustUIHeight(uint32_t new_height)
{
  if (new_height == (uint32_t)ui_height_)
    return;

  ui_height_ = (int)new_height;

  // Resize top UI overlay
  overlay_->Resize(window_->width(), ui_height_);

  // The content area starts below the chrome, so every tab has to move down
  // (or up) with it. Leaving them at the old y left a band of stale page
  // content sitting under the toolbar after a compact-tabs toggle.
  //
  // This uses Tab::Reposition rather than MoveTo because Reposition is
  // position-only. MoveTo re-ran Tab::Resize, which reflowed the page and reset
  // scroll; that is why this was previously left as a no-op.
  for (auto &entry : tabs_)
  {
    if (entry.second)
      entry.second->Reposition(0, (uint32_t)ui_height_);
  }
  // DRM tabs are native child windows rather than Overlays, and they already
  // take an explicit offset, so they are moved here for the same reason.
  int drm_height = (int)window_->height() - ui_height_;
  if (drm_height < 1)
    drm_height = 1;
  for (auto &entry : drm_tabs_)
  {
    if (entry.second)
      entry.second->Resize(window_->width(), (uint32_t)drm_height, 0, (uint32_t)ui_height_);
  }

  if (downloads_overlay_)
    LayoutDownloadsOverlay();
}

ultralight::JSValue UI::OnGetMemorySnapshot(const JSObject &obj, const JSArgs &args)
{
  return JSValue(String(PollMemory().ToJSON().c_str()));
}

void UI::EnsureMemoryMonitor()
{
  if (memory_monitor_)
    return;

  // Constructed lazily so the accessors stay safe even if called during
  // teardown. Both lazy paths funnel through here so the monitor can never end
  // up with a different configuration depending on which one ran first.
  memory_monitor_ = std::make_unique<memory::MemoryMonitor>();
  memory_monitor_->SetLowRamModeEnabled(settings_.low_ram_mode);
  memory_monitor_->SetBudgetBytes(
      static_cast<uint64_t>(settings_.memory_budget_mb) * 1024ull * 1024ull);
}

memory::MemorySnapshot UI::GetMemorySnapshot()
{
  EnsureMemoryMonitor();
  return memory_monitor_->Snapshot();
}

memory::MemorySnapshot UI::PollMemory()
{
  EnsureMemoryMonitor();

  // Keep the reported tab load accurate. tabs_ and drm_tabs_ are the real
  // containers; the monitor's running counter can drift across session restore.
  const int open_tabs = static_cast<int>(tabs_.size() + drm_tabs_.size());
  memory_monitor_->SetTabCounts(open_tabs, open_tabs);

  return memory_monitor_->Poll();
}

void UI::StartMemoryWatchdog()
{
  if (memory_watchdog_.joinable())
    return;

  memory_watchdog_stop_.store(false);
  memory_watchdog_ = std::thread([this]()
  {
    // Only reads process memory through the (mutex-protected) monitor and sets
    // an atomic flag. No view, tab or DOM access happens on this thread.
    while (!memory_watchdog_stop_.load(std::memory_order_relaxed))
    {
      if (memory_monitor_)
      {
        if (memory_monitor_->IsLowRamModeEnabled())
        {
          if (memory_monitor_->Poll().over_budget)
          {
            memory_pressure_pending_.store(true, std::memory_order_relaxed);
          }
        }
      }

      // Two seconds is well under the 5s grace period, so a transient spike is
      // always resolved before the signal is ever raised.
      for (int i = 0; i < 20 && !memory_watchdog_stop_.load(std::memory_order_relaxed); ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  });
}

void UI::StopMemoryWatchdog()
{
  memory_watchdog_stop_.store(true, std::memory_order_relaxed);
  if (memory_watchdog_.joinable())
  {
    if (memory_watchdog_.get_id() == std::this_thread::get_id())
    {
      // Never join ourselves; detaching is the only safe option here.
      memory_watchdog_.detach();
    }
    else
    {
      memory_watchdog_.join();
    }
  }
}

bool UI::ConsumeMemoryPressureSignal()
{
  return memory_pressure_pending_.exchange(false, std::memory_order_relaxed);
}

void UI::NoteTabActivated(uint64_t tab_id)
{
  tab_last_active_seq_[tab_id] = ++tab_activation_counter_;
}

void UI::ReclaimMemoryIfNeeded()
{
  if (!memory_monitor_ || !settings_.low_ram_mode)
    return;

  // The watchdog thread only raises a flag; all view mutation happens here on
  // the UI thread, which is the thread that created the App.
  if (!ConsumeMemoryPressureSignal())
    return;

  // Poll first: this refreshes the hysteresis and grace-period state.
  memory::MemorySnapshot snapshot = memory_monitor_->Poll();
  if (!snapshot.over_budget)
    return;

  // Evict one tab at a time, re-reading the footprint in between: closing a
  // single view rarely brings a process that is far over budget back under it.
  // The loop stops as soon as we are healthy, or when only the active tab is
  // left, so the user is never left with a blank browser.
  while (snapshot.over_budget)
  {
    if (!memory_monitor_->ShouldEvictBackgroundTab())
      break;

    // Pick the least-recently-activated background tab. Never touch the active
    // tab, and never close a DRM tab automatically: those hold a media session
    // the user is watching, and tearing one down would be far more disruptive
    // than the memory it costs.
    uint64_t victim = 0;
    bool found = false;
    uint64_t coldest_seq = UINT64_MAX;
    for (const auto &entry : tabs_)
    {
      const uint64_t id = entry.first;
      if (id == active_tab_id_ || !entry.second)
        continue;

      auto seq_it = tab_last_active_seq_.find(id);
      const uint64_t seq = (seq_it != tab_last_active_seq_.end()) ? seq_it->second : 0;
      if (!found || seq < coldest_seq)
      {
        found = true;
        coldest_seq = seq;
        victim = id;
      }
    }

    if (!found)
      break;

    tab_last_active_seq_.erase(victim);
    memory_monitor_->NoteTabClosed();
    ReclaimMemory(victim);

    // ReclaimMemory destroyed the view; hand the freed pages back to the OS and
    // restart the grace window so the next poll measures the new footprint.
    memory::MemoryMonitor::ReleaseFreedMemory();
    memory_monitor_->NotifyMemoryReclaimed();
    snapshot = memory_monitor_->Poll();

    std::fprintf(stderr,
                 "[UI] low-RAM: closed background tab %llu, now %.1f MB of %.1f MB budget\n",
                 static_cast<unsigned long long>(victim),
                 static_cast<double>(snapshot.resident_bytes) / (1024.0 * 1024.0),
                 static_cast<double>(snapshot.budget_bytes) / (1024.0 * 1024.0));
  }
}

void UI::ReclaimMemory(uint64_t tab_id)
{
  auto it = tabs_.find(tab_id);
  if (it == tabs_.end() || !it->second)
    return;

  it->second->set_ready_to_close(true);
  RefPtr<JSContext> lock(view()->LockJSContext());
  closeTab({tab_id});
  it->second.reset();
  tabs_.erase(it);
  SaveSessionToDisk();
}

void UI::ReloadChromeUI()
{
  if (!overlay_)
    return;
  auto v = overlay_->view();
  if (!v)
    return;
  v->LoadURL("file:///ui.html");
}

void UI::SyncAdblockStateToUI()
{
  if (updateAdblockEnabled)
    updateAdblockEnabled({adblock_enabled_cached_ ? 1.0 : 0.0});
}

std::string UI::BuildSettingsJSON() const
{
  const auto &catalog = GetSettingsCatalog();
  std::ostringstream ss;
  ss << "{";
  bool first = true;
  for (const auto &desc : catalog)
  {
    // Flat key -> value map consumed by the settings page's JS. Boolean and
    // numeric settings are both emitted as native JSON scalars.
    if (!desc.is_number && desc.member)
    {
      if (!first)
        ss << ",";
      ss << "\"" << util::EscapeJsonString(desc.key) << "\": "
         << (settings_.*(desc.member) ? "true" : "false");
      first = false;
    }
    else if (desc.is_number && desc.number_member)
    {
      if (!first)
        ss << ",";
      ss << "\"" << util::EscapeJsonString(desc.key) << "\": "
         << settings_.*(desc.number_member);
      first = false;
    }
  }
  ss << "}";
  return ss.str();
}

void UI::OnSuggestOpen(const JSObject &obj, const JSArgs &args)
{
  // No-op: suggestions are shown in a top overlay; do not move layout.
}

void UI::OnSuggestClose(const JSObject &obj, const JSArgs &args)
{
  // No-op.
}

void UI::ShowMenuOverlay()
{
  if (menu_overlay_)
    return;

  // Hide active DRM WebView2 tab so overlay appears on top
  auto drm_it = drm_tabs_.find(active_tab_id_);
  if (drm_it != drm_tabs_.end() && drm_it->second)
  {
    drm_it->second->Hide();
    // Show the pre-loaded solid background Ultralight tab
    auto tab_it = tabs_.find(active_tab_id_);
    if (tab_it != tabs_.end() && tab_it->second)
      tab_it->second->Show();
  }

  // Create a transparent View so only the dropdown is visible over content
  ultralight::ViewConfig cfg;
  cfg.is_transparent = true;
  cfg.initial_device_scale = window_->scale();
  // Match acceleration/display with main UI view to avoid renderer mismatch
  if (overlay_ && overlay_->view())
  {
    cfg.is_accelerated = overlay_->view()->is_accelerated();
    cfg.display_id = overlay_->view()->display_id();
  }
  auto view = App::instance()->renderer()->CreateView(window_->width(), window_->height(), cfg, nullptr);

  // Wrap it in an overlay on top of everything
  menu_overlay_ = Overlay::Create(window_, view, 0, 0);
  menu_overlay_->Show();
  menu_overlay_->Focus();
  view->set_load_listener(this);
  view->set_view_listener(this);
  view->LoadURL("file:///menu.html");
}

void UI::HideMenuOverlay()
{
  if (!menu_overlay_)
    return;
  menu_overlay_->Hide();
  menu_overlay_->Unfocus();
  if (overlay_)
    overlay_->Focus();
  menu_overlay_->view()->set_load_listener(nullptr);
  menu_overlay_ = nullptr;

  // Restore active DRM WebView2 tab if no other overlays are open
  // Note: suggestions_overlay_ is excluded because it doesn't hide the DRM tab
  if (!downloads_overlay_ && !context_menu_overlay_)
  {
    auto drm_it = drm_tabs_.find(active_tab_id_);
    if (drm_it != drm_tabs_.end() && drm_it->second)
      drm_it->second->Show();
  }
}

void UI::ShowContextMenuOverlay(int x, int y, const ultralight::String &json_info)
{
  // Recreate view each time for simplicity - but don't restore DRM tab during recreation
  if (context_menu_overlay_)
  {
    // Just destroy the old overlay without restoring DRM tab
    context_menu_overlay_->Hide();
    context_menu_overlay_->Unfocus();
    if (overlay_)
      overlay_->Focus();
    context_menu_overlay_->view()->set_load_listener(nullptr);
    context_menu_overlay_ = nullptr;
    pending_ctx_info_json_ = "";
  }

  // Hide active DRM WebView2 tab so overlay appears on top
  auto drm_it = drm_tabs_.find(active_tab_id_);
  if (drm_it != drm_tabs_.end() && drm_it->second)
  {
    drm_it->second->Hide();
    // Show the pre-loaded solid background Ultralight tab
    auto tab_it = tabs_.find(active_tab_id_);
    if (tab_it != tabs_.end() && tab_it->second)
      tab_it->second->Show();
  }

  ultralight::ViewConfig cfg;
  cfg.is_transparent = true;
  cfg.initial_device_scale = window_->scale();
  if (overlay_ && overlay_->view())
  {
    cfg.is_accelerated = overlay_->view()->is_accelerated();
    cfg.display_id = overlay_->view()->display_id();
  }
  auto view = App::instance()->renderer()->CreateView(window_->width(), window_->height(), cfg, nullptr);
  context_menu_overlay_ = Overlay::Create(window_, view, 0, 0);
  context_menu_overlay_->Show();
  context_menu_overlay_->Focus();
  view->set_load_listener(this);
  view->set_view_listener(this);
  // Make data available before loading so OnDOMReady can initialize immediately
  pending_ctx_position_ = {x, y};
  pending_ctx_info_json_ = json_info;
  // Load overlay document; we'll invoke setupContextMenu in OnDOMReady
  view->LoadURL("file:///contextmenu.html");
}

void UI::HideContextMenuOverlay()
{
  if (!context_menu_overlay_)
    return;
  context_menu_overlay_->Hide();
  context_menu_overlay_->Unfocus();
  if (overlay_)
    overlay_->Focus();
  context_menu_overlay_->view()->set_load_listener(nullptr);
  context_menu_overlay_ = nullptr;
  pending_ctx_info_json_ = "";

  // Restore active DRM WebView2 tab if no other overlays are open
  // Note: suggestions_overlay_ is excluded because it doesn't hide the DRM tab
  if (!menu_overlay_ && !downloads_overlay_)
  {
    auto drm_it = drm_tabs_.find(active_tab_id_);
    if (drm_it != drm_tabs_.end() && drm_it->second)
      drm_it->second->Show();
  }
}

void UI::OnContextMenuAction(const JSObject &obj, const JSArgs &args)
{
  // args: action, payload
  if (args.size() == 0)
  {
    HideContextMenuOverlay();
    return;
  }
  ultralight::String action = args[0];
  if (action == "close")
  {
    HideContextMenuOverlay();
    return;
  }
  if (action == "open_tab" && args.size() >= 2)
  {
    ultralight::String url = args[1];
    CreateNewTabForChildView(url); // Handles loading internally
    HideContextMenuOverlay();
    return;
  }
  if (action == "cut")
  {
    // Hide overlay then try to cut selection in page
    HideContextMenuOverlay();
    RefPtr<View> targetView = (ctx_target_ == 1) ? view() : (active_tab() ? active_tab()->view() : nullptr);
    if (targetView)
    {
      const char *script = R"JS((function(){
        try{
          if (document.execCommand && document.execCommand('cut')) return true;
          // Prefer storing settings next to the executable under a "setup" folder
          auto exe_dir = []() -> std::filesystem::path {
        #if defined(_WIN32)
            wchar_t buf[MAX_PATH];
            DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
              std::filesystem::path p(buf);
              return p.parent_path();
            }
            return std::filesystem::current_path();
        #elif defined(__APPLE__)
            // Fallback: current_path (resolving actual executable dir on macOS requires _NSGetExecutablePath)
            return std::filesystem::current_path();
        #else
            // Linux/Unix: try /proc/self/exe, else current_path
            char linkpath[4096];
            ssize_t len = readlink("/proc/self/exe", linkpath, sizeof(linkpath)-1);
            if (len > 0) {
              linkpath[len] = '\0';
              std::filesystem::path p(linkpath);
              return p.parent_path();
            }
            return std::filesystem::current_path();
        #endif
          }();
          return std::filesystem::absolute(exe_dir / "setup");
          try { selText = String(window.getSelection ? window.getSelection() : ''); } catch(_){ }
          if (!selText) return false;
          try { if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(selText); } catch(_){ }
          // Delete selection
          return SettingsDirectory() / "settings.json";
          var el = document.activeElement;
          if (el && (el.tagName==='INPUT'||el.tagName==='TEXTAREA')){
            var s=el.selectionStart||0, e=el.selectionEnd||0, v=el.value||'';
            if (e>s){ el.setRangeText('', s, e, 'start'); el.dispatchEvent(new Event('input',{bubbles:true})); return true; }
          }
        }catch(e){}
        return false;
      })())JS";
      targetView->EvaluateScript(script, nullptr);
    }
    return;
  }
  if (action == "paste-text" && args.size() >= 2)
  {
    // Hide overlay then insert text into focused element/contentEditable
    ultralight::String text = args[1];
    HideContextMenuOverlay();
    RefPtr<View> targetView = (ctx_target_ == 1) ? view() : (active_tab() ? active_tab()->view() : nullptr);
    if (targetView)
    {
      auto utf8 = text.utf8();
      std::string t = utf8.data() ? utf8.data() : "";
      // Minimal JS string escape for quotes and backslashes and newlines
      std::string esc;
      esc.reserve(t.size() + 8);
      for (char c : t)
      {
        switch (c)
        {
        case '\\':
          esc += "\\\\";
          break;
        case '"':
          esc += "\\\"";
          break;
        case '\n':
          esc += "\\n";
          break;
        case '\r':
          esc += "\\r";
          break;
        case '\t':
          esc += "\\t";
          break;
        default:
          esc += c;
          break;
        }
      }
      std::string script = "(function(t){try{var el=document.activeElement; if(!el) return false;";
      script += "if(el.isContentEditable){ document.execCommand && document.execCommand('insertText', false, t); return true;}";
      script += "var tag=el.tagName; if(tag==='INPUT'||tag==='TEXTAREA'){ var s=el.selectionStart||0,e=el.selectionEnd||0; el.setRangeText(t, s, e, 'end'); el.dispatchEvent(new Event('input',{bubbles:true})); return true;} return false;}catch(e){return false;}})(\"" + esc + "\")";
      targetView->EvaluateScript(script.c_str(), nullptr);
    }
    return;
  }
  // Default: just close
  HideContextMenuOverlay();
}

bool UI::IsBrowserInternalPage(const std::string &url)
{
  // Fast check for browser internal pages - called from C++ to skip JS execution
  if (url.find("file:///") != 0)
    return false;

  // List of browser internal pages that have their own dark styling
  static const char *internal_pages[] = {
      "settings.html",
      "passwords.html",
      "extensions.html",
      "downloads.html",
      "history.html",
      "ui.html",
      "menu.html",
      "contextmenu.html",
      "suggestions.html",
      "quick-inspector.html",
      "downloads-panel.html",
      "about.html",
      "new_tab_page.html",
      "release_notes.html",
      // These two load assets/themes/theme.js and style themselves from the
      // active theme's CSS variables. They were missing here, so
      // ApplyDarkModeToView was applying the invert-filter dark-mode hack on
      // top of an already-correctly-themed page, which visibly corrupts their
      // colours.
      "themes.html",
      "bookmarks.html",
      "static-sties/"};

  for (const char *page : internal_pages)
  {
    if (url.find(page) != std::string::npos)
      return true;
  }
  return false;
}

void UI::ApplyDarkModeToView(RefPtr<View> v)
{
  if (!v)
    return;

  // Fast C++ check: skip dark mode injection entirely for browser internal pages
  // This avoids expensive JS execution for pages that don't need it
  auto url = v->url().utf8();
  if (url.data() && IsBrowserInternalPage(std::string(url.data())))
    return;

  // Build excluded sites list from settings
  std::string excluded_patterns = settings_.dark_theme_excluded_sites;

  const char *js = R"JS((function(){
    try{
      var url = window.location.href;

      // Check user-defined excluded sites
      var excludedPatterns = %s;
      if(excludedPatterns && excludedPatterns.length > 0){
        for(var i=0; i<excludedPatterns.length; i++){
          var pattern = excludedPatterns[i].trim();
          if(!pattern) continue;
          // Simple wildcard matching
          var regex = pattern.replace(/\*/g, '.*').replace(/\?/g, '.');
          try{
            if(new RegExp(regex, 'i').test(url)){
              return false; // Skip dark mode for this excluded site
            }
          }catch(e){}
        }
      }

      var sid='__ul_auto_dark';
      var prev=document.getElementById(sid);
      if(prev) prev.remove();

      // Check if page is already dark or very bright
      function getPageBrightness(){
        var bgColor = window.getComputedStyle(document.body).backgroundColor;
        if(!bgColor || bgColor === 'transparent' || bgColor === 'rgba(0, 0, 0, 0)'){
          bgColor = window.getComputedStyle(document.documentElement).backgroundColor;
        }
        if(!bgColor || bgColor === 'transparent' || bgColor === 'rgba(0, 0, 0, 0)'){
          return 1.0; // Assume bright if we can't detect
        }
        var m = bgColor.match(/rgba?\(([^)]+)\)/);
        if(!m) return 1.0;
        var parts = m[1].split(',').map(function(x){ return parseFloat(x); });
        var r = parts[0]/255, g = parts[1]/255, b = parts[2]/255;
        // Calculate relative luminance
        function srgb(c){ return (c<=0.03928)? c/12.92 : Math.pow((c+0.055)/1.055, 2.4); }
        return 0.2126*srgb(r) + 0.7152*srgb(g) + 0.0722*srgb(b);
      }

      var brightness = getPageBrightness();

      // Only apply dark mode to bright pages (luminance > 0.5)
      if(brightness < 0.5){
        // Page is already dark, don't apply filter
        return false;
      }

      // Apply filter-based dark mode for bright pages
      var css = 'html { filter: invert(0.9) hue-rotate(180deg) !important; background: #1e1e1e !important; }\n';
      css += 'img, video, [style*="background-image"], picture, svg, iframe { filter: invert(1.11) hue-rotate(-180deg) !important; }';

      var s=document.createElement('style');
      s.id=sid;
      s.type='text/css';
      s.appendChild(document.createTextNode(css));
      (document.head||document.documentElement).appendChild(s);

      return true;
    }catch(e){return false;}
  })())JS";

  // Parse excluded patterns into JSON array
  std::string patterns_json = "[]";
  if (!excluded_patterns.empty())
  {
    std::stringstream ss;
    ss << "[";
    bool first = true;
    std::istringstream iss(excluded_patterns);
    std::string line;
    while (std::getline(iss, line))
    {
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);
      if (!line.empty() && line[0] != '#')
      {
        if (!first)
          ss << ",";
        ss << "\"" << line << "\"";
        first = false;
      }
    }
    ss << "]";
    patterns_json = ss.str();
  }

  char buffer[8192];
  snprintf(buffer, sizeof(buffer), js, patterns_json.c_str());
  v->EvaluateScript(buffer, nullptr);
}

void UI::RemoveDarkModeFromView(RefPtr<View> v)
{
  if (!v)
    return;
  const char *js = R"JS((function(){
    try{
      var s=document.getElementById('__ul_auto_dark'); if(s) s.remove();
      var obs=window.__ul_dark_observer; if (obs && obs.disconnect) obs.disconnect();
      // Remove inline marks we added
      var marked=document.querySelectorAll('[data-ul-dark="1"]');
      for (var i=0;i<marked.length;i++){
        try{ marked[i].style.removeProperty('background-color'); marked[i].style.removeProperty('color'); marked[i].removeAttribute('data-ul-dark'); }catch(e){}
      }
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::ApplyReduceMotionToView(RefPtr<View> v)
{
  if (!v)
    return;
  const char *js = R"JS((function(){
    try{
      var sid='__ul_reduce_motion';
      if(document.getElementById(sid)) return false;
      var css = '*, *::before, *::after { animation-duration: 0.001ms !important; animation-iteration-count: 1 !important; transition-duration: 0.001ms !important; scroll-behavior: auto !important; }';
      var s=document.createElement('style');
      s.id=sid;
      s.type='text/css';
      s.appendChild(document.createTextNode(css));
      (document.head||document.documentElement).appendChild(s);
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::RemoveReduceMotionFromView(RefPtr<View> v)
{
  if (!v)
    return;
  const char *js = R"JS((function(){
    try{
      var s=document.getElementById('__ul_reduce_motion'); if(s) s.remove();
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::ApplyHighContrastToView(RefPtr<View> v)
{
  if (!v)
    return;
  const char *js = R"JS((function(){
    try{
      var sid='__ul_high_contrast';
      if(document.getElementById(sid)) return false;
      var css = '* { border-color: currentColor !important; outline-color: currentColor !important; }\n';
      css += 'a, a:visited { text-decoration: underline !important; }\n';
      css += 'button, input, select, textarea { border: 2px solid currentColor !important; }\n';
      css += ':focus { outline: 3px solid #0066ff !important; outline-offset: 2px !important; }';
      var s=document.createElement('style');
      s.id=sid;
      s.type='text/css';
      s.appendChild(document.createTextNode(css));
      (document.head||document.documentElement).appendChild(s);
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::RemoveHighContrastFromView(RefPtr<View> v)
{
  if (!v)
    return;
  const char *js = R"JS((function(){
    try{
      var s=document.getElementById('__ul_high_contrast'); if(s) s.remove();
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::ApplyVibrantWindowTheme(bool enabled)
{
#if defined(_WIN32)
  HWND hwnd = (HWND)window_->native_handle();
  if (hwnd)
  {
    // Use DWM attribute for caption color (DWMWA_CAPTION_COLOR = 35)
    // Vibrant purple: brighter accent color, Dark: standard dark purple
    COLORREF color = enabled ? RGB(120, 100, 200) : RGB(42, 33, 60);
    DwmSetWindowAttribute(hwnd, 35, &color, sizeof(color));
  }
#endif
  (void)enabled; // Suppress unused parameter warning on non-Windows
}

bool UI::ShouldUseSmoothScrolling() const
{
  // Reduce-motion wins. It already forces `scroll-behavior: auto !important`, so
  // honouring both at once would be incoherent; the user asking for less motion
  // should not also get an animated scroll.
  return smooth_scrolling_enabled_ && !reduce_motion_enabled_;
}

bool UI::IsInternalPageURL(const String &url)
{
  auto u8 = url.utf8();
  if (!u8.data())
    return false;
  const std::string_view v(u8.data());

  // Only the browser's own file:// pages. A remote site that happens to contain
  // one of these names in its URL must not be treated as chrome.
  if (v.rfind("file:///", 0) != 0)
    return false;

  // Matches on the file name only, so "file:///settings.html" matches while
  // "https://example.com/settings.html" cannot.
  for (const char *page : kInternalPageNames)
  {
    if (v.find(page) != std::string_view::npos)
      return true;
  }
  return false;
}

void UI::SyncScrollModeForView(const RefPtr<View> &v)
{
  if (!v)
    return;

  // Smooth scrolling belongs on real web content only.
  //
  // It was applied to every view, including the browser's own pages. Settings is
  // a long scrolling form, and animating its document scroller is what made it
  // feel sluggish and "stuck": each wheel notch started an animation the engine
  // had to keep composing. Chrome animates scrolling for web content but not for
  // its own UI surfaces, so chrome pages use instant scrolling regardless of the
  // toggle.
  const bool smooth = ShouldUseSmoothScrolling() && !IsInternalPageURL(v->url());
  if (smooth)
    ApplySmoothScrollingToView(v);
  else
    RemoveSmoothScrollingFromView(v);
}

void UI::ApplySmoothScrollingToView(RefPtr<View> v)
{
  if (!v)
    return;
  // Scoped to the document scroller on purpose. A `*` rule also forces smooth
  // behaviour onto every scrollable element inside the page, including code
  // editors, virtualised lists and carousels, and it turns ordinary
  // `scrollTop`/`scrollIntoView` assignments into animations. That breaks
  // infinite-scroll and jump-to-anchor behaviour on real sites. Chrome's own
  // smooth-scrolling only affects the document scroller, so this matches.
  const char *js = R"JS((function(){
    try{
      if(document.getElementById('__ul_smooth_scroll')) return true;
      var s=document.createElement('style');
      s.id='__ul_smooth_scroll';
      s.type='text/css';
      s.appendChild(document.createTextNode('html { scroll-behavior: smooth !important; }'));
      (document.head||document.documentElement).appendChild(s);
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::RemoveSmoothScrollingFromView(RefPtr<View> v)
{
  if (!v)
    return;
  const char *js = R"JS((function(){
    try{
      var s=document.getElementById('__ul_smooth_scroll'); if(s) s.remove();
      return true;
    }catch(e){return false;}
  })())JS";
  v->EvaluateScript(js, nullptr);
}

void UI::ApplyTransparentToolbar(bool enabled)
{
  // Apply transparent/translucent effect to toolbar UI
  if (!overlay_)
    return;

  RefPtr<View> ui_view = overlay_->view();
  if (!ui_view)
    return;

  const char *js_enable = R"JS((function(){
    try{
      if(document.getElementById('__ul_transparent_toolbar')) return true;
      var s=document.createElement('style');
      s.id='__ul_transparent_toolbar';
      s.textContent=`
        .toolbar, .tab-bar, nav, header, .browser-toolbar {
          background: rgba(30, 30, 46, 0.85) !important;
          backdrop-filter: blur(10px) !important;
          -webkit-backdrop-filter: blur(10px) !important;
        }
        .tab-content, .url-bar, .address-bar {
          background: rgba(42, 33, 60, 0.9) !important;
        }
      `;
      (document.head||document.documentElement).appendChild(s);
      return true;
    }catch(e){return false;}
  })())JS";

  const char *js_disable = R"JS((function(){
    try{
      var s=document.getElementById('__ul_transparent_toolbar'); if(s) s.remove();
      return true;
    }catch(e){return false;}
  })())JS";

  ui_view->EvaluateScript(enabled ? js_enable : js_disable, nullptr);
}

void UI::RemoveTransparentToolbar()
{
  ApplyTransparentToolbar(false);
}

// --- URL Suggestions Implementation ---

void UI::LoadPopularSites()
{
  popular_sites_.clear();
  std::ifstream in("assets/popular_sites.json", std::ios::in | std::ios::binary);
  if (!in.is_open())
    return;

  std::ostringstream ss;
  ss << in.rdbuf();
  std::string content = ss.str();
  in.close();

  // Simple JSON parser for array of strings
  size_t start = content.find('[');
  size_t end = content.rfind(']');
  if (start == std::string::npos || end == std::string::npos)
    return;

  std::string sites_array = content.substr(start + 1, end - start - 1);
  size_t pos = 0;
  while (pos < sites_array.size())
  {
    size_t quote1 = sites_array.find('"', pos);
    if (quote1 == std::string::npos)
      break;
    size_t quote2 = sites_array.find('"', quote1 + 1);
    if (quote2 == std::string::npos)
      break;

    std::string site = sites_array.substr(quote1 + 1, quote2 - quote1 - 1);
    if (!site.empty())
      popular_sites_.push_back(site);

    pos = quote2 + 1;
  }
}

void UI::LoadHistoryFromDisk()
{
  std::ifstream in = OpenDataFileForRead("history.json");
  if (!in.is_open())
    return;

  std::ostringstream ss;
  ss << in.rdbuf();
  std::string content = ss.str();
  in.close();

  // Parse JSON array of history entries
  history_.clear();
  size_t pos = 0;
  while (pos < content.size())
  {
    size_t obj_start = content.find('{', pos);
    if (obj_start == std::string::npos)
      break;
    size_t obj_end = content.find('}', obj_start);
    if (obj_end == std::string::npos)
      break;

    std::string obj = content.substr(obj_start, obj_end - obj_start + 1);

    // Extract url
    size_t url_key = obj.find("\"url\"");
    std::string url;
    if (url_key != std::string::npos)
    {
      size_t url_start = obj.find('"', url_key + 5);
      if (url_start != std::string::npos)
      {
        size_t url_end = obj.find('"', url_start + 1);
        if (url_end != std::string::npos)
          url = obj.substr(url_start + 1, url_end - url_start - 1);
      }
    }

    // Extract title
    size_t title_key = obj.find("\"title\"");
    std::string title;
    if (title_key != std::string::npos)
    {
      size_t title_start = obj.find('"', title_key + 7);
      if (title_start != std::string::npos)
      {
        size_t title_end = obj.find('"', title_start + 1);
        if (title_end != std::string::npos)
          title = obj.substr(title_start + 1, title_end - title_start - 1);
      }
    }

    // Extract timestamp
    size_t time_key = obj.find("\"time\"");
    uint64_t timestamp = 0;
    if (time_key != std::string::npos)
    {
      size_t colon = obj.find(':', time_key);
      if (colon != std::string::npos)
      {
        size_t num_start = colon + 1;
        while (num_start < obj.size() && (obj[num_start] == ' ' || obj[num_start] == '\t'))
          num_start++;
        size_t num_end = num_start;
        while (num_end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[num_end])))
          num_end++;
        if (num_end > num_start)
        {
          // history.json is on-disk state and may be truncated or hand-edited;
          // std::stoull throws std::out_of_range on overflow, which would escape
          // as an unhandled exception and abort startup.
          try
          {
            timestamp = std::stoull(obj.substr(num_start, num_end - num_start));
          }
          catch (const std::exception &)
          {
            timestamp = 0;
          }
        }
      }
    }

    // Extract count (optional)
    size_t count_key = obj.find("\"count\"");
    uint32_t count = 1;
    if (count_key != std::string::npos)
    {
      size_t colon2 = obj.find(':', count_key);
      if (colon2 != std::string::npos)
      {
        size_t num_start = colon2 + 1;
        while (num_start < obj.size() && (obj[num_start] == ' ' || obj[num_start] == '\t'))
          num_start++;
        size_t num_end = num_start;
        while (num_end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[num_end])))
          num_end++;
        if (num_end > num_start)
        {
          try
          {
            count = (uint32_t)std::stoul(obj.substr(num_start, num_end - num_start));
          }
          catch (const std::exception &)
          {
            count = 1;
          }
        }
      }
    }

    if (!url.empty())
      history_.push_back({url, title, timestamp, count});

    pos = obj_end + 1;
  }
}

void UI::SaveHistoryToDisk()
{
  if (clear_history_on_exit_)
  {
    std::remove(DataFilePath("history.json").string().c_str());
    return;
  }

  EnsureDataDirectoryExists();
  std::ofstream out(DataFilePath("history.json"), std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return;

  out << "[";
  for (size_t i = 0; i < history_.size(); ++i)
  {
    if (i > 0)
      out << ",";
    const auto &entry = history_[i];
    out << "{\"url\":\"" << jsonEscape(entry.url)
        << "\",\"title\":\"" << jsonEscape(entry.title)
        << "\",\"time\":" << entry.timestamp_ms
        << ",\"count\":" << entry.visit_count << "}";
  }
  out << "]";
  out.close();
}

// ================================================================================
// Session Management (Crash Recovery / Restore Tabs)
// ================================================================================
// Closed-tab stack persistence.
//
// Kept in its own file rather than folded into session.json so that reopening a
// tab does not depend on continuous-session saving being enabled, and so a
// corrupt session cannot take the closed-tab list down with it.

void UI::SaveClosedTabs()
{
  EnsureDataDirectoryExists();
  std::ofstream out(DataFilePath("closed_tabs.json"), std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return;

  out << "{\n  \"version\": 1,\n  \"tabs\": [\n";
  for (size_t i = 0; i < closed_tabs_.size(); ++i)
  {
    if (i)
      out << ",\n";
    // URLs and titles are attacker-influenced, so both are escaped.
    out << "    {\"url\": \"" << util::EscapeJsonString(closed_tabs_[i].url)
        << "\", \"title\": \"" << util::EscapeJsonString(closed_tabs_[i].title) << "\"}";
  }
  out << "\n  ]\n}\n";
}

void UI::LoadClosedTabs()
{
  std::ifstream in = OpenDataFileForRead("closed_tabs.json");
  if (!in.is_open())
    return;

  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string doc = buffer.str();

  // Hand-rolled scan: the document is machine-written and small, and pulling in
  // a JSON parser for two fields would be disproportionate.
  size_t pos = 0;
  constexpr size_t kMaxClosedTabs = 25;
  while (closed_tabs_.size() < kMaxClosedTabs)
  {
    const size_t url_start = doc.find("\"url\"", pos);
    if (url_start == std::string::npos)
      break;
    const size_t title_start = doc.find("\"title\"", url_start);
    if (title_start == std::string::npos)
      break;

    std::string url;
    std::string title;
    if (!ExtractJsonStringField(doc.substr(url_start, title_start - url_start), "url", url) ||
        !ExtractJsonStringField(doc.substr(title_start), "title", title))
      break;
    if (url.empty())
      break;

    closed_tabs_.push_back(ClosedTabEntry{url, title});
    pos = title_start + 1;
  }
}

// ================================================================================

void UI::SaveSessionToDisk()
{
  // Save current session state to disk for crash recovery
  // This is called whenever tabs change (new tab, close tab, navigation)

  if (!settings_.save_session_continuously)
    return;

  // Don't overwrite saved session while restore bar is visible
  // User hasn't made a choice yet, so preserve their previous session
  if (session_restore_bar_visible_)
    return;

  EnsureDataDirectoryExists();
  std::ofstream out(DataFilePath("session.json"), std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return;

  // Get current timestamp
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();

  out << "{\n";
  out << "  \"version\": 1,\n";
  out << "  \"timestamp\": " << timestamp << ",\n";
  out << "  \"clean_exit\": false,\n";
  out << "  \"active_tab_id\": " << active_tab_id_ << ",\n";
  out << "  \"tabs\": [\n";

  bool first = true;
  for (const auto &entry : tabs_)
  {
    if (!entry.second)
      continue;

    auto view = entry.second->view();
    if (!view)
      continue;

    auto url_ul = view->url();
    auto title_ul = view->title();
    std::string url = url_ul.utf8().data() ? url_ul.utf8().data() : "";
    std::string title = title_ul.utf8().data() ? title_ul.utf8().data() : "";

    // Skip internal pages that shouldn't be restored
    if (url.find("file:///ui.html") != std::string::npos ||
        url.find("file:///menu.html") != std::string::npos ||
        url.find("file:///contextmenu.html") != std::string::npos ||
        url.find("file:///suggestions.html") != std::string::npos ||
        url.find("file:///downloads-panel.html") != std::string::npos)
      continue;

    // Skip empty URLs
    if (url.empty() || url == "about:blank")
      continue;

    if (!first)
      out << ",\n";
    first = false;

    out << "    {\"id\": " << entry.first
        << ", \"url\": \"" << jsonEscape(url)
        << "\", \"title\": \"" << jsonEscape(title) << "\"}";
  }

  out << "\n  ],\n";

  // Also save DRM tabs
  out << "  \"drm_tabs\": [\n";
  first = true;
  for (const auto &entry : drm_tab_urls_)
  {
    auto title_it = drm_tab_titles_.find(entry.first);
    std::string title = (title_it != drm_tab_titles_.end()) ? title_it->second : "";

    if (entry.second.empty())
      continue;

    if (!first)
      out << ",\n";
    first = false;

    out << "    {\"id\": " << entry.first
        << ", \"url\": \"" << jsonEscape(entry.second)
        << "\", \"title\": \"" << jsonEscape(title) << "\"}";
  }
  out << "\n  ]\n";
  out << "}\n";
  out.close();
}

void UI::LoadSessionFromDisk()
{
  // Load session data from disk (does not restore tabs, just loads the data)
  std::ifstream in = OpenDataFileForRead("session.json");
  if (!in.is_open())
  {
    session_restore_pending_ = false;
    session_was_clean_exit_ = true;
    return;
  }

  std::stringstream buffer;
  buffer << in.rdbuf();
  in.close();

  std::string content = buffer.str();

  // Parse clean_exit flag to determine if last session crashed
  size_t clean_exit_pos = content.find("\"clean_exit\"");
  if (clean_exit_pos != std::string::npos)
  {
    size_t colon_pos = content.find(":", clean_exit_pos);
    if (colon_pos != std::string::npos)
    {
      std::string value = content.substr(colon_pos + 1, 10);
      session_was_clean_exit_ = (value.find("true") != std::string::npos);
    }
  }

  // Mark for restore if we have session data (regardless of how last session ended)
  // Chrome-like behavior: always restore previous session if enabled
  if (content.find("\"tabs\"") != std::string::npos)
  {
    // Check if tabs array has content
    size_t tabs_pos = content.find("\"tabs\"");
    if (tabs_pos != std::string::npos)
    {
      size_t bracket_start = content.find("[", tabs_pos);
      size_t bracket_end = content.find("]", bracket_start);
      if (bracket_start != std::string::npos && bracket_end != std::string::npos)
      {
        std::string tabs_str = content.substr(bracket_start + 1, bracket_end - bracket_start - 1);
        // Remove whitespace to check if empty
        tabs_str.erase(std::remove_if(tabs_str.begin(), tabs_str.end(), ::isspace), tabs_str.end());
        if (!tabs_str.empty())
        {
          session_restore_pending_ = true;
        }
      }
    }
  }
}

bool UI::HasSavedSession() const
{
  std::ifstream in = OpenDataFileForRead("session.json");
  if (!in.is_open())
    return false;

  // Quick check if file has any tab data
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();

  // Check if there are any tabs saved
  size_t tabs_pos = content.find("\"tabs\"");
  if (tabs_pos == std::string::npos)
    return false;

  // Check if tabs array is non-empty
  size_t bracket_start = content.find("[", tabs_pos);
  size_t bracket_end = content.find("]", bracket_start);
  if (bracket_start == std::string::npos || bracket_end == std::string::npos)
    return false;

  std::string tabs_content = content.substr(bracket_start + 1, bracket_end - bracket_start - 1);
  // Remove whitespace
  tabs_content.erase(std::remove_if(tabs_content.begin(), tabs_content.end(), ::isspace), tabs_content.end());

  return !tabs_content.empty();
}

void UI::ClearSavedSession()
{
  // Clear the restore pending flag (used after session is restored)
  session_restore_pending_ = false;
}

void UI::SaveSessionToDiskWithCleanExit()
{
  // Save current session state with clean_exit=true
  // Called during normal shutdown to preserve tabs for next startup

  EnsureDataDirectoryExists();
  std::ofstream out(DataFilePath("session.json"), std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return;

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();

  out << "{\n";
  out << "  \"version\": 1,\n";
  out << "  \"timestamp\": " << timestamp << ",\n";
  out << "  \"clean_exit\": true,\n"; // Mark as clean exit
  out << "  \"active_tab_id\": " << active_tab_id_ << ",\n";
  out << "  \"tabs\": [\n";

  bool first = true;
  for (const auto &entry : tabs_)
  {
    if (!entry.second)
      continue;

    auto view = entry.second->view();
    if (!view)
      continue;

    auto url_ul = view->url();
    auto title_ul = view->title();
    std::string url = url_ul.utf8().data() ? url_ul.utf8().data() : "";
    std::string title = title_ul.utf8().data() ? title_ul.utf8().data() : "";

    // Skip internal UI pages
    if (url.find("file:///ui.html") != std::string::npos ||
        url.find("file:///menu.html") != std::string::npos ||
        url.find("file:///contextmenu.html") != std::string::npos ||
        url.find("file:///suggestions.html") != std::string::npos ||
        url.find("file:///downloads-panel.html") != std::string::npos)
      continue;

    if (url.empty() || url == "about:blank")
      continue;

    if (!first)
      out << ",\n";
    first = false;

    out << "    {\"id\": " << entry.first
        << ", \"url\": \"" << jsonEscape(url)
        << "\", \"title\": \"" << jsonEscape(title) << "\"}";
  }

  out << "\n  ],\n";
  out << "  \"drm_tabs\": [\n";
  first = true;
  for (const auto &entry : drm_tab_urls_)
  {
    auto title_it = drm_tab_titles_.find(entry.first);
    std::string title = (title_it != drm_tab_titles_.end()) ? title_it->second : "";

    if (entry.second.empty())
      continue;

    if (!first)
      out << ",\n";
    first = false;

    out << "    {\"id\": " << entry.first
        << ", \"url\": \"" << jsonEscape(entry.second)
        << "\", \"title\": \"" << jsonEscape(title) << "\"}";
  }
  out << "\n  ]\n";
  out << "}\n";
  out.close();
}

void UI::RestoreSavedSession()
{
  // Restore tabs from saved session
  std::ifstream in = OpenDataFileForRead("session.json");
  if (!in.is_open())
    return;

  std::stringstream buffer;
  buffer << in.rdbuf();
  in.close();

  std::string content = buffer.str();

  // Parse tabs array - simple JSON parsing
  size_t tabs_pos = content.find("\"tabs\"");
  if (tabs_pos == std::string::npos)
    return;

  size_t bracket_start = content.find("[", tabs_pos);
  size_t bracket_end = content.find("]", bracket_start);
  if (bracket_start == std::string::npos || bracket_end == std::string::npos)
    return;

  std::string tabs_content = content.substr(bracket_start + 1, bracket_end - bracket_start - 1);

  // Parse each tab entry - collect ALL tabs including duplicates
  size_t pos = 0;
  std::vector<std::string> urls_to_restore;

  while ((pos = tabs_content.find("{", pos)) != std::string::npos)
  {
    size_t end_obj = tabs_content.find("}", pos);
    if (end_obj == std::string::npos)
      break;

    std::string obj = tabs_content.substr(pos, end_obj - pos + 1);

    // Extract URL
    size_t url_pos = obj.find("\"url\"");
    std::string url;
    if (url_pos != std::string::npos)
    {
      size_t url_start = obj.find("\"", url_pos + 5);
      size_t url_end = obj.find("\"", url_start + 1);
      if (url_start != std::string::npos && url_end != std::string::npos)
      {
        url = obj.substr(url_start + 1, url_end - url_start - 1);
      }
    }

    // Add ALL non-empty URLs (including duplicates)
    if (!url.empty())
    {
      urls_to_restore.push_back(url);
    }

    pos = end_obj + 1;
  }

  // If no tabs to restore, do nothing
  if (urls_to_restore.empty())
  {
    session_restore_pending_ = false;
    return;
  }

  // Strategy: Navigate the existing first tab to the first URL,
  // then create new tabs for the remaining URLs.
  // This avoids the complexity of closing tabs.

  bool first_url = true;
  for (const auto &url : urls_to_restore)
  {
    if (first_url && !tabs_.empty())
    {
      // Navigate the existing (start page) tab to the first restored URL
      auto first_tab_it = tabs_.begin();
      if (first_tab_it->second && first_tab_it->second->view())
      {
        first_tab_it->second->view()->LoadURL(String(url.c_str()));
      }
      first_url = false;
    }
    else
    {
      // Create new tabs for remaining URLs
      CreateNewTabForChildView(String(url.c_str()));
    }
  }

  // Clear the pending restore flag
  session_restore_pending_ = false;

  // Mark current session as active (not clean exit) since we're running
  SaveSessionToDisk();
}

int UI::GetSavedSessionTabCount() const
{
  std::ifstream in = OpenDataFileForRead("session.json");
  if (!in.is_open())
    return 0;

  std::stringstream buffer;
  buffer << in.rdbuf();
  in.close();

  std::string content = buffer.str();

  // Count tabs in the array
  size_t tabs_pos = content.find("\"tabs\"");
  if (tabs_pos == std::string::npos)
    return 0;

  size_t bracket_start = content.find("[", tabs_pos);
  size_t bracket_end = content.find("]", bracket_start);
  if (bracket_start == std::string::npos || bracket_end == std::string::npos)
    return 0;

  std::string tabs_content = content.substr(bracket_start + 1, bracket_end - bracket_start - 1);

  // Count '{' characters to count objects
  int count = 0;
  for (char c : tabs_content)
  {
    if (c == '{')
      count++;
  }

  return count;
}

bool UI::IsInternalBrowserPage(const std::string &url) const
{
  // List of internal/default browser pages that don't need to be restored
  static const std::vector<std::string> internal_pages = {
      "file:///static-sties/google-static.html",
      "file:///new_tab_page.html",
      "file:///settings.html",
      "file:///history.html",
      "file:///downloads.html",
      "file:///passwords.html",
      "file:///extensions.html",
      "file:///about.html",
      "file:///release_notes.html",
      "file:///ui.html",
      "file:///menu.html",
      "file:///contextmenu.html",
      "file:///suggestions.html",
      "file:///downloads-panel.html",
      // Local pages that are browser UI, not web content. Restoring these would
      // resurrect an internal page as if it were a real session tab.
      "file:///themes.html",
      "file:///bookmarks.html",
      "about:blank"};

  for (const auto &page : internal_pages)
  {
    if (url.find(page) != std::string::npos || url == page)
      return true;
  }

  // Also check for any file:/// URL that's an internal asset
  if (url.find("file:///") == 0)
  {
    // Check if it's a local static site or internal page
    if (url.find("static-sties") != std::string::npos)
      return true;
  }

  return false;
}

int UI::GetMeaningfulSavedTabCount() const
{
  // Count tabs that are NOT internal browser pages
  std::ifstream in = OpenDataFileForRead("session.json");
  if (!in.is_open())
    return 0;

  std::stringstream buffer;
  buffer << in.rdbuf();
  in.close();

  std::string content = buffer.str();

  size_t tabs_pos = content.find("\"tabs\"");
  if (tabs_pos == std::string::npos)
    return 0;

  size_t bracket_start = content.find("[", tabs_pos);
  size_t bracket_end = content.find("]", bracket_start);
  if (bracket_start == std::string::npos || bracket_end == std::string::npos)
    return 0;

  std::string tabs_content = content.substr(bracket_start + 1, bracket_end - bracket_start - 1);

  int meaningful_count = 0;
  size_t pos = 0;

  while ((pos = tabs_content.find("{", pos)) != std::string::npos)
  {
    size_t end_obj = tabs_content.find("}", pos);
    if (end_obj == std::string::npos)
      break;

    std::string obj = tabs_content.substr(pos, end_obj - pos + 1);

    // Extract URL
    size_t url_pos = obj.find("\"url\"");
    if (url_pos != std::string::npos)
    {
      size_t url_start = obj.find("\"", url_pos + 5);
      size_t url_end = obj.find("\"", url_start + 1);
      if (url_start != std::string::npos && url_end != std::string::npos)
      {
        std::string url = obj.substr(url_start + 1, url_end - url_start - 1);
        if (!IsInternalBrowserPage(url))
        {
          meaningful_count++;
        }
      }
    }

    pos = end_obj + 1;
  }

  return meaningful_count;
}

void UI::ShowSessionRestoreBar()
{
  // Mark that restore bar is visible to prevent session saving
  session_restore_bar_visible_ = true;

  int tabCount = GetMeaningfulSavedTabCount();
  bool wasCrash = !session_was_clean_exit_;

  std::ostringstream js;
  js << "(function(){ if(typeof showSessionRestoreBar === 'function') showSessionRestoreBar("
     << tabCount << ", " << (wasCrash ? "true" : "false") << "); })();";

  view()->EvaluateScript(String(js.str().c_str()), nullptr);
}

void UI::OnRestoreSession(const JSObject &obj, const JSArgs &args)
{
  // User clicked "Restore" - restore all saved tabs
  // Clear the bar visibility flag first so we can save the restored session
  session_restore_bar_visible_ = false;
  RestoreSavedSession();
}

void UI::OnDismissSession(const JSObject &obj, const JSArgs &args)
{
  // User clicked "Start Fresh" or closed the bar
  // Clear the bar visibility flag so we can save the new session
  session_restore_bar_visible_ = false;

  // Clear the pending flag so we don't show the bar again
  session_restore_pending_ = false;

  // Start a new session with the current tab
  SaveSessionToDisk();
}

std::vector<std::string> UI::GetSuggestions(const std::string &input, int maxResults)
{
  std::vector<std::string> suggestions;
  if (maxResults <= 0)
    maxResults = 10;

  // We'll build (url, score) pairs, then sort by score
  struct Scored
  {
    std::string url;
    double score;
  };
  std::vector<Scored> scored;
  auto push_scored = [&](const std::string &u, double s)
  { scored.push_back({u, s}); };

  auto now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  auto score_history = [&](const std::string &u, const HistoryEntry &e, const std::string &q) -> double
  {
    std::string ul = u, ql = q;
    std::transform(ul.begin(), ul.end(), ul.begin(), ::tolower);
    std::transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
    double prefix = 0.0, contains = 0.0;
    // check domain part prefix
    size_t proto = ul.find("://");
    size_t start = (proto == std::string::npos ? 0 : proto + 3);
    if (ul.find(ql, start) == start)
      prefix = 1.0; // strong boost for prefix
    if (ul.find(ql) != std::string::npos)
      contains = 0.5;
    // recency: within ~30 days fades to 0
    double age_days = std::max<double>(0.0, (double)(now_ms - e.timestamp_ms) / (1000.0 * 60.0 * 60.0 * 24.0));
    double recency = std::max(0.0, 1.0 - (age_days / 30.0));
    // frequency: log scale
    double freq = std::log(1.0 + (double)std::max<uint32_t>(1, e.visit_count)) / std::log(10.0);
    return 2.0 * prefix + 1.0 * contains + 2.0 * recency + 2.0 * freq;
  };

  // If no input, suggest top by recency/frequency
  if (input.empty())
  {
    std::unordered_set<std::string> seen;
    // History candidates
    for (const auto &e : history_)
    {
      double s = score_history(e.url, e, std::string());
      if (seen.insert(e.url).second)
        push_scored(e.url, s);
    }
    // Sort by score desc
    std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b)
              { return a.score > b.score; });
    for (const auto &p : scored)
    {
      if ((int)suggestions.size() >= maxResults)
        break;
      suggestions.push_back(p.url);
    }
    // Fill with popular sites
    for (const auto &site : popular_sites_)
    {
      if ((int)suggestions.size() >= maxResults)
        break;
      if (std::find(suggestions.begin(), suggestions.end(), site) == suggestions.end())
        suggestions.push_back(site);
    }
    return suggestions;
  }

  std::string input_lower = input;
  std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);

  // Helper lambda to check if a URL matches the input
  auto matches = [&input_lower](const std::string &url) -> bool
  {
    std::string url_lower = url;
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);

    // Check if input matches the beginning of the URL (after protocol)
    size_t protocol_end = url_lower.find("://");
    if (protocol_end != std::string::npos)
    {
      std::string domain_part = url_lower.substr(protocol_end + 3);
      if (domain_part.find(input_lower) == 0)
        return true;
    }

    // Check if input appears anywhere in the URL
    return url_lower.find(input_lower) != std::string::npos;
  };

  // First, score matching history entries
  {
    std::unordered_set<std::string> seen;
    for (const auto &e : history_)
    {
      if (matches(e.url))
      {
        double s = score_history(e.url, e, input);
        if (seen.insert(e.url).second)
          push_scored(e.url, s);
      }
    }
  }

  // Then, add matching popular sites (lower score)
  for (const auto &site : popular_sites_)
  {
    if (matches(site))
    {
      // Low baseline score; small prefix boost
      double s = 0.6;
      std::string sl = site, il = input;
      std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
      std::transform(il.begin(), il.end(), il.begin(), ::tolower);
      if (sl.find(il) == sl.find("://") + 3)
        s += 0.6;
      push_scored(site, s);
    }
  }

  // Sort by score and emit unique URLs
  std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b)
            { return a.score > b.score; });
  std::unordered_set<std::string> emitted;
  for (const auto &p : scored)
  {
    if ((int)suggestions.size() >= maxResults)
      break;
    if (emitted.insert(p.url).second)
      suggestions.push_back(p.url);
  }
  return suggestions;
}

JSValue UI::OnGetSuggestions(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !args[0].IsString())
    return JSValue();

  if (!suggestions_enabled_)
    return JSValue("[]");

  ultralight::String input_ul = args[0];
  auto input_ul8 = input_ul.utf8();
  std::string input = input_ul8.data() ? input_ul8.data() : "";

  int maxResults = 10;
  if (args.size() >= 2 && args[1].IsNumber())
    maxResults = (int)args[1].ToInteger();

  auto suggestions = GetSuggestions(input, maxResults);

  // Build JSON array (strings or objects with favicon)
  std::string json = "[";
  for (size_t i = 0; i < suggestions.size(); ++i)
  {
    if (i > 0)
      json += ",";
    if (suggestion_favicons_enabled_)
    {
      std::string u = suggestions[i];
      // Prefer disk-cached favicon if available
      std::string origin = GetOriginStringFromURL(u);
      std::string f;
      auto itf = favicon_file_cache_.find(origin);
      if (itf != favicon_file_cache_.end())
      {
        f = itf->second; // file:///... path
      }
      if (f.empty())
      {
        auto fav = GetFaviconURL(String(u.c_str()));
        auto f8 = fav.utf8();
        f = f8.data() ? f8.data() : "";
      }
      json += "{\"url\":\"" + jsonEscape(u) + "\"";
      if (!f.empty())
        json += ",\"favicon\":\"" + jsonEscape(f) + "\"";
      json += "}";
    }
    else
    {
      json += "\"" + jsonEscape(suggestions[i]) + "\"";
    }
  }
  json += "]";

  return JSValue(json.c_str());
}

// --- Favicon Disk Cache Helpers ---
std::string UI::GetOriginStringFromURL(const std::string &url)
{
  // Extract scheme://host[:port]
  size_t scheme = url.find("://");
  if (scheme == std::string::npos)
    return std::string();
  size_t host_start = scheme + 3;
  size_t slash = url.find('/', host_start);
  if (slash == std::string::npos)
    return url; // whole string is origin
  return url.substr(0, slash);
}

std::string UI::EnsureFaviconCacheDir()
{
  // Windows-style path acceptable; assets live under working dir.
  // Use data/favicons for persisted favicons
  std::string dir = "data/favicons";
  // Create directories if missing (best-effort)
  // We can't use std::filesystem here reliably; try to create via C runtime
#ifdef _WIN32
  _mkdir("data");
  _mkdir("data\\favicons");
#else
  mkdir("data", 0755);
  mkdir("data/favicons", 0755);
#endif
  return dir;
}

// simple base64 decoder (RFC 4648) for PNG payloads
std::string UI::Base64Decode(const std::string &in)
{
  static const int T[256] = {
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      62,
      -1,
      -1,
      -1,
      63,
      52,
      53,
      54,
      55,
      56,
      57,
      58,
      59,
      60,
      61,
      -1,
      -1,
      -1,
      0,
      -1,
      -1,
      -1,
      0,
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10,
      11,
      12,
      13,
      14,
      15,
      16,
      17,
      18,
      19,
      20,
      21,
      22,
      23,
      24,
      25,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      26,
      27,
      28,
      29,
      30,
      31,
      32,
      33,
      34,
      35,
      36,
      37,
      38,
      39,
      40,
      41,
      42,
      43,
      44,
      45,
      46,
      47,
      48,
      49,
      50,
      51,
      -1,
      -1,
      -1,
      -1,
      -1,
      // rest -1
  };
  std::string out;
  out.reserve(in.size() * 3 / 4);
  int val = 0, valb = -8;
  for (unsigned char c : in)
  {
    int d = -1;
    if (c < 128)
      d = T[c];
    if (d == -1)
    {
      if (c == '=')
        break;
      else
        continue;
    }
    val = (val << 6) + d;
    valb += 6;
    if (valb >= 0)
    {
      out.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

void UI::LoadFaviconDiskCache()
{
  favicon_file_cache_.clear();
  // Ensure directory exists
  EnsureFaviconCacheDir();
  std::ifstream in("data/favicons/index.json", std::ios::in | std::ios::binary);
  if (!in.is_open())
    return;
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string txt = ss.str();
  in.close();
  // Parse simple object { "origin": "file:///...", ... }
  size_t pos = 0;
  while (true)
  {
    size_t k1 = txt.find('"', pos);
    if (k1 == std::string::npos)
      break;
    size_t k2 = txt.find('"', k1 + 1);
    if (k2 == std::string::npos)
      break;
    std::string key = txt.substr(k1 + 1, k2 - k1 - 1);
    size_t c = txt.find(':', k2 + 1);
    if (c == std::string::npos)
      break;
    size_t v1 = txt.find('"', c + 1);
    if (v1 == std::string::npos)
      break;
    size_t v2 = txt.find('"', v1 + 1);
    if (v2 == std::string::npos)
      break;
    std::string val = txt.substr(v1 + 1, v2 - v1 - 1);
    if (!key.empty() && !val.empty())
      favicon_file_cache_[key] = val;
    pos = v2 + 1;
    if (favicon_file_cache_.size() > favicon_cache_limit_)
      break;
  }
  if (favicon_file_cache_.size() > favicon_cache_limit_)
  {
    PruneFaviconDiskCacheToLimit();
    SaveFaviconDiskCache();
  }
}

void UI::SaveFaviconDiskCache()
{
  EnsureFaviconCacheDir();
  std::ofstream out("data/favicons/index.json", std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out.is_open())
    return;
  out << "{";
  bool first = true;
  for (auto &p : favicon_file_cache_)
  {
    if (!first)
      out << ",";
    first = false;
    out << "\"" << jsonEscape(p.first) << "\":\"" << jsonEscape(p.second) << "\"";
  }
  out << "}";
  out.close();
}

void UI::OnFaviconReady(const JSObject &obj, const JSArgs &args)
{
  // args: url, favicon
  //
  // The favicon arrives as a plain http(s) URL, not a data URL. It used to be
  // rasterised to a data URL in-page via canvas, which cannot work for
  // cross-origin images (see Tab.cpp), so the data-URL-only cache below now
  // sees a URL, fails the ',' check, and discards every favicon.
  if (args.size() < 2 || !args[0].IsString() || !args[1].IsString())
    return;
  if (!suggestion_favicons_enabled_)
    return;
  ultralight::String url_ul = args[0].ToString();
  ultralight::String data_ul = args[1].ToString();
  auto u8 = url_ul.utf8();
  std::string url = u8.data() ? u8.data() : "";
  auto d8 = data_ul.utf8();
  std::string data = d8.data() ? d8.data() : "";
  if (url.empty() || data.empty())
    return;
  std::string origin = GetOriginStringFromURL(url);
  if (origin.empty())
    return;

  // Respect cache size limit with smarter eviction based on origin score
  double new_score = GetOriginScore(origin);
  if (favicon_file_cache_.find(origin) == favicon_file_cache_.end() && favicon_file_cache_.size() >= favicon_cache_limit_)
  {
    // Find the lowest-score existing origin
    std::string worst_origin;
    double worst_score = 1e18;
    bool found = false;
    for (const auto &p : favicon_file_cache_)
    {
      double s = GetOriginScore(p.first);
      if (!found || s < worst_score)
      {
        worst_score = s;
        worst_origin = p.first;
        found = true;
      }
    }
    if (!found || new_score <= worst_score)
    {
      // New origin is not better than the worst cached one; skip caching
      return;
    }
    // Evict worst
    auto itw = favicon_file_cache_.find(worst_origin);
    if (itw != favicon_file_cache_.end())
    {
      // Try delete file on disk
      std::string furl = itw->second;
      std::string path;
      const std::string prefix = "file:///";
      if (furl.rfind(prefix, 0) == 0)
      {
        path = furl.substr(prefix.size());
#ifdef _WIN32
        for (auto &ch : path)
        {
          if (ch == '/')
            ch = '\\';
        }
#endif
        std::remove(path.c_str());
      }
      favicon_file_cache_.erase(itw);
    }
  }

// Cache the plain URL.
  //
  // A data URL is also accepted so previously cached entries keep working:
  // those get written to the favicon cache dir and referenced as file:///.
  if (data.rfind("data:", 0) == 0)
  {
    size_t comma = data.find(",");
    if (comma == std::string::npos)
      return;
    std::string b64 = data.substr(comma + 1);
    std::string bytes = Base64Decode(b64);
    if (bytes.size() < 8)
      return;

    std::string dir = EnsureFaviconCacheDir();
    // Hash filename from origin
    std::hash<std::string> hasher;
    size_t h = hasher(origin);
    std::ostringstream path;
    path << dir << "/" << std::hex << h << ".png";
    std::string file = path.str();
    // Write file
    std::ofstream f(file, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open())
      return;
    f.write(bytes.data(), (std::streamsize)bytes.size());
    f.close();
    // Store as file:/// absolute URL
    char cwd_buf[1024] = {0};
#ifdef _WIN32
    _getcwd(cwd_buf, sizeof(cwd_buf));
    std::string abs = std::string("file:///") + std::string(cwd_buf) + "/" + file;
    // replace backslashes
    for (auto &ch : abs)
    {
      if (ch == '\\')
        ch = '/';
    }
#else
    getcwd(cwd_buf, sizeof(cwd_buf));
    std::string abs = std::string("file://") + std::string(cwd_buf) + "/" + file;
#endif
    favicon_file_cache_[origin] = abs;
  }
  else if (data.rfind("http://", 0) == 0 || data.rfind("https://", 0) == 0)
  {
    favicon_file_cache_[origin] = data;
  }
  else
  {
    return;
  }

  // Keep the in-memory cache in step so a tab redraw picks the favicon up
  // immediately instead of waiting for the next page load.
  favicon_cache_[origin] = data;
  SaveFaviconDiskCache();
}

double UI::GetOriginScore(const std::string &origin)
{
  // Combine recency and frequency across history entries that match this origin
  if (origin.empty())
    return 0.0;
  auto now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  double total = 0.0;
  for (const auto &e : history_)
  {
    if (GetOriginStringFromURL(e.url) != origin)
      continue;
    double age_days = std::max<double>(0.0, (double)(now_ms - e.timestamp_ms) / (1000.0 * 60.0 * 60.0 * 24.0));
    double recency = std::max(0.0, 1.0 - (age_days / 30.0));
    double freq = std::log(1.0 + (double)std::max<uint32_t>(1, e.visit_count)) / std::log(10.0);
    total += (2.0 * recency + 2.0 * freq);
  }
  return total;
}

void UI::PruneFaviconDiskCacheToLimit()
{
  if (favicon_file_cache_.size() <= favicon_cache_limit_)
    return;
  // Collect and sort by score desc
  std::vector<std::pair<std::string, double>> arr;
  arr.reserve(favicon_file_cache_.size());
  for (const auto &p : favicon_file_cache_)
  {
    arr.emplace_back(p.first, GetOriginScore(p.first));
  }
  std::sort(arr.begin(), arr.end(), [](const auto &a, const auto &b)
            { return a.second > b.second; });
  // Determine origins to keep
  std::unordered_set<std::string> keep;
  for (size_t i = 0; i < arr.size() && i < favicon_cache_limit_; ++i)
    keep.insert(arr[i].first);
  // Remove the rest (and try delete files)
  for (auto it = favicon_file_cache_.begin(); it != favicon_file_cache_.end();)
  {
    if (keep.find(it->first) != keep.end())
    {
      ++it;
      continue;
    }
    std::string furl = it->second;
    const std::string prefix = "file:///";
    if (furl.rfind(prefix, 0) == 0)
    {
      std::string path = furl.substr(prefix.size());
#ifdef _WIN32
      for (auto &ch : path)
      {
        if (ch == '/')
          ch = '\\';
      }
#endif
      std::remove(path.c_str());
    }
    it = favicon_file_cache_.erase(it);
  }
}

void UI::LoadSuggestionsFaviconsFlag()
{
  suggestion_favicons_enabled_ = true; // default
  std::ifstream in("assets/suggestions_favicons.txt", std::ios::in | std::ios::binary);
  if (!in.is_open())
    return;
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string txt = ss.str();
  in.close();
  // Normalize
  std::string s;
  s.reserve(txt.size());
  for (char c : txt)
  {
    if (!std::isspace((unsigned char)c))
      s += (char)std::tolower((unsigned char)c);
  }
  if (s == "off" || s == "0" || s == "false")
    suggestion_favicons_enabled_ = false;
}

void UI::ShowSuggestionsOverlay(int x, int y, int width, const ultralight::String &json_items)
{
  // Recreate each time for simplicity - but don't restore DRM tab during recreation
  if (suggestions_overlay_)
  {
    // Just destroy the old overlay without restoring DRM tab
    suggestions_overlay_->Hide();
    suggestions_overlay_->Unfocus();
    suggestions_overlay_->view()->set_load_listener(nullptr);
    suggestions_overlay_ = nullptr;
    pending_sugg_json_ = "";
  }

  // NOTE: Don't hide DRM tab for suggestions - it's a small dropdown that appears
  // in the URL bar area, not covering the main content. Hiding/showing DRM tab
  // causes flickering and input issues.

  ultralight::ViewConfig cfg;
  cfg.is_transparent = true;
  cfg.initial_device_scale = window_->scale();
  if (overlay_ && overlay_->view())
  {
    cfg.is_accelerated = overlay_->view()->is_accelerated();
    cfg.display_id = overlay_->view()->display_id();
  }
  auto view = App::instance()->renderer()->CreateView(window_->width(), window_->height(), cfg, nullptr);
  suggestions_overlay_ = Overlay::Create(window_, view, 0, 0);
  suggestions_overlay_->Show();
  // Do not steal focus from address bar; leave unfocused
  view->set_load_listener(this);
  view->set_view_listener(this);
  pending_sugg_position_ = {x, y};
  pending_sugg_width_ = width;
  pending_sugg_json_ = json_items;
  view->LoadURL("file:///suggestions.html");
}

void UI::HideSuggestionsOverlay()
{
  if (!suggestions_overlay_)
    return;
  suggestions_overlay_->Hide();
  suggestions_overlay_->Unfocus();
  suggestions_overlay_->view()->set_load_listener(nullptr);
  suggestions_overlay_ = nullptr;
  pending_sugg_json_ = "";
  // NOTE: Don't restore DRM tab here - suggestions don't hide it in the first place
}

void UI::OnOpenSuggestionsOverlay(const JSObject &obj, const JSArgs &args)
{
  // args: x, y, width, items_json
  if (args.size() < 4)
    return;
  int x = (int)args[0].ToInteger();
  int y = (int)args[1].ToInteger();
  int w = (int)args[2].ToInteger();
  ultralight::String items = args[3].ToString();
  ShowSuggestionsOverlay(x, y, w, items);
}

void UI::OnCloseSuggestionsOverlay(const JSObject &obj, const JSArgs &args)
{
  HideSuggestionsOverlay();
}

void UI::OnSuggestionPick(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !args[0].IsString())
  {
    HideSuggestionsOverlay();
    return;
  }
  ultralight::String s = args[0].ToString();
  HideSuggestionsOverlay();
  // Navigate like pressing Enter on the address bar
  std::string url = s.utf8().data() ? s.utf8().data() : "";
  if (url.empty())
    return;
  // If second arg is "newtab", open in a new tab
  bool open_new_tab = false;
  if (args.size() >= 2 && args[1].IsString())
  {
    ultralight::String mode = args[1].ToString();
    auto m8 = mode.utf8();
    std::string m = m8.data() ? m8.data() : "";
    open_new_tab = (m == "newtab");
  }
  if (open_new_tab)
  {
    CreateNewTabForChildView(s); // Handles loading internally
    return;
  }
  // find() plus a null check. operator[] would insert a null entry for a DRM
  // active tab, and tab->view() on that null entry is a null dereference.
  if (!tabs_.empty())
  {
    auto tab_it = tabs_.find(active_tab_id_);
    if (tab_it != tabs_.end() && tab_it->second)
      tab_it->second->view()->LoadURL(s);
  }
}

void UI::OnSuggestionPaste(const JSObject &obj, const JSArgs &args)
{
  // args: url string to paste into address bar; no navigation
  if (args.size() < 1 || !args[0].IsString())
    return;
  ultralight::String s = args[0].ToString();
  // Update address bar text in main UI without navigating
  if (updateURL)
  {
    RefPtr<JSContext> lock(view()->LockJSContext());
    updateURL({s});
  }
  // keep focus on address bar for continued typing
  address_bar_is_focused_ = true;
}

void UI::OnNewDownloadStarted()
{
  if (!auto_open_download_panel_)
    return;

  downloads_overlay_user_dismissed_ = false;
  ShowDownloadsOverlay();
}

bool UI::BrowserSettings::operator==(const BrowserSettings &other) const
{
  return launch_dark_theme == other.launch_dark_theme &&
         dark_theme_excluded_sites == other.dark_theme_excluded_sites &&
         vibrant_window_theme == other.vibrant_window_theme &&
         experimental_transparent_toolbar == other.experimental_transparent_toolbar &&
         experimental_compact_tabs == other.experimental_compact_tabs &&
         enable_adblock == other.enable_adblock &&
         log_blocked_requests == other.log_blocked_requests &&
         clear_history_on_exit == other.clear_history_on_exit &&
         enable_javascript == other.enable_javascript &&
         enable_web_security == other.enable_web_security &&
         block_third_party_cookies == other.block_third_party_cookies &&
         do_not_track == other.do_not_track &&
         enable_suggestions == other.enable_suggestions &&
         enable_suggestion_favicons == other.enable_suggestion_favicons &&
         show_download_badge == other.show_download_badge &&
         auto_open_download_panel == other.auto_open_download_panel &&
         ask_download_location == other.ask_download_location &&
         smooth_scrolling == other.smooth_scrolling &&
         hardware_acceleration == other.hardware_acceleration &&
          enable_local_storage == other.enable_local_storage &&
          enable_database == other.enable_database &&
          low_ram_mode == other.low_ram_mode &&
          memory_budget_mb == other.memory_budget_mb &&
         reduce_motion == other.reduce_motion &&
         high_contrast_ui == other.high_contrast_ui &&
         enable_caret_browsing == other.enable_caret_browsing &&
         enable_remote_inspector == other.enable_remote_inspector &&
         show_performance_overlay == other.show_performance_overlay &&
         use_custom_user_agent == other.use_custom_user_agent &&
         custom_user_agent == other.custom_user_agent &&
         auto_save_settings == other.auto_save_settings &&
         enable_drm_webview == other.enable_drm_webview &&
      media_fallback_enabled == other.media_fallback_enabled &&
         restore_session_on_startup == other.restore_session_on_startup &&
         save_session_continuously == other.save_session_continuously;
}

std::filesystem::path UI::SettingsDirectory()
{
  namespace fs = std::filesystem;
#if defined(_WIN32)
  if (auto appdata = util::GetEnvVar("APPDATA"); !appdata.empty())
    return fs::path(appdata) / "UltralightWebBrowser";
#elif defined(__APPLE__)
  if (auto home = util::GetEnvVar("HOME"); !home.empty())
    return fs::path(home) / "Library/Application Support/UltralightWebBrowser";
#else
  if (auto xdg = util::GetEnvVar("XDG_CONFIG_HOME"); !xdg.empty())
    return fs::path(xdg) / "UltralightWebBrowser";
  if (auto home = util::GetEnvVar("HOME"); !home.empty())
    return fs::path(home) / ".config/UltralightWebBrowser";
#endif
  return fs::current_path() / "data";
}

std::filesystem::path UI::SettingsFilePath()
{
  return SettingsDirectory() / "settings.json";
}

std::filesystem::path UI::LegacySettingsFilePath()
{
  namespace fs = std::filesystem;
  return fs::path("data") / "settings.json";
}

// ============================================================================
// Password Manager Implementation
// ============================================================================

ultralight::JSValue UI::OnGetPasswords(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_)
    return JSValue("[]");

  auto credentials = password_manager_->GetAllCredentials();
  std::ostringstream ss;
  ss << "[";
  bool first = true;
  for (const auto &cred : credentials)
  {
    if (!first)
      ss << ",";
    first = false;

    ss << "{";
    ss << "\"id\":\"" << util::EscapeJsonString(cred.id) << "\",";
    ss << "\"origin\":\"" << util::EscapeJsonString(cred.origin) << "\",";
    ss << "\"username\":\"" << util::EscapeJsonString(cred.username) << "\",";
    ss << "\"password\":\"" << util::EscapeJsonString(cred.password) << "\",";
    ss << "\"notes\":\"" << util::EscapeJsonString(cred.notes) << "\",";
    ss << "\"created\":" << cred.date_created << ",";
    ss << "\"modified\":" << cred.date_password_modified << ",";
    ss << "\"last_used\":" << cred.date_last_used;
    ss << "}";
  }
  ss << "]";
  return JSValue(String(ss.str().c_str()));
}

ultralight::JSValue UI::OnGetPasswordStats(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_)
    return JSValue("{}");

  auto credentials = password_manager_->GetAllCredentials();
  int total = static_cast<int>(credentials.size());
  int weak = 0;
  int reused = 0;
  std::unordered_map<std::string, int> password_counts;

  for (const auto &cred : credentials)
  {
    auto strength = password_manager_->CheckPasswordStrength(cred.password);
    if (strength.score < 3)
      weak++;

    password_counts[cred.password]++;
  }

  for (const auto &pair : password_counts)
  {
    if (pair.second > 1)
      reused += pair.second;
  }

  int blacklisted = static_cast<int>(password_manager_->GetBlacklistedOrigins().size());

  std::ostringstream ss;
  ss << "{";
  ss << "\"total_passwords\":" << total << ",";
  ss << "\"weak_passwords\":" << weak << ",";
  ss << "\"reused_passwords\":" << reused << ",";
  ss << "\"blacklisted_sites\":" << blacklisted;
  ss << "}";
  return JSValue(String(ss.str().c_str()));
}

void UI::OnSavePassword(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return;

  ultralight::String json = args[0].ToString();
  auto json_str = json.utf8();
  std::string data = json_str.data() ? json_str.data() : "";

  // Parse JSON manually
  auto extract_string = [&data](const std::string &key) -> std::string
  {
    std::string search_key = "\"" + key + "\":\"";
    size_t pos = data.find(search_key);
    if (pos == std::string::npos)
      return "";
    pos += search_key.length();
    std::string result;
    while (pos < data.length() && data[pos] != '"')
    {
      if (data[pos] == '\\' && pos + 1 < data.length())
      {
        pos++;
        if (data[pos] == 'n')
          result += '\n';
        else if (data[pos] == 't')
          result += '\t';
        else if (data[pos] == '"')
          result += '"';
        else if (data[pos] == '\\')
          result += '\\';
        else
          result += data[pos];
      }
      else
      {
        result += data[pos];
      }
      pos++;
    }
    return result;
  };

  std::string id = extract_string("id");
  std::string origin = extract_string("origin");
  std::string username = extract_string("username");
  std::string password = extract_string("password");
  std::string notes = extract_string("notes");

  if (origin.empty() || username.empty() || password.empty())
    return;

  password::SavedCredential cred;
  cred.id = id.empty() ? password_manager_->GenerateUUID() : id;
  cred.origin = origin;
  cred.signon_realm = origin;
  cred.username = username;
  cred.password = password;
  cred.notes = notes;
  cred.date_created = static_cast<uint64_t>(std::time(nullptr));
  cred.date_password_modified = cred.date_created;
  cred.date_last_used = 0;
  cred.times_used = 0;
  cred.blacklisted = false;

  if (id.empty())
  {
    password_manager_->SaveCredential(cred);
  }
  else
  {
    password_manager_->UpdateCredential(cred);
  }
}

void UI::OnDeletePassword(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return;

  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";

  if (!id.empty())
    password_manager_->DeleteCredential(id);
}

ultralight::JSValue UI::OnGetDecryptedPassword(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return JSValue("");

  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";

  auto credentials = password_manager_->GetAllCredentials();
  for (const auto &cred : credentials)
  {
    if (cred.id == id)
      return JSValue(String(cred.password.c_str()));
  }
  return JSValue("");
}

void UI::OnSavePasswordSettings(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return;

  ultralight::String json_ul = args[0].ToString();
  auto json_str = json_ul.utf8();
  std::string json = json_str.data() ? json_str.data() : "";
  if (json.empty())
    return;

  // passwords.html sends a JSON object with the checkbox states. Each key is applied
  // through UpdateBoolSetting so the mutation and the persistence happen under the
  // password manager's own lock, and unknown/absent keys are left untouched.
  static const char *const kBoolKeys[] = {
      "offer_to_save_passwords",
      "auto_signin",
      "generate_passwords_automatically"};

  for (const char *key : kBoolKeys)
  {
    std::string search = std::string("\"") + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
      continue;

    pos = json.find(':', pos);
    if (pos == std::string::npos)
      continue;
    ++pos;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r'))
      ++pos;

    if (json.compare(pos, 4, "true") == 0)
      password_manager_->UpdateBoolSetting(key, true);
    else if (json.compare(pos, 5, "false") == 0)
      password_manager_->UpdateBoolSetting(key, false);
  }
}

void UI::OnExportPasswords(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return;

  ultralight::String format_ul = args[0].ToString();
  auto format_str = format_ul.utf8();
  std::string format = format_str.data() ? format_str.data() : "csv";

  std::string filename = "passwords_export." + format;
  std::filesystem::path export_path = SettingsDirectory() / filename;

  if (format == "json")
    password_manager_->ExportToJSON(export_path.string());
  else
    password_manager_->ExportToCSV(export_path.string());
}

void UI::OnImportPasswords(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.size() < 2)
    return;

  ultralight::String content_ul = args[0].ToString();
  ultralight::String format_ul = args[1].ToString();

  auto content_str = content_ul.utf8();
  auto format_str = format_ul.utf8();

  std::string content = content_str.data() ? content_str.data() : "";
  std::string format = format_str.data() ? format_str.data() : "csv";

  // Write to temp file and import
  std::filesystem::path temp_path = SettingsDirectory() / ("temp_import." + format);
  {
    std::ofstream out(temp_path, std::ios::binary);
    if (!out.is_open())
      return;
    out << content;
  }

  if (format == "json")
    password_manager_->ImportFromJSON(temp_path.string());
  else
    password_manager_->ImportFromCSV(temp_path.string());

  std::filesystem::remove(temp_path);
}

void UI::OnShowPasswordSavePrompt(const JSObject &obj, const JSArgs &args)
{
  // Delegate to the working prompt implementation used by Tab.
  if (args.size() < 2)
    return;

  auto origin_str = JSStringToUtf8(args[0].ToString());
  auto username_str = JSStringToUtf8(args[1].ToString());

  ShowPasswordSavePrompt(origin_str.data() ? origin_str.data() : "",
                         username_str.data() ? username_str.data() : "");
}

void UI::OnHidePasswordSavePrompt(const JSObject &obj, const JSArgs &args)
{
  HidePasswordSavePrompt();
}

void UI::OnPasswordSaveResponse(const JSObject &obj, const JSArgs &args)
{
  OnPasswordSaveBarResponse(obj, args);
}

// Non-JS versions called from Tab
void UI::ShowPasswordSavePrompt(const std::string &origin, const std::string &username)
{
  // Show password save prompt bar in the UI
  std::ostringstream js;
  js << "(function(){ "
     << "if(typeof window.showPasswordSaveBar === 'function') { "
     << "  window.showPasswordSaveBar('" << util::EscapeJsStringLiteral(origin) << "', '" << util::EscapeJsStringLiteral(username) << "'); "
     << "} "
     << "})();";
  view()->EvaluateScript(String(js.str().c_str()), nullptr);
}

void UI::HidePasswordSavePrompt()
{
  // Hide password save prompt bar in the UI
  view()->EvaluateScript("(function(){ if(typeof window.hidePasswordSaveBar === 'function') window.hidePasswordSaveBar(); })();", nullptr);
}

void UI::OnPasswordSaveBarResponse(const JSObject &obj, const JSArgs &args)
{
  // Called when user clicks Save/Never on the password save bar
  if (!password_manager_ || args.size() < 3)
    return;

  ultralight::String action_ul = args[0].ToString();
  ultralight::String origin_ul = args[1].ToString();
  ultralight::String username_ul = args[2].ToString();

  auto action_str = action_ul.utf8();
  auto origin_str = origin_ul.utf8();
  auto username_str = username_ul.utf8();

  std::string action = action_str.data() ? action_str.data() : "";
  std::string origin = origin_str.data() ? origin_str.data() : "";
  std::string username = username_str.data() ? username_str.data() : "";

  // Get the active tab to retrieve pending credentials
  if (active_tab_id_ && tabs_.count(active_tab_id_) && tabs_[active_tab_id_])
  {
    auto &tab = tabs_[active_tab_id_];
    // Call the tab's password save response handler
    JSArgs response_args;
    response_args.push_back(JSValue(String(action.c_str())));
    tab->OnPasswordSaveResponse(JSObject(), response_args);
  }
}

void UI::OnPasswordNeverSave(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return;

  ultralight::String origin_ul = args[0].ToString();
  auto origin_str = origin_ul.utf8();
  std::string origin = origin_str.data() ? origin_str.data() : "";

  if (!origin.empty())
    password_manager_->BlacklistOrigin(origin);
}

// DRM Prompt functionality
void UI::ShowDrmPrompt(const std::string &url, uint64_t tab_id)
{
  // Show DRM prompt bar in the UI
  std::ostringstream js;
  js << "(function(){ "
     << "if(typeof window.showDrmPromptBar === 'function') { "
     << "  window.showDrmPromptBar('" << util::EscapeJsStringLiteral(url) << "', " << tab_id << "); "
     << "} "
     << "})();";
  view()->EvaluateScript(String(js.str().c_str()), nullptr);
}

void UI::HideDrmPrompt()
{
  // Hide DRM prompt bar in the UI
  view()->EvaluateScript("(function(){ if(typeof window.hideDrmPromptBar === 'function') window.hideDrmPromptBar(); })();", nullptr);
}

void UI::OnDrmPromptResponse(const JSObject &obj, const JSArgs &args)
{
  // Called when user clicks Enable DRM / Always Enable / Dismiss on the DRM prompt bar
  if (args.size() < 3)
    return;

  ultralight::String action_ul = args[0].ToString();
  ultralight::String url_ul = args[1].ToString();
  int64_t tab_id_int = args[2].ToInteger();

  auto action_str = action_ul.utf8();
  auto url_str = url_ul.utf8();

  std::string action = action_str.data() ? action_str.data() : "";
  std::string url = url_str.data() ? url_str.data() : "";
  uint64_t tab_id = static_cast<uint64_t>(tab_id_int);

  if (action == "enable_once")
  {
    // Temporarily enable DRM for this navigation only
    // We'll directly open the DRM tab without changing the setting
    bool old_setting = settings_.enable_drm_webview;
    settings_.enable_drm_webview = true;

    // Try to open the DRM tab
    if (tab_id > 0 && tabs_.count(tab_id))
    {
      // Force open DRM tab for this URL
      MaybeOpenDrmTab(tab_id, url, true);
    }

    // Restore the setting (user didn't want it permanently enabled)
    settings_.enable_drm_webview = old_setting;
  }
  else if (action == "enable_always")
  {
    // Permanently enable DRM setting
    settings_.enable_drm_webview = true;
    ApplySettings(false, false);
    SaveSettingsToDisk();

    // Now open the DRM tab
    if (tab_id > 0 && tabs_.count(tab_id))
    {
      MaybeOpenDrmTab(tab_id, url, true);
    }
  }
  // "dismiss" action - do nothing, just close the bar
}

ultralight::JSValue UI::OnGetAutofillSuggestions(const JSObject &obj, const JSArgs &args)
{
  if (!password_manager_ || args.empty())
    return JSValue("[]");

  ultralight::String origin_ul = args[0].ToString();
  auto origin_str = origin_ul.utf8();
  std::string origin = origin_str.data() ? origin_str.data() : "";

  auto credentials = password_manager_->GetCredentialsForOrigin(origin);

  std::ostringstream ss;
  ss << "[";
  bool first = true;
  for (const auto &cred : credentials)
  {
    if (!first)
      ss << ",";
    first = false;

    ss << "{";
    ss << "\"id\":\"" << util::EscapeJsonString(cred.id) << "\",";
    ss << "\"username\":\"" << util::EscapeJsonString(cred.username) << "\"";
    ss << "}";
  }
  ss << "]";
  return JSValue(String(ss.str().c_str()));
}

ultralight::JSValue UI::OnIsDarkModeEnabled(const JSObject &obj, const JSArgs &args)
{
  return JSValue(dark_mode_enabled_);
}

// ============================================================================
// Bookmark Manager Implementation
// ============================================================================

ultralight::JSValue UI::OnGetBookmarks(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_)
    return JSValue(String("[]"));
  return JSValue(String(bookmark_store_->ToJSON().c_str()));
}

ultralight::JSValue UI::OnGetBookmarkBar(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_)
    return JSValue(String("[]"));
  return JSValue(String(bookmark_store_->BookmarkBarToJSON().c_str()));
}

ultralight::JSValue UI::OnAddBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_ || args.empty())
    return JSValue(0);

  ultralight::String url_ul = args[0].ToString();
  auto url_str = url_ul.utf8();
  std::string url = url_str.data() ? url_str.data() : "";

  std::string title;
  if (args.size() > 1)
  {
    ultralight::String title_ul = args[1].ToString();
    auto title_str = title_ul.utf8();
    title = title_str.data() ? title_str.data() : "";
  }

  std::string favicon;
  if (args.size() > 2)
  {
    ultralight::String favicon_ul = args[2].ToString();
    auto favicon_str = favicon_ul.utf8();
    favicon = favicon_str.data() ? favicon_str.data() : "";
  }

  bool show_on_bar = args.size() > 3 ? (bool)args[3] : true;

  uint64_t id = bookmark_store_->AddBookmark(url, title, favicon, show_on_bar);
  return JSValue((double)id);
}

void UI::OnRemoveBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_ || args.empty())
    return;

  uint64_t id = static_cast<uint64_t>((double)args[0]);
  bookmark_store_->RemoveBookmark(id);
}

ultralight::JSValue UI::OnIsBookmarked(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_ || args.empty())
    return JSValue(false);

  ultralight::String url_ul = args[0].ToString();
  auto url_str = url_ul.utf8();
  std::string url = url_str.data() ? url_str.data() : "";
  return JSValue(bookmark_store_->IsBookmarked(url));
}

void UI::OnToggleBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_)
    return;

  std::string url;
  std::string title;
  std::string favicon;

  // If no arguments provided, use the active tab's data
  if (args.empty())
  {
    if (!active_tab())
      return;

    auto tab_url = active_tab()->view()->url();
    auto tab_url_str = tab_url.utf8();
    url = tab_url_str.data() ? tab_url_str.data() : "";

    auto tab_title = active_tab()->view()->title();
    auto tab_title_str = tab_title.utf8();
    title = tab_title_str.data() ? tab_title_str.data() : "";

    // Get favicon URL via UI's helper
    auto favicon_str_ul = GetFaviconURL(tab_url);
    auto favicon_str = favicon_str_ul.utf8();
    favicon = favicon_str.data() ? favicon_str.data() : "";
  }
  else
  {
    ultralight::String url_ul = args[0].ToString();
    auto url_str = url_ul.utf8();
    url = url_str.data() ? url_str.data() : "";

    if (args.size() > 1)
    {
      ultralight::String title_ul = args[1].ToString();
      auto title_str = title_ul.utf8();
      title = title_str.data() ? title_str.data() : "";
    }

    if (args.size() > 2)
    {
      ultralight::String favicon_ul = args[2].ToString();
      auto favicon_str = favicon_ul.utf8();
      favicon = favicon_str.data() ? favicon_str.data() : "";
    }
  }

  if (url.empty())
    return;

  bool was_bookmarked = bookmark_store_->IsBookmarked(url);

  if (was_bookmarked)
  {
    auto *bm = bookmark_store_->GetBookmarkByUrl(url);
    if (bm)
      bookmark_store_->RemoveBookmark(bm->id);
  }
  else
  {
    bookmark_store_->AddBookmark(url, title, favicon, true);
  }

  // Update the bookmark button icon in the UI
  RefPtr<JSContext> lock(view()->LockJSContext());
  JSContextRef ctx = lock->ctx();
  ultralight::String js = ultralight::String("if(typeof updateBookmarkButton === 'function') updateBookmarkButton(") +
                          ultralight::String(was_bookmarked ? "false" : "true") +
                          ultralight::String(");");
  view()->EvaluateScript(js, nullptr);
}

void UI::OnUpdateBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!bookmark_store_ || args.size() < 2)
    return;

  uint64_t id = static_cast<uint64_t>((double)args[0]);

  ultralight::String url_ul = args[1].ToString();
  auto url_str = url_ul.utf8();
  std::string url = url_str.data() ? url_str.data() : "";

  std::string title;
  if (args.size() > 2)
  {
    ultralight::String title_ul = args[2].ToString();
    auto title_str = title_ul.utf8();
    title = title_str.data() ? title_str.data() : "";
  }

  std::string favicon;
  if (args.size() > 3)
  {
    ultralight::String favicon_ul = args[3].ToString();
    auto favicon_str = favicon_ul.utf8();
    favicon = favicon_str.data() ? favicon_str.data() : "";
  }

  bool show_on_bar = args.size() > 4 ? (bool)args[4] : true;

  bookmark_store_->UpdateBookmark(id, url, title, favicon, show_on_bar);
}
