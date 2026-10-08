# DRM WebView Manual Test Checklist

The following steps verify the optional DRM WebView subsystem behaves correctly on every supported platform. Perform these steps whenever native DRM dependencies, the Ultralight-to-native bridge, or the settings UI changes.

## Prerequisites
- Fresh build of Ultralight-WebBrowser (Release configuration recommended).
- Runtime bundles available locally:
  - Windows: Microsoft Edge WebView2 Runtime bootstrapper accessible.
  - macOS: System-provided WKWebView (no manual install required).
  - Linux: WebKitGTK packages available through the platform package manager.
- Writable `%APPDATA%/Ultralight/` (Windows) or `~/Library/Application Support/` (macOS) or `~/.config/` (Linux) so `drm_settings.json` can be created.

## Checklist
1. **Initial launch**
   - Start the browser with a clean profile (delete `data/drm_settings.json` if it exists).
   - Open `Settings` and confirm the new DRM WebView toggle appears under "DRM WebView" with the runtime status card.
   - Verify the status chip shows `Disabled`, runtime name reflects the current platform, and log pane contains the initialization line.
2. **Toggle persistence**
   - Enable the DRM WebView toggle.
   - Click `Save Changes`, close the browser, relaunch, and confirm the toggle remains enabled and the status payload reports `"enabled": true` via the log panel.
3. **Runtime detection**
   - On Windows where WebView2 is missing, select `Install Runtime` and monitor the log output for download/start/completion lines. After success the Runtime Status chip should flip to `Installed` without restart.
   - On Linux without WebKitGTK, attempt install and confirm failure is logged with actionable text. Install the packages manually, click `Refresh Status`, and verify the chip shows `Installed`.
4. **Site rule enforcement**
   - With DRM enabled, navigate to `https://www.netflix.com/` and confirm a native DRMed tab opens (Ultralight tab hides, `Back/Forward` reflect the WebView state).
   - Add a custom rule (edit `drm_settings.json` or use future UI) for a test host and confirm navigation to that host also opens a DRM tab.
5. **Resizing and focus**
   - Open multiple tabs mixing Ultralight and DRM content. Switch between them and resize the window; the native view should stay aligned under the toolbar.
   - Verify keyboard shortcuts (Ctrl+L, Ctrl+R) continue to work with a DRM tab active.
6. **Fallback behavior**
   - Turn the DRM toggle off and restart. Visiting Netflix should now remain in an Ultralight tab and the log panel should mention that the subsystem is disabled.
7. **Log retention**
   - Confirm the log view scrolls automatically and only keeps the most recent entries (older rows roll off beyond ~200 lines).

Document any deviations or platform-specific notes directly in this file so future testers understand edge cases.
