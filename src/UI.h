#pragma once
#include <AppCore/AppCore.h>
#include "Tab.h"
#include "drm/DRMSettings.h"
#include "ExtensionManager.h"
#include "BookmarkStore.h"
#include "MemoryMonitor.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <deque>
#include <optional>
#include <atomic>
#include <thread>

namespace drm
{
  class DRMWebViewManager;
  class DRMWebViewTab;
}

namespace password
{
  class PasswordManager;
}

using ultralight::JSArgs;
using ultralight::JSFunction;
using ultralight::JSObject;
using namespace ultralight;

class Console;
class AdBlocker;       // forward declaration, optional dependency
class SettingsManager; // forward-declare the helper to make it a friend
class DownloadManager;

/**
 * Browser UI implementation. Renders the toolbar/addressbar/tabs in top pane.
 */
class UI : public WindowListener,
           public LoadListener,
           public ViewListener
{
public:
  UI(RefPtr<Window> window);
  // Overload retained for compatibility; adblockers are optional and may be unused.
  UI(RefPtr<Window> window, AdBlocker *adblock, AdBlocker *tracker);
  ~UI();

  struct BrowserSettings
  {
    // Appearance
    bool launch_dark_theme = false;
    std::string dark_theme_excluded_sites; // URL patterns where dark theme should NOT be applied (newline-separated)
    bool vibrant_window_theme = false;
    bool experimental_transparent_toolbar = false;
    bool experimental_compact_tabs = false;

    // Privacy & Security
    bool enable_adblock = true;
    bool log_blocked_requests = false;
    bool clear_history_on_exit = true;
    bool enable_javascript = true;
    bool enable_web_security = true;
    bool block_third_party_cookies = false;
    bool do_not_track = true;

    // Address Bar & Suggestions
    bool enable_suggestions = true;
    bool enable_suggestion_favicons = true;

    // Downloads
    bool show_download_badge = true;
    bool auto_open_download_panel = true;
    bool ask_download_location = false;
    bool convert_webp_to_png = false; // Convert downloaded WebP images to PNG format

    // Performance
    bool smooth_scrolling = true;
    bool hardware_acceleration = true;
    bool enable_local_storage = true;
    bool enable_database = true;

  // Low-RAM mode
  bool low_ram_mode = false;          // Enable memory-pressure monitoring
  int memory_budget_mb = 512;         // Working-set budget before pressure is reported

    // Accessibility
    bool reduce_motion = false;
    bool high_contrast_ui = false;
    bool enable_caret_browsing = false;

    // Developer
    bool enable_remote_inspector = false;
    bool show_performance_overlay = false;

    // Networking / User Agent
    // When true, always send the value from custom_user_agent.
    // When false, send a best-effort Chromium-like user agent string.
    bool use_custom_user_agent = false;
    // User-defined override string (used when use_custom_user_agent == true).
    std::string custom_user_agent;

    // General behavior
    // When true, settings changes are written to disk immediately.
    // When false, user must press "Save Changes" in the Settings UI.
    bool auto_save_settings = true;

    // DRM WebView subsystem toggle (disabled by default, user must opt-in)
    bool enable_drm_webview = false;

    // Session restore settings
    bool restore_session_on_startup = true; // Restore previous session tabs on startup
    bool save_session_continuously = true;  // Save session state continuously (crash recovery)

    // Location Spoofing
    bool enable_location_spoofing = false; // When true, override navigator.geolocation
    double spoofed_latitude = 0.0;         // Latitude to report (-90 to 90)
    double spoofed_longitude = 0.0;        // Longitude to report (-180 to 180)

    bool operator==(const BrowserSettings &other) const;
    bool operator!=(const BrowserSettings &other) const { return !(*this == other); }
  };

  // Inherited from WindowListener
  virtual bool OnKeyEvent(const ultralight::KeyEvent &evt) override;
  virtual bool OnMouseEvent(const ultralight::MouseEvent &evt) override;
  // Mouse wheel arrives as a ScrollEvent, not a MouseEvent, so it bypasses the
  // overlay routing in OnMouseEvent. Without this override, wheel input scrolls
  // the page *behind* an open menu/context menu/suggestions list, because those
  // overlays consume mouse events but never see the wheel.
  virtual bool OnScrollEvent(const ultralight::ScrollEvent &evt) override;
  virtual void OnClose(ultralight::Window *window) override;
  virtual void OnResize(ultralight::Window *window, uint32_t width, uint32_t height) override;

  // Inherited from LoadListener
  virtual void OnDOMReady(View *caller, uint64_t frame_id,
                          bool is_main_frame, const String &url) override;

  // Inherited from ViewListener
  virtual void OnChangeCursor(ultralight::View *caller, Cursor cursor) override { SetCursor(cursor); }

  // Called by UI JavaScript
  void OnBack(const JSObject &obj, const JSArgs &args);
  void OnForward(const JSObject &obj, const JSArgs &args);
  void OnRefresh(const JSObject &obj, const JSArgs &args);
  void OnStop(const JSObject &obj, const JSArgs &args);
  void OnToggleTools(const JSObject &obj, const JSArgs &args);
  void OnRequestNewTab(const JSObject &obj, const JSArgs &args);
  void OnRequestNewWindow(const JSObject &obj, const JSArgs &args);
  void OnRequestTabClose(const JSObject &obj, const JSArgs &args);
  void OnActiveTabChange(const JSObject &obj, const JSArgs &args);
  void OnRequestChangeURL(const JSObject &obj, const JSArgs &args);
  // Explicitly called when user pressed Enter in the address bar
  void OnAddressBarNavigate(const JSObject &obj, const JSArgs &args);
  // Open History page in a new tab (used by menu button and shortcuts)
  void OnOpenHistoryNewTab(const JSObject &obj, const JSArgs &args);
  void OnOpenBookmarksNewTab(const JSObject &obj, const JSArgs &args);
  void OnOpenDownloadsNewTab(const JSObject &obj, const JSArgs &args);
  void OnOpenPasswordsNewTab(const JSObject &obj, const JSArgs &args);
  void OnOpenThemesNewTab(const JSObject &obj, const JSArgs &args);
  void OnOpenThemesDirectory(const JSObject &obj, const JSArgs &args);
  void OnAddressBarBlur(const JSObject &obj, const JSArgs &args);
  void OnAddressBarFocus(const JSObject &obj, const JSArgs &args);
  void OnMenuOpen(const JSObject &obj, const JSArgs &args);
  void OnMenuClose(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayToggle(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayClose(const JSObject &obj, const JSArgs &args);
  void OnContextMenuAction(const JSObject &obj, const JSArgs &args);
  void OnToggleDarkMode(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnDownloadsOverlayGet(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayClear(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayOpenItem(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayRevealItem(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayPauseItem(const JSObject &obj, const JSArgs &args);
  void OnDownloadsOverlayRemoveItem(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetDarkModeEnabled(const JSObject &obj, const JSArgs &args);
  void OnToggleAdblock(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetAdblockEnabled(const JSObject &obj, const JSArgs &args);
  void OnOpenSettingsPanel(const JSObject &obj, const JSArgs &args);
  void OnCloseSettingsPanel(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetSettings(const JSObject &obj, const JSArgs &args);
  void OnUpdateSetting(const JSObject &obj, const JSArgs &args);
  // Returns the live working-set / budget snapshot as JSON for the settings UI.
  ultralight::JSValue OnGetMemorySnapshot(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnRestoreSettingsDefaults(const JSObject &obj, const JSArgs &args);
  void OnSaveSettings(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetDrmStatus(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnInstallDrmDependencies(const JSObject &obj, const JSArgs &args);
  // Reload the main chrome UI overlay (ui.html) so layout-dependent
  // settings such as compact tabs take effect immediately.
  void OnReloadChromeUI(const JSObject &obj, const JSArgs &args);
  // Reload the active browsing tab (skip settings tab) when a setting
  // indicates the page should be reloaded (e.g. compact tabs metadata).
  void OnReloadActiveNonSettingsTab(const JSObject &obj, const JSArgs &args);
  // Suggestions callback (address bar autocomplete)
  ultralight::JSValue OnGetSuggestions(const JSObject &obj, const JSArgs &args);
  // Adjust UI overlay height for suggestions dropdown
  void OnSuggestOpen(const JSObject &obj, const JSArgs &args);
  void OnSuggestClose(const JSObject &obj, const JSArgs &args);

  // Extension system callbacks
  void OnOpenExtensionsNewTab(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetExtensions(const JSObject &obj, const JSArgs &args);
  void OnToggleExtension(const JSObject &obj, const JSArgs &args);
  void OnReloadExtension(const JSObject &obj, const JSArgs &args);
  void OnReloadAllExtensions(const JSObject &obj, const JSArgs &args);
  void OnDeleteExtension(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnLoadExtension(const JSObject &obj, const JSArgs &args);
  void OnCreateExtension(const JSObject &obj, const JSArgs &args);
  void OnOpenExtensionsFolder(const JSObject &obj, const JSArgs &args);

  // Bookmark Manager callbacks
  ultralight::JSValue OnGetBookmarks(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetBookmarkBar(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnAddBookmark(const JSObject &obj, const JSArgs &args);
  void OnRemoveBookmark(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnIsBookmarked(const JSObject &obj, const JSArgs &args);
  void OnToggleBookmark(const JSObject &obj, const JSArgs &args);
  void OnUpdateBookmark(const JSObject &obj, const JSArgs &args);

  // Password Manager callbacks
  ultralight::JSValue OnGetPasswords(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetPasswordStats(const JSObject &obj, const JSArgs &args);
  void OnSavePassword(const JSObject &obj, const JSArgs &args);
  void OnDeletePassword(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetDecryptedPassword(const JSObject &obj, const JSArgs &args);
  void OnSavePasswordSettings(const JSObject &obj, const JSArgs &args);
  void OnExportPasswords(const JSObject &obj, const JSArgs &args);
  void OnImportPasswords(const JSObject &obj, const JSArgs &args);
  void OnShowPasswordSavePrompt(const JSObject &obj, const JSArgs &args);
  void OnHidePasswordSavePrompt(const JSObject &obj, const JSArgs &args);
  void OnPasswordSaveResponse(const JSObject &obj, const JSArgs &args);
  void OnPasswordNeverSave(const JSObject &obj, const JSArgs &args);
  void OnPasswordSaveBarResponse(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnGetAutofillSuggestions(const JSObject &obj, const JSArgs &args);
  ultralight::JSValue OnIsDarkModeEnabled(const JSObject &obj, const JSArgs &args);

  // DRM prompt methods
  void OnDrmPromptResponse(const JSObject &obj, const JSArgs &args);
  void ShowDrmPrompt(const std::string &url, uint64_t tab_id);
  void HideDrmPrompt();

  // Password save prompt methods (called from Tab)
  void ShowPasswordSavePrompt(const std::string &origin, const std::string &username);
  void HidePasswordSavePrompt();

  RefPtr<Window> window() { return window_; }
  DownloadManager *download_manager() { return download_manager_.get(); }
  password::PasswordManager *password_manager() { return password_manager_.get(); }
  BookmarkStore *bookmark_store() { return bookmark_store_.get(); }

  // Memory monitor for the low-RAM mode budget policy. Never null after
  // construction; returns the last computed sample without re-reading the OS.
  memory::MemorySnapshot GetMemorySnapshot();
  // Polls the platform and refreshes the over-budget state. Cheap enough to call
  // from the UI bridge on demand.
  memory::MemorySnapshot PollMemory();
  // Closes background tabs until the process is back under its memory budget.
  // No-op unless low-RAM mode is enabled and the budget has been exceeded for
  // longer than the grace period. Never closes the active tab.
  void ReclaimMemoryIfNeeded();
  // Destroys a specific tab and its view. Used by low-RAM reclamation and by
  // ordinary tab close paths.
  void ReclaimMemory(uint64_t tab_id);
  // Records that a tab became active, so eviction can pick the coldest tab
  // first. Cheap; called on every tab activation.
  void NoteTabActivated(uint64_t tab_id);
  // Starts the background sampler. It only reads process memory and raises an
  // atomic flag; it never touches views, so it is safe off the UI thread.
  // Reclamation itself happens on the UI thread, at tab open/close/activate.
  void StartMemoryWatchdog();
  // Stops the background sampler. Called from the destructor.
  void StopMemoryWatchdog();
  // True when the watchdog has seen sustained pressure since the last check.
  bool ConsumeMemoryPressureSignal();
  // Creates the monitor on first use, configured from the current settings.
  // Single lazy-init path so the two accessors above can never disagree about
  // the monitor's mode or budget.
  void EnsureMemoryMonitor();
  AdBlocker *network_blocker() { return adblock_; }

  // Privacy settings accessors for Tab's JavaScript injection
  bool do_not_track_enabled() const { return settings_.do_not_track; }
  bool block_third_party_cookies_enabled() const { return settings_.block_third_party_cookies; }
  bool web_security_enabled() const { return settings_.enable_web_security; }

  // Location spoofing accessors for Tab's JavaScript injection
  bool location_spoofing_enabled() const { return settings_.enable_location_spoofing; }
  double spoofed_latitude() const { return settings_.spoofed_latitude; }
  double spoofed_longitude() const { return settings_.spoofed_longitude; }

protected:
  static std::filesystem::path SettingsDirectory();
  static std::filesystem::path SettingsFilePath();
  static std::filesystem::path LegacySettingsFilePath();
  void CreateNewTab();
  RefPtr<View> CreateNewTabForChildView(const String &url);
  void UpdateTabTitle(uint64_t id, const String &title);
  void UpdateTabURL(uint64_t id, const String &url);
  void UpdateTabNavigation(uint64_t id, bool is_loading, bool can_go_back, bool can_go_forward);
  void UpdateTabFavicon(uint64_t id, const String &favicon_data_url);

  void SetLoading(bool is_loading);
  void SetCanGoBack(bool can_go_back);
  void SetCanGoForward(bool can_go_forward);
  void SetURL(const String &url);
  void SetCursor(Cursor cursor);
  void UpdateBookmarkButtonState();
  std::string BuildDrmStatusPayload();
  void AdjustUIHeight(uint32_t new_height);
  void ShowMenuOverlay();
  void HideMenuOverlay();
  void ShowDownloadsOverlay();
  void HideDownloadsOverlay();
  void LayoutDownloadsOverlay();
  void ShowContextMenuOverlay(int x, int y, const ultralight::String &json_info);
  void HideContextMenuOverlay();
  // Suggestions overlay (above all UI)
  void ShowSuggestionsOverlay(int x, int y, int width, const ultralight::String &json_items);
  void HideSuggestionsOverlay();

  // History management
  void RecordHistory(const String &url, const String &title);
  String GetHistoryJSON();
  void ClearHistory();

  // Session management (crash recovery / restore tabs)
  void SaveSessionToDisk();
  void SaveSessionToDiskWithCleanExit();
  void LoadSessionFromDisk();
  bool HasSavedSession() const;
  void ClearSavedSession();
  void RestoreSavedSession();
  void ShowSessionRestoreBar();                                   // Show restore prompt in UI
  void OnRestoreSession(const JSObject &obj, const JSArgs &args); // User clicked restore
  void OnDismissSession(const JSObject &obj, const JSArgs &args); // User clicked dismiss
  int GetSavedSessionTabCount() const;                            // Get number of saved tabs
  int GetMeaningfulSavedTabCount() const;                         // Get count of non-internal tabs
  bool IsInternalBrowserPage(const std::string &url) const;       // Check if URL is internal

  // Downloads management helpers
  String GetDownloadsJSON();
  void ClearCompletedDownloads();
  bool OpenDownloadItem(uint64_t id);
  bool RevealDownloadItem(uint64_t id);
  bool PauseDownloadItem(uint64_t id);
  bool RemoveDownloadItem(uint64_t id);
  void NotifyDownloadsChanged();
  void OnNewDownloadStarted();
  void SyncAdblockStateToUI();
  void SyncSettingsStateToUI(bool snapshot_is_baseline = false);
  void ApplySettings(bool initial, bool snapshot_is_baseline);
  void SetDarkModeEnabled(bool enabled);
  void LoadSettingsFromDisk();
  bool SaveSettingsToDisk();
  void EnsureDataDirectoryExists();
  void RestoreSettingsToDefaults();
  // Helper used by OnReloadChromeUI to refresh the chrome overlay.
  void ReloadChromeUI();
  std::string BuildSettingsJSON() const;
  std::string BuildSettingsPayload(bool snapshot_is_baseline) const;
  bool ParseSettingsBool(const std::string &buffer, const char *key, bool fallback) const;
  void HandleSettingMutation(const std::string &key, bool value);
  void UpdateSettingsDirtyFlag();
  // Reload helpers
  void ReloadActiveNonSettingsTab();

  // Suggestions / persistence helpers
  void LoadPopularSites();
  void LoadHistoryFromDisk();
  void SaveHistoryToDisk();
  std::vector<std::string> GetSuggestions(const std::string &input, int maxResults);
  // JS bridge to open/close suggestions overlay and pick
  void OnOpenSuggestionsOverlay(const JSObject &obj, const JSArgs &args);
  void OnCloseSuggestionsOverlay(const JSObject &obj, const JSArgs &args);
  void OnSuggestionPick(const JSObject &obj, const JSArgs &args);
  // Paste a suggestion URL into the address bar without navigating
  void OnSuggestionPaste(const JSObject &obj, const JSArgs &args);
  // Receive favicon image data (as data URL) from suggestions overlay and persist cache
  void OnFaviconReady(const JSObject &obj, const JSArgs &args);

  // Compute a best-effort favicon URL (origin + "/favicon.ico") for http/https URLs
  String GetFaviconURL(const String &page_url);
  // Get origin string (scheme+host+optional port)
  static std::string GetOriginStringFromURL(const std::string &url);
  // Favicon disk cache
  void LoadFaviconDiskCache();
  void SaveFaviconDiskCache();
  std::string EnsureFaviconCacheDir();
  static std::string Base64Decode(const std::string &in);
  double GetOriginScore(const std::string &origin);
  void PruneFaviconDiskCacheToLimit();

  // Extension system helpers
  void InitializeExtensions();
  std::string BuildExtensionsPayload() const;
  std::string GetExtensionsDirectory() const;

  Tab *active_tab();
  drm::DRMWebViewTab *active_drm_tab();
  bool ActiveTabIsDRM() const;

  RefPtr<View> view() { return overlay_->view(); }

  RefPtr<Window> window_;
  RefPtr<Overlay> overlay_;
  int ui_height_;
  int base_ui_height_;
  int tab_height_;
  RefPtr<Overlay> menu_overlay_;
  RefPtr<Overlay> downloads_overlay_;
  RefPtr<Overlay> context_menu_overlay_;
  RefPtr<Overlay> suggestions_overlay_;
  float scale_;
  // Optional ad/tracker blocker references (may be unused in this build)
  AdBlocker *adblock_ = nullptr;
  AdBlocker *trackerblock_ = nullptr;
  std::unique_ptr<DownloadManager> download_manager_;
  std::unique_ptr<password::PasswordManager> password_manager_;
  std::unique_ptr<BookmarkStore> bookmark_store_;
  // Samples the process working set and decides when memory pressure warrants
  // reclaiming background tabs. Always constructed; only polled when the
  // low_ram_mode setting is enabled.
  std::unique_ptr<memory::MemoryMonitor> memory_monitor_;
  bool downloads_overlay_had_active_ = false;
  bool downloads_overlay_user_dismissed_ = false;
  uint64_t downloads_last_sequence_seen_ = 0;
  // Transient context menu state
  std::pair<int, int> pending_ctx_position_ = {0, 0};
  ultralight::String pending_ctx_info_json_;
  // Which view the context menu operates on (0=none,1=UI overlay,2=page/tab)
  int ctx_target_ = 0;
  // Transient suggestions state
  std::pair<int, int> pending_sugg_position_ = {0, 0};
  int pending_sugg_width_ = 0;
  ultralight::String pending_sugg_json_;

  std::map<uint64_t, std::unique_ptr<Tab>> tabs_;
  std::map<uint64_t, std::unique_ptr<drm::DRMWebViewTab>> drm_tabs_;
  // Monotonic activation counter used to find the least-recently-used tab.
  // Map order is by tab id, which says nothing about recency, so low-RAM mode
  // needs its own ordering.
  std::map<uint64_t, uint64_t> tab_last_active_seq_;
  uint64_t tab_activation_counter_ = 0;
  // Raised by the watchdog thread, consumed on the UI thread. Using an atomic
  // flag keeps every view mutation on the thread that created the App, which
  // Ultralight requires.
  std::atomic<bool> memory_pressure_pending_{false};
  std::atomic<bool> memory_watchdog_stop_{false};
  std::thread memory_watchdog_;
  std::map<uint64_t, std::string> drm_tab_titles_;
  std::map<uint64_t, std::string> drm_tab_urls_;
  uint64_t active_tab_id_ = 0;
  // Track the most recently active non-settings tab so we can reload it
  // when the settings tab is active and requests a reload.
  uint64_t last_non_settings_tab_id_ = 0;
  uint64_t tab_id_counter_ = 0;
  Cursor cur_cursor_;
  bool is_resizing_inspector_;
  bool is_over_inspector_resize_drag_handle_;
  int inspector_resize_begin_height_;
  int inspector_resize_begin_mouse_y_;
  bool address_bar_is_focused_ = false;
  bool adblock_enabled_cached_ = true;
  bool suggestions_enabled_ = true;
  bool show_download_badge_ = true;
  bool auto_open_download_panel_ = true;
  bool clear_history_on_exit_ = false;
  bool experimental_transparent_toolbar_enabled_ = false;
  bool experimental_compact_tabs_enabled_ = false;
  bool reduce_motion_enabled_ = false;
  bool high_contrast_ui_enabled_ = false;
  bool vibrant_window_theme_enabled_ = false;
  bool smooth_scrolling_enabled_ = true;

  // Session restore state
  bool session_restore_pending_ = false;     // True if we should prompt to restore session
  bool session_was_clean_exit_ = false;      // True if last exit was clean (not crash/ALT+F4)
  bool session_restore_bar_visible_ = false; // True while restore bar is shown (prevents session saving)
  std::string session_file_path_;            // Path to session.json

  JSFunction updateBack;
  JSFunction updateForward;
  JSFunction updateLoading;
  JSFunction updateURL;
  JSFunction addTab;
  JSFunction updateTab;
  JSFunction closeTab;
  JSFunction focusAddressBar;
  JSFunction isAddressBarFocused;
  JSFunction updateAdblockEnabled;
  JSFunction setTabDrmState;
  JSFunction applySettings;
  JSFunction applySettingsPanel;
  // Context menu setup function in overlay view
  JSFunction setupContextMenu;
  JSFunction setupSuggestions;

  // Cache favicon URL per site origin so multiple tabs/pages reuse it
  // Key: origin string (eg, https://example.com), Value: favicon URL
  std::map<std::string, std::string> favicon_cache_;
  // Disk-persisted favicon file cache (origin -> file path)
  std::map<std::string, std::string> favicon_file_cache_;
  size_t favicon_cache_limit_ = 128;

  // Simple in-memory history
  struct HistoryEntry
  {
    std::string url;
    std::string title;
    uint64_t timestamp_ms;
    uint32_t visit_count;
  };
  std::vector<HistoryEntry> history_;
  // Always enabled (disable-history feature removed)

  // Popular sites loaded from assets/popular_sites.json
  std::vector<std::string> popular_sites_;

  // Suggestions favicons toggle (read from assets/suggestions_favicons.txt: on/off)
  bool suggestion_favicons_enabled_ = true;
  void LoadSuggestionsFaviconsFlag();

  // Check if URL is a browser internal page (settings, history, etc.)
  static bool IsBrowserInternalPage(const std::string &url);

  // Auto Dark Mode state
  bool dark_mode_enabled_ = false;
  void ApplyDarkModeToView(RefPtr<View> v);
  void RemoveDarkModeFromView(RefPtr<View> v);

  // Accessibility CSS injections
  void ApplyReduceMotionToView(RefPtr<View> v);
  void RemoveReduceMotionFromView(RefPtr<View> v);
  void ApplyHighContrastToView(RefPtr<View> v);
  void RemoveHighContrastFromView(RefPtr<View> v);

  // Performance CSS injections
  void ApplySmoothScrollingToView(RefPtr<View> v);
  void RemoveSmoothScrollingFromView(RefPtr<View> v);
  // True when smooth scrolling should actually be in effect. Reduce-motion takes
  // precedence over the smooth-scrolling toggle, so this is the single place
  // that decides. The two settings must never be applied independently: each
  // injects a competing `scroll-behavior` rule marked !important, so whichever
  // was injected last would silently win and the effective behaviour would
  // depend on injection order rather than on what the user asked for.
  bool ShouldUseSmoothScrolling() const;

  // Window appearance
  void ApplyVibrantWindowTheme(bool enabled);
  void ApplyTransparentToolbar(bool enabled);
  void RemoveTransparentToolbar();

  // Cached page HTML for instant loading (avoids file I/O delay)
  std::string cached_start_page_html_;
  std::unordered_map<std::string, std::string> cached_internal_pages_;
  void LoadCachedStartPage();
  void LoadCachedInternalPages();
  // Get cached HTML for a file:/// URL, or empty if not cached
  const std::string &GetCachedPageHTML(const std::string &url) const;

  // Cached user agent string currently applied to outgoing requests.
  std::string active_user_agent_;
  // Compute a Chromium-like user agent string approximating the host platform.
  std::string BuildDefaultChromiumUserAgent() const;

  // Shortcuts mapping (eg, "Ctrl+T" -> "new-tab") loaded from assets/shortcuts.json
  std::map<std::string, std::string> shortcuts_;
  void LoadShortcuts();
  bool RunShortcutAction(const std::string &action);

  BrowserSettings settings_;

  BrowserSettings saved_settings_;
  bool settings_dirty_ = false;
  std::string settings_storage_path_;

  drm::DRMSettings drm_settings_;
  std::unique_ptr<drm::DRMWebViewManager> drm_manager_;
  bool drm_install_running_ = false;
  bool drm_dependencies_installed_cached_ = false;
  std::optional<bool> drm_last_install_result_;
  std::deque<std::string> drm_log_lines_;

  void EnsureDrmManager();
  bool MaybeOpenDrmTab(uint64_t tab_id, const std::string &url, bool user_initiated);
  void HandleDrmTitleChanged(uint64_t tab_id, const std::string &title);
  void HandleDrmUrlChanged(uint64_t tab_id, const std::string &url);
  void HandleDrmLoading(uint64_t tab_id, bool is_loading);
  void HandleDrmNavigationState(uint64_t tab_id, bool can_back, bool can_forward);
  void AppendDrmLog(const std::string &line);
  Tab *GetUltralightTab(uint64_t id);
  drm::DRMWebViewTab *GetDrmTab(uint64_t id);
  void HideDrmTab(uint64_t id);
  void HideAllDrmTabs();
  void UpdateDrmBadge(uint64_t id, bool is_drm);

  friend class Tab;
  friend class SettingsManager;
};

// Datatype used at runtime to represent a settings catalog entry.
//
// Boolean settings use `member`/`default_value`. Integer settings set
// `is_number` and use `number_member` instead; the two forms are mutually
// exclusive so a descriptor always maps to exactly one field.
struct RuntimeSettingDescriptor
{
  std::string key;
  std::string name;
  std::string description;
  std::string category;
  std::string note;
  bool reload_page = false;
  bool UI::BrowserSettings::*member = nullptr;
  bool default_value = false;

  bool is_number = false;
  int UI::BrowserSettings::*number_member = nullptr;
  int number_default = 0;
  // Inclusive bounds applied to incoming values. min_value == max_value means
  // "no range restriction" is not assumed; callers should always clamp.
  int min_value = 0;
  int max_value = 0;
  int step = 1;
};

// Accessors for the runtime settings catalog. Implemented in UI.cpp.
const std::vector<RuntimeSettingDescriptor> &GetSettingsCatalog();
const RuntimeSettingDescriptor *FindSettingDescriptor(const std::string &key);
