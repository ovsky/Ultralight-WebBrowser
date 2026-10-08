#include "Tab.h"
#include "UI.h"
#include "Utils.h"
#include "DownloadManager.h"
#include "ExtensionManager.h"
#include "AdBlocker.h"
#include "PasswordManager.h"
#include "BookmarkStore.h"
#include <iostream>
#include <string>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#define INSPECTOR_DRAG_HANDLE_HEIGHT 10

Tab::Tab(UI *ui, uint64_t id, uint32_t width, uint32_t height, int x, int y,
         const std::string &user_agent, const TabViewSettings &view_settings)
    : ui_(ui), id_(id), container_width_(width), container_height_(height)
{
  // Create a ViewConfig with the user agent - always set one
  ultralight::ViewConfig cfg;
  cfg.initial_device_scale = ui->window_->scale();

  // Apply view settings from browser settings
  cfg.enable_javascript = view_settings.enable_javascript;

  // Match acceleration/display settings with main UI view to avoid GPU driver issues
  // But allow user to override via settings (hardware_acceleration)
  if (ui->overlay_ && ui->overlay_->view())
  {
    // Use hardware acceleration if both the main UI supports it AND user has it enabled
    cfg.is_accelerated = ui->overlay_->view()->is_accelerated() && view_settings.hardware_acceleration;
    cfg.display_id = ui->overlay_->view()->display_id();
  }

  // Always set a user agent - use provided one or fall back to a Chromium-like default
  if (!user_agent.empty())
  {
    cfg.user_agent = String(user_agent.c_str());
  }
  else
  {
    // Fallback default user agent with Chrome and Safari identifiers
    // Using Chrome 131 which is a real stable version (as of late 2024/early 2025)
    cfg.user_agent = String("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
  }

  // Create the view with the custom config
  auto renderer = App::instance()->renderer();
  auto view = renderer->CreateView(width, height, cfg, nullptr);

  // Create overlay wrapping the view
  overlay_ = Overlay::Create(ui->window_, view, x, y);
  this->view()->set_view_listener(this);
  this->view()->set_load_listener(this);
  this->view()->set_download_listener(ui->download_manager());
  // Connect the network listener for ad/tracker blocking (if available)
  if (ui->network_blocker())
  {
    this->view()->set_network_listener(ui->network_blocker());
  }
}

Tab::~Tab()
{
  view()->set_view_listener(nullptr);
  view()->set_load_listener(nullptr);
  view()->set_download_listener(nullptr);
  view()->set_network_listener(nullptr);
}

void Tab::Show()
{
  overlay_->Show();
  overlay_->Focus();

  if (inspector_overlay_)
  {
    inspector_overlay_->Show();
    inspector_overlay_->Focus();
  }
}

void Tab::Hide()
{
  overlay_->Hide();
  overlay_->Unfocus();

  if (inspector_overlay_)
    inspector_overlay_->Hide();
}

void Tab::ToggleInspector()
{
  if (!inspector_overlay_)
  {
    // Lightweight Quick Inspector: create our own overlay and load local UI
    inspector_overlay_ = Overlay::Create(ui_->window_, container_width_, container_height_ / 3, 0, 0);
    // Re-layout so the content overlay makes room and inspector is at the bottom
    Resize(container_width_, container_height_);
    inspector_overlay_->Show();
    auto iv = inspector_overlay_->view();
    if (iv)
    {
      iv->set_load_listener(this);
      iv->set_view_listener(this);
      iv->set_download_listener(ui_->download_manager());
      iv->LoadURL("file:///quick-inspector.html");
    }
  }
  else
  {
    if (inspector_overlay_->is_hidden())
    {
      inspector_overlay_->Show();
      inspector_overlay_->Focus();
      // Ensure layout updates so inspector sits below content
      Resize(container_width_, container_height_);
    }
    else
    {
      inspector_overlay_->Hide();
      // Restore full content height when inspector is hidden
      Resize(container_width_, container_height_);
    }
  }

  // Force resize to update layout
}

bool Tab::IsInspectorShowing() const
{
  if (!inspector_overlay_)
    return false;

  return !inspector_overlay_->is_hidden();
}

IntRect Tab::GetInspectorResizeDragHandle() const
{
  if (!IsInspectorShowing())
    return IntRect::MakeEmpty();

  int drag_handle_height_px = (uint32_t)std::round(INSPECTOR_DRAG_HANDLE_HEIGHT * ui_->window()->scale());

  // This drag handle should span the width of the UI and be centered vertically at the boundary between
  // the page overlay and inspector overlay.

  int drag_handle_x = (int)inspector_overlay_->x();
  int drag_handle_y = (int)inspector_overlay_->y() - drag_handle_height_px / 2;

  return {drag_handle_x, drag_handle_y, drag_handle_x + (int)inspector_overlay_->width(),
          drag_handle_y + drag_handle_height_px};
}

int Tab::GetInspectorHeight() const
{
  if (inspector_overlay_)
    return inspector_overlay_->height();

  return 0;
}

void Tab::SetInspectorHeight(int height)
{
  if (height > 2)
  {
    inspector_overlay_->Resize(inspector_overlay_->width(), height);

    // Trigger a resize to perform re-layout / re-size of content overlay
    Resize(container_width_, container_height_);
  }
}

void Tab::Resize(uint32_t width, uint32_t height)
{
  container_width_ = width;
  container_height_ = height;

  uint32_t content_height = container_height_;
  if (inspector_overlay_ && !inspector_overlay_->is_hidden())
  {
    content_height -= inspector_overlay_->height();
  }

  if (content_height < 1)
    content_height = 1;

  overlay_->Resize(container_width_, content_height);

  if (inspector_overlay_ && !inspector_overlay_->is_hidden())
  {
    inspector_overlay_->MoveTo(0, overlay_->y() + overlay_->height());
    inspector_overlay_->Resize(container_width_, inspector_overlay_->height());
  }
}

void Tab::MoveTo(uint32_t x, uint32_t y)
{
  overlay_->MoveTo(x, y);
  // Re-layout any inspector overlay relative to new content position
  Resize(container_width_, container_height_);
}

void Tab::OnChangeTitle(View *caller, const String &title)
{
  ui_->UpdateTabTitle(id_, title);
}

void Tab::OnChangeURL(View *caller, const String &url)
{
  ui_->UpdateTabURL(id_, url);
  // Record history when the page URL changes (navigation start/change), not on load finish
  if (ui_)
  {
    ui_->RecordHistory(url, caller->title());
  }
}

void Tab::OnChangeTooltip(View *caller, const String &tooltip) {}

void Tab::OnChangeCursor(View *caller, Cursor cursor)
{
  if (id_ == ui_->active_tab_id_)
    ui_->SetCursor(cursor);
}

void Tab::OnAddConsoleMessage(View *caller, const ConsoleMessage &msg)
{
  // Log console messages to stderr for debugging
  String smsg = msg.message();
  auto u = smsg.utf8();
  std::string m = u.data() ? u.data() : "";
  auto src = msg.source_id().utf8();
  std::string source = src.data() ? src.data() : "";

  const char *level_str = "LOG";
  switch (msg.level())
  {
  case kMessageLevel_Warning:
    level_str = "WARN";
    break;
  case kMessageLevel_Error:
    level_str = "ERROR";
    break;
  case kMessageLevel_Debug:
    level_str = "DEBUG";
    break;
  case kMessageLevel_Info:
    level_str = "INFO";
    break;
  default:
    break;
  }
  std::fprintf(stderr, "[CONSOLE:%s] %s (line %u, %s)\n", level_str, m.c_str(), msg.line_number(), source.c_str());

  // Forward console messages to Quick Inspector if visible
  if (inspector_overlay_ && !inspector_overlay_->is_hidden())
  {
    auto iv = inspector_overlay_->view();
    if (iv)
    {
      // Minimal escaping for safe JS string literal
      std::string js = std::string("(function(m){ if(window.__qi && __qi.onConsole){ __qi.onConsole({message:m}); } })(\"") + util::EscapeJsStringLiteral(m) + "\")";
      iv->EvaluateScript(String(js.c_str()), nullptr);
    }
  }
}

RefPtr<View> Tab::OnCreateChildView(ultralight::View *caller,
                                    const String &opener_url, const String &target_url,
                                    bool is_popup, const IntRect &popup_rect)
{
  return ui_->CreateNewTabForChildView(target_url);
}

RefPtr<View> Tab::OnCreateInspectorView(ultralight::View *caller, bool is_local,
                                        const String &inspected_url)
{
  // For compatibility: if devtools requests an inspector view, host it in our overlay.
  if (!inspector_overlay_)
  {
    inspector_overlay_ = Overlay::Create(ui_->window_, container_width_, container_height_ / 2, 0, 0);
    // Force resize to update layout
    Resize(container_width_, container_height_);
    inspector_overlay_->Show();
  }
  auto iv = inspector_overlay_->view();
  if (iv)
    iv->set_download_listener(ui_->download_manager());
  return iv;
}

void Tab::OnBeginLoading(View *caller, uint64_t frame_id, bool is_main_frame, const String &url)
{
  ui_->UpdateTabNavigation(id_, caller->is_loading(), caller->CanGoBack(), caller->CanGoForward());
}

void Tab::OnFinishLoading(View *caller, uint64_t frame_id, bool is_main_frame, const String &url)
{
  ui_->UpdateTabNavigation(id_, caller->is_loading(), caller->CanGoBack(), caller->CanGoForward());
}

void Tab::OnFailLoading(View *caller, uint64_t frame_id, bool is_main_frame, const String &url,
                        const String &description, const String &error_domain, int error_code)
{
  if (is_main_frame)
  {
    char error_code_str[16];
    std::snprintf(error_code_str, sizeof(error_code_str), "%d", error_code);

    String html_string = "<html><head><style>";
    html_string += "* { font-family: sans-serif; }";
    html_string += "body { background-color: #CCC; color: #555; padding: 4em; }";
    html_string += "dt { font-weight: bold; padding: 1em; }";
    html_string += "</style></head><body>";
    html_string += "<h2>A Network Error was Encountered</h2>";
    html_string += "<dl>";
    html_string += "<dt>URL</dt><dd>" + url + "</dd>";
    html_string += "<dt>Description</dt><dd>" + description + "</dd>";
    html_string += "<dt>Error Domain</dt><dd>" + error_domain + "</dd>";
    html_string += "<dt>Error Code</dt><dd>" + String(error_code_str) + "</dd>";
    html_string += "</dl></body></html>";

    view()->LoadHTML(html_string);
  }
}

void Tab::OnUpdateHistory(View *caller)
{
  ui_->UpdateTabNavigation(id_, caller->is_loading(), caller->CanGoBack(), caller->CanGoForward());
}

void Tab::OnWindowObjectReady(View *caller, uint64_t frame_id, bool is_main_frame, const String &url)
{
  // Inject Web Crypto API polyfill and XHR fixes if needed
  // This is needed for sites like Facebook that use SubtleCrypto for authentication
  if (is_main_frame)
  {
    // First inject XHR fix to ensure credentials are sent with requests
    // This helps with same-origin requests that might fail due to CORS quirks
    const char *xhrFix = R"JS(
(function() {
  'use strict';
  // Patch XMLHttpRequest to always include credentials for same-origin requests
  var originalXHROpen = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function(method, url, async, user, password) {
    var result = originalXHROpen.apply(this, arguments);
    // Enable credentials for same-origin requests
    try {
      var urlObj = new URL(url, window.location.origin);
      if (urlObj.origin === window.location.origin) {
        this.withCredentials = true;
      }
    } catch(e) {
      // If URL parsing fails, try enabling credentials anyway for relative URLs
      if (url && !url.startsWith('http')) {
        this.withCredentials = true;
      }
    }
    return result;
  };

  // Also patch fetch to include credentials
  var originalFetch = window.fetch;
  window.fetch = function(input, init) {
    init = init || {};
    // Default to same-origin credentials if not specified
    if (!init.credentials) {
      init.credentials = 'same-origin';
    }
    return originalFetch.call(this, input, init);
  };

  console.log('[Ultralight] XHR/Fetch credentials fix loaded');
})();
)JS";
    caller->EvaluateScript(String(xhrFix), nullptr);

    // Check if crypto.subtle exists and provide a basic polyfill if not
    // Note: This is a minimal polyfill - real crypto operations may not work correctly
    // but it prevents "undefined is not an object" errors
    const char *cryptoPolyfill = R"JS(
(function() {
  'use strict';
  if (typeof window.crypto === 'undefined') {
    window.crypto = {};
  }
  if (typeof window.crypto.subtle === 'undefined') {
    // Minimal SubtleCrypto polyfill to prevent errors
    // This won't provide real cryptographic security but allows pages to load
    window.crypto.subtle = {
      generateKey: function(algorithm, extractable, keyUsages) {
        return Promise.resolve({
          algorithm: algorithm,
          extractable: extractable,
          usages: keyUsages,
          type: 'secret'
        });
      },
      encrypt: function(algorithm, key, data) {
        // Return data as-is (no real encryption)
        return Promise.resolve(data);
      },
      decrypt: function(algorithm, key, data) {
        return Promise.resolve(data);
      },
      sign: function(algorithm, key, data) {
        // Return a mock signature
        var arr = new Uint8Array(32);
        for (var i = 0; i < 32; i++) arr[i] = Math.floor(Math.random() * 256);
        return Promise.resolve(arr.buffer);
      },
      verify: function(algorithm, key, signature, data) {
        return Promise.resolve(true);
      },
      digest: function(algorithm, data) {
        // Return a mock hash
        var arr = new Uint8Array(32);
        for (var i = 0; i < 32; i++) arr[i] = Math.floor(Math.random() * 256);
        return Promise.resolve(arr.buffer);
      },
      importKey: function(format, keyData, algorithm, extractable, keyUsages) {
        return Promise.resolve({
          algorithm: algorithm,
          extractable: extractable,
          usages: keyUsages,
          type: 'secret'
        });
      },
      exportKey: function(format, key) {
        return Promise.resolve(new ArrayBuffer(32));
      },
      deriveBits: function(algorithm, baseKey, length) {
        var arr = new Uint8Array(length / 8);
        for (var i = 0; i < arr.length; i++) arr[i] = Math.floor(Math.random() * 256);
        return Promise.resolve(arr.buffer);
      },
      deriveKey: function(algorithm, baseKey, derivedKeyAlgorithm, extractable, keyUsages) {
        return Promise.resolve({
          algorithm: derivedKeyAlgorithm,
          extractable: extractable,
          usages: keyUsages,
          type: 'secret'
        });
      },
      wrapKey: function(format, key, wrappingKey, wrapAlgorithm) {
        return Promise.resolve(new ArrayBuffer(32));
      },
      unwrapKey: function(format, wrappedKey, unwrappingKey, unwrapAlgorithm, unwrappedKeyAlgorithm, extractable, keyUsages) {
        return Promise.resolve({
          algorithm: unwrappedKeyAlgorithm,
          extractable: extractable,
          usages: keyUsages,
          type: 'secret'
        });
      }
    };
    console.log('[Ultralight] Web Crypto API polyfill loaded');
  }
  // Also ensure crypto.getRandomValues exists
  if (typeof window.crypto.getRandomValues === 'undefined') {
    window.crypto.getRandomValues = function(array) {
      for (var i = 0; i < array.length; i++) {
        array[i] = Math.floor(Math.random() * 256);
      }
      return array;
    };
  }
})();
)JS";
    caller->EvaluateScript(String(cryptoPolyfill), nullptr);

    // Inject location spoofing if enabled in settings
    // This overrides navigator.geolocation to report custom coordinates
    if (ui_->location_spoofing_enabled())
    {
      double lat = ui_->spoofed_latitude();
      double lng = ui_->spoofed_longitude();
      std::ostringstream geoScript;
      geoScript << R"JS(
(function() {
  'use strict';
  var spoofedLat = )JS"
                << lat << R"JS(;
  var spoofedLng = )JS"
                << lng << R"JS(;

  // Create a fake GeolocationPosition object
  function createPosition() {
    return {
      coords: {
        latitude: spoofedLat,
        longitude: spoofedLng,
        accuracy: 10,
        altitude: null,
        altitudeAccuracy: null,
        heading: null,
        speed: null
      },
      timestamp: Date.now()
    };
  }

  // Override getCurrentPosition
  var originalGetCurrentPosition = navigator.geolocation.getCurrentPosition;
  navigator.geolocation.getCurrentPosition = function(success, error, options) {
    console.log('[Ultralight] Geolocation spoofed to:', spoofedLat, spoofedLng);
    setTimeout(function() {
      success(createPosition());
    }, 100);
  };

  // Override watchPosition
  var watchId = 0;
  var watches = {};
  navigator.geolocation.watchPosition = function(success, error, options) {
    var id = ++watchId;
    console.log('[Ultralight] Geolocation watch spoofed to:', spoofedLat, spoofedLng);
    watches[id] = setInterval(function() {
      success(createPosition());
    }, 1000);
    return id;
  };

  // Override clearWatch
  navigator.geolocation.clearWatch = function(id) {
    if (watches[id]) {
      clearInterval(watches[id]);
      delete watches[id];
    }
  };

  console.log('[Ultralight] Location spoofing enabled:', spoofedLat, spoofedLng);
})();
)JS";
      caller->EvaluateScript(String(geoScript.str().c_str()), nullptr);
    }

    // Inject Do Not Track (DNT) header simulation if enabled in settings
    // This overrides navigator.doNotTrack to report the user's preference
    if (ui_->do_not_track_enabled())
    {
      const char *dntScript = R"JS(
(function() {
  'use strict';
  // Set Do Not Track property to '1' (enabled)
  // This tells websites the user prefers not to be tracked
  try {
    Object.defineProperty(Navigator.prototype, 'doNotTrack', {
      value: '1',
      writable: false,
      configurable: false,
      enumerable: true
    });
    // Also set the older msDoNotTrack property for IE compatibility
    if (typeof navigator.msDoNotTrack === 'undefined') {
      Object.defineProperty(Navigator.prototype, 'msDoNotTrack', {
        value: '1',
        writable: false,
        configurable: false,
        enumerable: true
      });
    }
    console.log('[Ultralight] Do Not Track enabled');
  } catch(e) {
    console.warn('[Ultralight] Failed to set DNT:', e);
  }
})();
)JS";
      caller->EvaluateScript(String(dntScript), nullptr);
    }

    // Block third-party cookie access if enabled in settings
    // This is a best-effort approach since true cookie blocking requires network-level control
    if (ui_->block_third_party_cookies_enabled())
    {
      const char *cookieBlockScript = R"JS(
(function() {
  'use strict';
  // Monitor and log third-party cookie attempts
  var originalDescriptor = Object.getOwnPropertyDescriptor(Document.prototype, 'cookie');
  var topOrigin = window.top.location.origin;

  Object.defineProperty(document, 'cookie', {
    get: function() {
      return originalDescriptor.get.call(this);
    },
    set: function(value) {
      try {
        // Check if this is a cross-origin iframe attempting to set cookies
        if (window !== window.top) {
          var currentOrigin = window.location.origin;
          if (currentOrigin !== topOrigin) {
            console.warn('[Ultralight] Blocked third-party cookie set from:', currentOrigin);
            return; // Block the cookie
          }
        }
      } catch(e) {
        // Cross-origin frame access might throw, in which case this is third-party
        console.warn('[Ultralight] Blocked third-party cookie set (cross-origin iframe)');
        return;
      }
      return originalDescriptor.set.call(this, value);
    },
    configurable: true,
    enumerable: true
  });
  console.log('[Ultralight] Third-party cookie blocking enabled');
})();
)JS";
      caller->EvaluateScript(String(cookieBlockScript), nullptr);
    }
  }
}

void Tab::OnDOMReady(View *caller, uint64_t frame_id, bool is_main_frame, const String &url)
{
  // Install hooks for all frames (main and subframes)

  // Bind a native JS callback that the page can call when right-click occurs (main frame only)
  if (is_main_frame)
  {
    auto url_utf8 = url.utf8();

    RefPtr<JSContext> ctx = caller->LockJSContext();
    SetJSContext(ctx->ctx());
    JSObject global = JSGlobalObject();
    global["NativeOpenContextMenu"] = BindJSCallback(&Tab::OnOpenContextMenu);

    // Check if this is the settings page
    bool is_settings_page = url_utf8.data() && std::strstr(url_utf8.data(), "settings.html") != nullptr;
    bool is_extensions_page = url_utf8.data() && std::strstr(url_utf8.data(), "extensions.html") != nullptr;
    bool is_passwords_page = url_utf8.data() && std::strstr(url_utf8.data(), "passwords.html") != nullptr;

    if (is_settings_page)
    {
      // Bind settings bridge functions when settings page loads in a tab
      global["GetSettingsSnapshot"] = BindJSCallbackWithRetval(&Tab::JS_GetSettingsSnapshot);
      global["OnUpdateSetting"] = BindJSCallback(&Tab::JS_UpdateSetting);
      global["OnSaveSettings"] = BindJSCallback(&Tab::JS_SaveSettings);
      global["OnRestoreSettingsDefaults"] = BindJSCallbackWithRetval(&Tab::JS_RestoreSettingsDefaults);
      global["GetDrmStatus"] = BindJSCallbackWithRetval(&Tab::JS_GetDrmStatus);
      global["InstallDrmDependencies"] = BindJSCallbackWithRetval(&Tab::JS_InstallDrmDependencies);
      // settings page detected with bridge functions bound
    }

    if (is_extensions_page)
    {
      // Bind extensions bridge functions when extensions page loads in a tab
      global["GetExtensions"] = BindJSCallbackWithRetval(&Tab::JS_GetExtensions);
      global["OnToggleExtension"] = BindJSCallback(&Tab::JS_ToggleExtension);
      global["OnReloadExtension"] = BindJSCallback(&Tab::JS_ReloadExtension);
      global["OnReloadAllExtensions"] = BindJSCallback(&Tab::JS_ReloadAllExtensions);
      global["OnDeleteExtension"] = BindJSCallback(&Tab::JS_DeleteExtension);
      global["OnLoadExtension"] = BindJSCallback(&Tab::JS_LoadExtension);
      global["OnCreateExtension"] = BindJSCallback(&Tab::JS_CreateExtension);
      global["OnOpenExtensionsFolder"] = BindJSCallback(&Tab::JS_OpenExtensionsFolder);
    }

    if (is_passwords_page)
    {
      // Bind password manager functions when passwords page loads in a tab
      global["getPasswords"] = BindJSCallbackWithRetval(&Tab::JS_GetPasswords);
      global["getPasswordStats"] = BindJSCallbackWithRetval(&Tab::JS_GetPasswordStats);
      global["savePassword"] = BindJSCallback(&Tab::JS_SavePassword);
      global["deletePassword"] = BindJSCallback(&Tab::JS_DeletePassword);
      global["getDecryptedPassword"] = BindJSCallbackWithRetval(&Tab::JS_GetDecryptedPassword);
      global["savePasswordSettings"] = BindJSCallback(&Tab::JS_SavePasswordSettings);
      global["exportPasswords"] = BindJSCallback(&Tab::JS_ExportPasswords);
      global["importPasswords"] = BindJSCallback(&Tab::JS_ImportPasswords);
      global["isDarkModeEnabled"] = BindJSCallbackWithRetval(&Tab::JS_IsDarkModeEnabled);

      // Notify the page that native bindings are ready, so it can reload passwords
      caller->EvaluateScript("(function(){ if(typeof loadPasswords === 'function') loadPasswords(); })();", nullptr);
    }

    // Expose a unified native bridge on window.__ul using global function proxies
    global["__ul_back"] = BindJSCallback(&Tab::JS_Back);
    global["__ul_forward"] = BindJSCallback(&Tab::JS_Forward);
    global["__ul_reload"] = BindJSCallback(&Tab::JS_Reload);
    global["__ul_stop"] = BindJSCallback(&Tab::JS_Stop);
    global["__ul_navigate"] = BindJSCallback(&Tab::JS_Navigate);
    global["__ul_newTab"] = BindJSCallback(&Tab::JS_NewTab);
    global["__ul_closeTab"] = BindJSCallback(&Tab::JS_CloseTab);
    global["__ul_openHistory"] = BindJSCallback(&Tab::JS_OpenHistory);
    global["__ul_getHistory"] = BindJSCallbackWithRetval(&Tab::JS_GetHistory);
    global["__ul_clearHistory"] = BindJSCallback(&Tab::JS_ClearHistory);
    global["__ul_toggleDarkMode"] = BindJSCallback(&Tab::JS_ToggleDarkMode);
    global["__ul_isDarkModeEnabled"] = BindJSCallbackWithRetval(&Tab::JS_IsDarkModeEnabled);
    global["__ul_getAppInfo"] = BindJSCallbackWithRetval(&Tab::JS_GetAppInfo);

    // Bookmark bridge functions
    global["getBookmarks"] = BindJSCallbackWithRetval(&Tab::JS_GetBookmarks);
    global["getBookmarkBar"] = BindJSCallbackWithRetval(&Tab::JS_GetBookmarkBar);
    global["addBookmark"] = BindJSCallbackWithRetval(&Tab::JS_AddBookmark);
    global["removeBookmark"] = BindJSCallback(&Tab::JS_RemoveBookmark);
    global["isBookmarked"] = BindJSCallbackWithRetval(&Tab::JS_IsBookmarked);
    global["toggleBookmark"] = BindJSCallback(&Tab::JS_ToggleBookmark);
    global["reorderBookmarks"] = BindJSCallback(&Tab::JS_ReorderBookmarks);
    global["updateBookmark"] = BindJSCallbackWithRetval(&Tab::JS_UpdateBookmark);

    const char *attachScript = R"JS((function(){
      try{
        var n = (window.__ul = window.__ul || {});
        n.back = window.__ul_back;
        n.forward = window.__ul_forward;
        n.reload = window.__ul_reload;
        n.stop = window.__ul_stop;
        n.navigate = window.__ul_navigate;
        n.newTab = window.__ul_newTab;
        n.closeTab = window.__ul_closeTab;
        n.openHistory = window.__ul_openHistory;
        n.getHistory = window.__ul_getHistory;
        n.clearHistory = window.__ul_clearHistory;
        n.toggleDarkMode = window.__ul_toggleDarkMode;
        n.isDarkModeEnabled = window.__ul_isDarkModeEnabled;
        n.getAppInfo = window.__ul_getAppInfo;
        // Trigger bookmark loading if function exists (with delay to ensure page JS is loaded)
        setTimeout(function() {
          if(typeof loadBookmarks === 'function') loadBookmarks();
        }, 50);
      }catch(e){}
    })())JS";
    caller->EvaluateScript(attachScript, nullptr);
  }

  // Inject a contextmenu handler into the page to capture link/image/selection info
  const char *script = R"JS(
    (function(){
      try {
        if (window.__ul_ctxmenu_installed) return;
        window.__ul_ctxmenu_installed = true;
        document.addEventListener('contextmenu', function(e){
          try {
            e.preventDefault();
            var t = e.target;
            var a = t && t.closest ? t.closest('a[href]') : null;
            var img = t && t.closest ? t.closest('img[src]') : null;
            var sel = '';
            try { sel = String(window.getSelection ? window.getSelection() : ''); } catch(_) {}
            var info = {
              linkURL: a && a.href ? a.href : '',
              imageURL: img && img.src ? img.src : '',
              selectionText: sel || '',
              isEditable: !!(t && (t.isContentEditable || (t.tagName==='INPUT' || t.tagName==='TEXTAREA')))
            };
            if (window.NativeOpenContextMenu) {
              window.NativeOpenContextMenu(e.clientX, e.clientY, JSON.stringify(info));
            }
          } catch (err) {
          }
        }, true);
      } catch (err) {
      }
    })();
  )JS";
  if (is_main_frame)
    caller->EvaluateScript(script, nullptr);

  // Lightweight network monitor (fetch / XHR)
  if (is_main_frame)
  {
    RefPtr<JSContext> ctx = caller->LockJSContext();
    SetJSContext(ctx->ctx());
    JSObject global = JSGlobalObject();
    global["NativeNetworkEvent"] = BindJSCallback(&Tab::OnNetworkEvent);
    const char *netPatch = R"JS((function(){
      if (window.__ul_net_patched) return; window.__ul_net_patched = true;
      function now(){ return Date.now(); }
      function report(o){ try{ if(window.NativeNetworkEvent) window.NativeNetworkEvent(JSON.stringify(o)); }catch(e){} }
      var ofetch = window.fetch;
      if (ofetch) {
        window.fetch = function(input, init){
          var start = now();
          var url = (typeof input==='string') ? input : ((input && input.url) || '');
          return ofetch.apply(this, arguments).then(function(resp){
            try{ report({type:'fetch', url:url, status: resp.status, ok: !!resp.ok, time: now()-start}); }catch(e){}
            return resp; });
        };
      }
      var xo = XMLHttpRequest.prototype.open, xs = XMLHttpRequest.prototype.send;
      XMLHttpRequest.prototype.open = function(method, url){ this.__ul = {m:method,u:url,s:0}; return xo.apply(this, arguments); };
      XMLHttpRequest.prototype.send = function(){
        var self=this; self.__ul.s = now();
        self.addEventListener('loadend', function(){ try{ report({type:'xhr', url:(self.__ul && self.__ul.u)||'', status:self.status, time: now()-self.__ul.s}); }catch(e){} });
        return xs.apply(this, arguments);
      };
    })())JS";
    caller->EvaluateScript(netPatch, nullptr);
  }

  // Inject extension scripts for matching URLs (only for non-internal pages)
  if (is_main_frame)
  {
    auto url_str = url.utf8();
    const char *c = url_str.data();
    // Skip internal pages (file:/// URLs)
    std::string_view url_view(c ? c : "");
    if (c && (url_view.size() < 7 || url_view.substr(0, 7) != "file://"))
    {
      // Inject favicon fetcher script
      {
        RefPtr<JSContext> ctx = caller->LockJSContext();
        SetJSContext(ctx->ctx());
        JSObject global = JSGlobalObject();
        global["NativeFaviconFetched"] = BindJSCallback(&Tab::OnFaviconFetched);

        const char *faviconScript = R"JS((function(){
          if (window.__ul_favicon_fetched) return;
          window.__ul_favicon_fetched = true;

          function fetchFavicon() {
            var pageUrl = window.location.href;
            var origin = window.location.origin;

            // Try to find favicon in page head
            var iconLinks = document.querySelectorAll('link[rel*="icon"]');
            var faviconUrl = null;

            // Priority: apple-touch-icon > icon > shortcut icon
            for (var i = 0; i < iconLinks.length; i++) {
              var link = iconLinks[i];
              var rel = (link.rel || '').toLowerCase();
              var href = link.href;
              if (href && (rel.indexOf('icon') >= 0)) {
                // Prefer larger icons (apple-touch-icon usually bigger)
                if (rel.indexOf('apple-touch') >= 0 || !faviconUrl) {
                  faviconUrl = href;
                }
              }
            }

            // Fallback to /favicon.ico
            if (!faviconUrl) {
              faviconUrl = origin + '/favicon.ico';
            }

            // Fetch and convert to data URL
            var img = new Image();
            img.crossOrigin = 'anonymous';
            img.onload = function() {
              try {
                var canvas = document.createElement('canvas');
                canvas.width = 16;
                canvas.height = 16;
                var ctx = canvas.getContext('2d');
                ctx.imageSmoothingEnabled = true;
                ctx.drawImage(img, 0, 0, 16, 16);
                var dataUrl = canvas.toDataURL('image/png');
                if (window.NativeFaviconFetched) {
                  window.NativeFaviconFetched(pageUrl, dataUrl);
                }
              } catch(e) {
                // Canvas tainted by cross-origin image
              }
            };
            img.onerror = function() {
              // Try fallback to /favicon.ico if we tried a different icon
              if (faviconUrl !== origin + '/favicon.ico') {
                var fallbackImg = new Image();
                fallbackImg.crossOrigin = 'anonymous';
                fallbackImg.onload = function() {
                  try {
                    var canvas = document.createElement('canvas');
                    canvas.width = 16;
                    canvas.height = 16;
                    var ctx = canvas.getContext('2d');
                    ctx.imageSmoothingEnabled = true;
                    ctx.drawImage(fallbackImg, 0, 0, 16, 16);
                    var dataUrl = canvas.toDataURL('image/png');
                    if (window.NativeFaviconFetched) {
                      window.NativeFaviconFetched(pageUrl, dataUrl);
                    }
                  } catch(e) {}
                };
                fallbackImg.src = origin + '/favicon.ico';
              }
            };
            img.src = faviconUrl;
          }

          // Run after a short delay to let page set up icons
          setTimeout(fetchFavicon, 100);
        })();)JS";
        caller->EvaluateScript(faviconScript, nullptr);
      }

      std::string script_code = extensions::ExtensionManager::Instance().GetContentScriptsForURL(c);
      if (!script_code.empty())
      {
        // Wrap in IIFE to isolate scope
        std::string wrapped = "(function(){\ntry{\n" + script_code + "\n}catch(e){console.error('Extension error:',e);}\n})();";
        caller->EvaluateScript(String(wrapped.c_str()), nullptr);
      }

      // Inject password form detection and autofill script
      {
        RefPtr<JSContext> ctx = caller->LockJSContext();
        SetJSContext(ctx->ctx());
        JSObject global = JSGlobalObject();

        // Bind password manager callbacks
        global["NativePasswordFormDetected"] = BindJSCallback(&Tab::OnPasswordFormDetected);
        global["NativePasswordFormSubmitted"] = BindJSCallback(&Tab::OnPasswordFormSubmitted);
        global["NativeGetPasswordSuggestions"] = BindJSCallbackWithRetval(&Tab::OnGetPasswordSuggestions);
        global["NativePasswordSelected"] = BindJSCallback(&Tab::OnPasswordSelected);
        global["NativePasswordSaveResponse"] = BindJSCallback(&Tab::OnPasswordSaveResponse);

        // Inject the password form detection script
        const char *passwordScript = R"JS((function(){
          if (window.__ul_password_manager_installed) return;
          window.__ul_password_manager_installed = true;

          var origin = window.location.origin;
          var pendingForms = [];
          var lastSubmittedCredentials = null;

          // Find password fields
          function findPasswordFields() {
            return document.querySelectorAll('input[type="password"]');
          }

          // Find associated username field for a password field
          function findUsernameField(passwordField) {
            var form = passwordField.closest('form');
            var fields = form ? form.querySelectorAll('input') : document.querySelectorAll('input');
            var usernameTypes = ['text', 'email', 'tel'];
            var usernameNames = ['user', 'email', 'login', 'name', 'account', 'id'];

            for (var i = 0; i < fields.length; i++) {
              var f = fields[i];
              if (f === passwordField) continue;
              var type = (f.type || '').toLowerCase();
              var name = ((f.name || '') + (f.id || '')).toLowerCase();

              if (usernameTypes.indexOf(type) >= 0) {
                for (var j = 0; j < usernameNames.length; j++) {
                  if (name.indexOf(usernameNames[j]) >= 0) {
                    return f;
                  }
                }
                // If no specific name match, return the first text/email field before password
                if (f.compareDocumentPosition(passwordField) & Node.DOCUMENT_POSITION_FOLLOWING) {
                  return f;
                }
              }
            }
            return null;
          }

          // Create autofill dropdown
          var dropdown = null;
          function showAutofillDropdown(field, suggestions) {
            hideAutofillDropdown();
            if (!suggestions || suggestions.length === 0) return;

            dropdown = document.createElement('div');
            dropdown.className = '__ul_password_dropdown';
            dropdown.style.cssText = 'position:absolute;z-index:999999;background:#fff;border:1px solid #ccc;border-radius:4px;box-shadow:0 2px 10px rgba(0,0,0,0.2);max-height:200px;overflow-y:auto;min-width:200px;';

            var rect = field.getBoundingClientRect();
            dropdown.style.top = (window.scrollY + rect.bottom + 2) + 'px';
            dropdown.style.left = (window.scrollX + rect.left) + 'px';
            dropdown.style.width = Math.max(rect.width, 200) + 'px';

            suggestions.forEach(function(s) {
              var item = document.createElement('div');
              item.style.cssText = 'padding:8px 12px;cursor:pointer;border-bottom:1px solid #eee;';
              item.innerHTML = '<div style="font-weight:500;">' + escapeHtml(s.username) + '</div><div style="font-size:11px;color:#666;">Password saved</div>';
              item.addEventListener('mouseenter', function() { this.style.background = '#f0f0f0'; });
              item.addEventListener('mouseleave', function() { this.style.background = '#fff'; });
              item.addEventListener('click', function(e) {
                e.preventDefault();
                e.stopPropagation();
                if (window.NativePasswordSelected) {
                  window.NativePasswordSelected(s.username, s.password);
                }
                hideAutofillDropdown();
              });
              dropdown.appendChild(item);
            });

            document.body.appendChild(dropdown);
          }

          function hideAutofillDropdown() {
            if (dropdown && dropdown.parentNode) {
              dropdown.parentNode.removeChild(dropdown);
            }
            dropdown = null;
          }

          function escapeHtml(text) {
            var div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
          }

          // Fill form with credentials
          window.__ul_fill_password_form = function(username, password) {
            var pwFields = findPasswordFields();
            pwFields.forEach(function(pwField) {
              var userField = findUsernameField(pwField);
              if (userField) {
                userField.value = username;
                userField.dispatchEvent(new Event('input', {bubbles: true}));
                userField.dispatchEvent(new Event('change', {bubbles: true}));
              }
              pwField.value = password;
              pwField.dispatchEvent(new Event('input', {bubbles: true}));
              pwField.dispatchEvent(new Event('change', {bubbles: true}));
            });
          };

          // Submit credentials to native
          function submitCredentials(username, password) {
            // Avoid duplicate submissions
            var credKey = username + '|' + password;
            if (lastSubmittedCredentials === credKey) return;
            lastSubmittedCredentials = credKey;

            if (username && password && window.NativePasswordFormSubmitted) {
              console.log('[PasswordManager] Submitting credentials for:', origin, username);
              window.NativePasswordFormSubmitted(JSON.stringify({
                origin: origin,
                username: username,
                password: password
              }));
            }
          }

          // Detect and handle password forms
          function setupPasswordFields() {
            var pwFields = findPasswordFields();
            if (pwFields.length === 0) return;

            // Notify native that we found password forms
            if (window.NativePasswordFormDetected) {
              window.NativePasswordFormDetected(JSON.stringify({origin: origin}));
            }

            pwFields.forEach(function(pwField) {
              if (pwField.__ul_pw_setup) return;
              pwField.__ul_pw_setup = true;

              var userField = findUsernameField(pwField);

              // Show autofill dropdown on focus
              function showDropdown(field) {
                if (window.NativeGetPasswordSuggestions) {
                  var suggestionsJson = window.NativeGetPasswordSuggestions(origin);
                  try {
                    var suggestions = JSON.parse(suggestionsJson);
                    if (suggestions && suggestions.length > 0) {
                      showAutofillDropdown(field, suggestions);
                    }
                  } catch(e) {}
                }
              }

              pwField.addEventListener('focus', function() { showDropdown(pwField); });
              if (userField) {
                userField.addEventListener('focus', function() { showDropdown(userField); });
              }

              // Hide dropdown when clicking elsewhere
              document.addEventListener('click', function(e) {
                if (dropdown && !dropdown.contains(e.target) && e.target !== pwField && e.target !== userField) {
                  hideAutofillDropdown();
                }
              });

              // Handle form submission
              var form = pwField.closest('form');
              if (form && !form.__ul_pw_submit_setup) {
                form.__ul_pw_submit_setup = true;

                // Traditional form submit
                form.addEventListener('submit', function(e) {
                  var username = userField ? userField.value : '';
                  var password = pwField.value;
                  submitCredentials(username, password);
                });

                // Also capture click on submit buttons (for JS-based form handling)
                var submitBtns = form.querySelectorAll('button[type="submit"], input[type="submit"], button:not([type])');
                submitBtns.forEach(function(btn) {
                  if (btn.__ul_pw_click_setup) return;
                  btn.__ul_pw_click_setup = true;
                  btn.addEventListener('click', function(e) {
                    // Small delay to let form validation happen
                    setTimeout(function() {
                      var username = userField ? userField.value : '';
                      var password = pwField.value;
                      if (username && password) {
                        submitCredentials(username, password);
                      }
                    }, 100);
                  });
                });
              }

              // Also handle Enter key on password field
              pwField.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
                  setTimeout(function() {
                    var username = userField ? userField.value : '';
                    var password = pwField.value;
                    if (username && password) {
                      submitCredentials(username, password);
                    }
                  }, 100);
                }
              });
            });
          }

          // Run on page load
          if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', setupPasswordFields);
          } else {
            setupPasswordFields();
          }

          // Also watch for dynamically added forms
          var observer = new MutationObserver(function(mutations) {
            var hasNewInputs = mutations.some(function(m) {
              return m.addedNodes.length > 0;
            });
            if (hasNewInputs) {
              setTimeout(setupPasswordFields, 100);
            }
          });
          observer.observe(document.body || document.documentElement, {childList: true, subtree: true});

        })();)JS";

        caller->EvaluateScript(passwordScript, nullptr);
      }
    }
  }

  // If this is our History page, expose native methods
  {
    auto url_u = url.utf8();
    const char *c = url_u.data();
    if (c && std::strstr(c, "history.html"))
    {
      RefPtr<JSContext> ctx = caller->LockJSContext();
      SetJSContext(ctx->ctx());
      JSObject global = JSGlobalObject();
      global["NativeGetHistory"] = BindJSCallbackWithRetval(&Tab::OnHistoryGetData);
      global["NativeClearHistory"] = BindJSCallback(&Tab::OnHistoryClear);
      // Notify the page JS that native bridge is ready so it can refresh now
      caller->EvaluateScript("(function(){ if (window.__ul_history_ready) window.__ul_history_ready(); })();", nullptr);
    }
    else if (c && std::strstr(c, "downloads.html"))
    {
      RefPtr<JSContext> ctx = caller->LockJSContext();
      SetJSContext(ctx->ctx());
      JSObject global = JSGlobalObject();
      global["NativeGetDownloads"] = BindJSCallbackWithRetval(&Tab::OnDownloadsGetData);
      global["NativeClearDownloads"] = BindJSCallback(&Tab::OnDownloadsClear);
      global["NativeOpenDownload"] = BindJSCallback(&Tab::OnDownloadsOpen);
      global["NativeRevealDownload"] = BindJSCallback(&Tab::OnDownloadsReveal);
      caller->EvaluateScript("(function(){ if (window.__ul_downloads_ready) window.__ul_downloads_ready(); })();", nullptr);
      if (ui_)
        ui_->NotifyDownloadsChanged();
    }
    else if (c && std::strstr(c, "quick-inspector.html"))
    {
      RefPtr<JSContext> ctx = caller->LockJSContext();
      SetJSContext(ctx->ctx());
      JSObject global = JSGlobalObject();
      // Bind Quick Inspector callbacks
      global["NativeQuickClose"] = BindJSCallback(&Tab::OnQuickInspectorClose);
      global["NativeQuickOpenDevtools"] = BindJSCallback(&Tab::OnQuickInspectorOpenDevtools);
      global["NativeQuickGetInfo"] = BindJSCallbackWithRetval(&Tab::OnQuickInspectorGetInfo);
      // Advanced APIs
      global["NativeQuickEval"] = BindJSCallbackWithRetval(&Tab::QI_Eval);
      global["NativeQuickGetDOMTree"] = BindJSCallbackWithRetval(&Tab::QI_GetDOMTree);
      global["NativeQuickGetNodeRect"] = BindJSCallbackWithRetval(&Tab::QI_GetNodeRect);
      global["NativeQuickStartPicker"] = BindJSCallback(&Tab::QI_StartPicker);
      global["NativeQuickStopPicker"] = BindJSCallback(&Tab::QI_StopPicker);
      global["NativeQuickHighlightSelector"] = BindJSCallback(&Tab::QI_HighlightSelector);
      global["NativeQuickGetComputedStyle"] = BindJSCallbackWithRetval(&Tab::QI_GetComputedStyle);
      global["NativeQuickGetStorage"] = BindJSCallbackWithRetval(&Tab::QI_GetStorage);
      global["NativeQuickGetPerformance"] = BindJSCallbackWithRetval(&Tab::QI_GetPerformance);
      global["NativeQuickGetOuterHTML"] = BindJSCallbackWithRetval(&Tab::QI_GetOuterHTML);
      global["NativeQuickSetAttribute"] = BindJSCallback(&Tab::QI_SetAttribute);
      global["NativeQuickRemoveAttribute"] = BindJSCallback(&Tab::QI_RemoveAttribute);
      // Initial refresh happens in page JS
    }
  }

  // Skip all CSS injections for browser internal pages - they have their own styling
  // This significantly speeds up internal page loading
  auto page_url = caller->url().utf8();
  bool is_internal = page_url.data() && UI::IsBrowserInternalPage(std::string(page_url.data()));

  if (!is_internal && ui_)
  {
    // Auto Dark Mode: if enabled, inject dark styling in this document
    if (ui_->dark_mode_enabled_)
    {
      ui_->ApplyDarkModeToView(caller);
    }

    // Accessibility: Apply reduce motion and high contrast CSS if enabled
    if (ui_->reduce_motion_enabled_)
      ui_->ApplyReduceMotionToView(caller);
    if (ui_->high_contrast_ui_enabled_)
      ui_->ApplyHighContrastToView(caller);
    if (ui_->smooth_scrolling_enabled_)
      ui_->ApplySmoothScrollingToView(caller);
  }
}

void Tab::OnQuickInspectorClose(const JSObject &obj, const JSArgs &args)
{
  if (inspector_overlay_)
  {
    // Stop the picker if it's active
    QI_StopPicker(obj, args);

    inspector_overlay_->Hide();
    // Restore content area to full height and return focus to page
    Resize(container_width_, container_height_);
    overlay_->Focus();
  }
}

void Tab::OnQuickInspectorOpenDevtools(const JSObject &obj, const JSArgs &args)
{
  // Hide quick inspector and open full devtools into the same overlay
  if (inspector_overlay_)
  {
    inspector_overlay_->Show();
  }
  view()->CreateLocalInspectorView();
}

JSValue Tab::OnQuickInspectorGetInfo(const JSObject &obj, const JSArgs &args)
{
  std::string t, u;
  if (view())
  {
    auto tu = view()->title().utf8();
    auto uu = view()->url().utf8();
    if (tu.data())
      t = tu.data();
    if (uu.data())
      u = uu.data();
  }
  bool loading = view() ? view()->is_loading() : false;
  std::string json = std::string("{") +
                     "\"title\":\"" + t + "\"," +
                     "\"url\":\"" + u + "\"," +
                     "\"loading\":" + (loading ? "true" : "false") +
                     "}";
  return JSValue(String(json.c_str()));
}

// ---- Events forwarded to Quick Inspector ----
void Tab::OnNetworkEvent(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1)
    return;
  if (!(inspector_overlay_ && inspector_overlay_->view()))
    return;
  ultralight::String payload = args[0];
  auto u = payload.utf8();
  std::string s = u.data() ? u.data() : "{}";
  std::string js = std::string("(function(p){ if(window.__qi && __qi.onNetwork) __qi.onNetwork(p); })(") + s + ")";
  inspector_overlay_->view()->EvaluateScript(String(js.c_str()), nullptr);
}

void Tab::OnPickerHover(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1)
    return;
  if (!(inspector_overlay_ && inspector_overlay_->view()))
    return;
  ultralight::String payload = args[0];
  auto u = payload.utf8();
  std::string s = u.data() ? u.data() : "{}";
  std::string js = std::string("(function(p){ if(window.__qi && __qi.onPickHover) __qi.onPickHover(p); })(") + s + ")";
  inspector_overlay_->view()->EvaluateScript(String(js.c_str()), nullptr);
}

void Tab::OnPickerSelect(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1)
    return;
  if (!(inspector_overlay_ && inspector_overlay_->view()))
    return;
  ultralight::String payload = args[0];
  auto u = payload.utf8();
  std::string s = u.data() ? u.data() : "{}";
  std::string js = std::string("(function(p){ if(window.__qi && __qi.onPickSelect) __qi.onPickSelect(p); })(") + s + ")";
  inspector_overlay_->view()->EvaluateScript(String(js.c_str()), nullptr);
}

// ---- Quick Inspector advanced APIs ----
JSValue Tab::QI_Eval(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !view())
    return JSValue(String(""));
  String expr = args[0];
  String result;
  view()->EvaluateScript(expr, &result);
  return JSValue(result);
}

JSValue Tab::QI_GetDOMTree(const JSObject &obj, const JSArgs &args)
{
  int depth = 3;
  int maxChildren = 20;
  if (args.size() >= 1)
    depth = (int)(double)args[0];
  if (args.size() >= 2)
    maxChildren = (int)(double)args[1];
  if (!view())
    return JSValue(String("{}"));
  std::ostringstream ss;
  ss << "(function(){\n";
  ss << "function nodeInfo(n,d){ if(!n||d<0) return null; if(n.nodeType!==1) return null; var c=[]; var kids=n.children; var m=kids?Math.min(kids.length," << maxChildren << "):0; for(var i=0;i<m;i++){ var ci=nodeInfo(kids[i],d-1); if(ci) c.push(ci);} return { tag:n.tagName.toLowerCase(), id:n.id||'', cls:(n.className||'').toString(), children:c }; }\n";
  ss << "var r=nodeInfo(document.documentElement," << depth << "); return JSON.stringify(r||{});\n";
  ss << "})()";
  String res;
  view()->EvaluateScript(String(ss.str().c_str()), &res);
  return JSValue(res);
}

JSValue Tab::QI_GetNodeRect(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !view())
    return JSValue(String("{}"));
  String sel = args[0];
  auto u = sel.utf8();
  std::string jssel = u.data() ? u.data() : "";
  std::string script = std::string("(function(){ var el=document.querySelector(\"") + jssel + "\"); if(!el) return '{}'; var r=el.getBoundingClientRect(); return JSON.stringify({left:r.left,top:r.top,width:r.width,height:r.height}); })()";
  String res;
  view()->EvaluateScript(String(script.c_str()), &res);
  return JSValue(res);
}

void Tab::QI_StartPicker(const JSObject &obj, const JSArgs &args)
{
  if (!view())
    return;
  RefPtr<JSContext> ctx = view()->LockJSContext();
  SetJSContext(ctx->ctx());
  JSObject global = JSGlobalObject();
  global["NativeInspectorPickHover"] = BindJSCallback(&Tab::OnPickerHover);
  global["NativeInspectorPickSelect"] = BindJSCallback(&Tab::OnPickerSelect);
  const char *picker = R"JS((function(){
    if (window.__ul_picker_active) return; window.__ul_picker_active = true;
    if (!window.__ul_pick_overlay){ var o=document.createElement('div'); o.style.cssText='position:fixed;pointer-events:none;z-index:2147483647;border:2px solid #4af;background:rgba(68,170,255,.15)'; document.documentElement.appendChild(o); window.__ul_pick_overlay=o; }
    function cssPath(el){ try{ var p=[]; for(;el&&el.nodeType===1; el=el.parentElement){ var s=el.nodeName.toLowerCase(); if(el.id){ s += '#'+el.id; p.unshift(s); break; } else { var cls = (el.className||'').toString().trim().split(/\s+/).filter(Boolean); if(cls.length) s += '.'+cls.join('.'); var idx=1, sib=el; while((sib=sib.previousElementSibling)!=null){ if(sib.nodeName===el.nodeName) idx++; } p.unshift(s+':nth-of-type('+idx+')'); } } return p.join('>');} catch(e){ return ''; } }
    function update(el){ if(!el) return; var r=el.getBoundingClientRect(); var o=window.__ul_pick_overlay; o.style.left=r.left+'px'; o.style.top=r.top+'px'; o.style.width=r.width+'px'; o.style.height=r.height+'px'; }
    window.__ul_pick_move = function(e){ var t=e.target; update(t); try{ if(window.NativeInspectorPickHover) NativeInspectorPickHover(JSON.stringify({selector:cssPath(t)})); }catch(_){} };
    window.__ul_pick_click = function(e){ e.preventDefault(); e.stopPropagation(); var t=e.target; try{ if(window.NativeInspectorPickSelect) NativeInspectorPickSelect(JSON.stringify({selector:cssPath(t)})); }catch(_){} };
    document.addEventListener('mousemove', window.__ul_pick_move, true);
    document.addEventListener('click', window.__ul_pick_click, true);
  })())JS";
  view()->EvaluateScript(String(picker), nullptr);
}

void Tab::QI_StopPicker(const JSObject &obj, const JSArgs &args)
{
  if (!view())
    return;
  const char *stop = R"JS((function(){
    if (!window.__ul_picker_active) return; window.__ul_picker_active=false;
    document.removeEventListener('mousemove', window.__ul_pick_move, true);
    document.removeEventListener('click', window.__ul_pick_click, true);
    if (window.__ul_pick_overlay && window.__ul_pick_overlay.parentNode) window.__ul_pick_overlay.parentNode.removeChild(window.__ul_pick_overlay);
    window.__ul_pick_overlay=null;
  })())JS";
  view()->EvaluateScript(String(stop), nullptr);
}

void Tab::QI_HighlightSelector(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !view())
    return;
  String sel = args[0];
  auto u = sel.utf8();
  std::string jssel = u.data() ? u.data() : "";
  std::string script = std::string("(function(){ var el=document.querySelector(\"") + jssel + "\"); if(!el) return; if(!window.__ul_pick_overlay){ var o=document.createElement('div'); o.style.cssText='position:fixed;pointer-events:none;z-index:2147483647;border:2px solid #4af;background:rgba(68,170,255,.15)'; document.documentElement.appendChild(o); window.__ul_pick_overlay=o; } var r=el.getBoundingClientRect(); var o=window.__ul_pick_overlay; o.style.left=r.left+'px'; o.style.top=r.top+'px'; o.style.width=r.width+'px'; o.style.height=r.height+'px'; })()";
  view()->EvaluateScript(String(script.c_str()), nullptr);
}

JSValue Tab::QI_GetComputedStyle(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !view())
    return JSValue(String("{}"));
  String sel = args[0];
  auto u = sel.utf8();
  std::string raw = u.data() ? u.data() : "";
  // Use util::EscapeJsStringLiteral
  std::ostringstream ss;
  ss << "(function(){ var el=document.querySelector(\"" << util::EscapeJsStringLiteral(raw) << "\"); if(!el) return '{}'; var cs=getComputedStyle(el); var out={}; try{ for(var i=0;i<cs.length;i++){ var k=cs.item(i); out[k]=cs.getPropertyValue(k); } }catch(e){} return JSON.stringify(out); })()";
  String res;
  std::string js = ss.str();
  view()->EvaluateScript(String(js.c_str()), &res);
  return JSValue(res);
}

JSValue Tab::QI_GetStorage(const JSObject &obj, const JSArgs &args)
{
  if (!view())
    return JSValue(String("{}"));
  const char *script = R"JS((function(){
    function dumpLS(ls){ var o={}; try{ for(var i=0;i<ls.length;i++){ var k=ls.key(i); o[k]=ls.getItem(k); } }catch(e){} return o; }
    var out={ localStorage: dumpLS(window.localStorage||{}), sessionStorage: dumpLS(window.sessionStorage||{}), cookies: document.cookie||'' };
    return JSON.stringify(out);
  })())JS";
  String res;
  view()->EvaluateScript(String(script), &res);
  return JSValue(res);
}

JSValue Tab::QI_GetPerformance(const JSObject &obj, const JSArgs &args)
{
  if (!view())
    return JSValue(String("{}"));
  const char *script = R"JS((function(){
    var nav = (performance.getEntriesByType && performance.getEntriesByType('navigation'));
    var resources = (performance.getEntriesByType && performance.getEntriesByType('resource'))||[];
    var t = performance.timing || {};
    return JSON.stringify({ navigation: nav && nav[0] || {}, resourceCount: resources.length, timing: t });
  })())JS";
  String res;
  view()->EvaluateScript(String(script), &res);
  return JSValue(res);
}

JSValue Tab::QI_GetOuterHTML(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 1 || !view())
    return JSValue(String(""));
  String sSel = args[0];
  auto us = sSel.utf8();
  std::string sel = us.data() ? us.data() : "";
  std::string js = std::string("(function(){ var el=document.querySelector(\"") + util::EscapeJsStringLiteral(sel) + "\"); if(!el) return ''; return el.outerHTML; })()";
  String res;
  view()->EvaluateScript(String(js.c_str()), &res);
  return JSValue(res);
}

void Tab::QI_SetAttribute(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 3 || !view())
    return;
  String sSel = args[0];
  String sName = args[1];
  String sVal = args[2];
  auto us = sSel.utf8();
  auto un = sName.utf8();
  auto uv = sVal.utf8();
  std::string sel = us.data() ? us.data() : "";
  std::string name = un.data() ? un.data() : "";
  std::string val = uv.data() ? uv.data() : "";
  std::ostringstream ss;
  ss << "(function(){ var el=document.querySelector(\"" << util::EscapeJsStringLiteral(sel) << "\"); if(!el) return; el.setAttribute(\"" << util::EscapeJsStringLiteral(name) << "\",\"" << util::EscapeJsStringLiteral(val) << "\"); })()";
  std::string js = ss.str();
  view()->EvaluateScript(String(js.c_str()), nullptr);
}

void Tab::QI_RemoveAttribute(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 2 || !view())
    return;
  String sSel = args[0];
  String sName = args[1];
  auto us = sSel.utf8();
  auto un = sName.utf8();
  std::string sel = us.data() ? us.data() : "";
  std::string name = un.data() ? un.data() : "";
  std::ostringstream ss;
  ss << "(function(){ var el=document.querySelector(\"" << util::EscapeJsStringLiteral(sel) << "\"); if(!el) return; el.removeAttribute(\"" << util::EscapeJsStringLiteral(name) << "\"); })()";
  std::string js = ss.str();
  view()->EvaluateScript(String(js.c_str()), nullptr);
}

// --- General JS bridge implementations ---
void Tab::JS_Back(const JSObject &obj, const JSArgs &args)
{
  if (view())
    view()->GoBack();
}

void Tab::JS_Forward(const JSObject &obj, const JSArgs &args)
{
  if (view())
    view()->GoForward();
}

void Tab::JS_Reload(const JSObject &obj, const JSArgs &args)
{
  if (view())
    view()->Reload();
}

void Tab::JS_Stop(const JSObject &obj, const JSArgs &args)
{
  if (view())
    view()->Stop();
}

void Tab::JS_Navigate(const JSObject &obj, const JSArgs &args)
{
  if (args.size() >= 1 && view())
  {
    ultralight::String url = args[0];
    view()->LoadURL(url);
  }
}

void Tab::JS_NewTab(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return;
  if (args.size() >= 1)
  {
    ultralight::String url = args[0];
    RefPtr<View> child = ui_->CreateNewTabForChildView(url);
    if (child)
      child->LoadURL(url);
  }
  else
  {
    ui_->CreateNewTab();
  }
}

void Tab::JS_CloseTab(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return;
  // If an id is passed, close it, else close current tab
  uint64_t target = id_;
  if (args.size() >= 1)
  {
    // Clamp to uint64 from JS number
    target = static_cast<uint64_t>((double)args[0]);
  }
  ui_->OnRequestTabClose({}, {(double)target});
}

void Tab::JS_OpenHistory(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return;
  RefPtr<View> child = ui_->CreateNewTabForChildView(String("file:///history.html"));
  if (child)
    child->LoadURL("file:///history.html");
}

JSValue Tab::JS_GetHistory(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return JSValue(ui_->GetHistoryJSON());
}

void Tab::JS_ClearHistory(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->ClearHistory();
}

void Tab::JS_ToggleDarkMode(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return;
  ui_->OnToggleDarkMode({}, {});
}

JSValue Tab::JS_IsDarkModeEnabled(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue(false);
  return ui_->OnGetDarkModeEnabled({}, {});
}

JSValue Tab::JS_GetAppInfo(const JSObject &obj, const JSArgs &args)
{
  // Minimal app info
  std::string json = std::string("{\"name\":\"Ultralight-WebBrowser\",\"version\":\"1.0\"}");
  return JSValue(String(json.c_str()));
}

// Bookmark bridge implementations
JSValue Tab::JS_GetBookmarks(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store())
    return JSValue(String("[]"));
  return JSValue(String(ui_->bookmark_store()->ToJSON().c_str()));
}

JSValue Tab::JS_GetBookmarkBar(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store())
    return JSValue(String("[]"));
  return JSValue(String(ui_->bookmark_store()->BookmarkBarToJSON().c_str()));
}

JSValue Tab::JS_AddBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store() || args.empty())
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

  uint64_t id = ui_->bookmark_store()->AddBookmark(url, title, favicon, show_on_bar);
  return JSValue((double)id);
}

void Tab::JS_RemoveBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store() || args.empty())
    return;

  uint64_t id = static_cast<uint64_t>((double)args[0]);
  ui_->bookmark_store()->RemoveBookmark(id);
}

JSValue Tab::JS_IsBookmarked(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store() || args.empty())
    return JSValue(false);

  ultralight::String url_ul = args[0].ToString();
  auto url_str = url_ul.utf8();
  std::string url = url_str.data() ? url_str.data() : "";
  return JSValue(ui_->bookmark_store()->IsBookmarked(url));
}

void Tab::JS_ToggleBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store() || args.empty())
    return;

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

  if (ui_->bookmark_store()->IsBookmarked(url))
  {
    // Remove the bookmark
    auto *bm = ui_->bookmark_store()->GetBookmarkByUrl(url);
    if (bm)
      ui_->bookmark_store()->RemoveBookmark(bm->id);
  }
  else
  {
    // Add the bookmark
    ui_->bookmark_store()->AddBookmark(url, title, favicon, true);
  }
}

void Tab::JS_ReorderBookmarks(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store() || args.empty())
    return;

  // Parse the JSON array of IDs
  ultralight::String json_ul = args[0].ToString();
  auto json_str = json_ul.utf8();
  std::string json = json_str.data() ? json_str.data() : "";

  std::vector<uint64_t> ordered_ids;

  // Simple JSON array parsing for [id1, id2, id3, ...]
  size_t pos = json.find('[');
  if (pos == std::string::npos)
    return;
  pos++;

  while (pos < json.length())
  {
    // Skip whitespace
    while (pos < json.length() && std::isspace(json[pos]))
      pos++;

    if (json[pos] == ']')
      break;

    // Parse number
    std::string num;
    while (pos < json.length() && std::isdigit(json[pos]))
    {
      num += json[pos++];
    }

    if (!num.empty())
    {
      ordered_ids.push_back(std::stoull(num));
    }

    // Skip comma and whitespace
    while (pos < json.length() && (json[pos] == ',' || std::isspace(json[pos])))
      pos++;
  }

  if (!ordered_ids.empty())
  {
    ui_->bookmark_store()->ReorderBookmarks(ordered_ids);
  }
}

JSValue Tab::JS_UpdateBookmark(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->bookmark_store() || args.size() < 3)
    return JSValue(false);

  // Args: id, url, title
  uint64_t id = static_cast<uint64_t>((double)args[0]);

  ultralight::String url_ul = args[1].ToString();
  auto url_str = url_ul.utf8();
  std::string url = url_str.data() ? url_str.data() : "";

  ultralight::String title_ul = args[2].ToString();
  auto title_str = title_ul.utf8();
  std::string title = title_str.data() ? title_str.data() : "";

  // Keep existing favicon and show_on_bar settings
  std::string favicon;
  bool show_on_bar = true;

  auto *existing = ui_->bookmark_store()->GetBookmarkById(id);
  if (existing)
  {
    favicon = existing->favicon;
    show_on_bar = existing->show_on_bar;
  }

  return JSValue(ui_->bookmark_store()->UpdateBookmark(id, url, title, favicon, show_on_bar));
}

JSValue Tab::OnDownloadsGetData(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue(String("{\"items\":[]}"));
  return JSValue(ui_->GetDownloadsJSON());
}

void Tab::OnDownloadsClear(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->ClearCompletedDownloads();
}

void Tab::OnDownloadsOpen(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || args.empty())
    return;
  uint64_t id = static_cast<uint64_t>((double)args[0]);
  ui_->OpenDownloadItem(id);
}

void Tab::OnDownloadsReveal(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || args.empty())
    return;
  uint64_t id = static_cast<uint64_t>((double)args[0]);
  ui_->RevealDownloadItem(id);
}

void Tab::OnOpenContextMenu(const JSObject &obj, const JSArgs &args)
{
  if (args.size() < 3)
    return;

  int view_x = (int)args[0];
  int view_y = (int)args[1];
  ultralight::String json = args[2];

  // Convert to window coords by offsetting with our overlay position
  // Convert overlay pixel offsets to CSS pixels (DIP) before adding client coords
  double scale = ui_->window()->scale();
  int win_x = (int)std::lround(((double)overlay_->x() / scale)) + view_x;
  int win_y = (int)std::lround(((double)overlay_->y() / scale)) + view_y;

  if (ui_)
  {
    ui_->ShowContextMenuOverlay(win_x, win_y, json);
  }
}

// --- History page JS bridge ---
JSValue Tab::OnHistoryGetData(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return JSValue(ui_->GetHistoryJSON());
}

void Tab::OnHistoryClear(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->ClearHistory();
}

// --- Settings page JS bridge (forward to UI) ---
JSValue Tab::JS_GetSettingsSnapshot(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return ui_->OnGetSettings(obj, args);
}

void Tab::JS_UpdateSetting(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnUpdateSetting(obj, args);
}

void Tab::JS_SaveSettings(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnSaveSettings(obj, args);
}

JSValue Tab::JS_RestoreSettingsDefaults(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return ui_->OnRestoreSettingsDefaults(obj, args);
}

JSValue Tab::JS_GetDrmStatus(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return ui_->OnGetDrmStatus(obj, args);
}

JSValue Tab::JS_InstallDrmDependencies(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return ui_->OnInstallDrmDependencies(obj, args);
}

// --- Extensions page JS bridge (forward to UI) ---
JSValue Tab::JS_GetExtensions(const JSObject &obj, const JSArgs &args)
{
  if (!ui_)
    return JSValue();
  return ui_->OnGetExtensions(obj, args);
}

void Tab::JS_ToggleExtension(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnToggleExtension(obj, args);
}

void Tab::JS_ReloadExtension(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnReloadExtension(obj, args);
}

void Tab::JS_ReloadAllExtensions(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnReloadAllExtensions(obj, args);
}

void Tab::JS_DeleteExtension(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnDeleteExtension(obj, args);
}

void Tab::JS_LoadExtension(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnLoadExtension(obj, args);
}

void Tab::JS_CreateExtension(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnCreateExtension(obj, args);
}

void Tab::JS_OpenExtensionsFolder(const JSObject &obj, const JSArgs &args)
{
  if (ui_)
    ui_->OnOpenExtensionsFolder(obj, args);
}

// --- Password Manager callbacks ---

void Tab::OnPasswordFormDetected(const JSObject &obj, const JSArgs &args)
{
  // Called when a login form is detected on the page
  // This is informational - we'll autofill if we have credentials
  if (!ui_ || !ui_->password_manager() || args.empty())
    return;

  ultralight::String json_ul = args[0].ToString();
  auto json_str = json_ul.utf8();
  std::string data = json_str.data() ? json_str.data() : "";

  // Parse origin from the JSON
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

  std::string origin = extract_string("origin");
  if (origin.empty())
    return;

  // Check if we have saved credentials for this origin
  auto creds = ui_->password_manager()->GetCredentialsForOrigin(origin);
  if (!creds.empty())
  {
    // Notify JS that autofill is available
    std::ostringstream ss;
    ss << "(function(){ if(window.__ul_password_autofill_available) window.__ul_password_autofill_available(" << creds.size() << "); })();";
    view()->EvaluateScript(String(ss.str().c_str()), nullptr);
  }
}

void Tab::OnPasswordFormSubmitted(const JSObject &obj, const JSArgs &args)
{
  // Called when a login form is submitted - offer to save the password
  if (!ui_ || !ui_->password_manager() || args.empty())
    return;

  ultralight::String json_ul = args[0].ToString();
  auto json_str = json_ul.utf8();
  std::string data = json_str.data() ? json_str.data() : "";

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

  std::string origin = extract_string("origin");
  std::string username = extract_string("username");
  std::string password = extract_string("password");

  if (origin.empty() || username.empty() || password.empty())
    return;

  // Check if this origin is blacklisted
  if (ui_->password_manager()->IsOriginBlacklisted(origin))
    return;

  // Check if we already have this exact credential
  auto existing = ui_->password_manager()->GetCredentialsForOrigin(origin);
  for (const auto &cred : existing)
  {
    if (cred.username == username && cred.password == password)
    {
      // Already saved, just update last used time
      ui_->password_manager()->RecordAutofillUsage(cred.id);
      return;
    }
    if (cred.username == username && cred.password != password)
    {
      // Password changed - store pending and show update prompt
      pending_save_origin_ = origin;
      pending_save_username_ = username;
      pending_save_password_ = password;

      // Show update prompt in the UI overlay (same as save, but will update)
      if (ui_)
      {
        ui_->ShowPasswordSavePrompt(origin, username);
      }
      return;
    }
  }

  // New credential - store pending and show save prompt
  pending_save_origin_ = origin;
  pending_save_username_ = username;
  pending_save_password_ = password;

  // Notify the UI overlay to show the save prompt bar
  if (ui_)
  {
    ui_->ShowPasswordSavePrompt(origin, username);
  }
}

JSValue Tab::OnGetPasswordSuggestions(const JSObject &obj, const JSArgs &args)
{
  // Return list of saved passwords for autofill dropdown
  if (!ui_ || !ui_->password_manager() || args.empty())
    return JSValue("[]");

  ultralight::String origin_ul = args[0].ToString();
  auto origin_str = origin_ul.utf8();
  std::string origin = origin_str.data() ? origin_str.data() : "";

  if (origin.empty())
    return JSValue("[]");

  auto creds = ui_->password_manager()->GetCredentialsForOrigin(origin);

  std::ostringstream ss;
  ss << "[";
  bool first = true;
  for (const auto &cred : creds)
  {
    if (!first)
      ss << ",";
    first = false;
    ss << "{";
    ss << "\"id\":\"" << util::EscapeJsonString(cred.id) << "\",";
    ss << "\"username\":\"" << util::EscapeJsonString(cred.username) << "\",";
    ss << "\"password\":\"" << util::EscapeJsonString(cred.password) << "\"";
    ss << "}";
  }
  ss << "]";

  return JSValue(String(ss.str().c_str()));
}

void Tab::OnPasswordSelected(const JSObject &obj, const JSArgs &args)
{
  // User selected a password from the dropdown - fill it in
  if (!ui_ || !ui_->password_manager() || args.size() < 2)
    return;

  ultralight::String username_ul = args[0].ToString();
  ultralight::String password_ul = args[1].ToString();

  auto username_str = username_ul.utf8();
  auto password_str = password_ul.utf8();

  std::string username = username_str.data() ? username_str.data() : "";
  std::string password = password_str.data() ? password_str.data() : "";

  // Fill the form via JS
  std::ostringstream ss;
  ss << "(function(){ if(window.__ul_fill_password_form) window.__ul_fill_password_form("
     << "'" << util::EscapeJsonString(username) << "',"
     << "'" << util::EscapeJsonString(password) << "'"
     << "); })();";
  view()->EvaluateScript(String(ss.str().c_str()), nullptr);
}

void Tab::OnPasswordSaveResponse(const JSObject &obj, const JSArgs &args)
{
  // User responded to save/update password prompt
  if (!ui_ || !ui_->password_manager() || args.empty())
    return;

  ultralight::String response_ul = args[0].ToString();
  auto response_str = response_ul.utf8();
  std::string response = response_str.data() ? response_str.data() : "";

  if (response == "save" || response == "update")
  {
    if (!pending_save_origin_.empty() && !pending_save_username_.empty())
    {
      // Check if updating existing or saving new
      auto existing = ui_->password_manager()->GetCredentialsForOrigin(pending_save_origin_);
      bool found = false;
      for (auto &cred : existing)
      {
        if (cred.username == pending_save_username_)
        {
          // Update existing credential
          cred.password = pending_save_password_;
          cred.date_password_modified = password::PasswordManager::GetCurrentTimestamp();
          ui_->password_manager()->UpdateCredential(cred);
          found = true;
          break;
        }
      }

      if (!found)
      {
        // Save new credential
        password::SavedCredential cred;
        cred.id = password::PasswordManager::GenerateUUID();
        cred.origin = pending_save_origin_;
        cred.signon_realm = pending_save_origin_;
        cred.username = pending_save_username_;
        cred.password = pending_save_password_;
        cred.date_created = password::PasswordManager::GetCurrentTimestamp();
        cred.date_password_modified = cred.date_created;
        cred.date_last_used = 0;
        cred.times_used = 0;
        cred.blacklisted = false;
        ui_->password_manager()->SaveCredential(cred);
      }
    }
  }
  else if (response == "never")
  {
    // Add to blacklist
    if (!pending_save_origin_.empty())
    {
      ui_->password_manager()->BlacklistOrigin(pending_save_origin_);
    }
  }

  // Clear pending
  pending_save_origin_.clear();
  pending_save_username_.clear();
  pending_save_password_.clear();

  // Hide the prompt
  if (ui_)
  {
    ui_->HidePasswordSavePrompt();
  }
}

void Tab::OnFaviconFetched(const JSObject &obj, const JSArgs &args)
{
  // Forward favicon data to UI for caching and tab update
  if (!ui_ || args.size() < 2)
    return;

  // Delegate to UI's OnFaviconReady which handles caching
  ui_->OnFaviconReady(obj, args);

  // Update this tab's favicon display
  if (args[1].IsString())
  {
    ultralight::String data_url = args[1].ToString();
    ultralight::String url = view()->url();
    ui_->UpdateTabFavicon(id_, data_url);
  }
}

// ============================================================================
// Password Page JS Callbacks (for passwords.html)
// ============================================================================

JSValue Tab::JS_GetPasswords(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager())
    return JSValue("[]");

  auto credentials = ui_->password_manager()->GetAllCredentials();
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

JSValue Tab::JS_GetPasswordStats(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager())
    return JSValue("{}");

  auto credentials = ui_->password_manager()->GetAllCredentials();
  int total = static_cast<int>(credentials.size());
  int weak = 0;
  int reused = 0;
  std::unordered_map<std::string, int> password_counts;

  for (const auto &cred : credentials)
  {
    auto strength = ui_->password_manager()->CheckPasswordStrength(cred.password);
    if (strength.score < 3)
      weak++;

    password_counts[cred.password]++;
  }

  for (const auto &p : password_counts)
  {
    if (p.second > 1)
      reused += p.second;
  }

  int blacklisted = static_cast<int>(ui_->password_manager()->GetBlacklistedOrigins().size());

  std::ostringstream ss;
  ss << "{";
  ss << "\"total_passwords\":" << total << ",";
  ss << "\"weak_passwords\":" << weak << ",";
  ss << "\"reused_passwords\":" << reused << ",";
  ss << "\"blacklisted_sites\":" << blacklisted;
  ss << "}";

  return JSValue(String(ss.str().c_str()));
}

void Tab::JS_SavePassword(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager() || args.empty())
    return;

  ultralight::String json_ul = args[0].ToString();
  auto json_str = json_ul.utf8();
  std::string data = json_str.data() ? json_str.data() : "";

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

  if (origin.empty() || username.empty())
    return;

  password::SavedCredential cred;
  cred.id = id.empty() ? password::PasswordManager::GenerateUUID() : id;
  cred.origin = origin;
  cred.signon_realm = origin;
  cred.username = username;
  cred.password = password;
  cred.notes = notes;
  cred.date_created = password::PasswordManager::GetCurrentTimestamp();
  cred.date_password_modified = cred.date_created;
  cred.date_last_used = 0;
  cred.times_used = 0;
  cred.blacklisted = false;

  if (id.empty())
  {
    ui_->password_manager()->SaveCredential(cred);
  }
  else
  {
    ui_->password_manager()->UpdateCredential(cred);
  }
}

void Tab::JS_DeletePassword(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager() || args.empty())
    return;

  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";

  if (!id.empty())
    ui_->password_manager()->DeleteCredential(id);
}

JSValue Tab::JS_GetDecryptedPassword(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager() || args.empty())
    return JSValue("");

  ultralight::String id_ul = args[0].ToString();
  auto id_str = id_ul.utf8();
  std::string id = id_str.data() ? id_str.data() : "";

  auto credentials = ui_->password_manager()->GetAllCredentials();
  for (const auto &cred : credentials)
  {
    if (cred.id == id)
      return JSValue(String(cred.password.c_str()));
  }

  return JSValue("");
}

void Tab::JS_SavePasswordSettings(const JSObject &obj, const JSArgs &args)
{
  // TODO: Implement password settings storage
}

void Tab::JS_ExportPasswords(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager() || args.empty())
    return;

  ultralight::String format_ul = args[0].ToString();
  auto format_str = format_ul.utf8();
  std::string format = format_str.data() ? format_str.data() : "json";

  std::filesystem::path export_path = std::filesystem::path(ui_->SettingsDirectory()) / "passwords_export";
  if (format == "csv")
  {
    export_path += ".csv";
    ui_->password_manager()->ExportToCSV(export_path.string());
  }
  else
  {
    export_path += ".json";
    ui_->password_manager()->ExportToJSON(export_path.string());
  }
}

void Tab::JS_ImportPasswords(const JSObject &obj, const JSArgs &args)
{
  if (!ui_ || !ui_->password_manager() || args.size() < 2)
    return;

  ultralight::String content_ul = args[0].ToString();
  ultralight::String format_ul = args[1].ToString();

  auto content_str = content_ul.utf8();
  auto format_str = format_ul.utf8();

  std::string content = content_str.data() ? content_str.data() : "";
  std::string format = format_str.data() ? format_str.data() : "json";

  // Create a temp file and import from it
  std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "passwords_import_temp";
  if (format == "csv")
    temp_path += ".csv";
  else
    temp_path += ".json";

  std::ofstream temp_file(temp_path);
  if (temp_file.is_open())
  {
    temp_file << content;
    temp_file.close();

    if (format == "csv")
      ui_->password_manager()->ImportFromCSV(temp_path.string());
    else
      ui_->password_manager()->ImportFromJSON(temp_path.string());

    std::filesystem::remove(temp_path);
  }
}

// (Disable-history removed)
