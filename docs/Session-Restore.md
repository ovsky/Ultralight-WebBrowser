# Session Restore Feature

**Branch:** `feature/restore-session`  
**Date:** December 2025

This document describes the Chrome-like session restore feature that preserves open tabs and allows restoration after unexpected closures.

---

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Session Restore Bar](#session-restore-bar)
4. [Settings](#settings)
5. [Implementation Details](#implementation-details)
6. [Session File Format](#session-file-format)
7. [Internal Page Filtering](#internal-page-filtering)
8. [Files Modified](#files-modified)

---

## Overview

The session restore feature automatically saves the browser's tab state and allows users to restore their previous session after:
- Unexpected closures (ALT+F4)
- Crashes
- Power loss
- Normal browser restarts

Unlike traditional session restore that silently restores tabs, this implementation shows a **user-facing restore bar** that asks the user whether they want to restore their previous session, similar to Chrome's behavior.

---

## Features

### Continuous Session Saving
Session state is automatically saved when:
- A new tab is created
- A tab is closed  
- Navigation completes (page finishes loading)

Session file location: `data/session.json`

### Crash Detection
- On clean exit (normal close), session is marked as `"clean_exit": true`
- On unexpected exit (ALT+F4, crash), the flag remains `false`
- At startup, the browser checks this flag to detect crashes

### Smart Restore Filtering
The restore bar only appears when there are **meaningful tabs** to restore:
- Internal browser pages (settings, history, downloads, etc.) are excluded
- Empty/blank pages are excluded
- Only external websites trigger the restore prompt

---

## Session Restore Bar

### Visual Design
A blue gradient notification bar appears at the top of the browser window with:
- **Icon:** Clock/history icon
- **Message:** Dynamic text showing number of tabs to restore
- **Primary Button:** "Restore" - restores all saved tabs
- **Secondary Button:** "Start Fresh" - dismisses and starts with new tab
- **Close Button:** X icon to dismiss

### Messages
- **After crash:** "Your previous session was interrupted. Restore X tabs?"
- **Normal restart:** "Would you like to restore X tabs from your last session?"

### Behavior
1. When restore bar is visible, session saving is **paused** to prevent overwriting
2. Clicking "Restore":
   - Navigates the existing start page tab to the first saved URL
   - Creates new tabs for remaining saved URLs
   - All tabs are restored with their original URLs
3. Clicking "Start Fresh" or X:
   - Dismisses the bar
   - Clears restore pending state
   - Starts saving new session

### CSS Styling
```css
#session-restore-bar {
  background: linear-gradient(180deg, #2d4a6f 0%, #1e3a5f 100%);
  border-bottom: 1px solid #3d6a9f;
  padding: 8px 16px;
  z-index: 9999;
  animation: slideDown 0.3s ease-out;
}
```

---

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `restore_session_on_startup` | `true` | Show restore bar when previous session exists |
| `save_session_continuously` | `true` | Enable crash recovery by saving session state continuously |

Both settings are available in the Settings page under the "General" category.

---

## Implementation Details

### New Methods in `UI` class

```cpp
// Core session management
void SaveSessionToDisk();           // Save current session state
void SaveSessionToDiskWithCleanExit(); // Save with clean_exit=true
void LoadSessionFromDisk();         // Load session data at startup
bool HasSavedSession() const;       // Check if valid session exists
void ClearSavedSession();           // Clear restore pending flag
void RestoreSavedSession();         // Restore tabs from saved session

// Restore bar UI
void ShowSessionRestoreBar();       // Show the restore prompt bar
void OnRestoreSession(const JSObject &obj, const JSArgs &args);  // User clicked Restore
void OnDismissSession(const JSObject &obj, const JSArgs &args);  // User clicked Dismiss

// Tab counting
int GetSavedSessionTabCount() const;       // Total saved tabs
int GetMeaningfulSavedTabCount() const;    // Non-internal tabs only
bool IsInternalBrowserPage(const std::string &url) const; // Check if URL is internal
```

### State Variables

```cpp
bool session_restore_pending_ = false;      // Should prompt to restore
bool session_was_clean_exit_ = false;       // Was last exit clean
bool session_restore_bar_visible_ = false;  // Is bar currently showing
std::string session_file_path_;             // Path to session.json
```

### JavaScript Functions (ui.html)

```javascript
// Show restore bar with tab count and crash status
function showSessionRestoreBar(tabCount, wasCrash) { ... }

// Hide the restore bar
function hideSessionRestoreBar() { ... }
```

### JavaScript Callbacks

```javascript
// Bound to C++ methods
window.OnRestoreSession  // Called when user clicks Restore
window.OnDismissSession  // Called when user clicks Start Fresh or X
```

---

## Session File Format

```json
{
  "version": 1,
  "timestamp": 1733410800000,
  "clean_exit": false,
  "active_tab_id": 2,
  "tabs": [
    {"id": 0, "url": "https://www.google.com/", "title": "Google"},
    {"id": 1, "url": "https://github.com/", "title": "GitHub"}
  ],
  "drm_tabs": [
    {"id": 3, "url": "https://netflix.com/"}
  ]
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `version` | number | Schema version (currently 1) |
| `timestamp` | number | Unix timestamp in milliseconds |
| `clean_exit` | boolean | `true` if browser closed normally |
| `active_tab_id` | number | ID of the last active tab |
| `tabs` | array | Regular browser tabs |
| `drm_tabs` | array | DRM-enabled tabs (for DRM sites) |

---

## Internal Page Filtering

The following internal browser pages are **excluded** from session restore:

| URL Pattern | Description |
|-------------|-------------|
| `file:///static-sites/google-static.html` | Default home page |
| `file:///new_tab_page.html` | New tab page |
| `file:///settings.html` | Settings page |
| `file:///history.html` | History page |
| `file:///downloads.html` | Downloads page |
| `file:///passwords.html` | Passwords page |
| `file:///extensions.html` | Extensions page |
| `file:///about.html` | About page |
| `file:///release_notes.html` | Release notes |
| `file:///ui.html` | Main chrome UI |
| `file:///menu.html` | Menu overlay |
| `file:///contextmenu.html` | Context menu |
| `file:///suggestions.html` | Suggestions overlay |
| `file:///downloads-panel.html` | Downloads panel |
| `about:blank` | Empty pages |

If a session contains **only** internal pages, the restore bar will not appear.

---

## Files Modified

### Source Files
- `src/UI.cpp` - Session management logic, restore bar functions
- `src/UI.h` - Method declarations, state variables

### Asset Files
- `assets/ui.html` - Session restore bar HTML and JavaScript
- `assets/ui.css` - Session restore bar styling
- `assets/settings_catalog.json` - Session restore settings entries

---

## Testing Checklist

1. **Basic Restore Flow**
   - [ ] Open browser, navigate to external site (google.com)
   - [ ] Close with ALT+F4
   - [ ] Reopen - restore bar should appear
   - [ ] Click Restore - tab should load saved URL

2. **Multiple Tabs**
   - [ ] Open 2+ tabs with same URL
   - [ ] Force close
   - [ ] Restore - all tabs should be restored (including duplicates)

3. **Internal Page Filtering**
   - [ ] Open only settings/history pages
   - [ ] Close and reopen
   - [ ] Restore bar should NOT appear

4. **Start Fresh**
   - [ ] With restore bar showing, click "Start Fresh"
   - [ ] Bar should dismiss, new session should start
   - [ ] Close and reopen - restore bar should show new tab only

5. **Session Protection**
   - [ ] With restore bar visible, verify session file not overwritten
   - [ ] Session should preserve original URLs until user decides

---

## Known Limitations

1. **Tab Scroll Position:** Scroll position within pages is not preserved
2. **Form Data:** Unsaved form data is not preserved
3. **Tab History:** Back/forward history within tabs is not preserved
4. **Pinned Tabs:** No support for pinned tab state yet

---

## Future Improvements

- [ ] Preserve scroll position per tab
- [ ] Add "Always restore" option to skip prompt
- [ ] Preserve tab groups (if implemented)
- [ ] Add session history (restore from older sessions)
- [ ] Sync sessions across devices
