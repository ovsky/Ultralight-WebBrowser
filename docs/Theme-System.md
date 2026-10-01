# Theme System Documentation

The Ultralight WebBrowser features a comprehensive theme system that allows users to customize the browser's appearance. This document covers the architecture, usage, and how to create custom themes.

## Table of Contents

1. [Overview](#overview)
2. [File Structure](#file-structure)
3. [Built-in Themes](#built-in-themes)
4. [Using Themes](#using-themes)
5. [Creating Custom Themes](#creating-custom-themes)
6. [Theme File Format](#theme-file-format)
7. [CSS Variables Reference](#css-variables-reference)
8. [JavaScript API](#javascript-api)
9. [C++ Backend](#c-backend)
10. [Best Practices](#best-practices)

---

## Overview

The theme system provides:

- **15 built-in themes**: 13 dark themes and 2 light themes
- **Full browser theming**: Themes affect toolbar, tabs, address bar, menus, and all pages
- **Custom theme creation**: Users can create and save their own themes
- **Import/Export**: Share themes as JSON files
- **Click-to-apply**: Themes apply immediately on selection
- **Persistent storage**: Themes are saved and remembered across sessions

---

## File Structure

```
assets/
├── themes/
│   ├── theme-variables.css   # Central CSS variables
│   ├── theme.js              # JavaScript theme engine (9 built-in themes)
│   ├── dark.json             # Legacy palette, surfaced as "Dark (Classic)"
│   ├── light.json            # Legacy palette, surfaced as "Light (Classic)"
│   ├── midnight.json         # Legacy palette, surfaced as "Midnight (Classic)"
│   ├── nord.json             # Legacy palette, surfaced as "Nord (Classic)"
│   └── monokai.json          # Legacy palette, surfaced as "Monokai (Classic)"
├── themes.html               # Theme management page
├── ui.html                   # Browser UI (imports theme system)
├── ui.css                    # Browser chrome styling (uses CSS variables)
├── chrome-tabs.css           # Tab bar styling (uses CSS variables)
└── ...

src/
├── ThemeManager.h/cpp        # Native persistence: validation + atomic writes
└── UI.cpp                    # Binds the bridges, delegates to ThemeManager
```

### Where the split is

`theme.js` is the engine. It owns the theme objects, the CSS-variable
generation, applying a theme to a page, and the editing logic.

`src/ThemeManager.cpp` is the native backing store. It owns nothing about how a
theme looks; it owns where the data lives, whether it is well-formed, and how it
is written. Those are things JavaScript cannot be trusted with — every value
arriving over the bridge comes from a settings UI or an imported file.

An earlier revision shipped a *complete second engine* in C++ — theme objects,
JSON parsing and serialisation, add/remove/import/export — that was never
constructed anywhere in the project, while `theme.js` defined its own
`ThemeManager` class. Two full implementations, one of them dead. That
duplication is resolved: the engine stays in JavaScript, and the C++ side is
narrowed to persistence and validation.

---

## Built-in Themes

### Dark Themes (13)

| Theme | Description | Accent Color |
|-------|-------------|--------------|
| **Dark (Default)** | Default dark purple theme | `#6C63FF` |
| **Midnight** | Deep midnight black | `#818CF8` |
| **Dracula** | Classic Dracula dark theme | `#BD93F9` |
| **One Dark** | Atom One Dark inspired | `#E5C07B` |
| **Gruvbox Dark** | Retro groove color scheme | `#D79921` |
| **Catppuccin Mocha** | Soothing pastel theme | `#CBA6F7` |
| **Tokyo Night** | Tokyo nighttime palette | `#7AA2F7` |
| **Ayu Dark** | Ayu dark mirage | `#FFB454` |
| **Solarized Dark** | Classic Solarized dark | `#268BD2` |
| **Material Dark** | Material Design dark | `#82AAFF` |
| **Nord** | Arctic, north-bluish palette | `#88C0D0` |
| **Monokai** | Classic Monokai colors | `#A6E22E` |
| **Ocean Deep** | Deep ocean blues | `#64D2FF` |

### Light Themes (2)

| Theme | Description | Accent Color |
|-------|-------------|--------------|
| **Light** | Clean bright theme | `#0969DA` |
| **Light Soft** | Softer cream-tinted light | `#059669` |

---

## Using Themes

### Accessing the Theme Manager

1. Click the **menu button** (three dots) in the toolbar
2. Select **"Themes"** from the dropdown
3. The Theme Management page opens in a new tab

### Applying a Theme

1. Browse available themes in the grid
2. **Click on any theme** to apply it immediately
3. The theme is applied to the entire browser including:
   - Navigation bar and toolbar
   - Tab bar
   - Address bar
   - All browser pages (history, bookmarks, settings, etc.)
   - Session restore bar
   - Menus and dropdowns

### Importing a Theme

1. Click the **"Import"** button
2. Select a `.json` theme file from your computer
3. The theme is imported and appears in Custom Themes

### Exporting a Theme

1. Click **"Create Theme"** or edit an existing custom theme
2. Configure the theme as desired
3. Click **"Export"** in the modal footer
4. Save the downloaded JSON file

---

## Creating Custom Themes

### Quick Start

1. Open the Themes page
2. Click **"Create Theme"**
3. Fill in the theme details:
   - **Name**: A descriptive name
   - **Description**: What makes this theme special
   - **Author**: Your name
   - **Base Theme**: Choose a starting point
4. Switch to the **"Colors"** tab
5. Modify colors using the color pickers
6. Preview your changes in the **"Preview"** tab
7. Click **"Save Theme"**

### Duplicating an Existing Theme

1. Find a built-in or custom theme you like
2. Click **"Duplicate"**
3. The theme editor opens with all colors copied
4. Modify as needed and save

---

## Theme File Format

Themes are stored as JSON files with the following structure:

```json
{
    "id": "my-theme",
    "name": "My Custom Theme",
    "description": "A beautiful custom theme",
    "author": "Your Name",
    "version": "1.0.0",
    "isBuiltIn": false,
    "colors": {
        "color-bg-primary": "#16151d",
        "color-bg-secondary": "#1e1e2e",
        "color-text-primary": "#e4e4ef",
        "color-accent-primary": "#6C63FF",
        ...
    }
}
```

### Required Fields

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Unique identifier (auto-generated for custom themes) |
| `name` | string | Display name shown in the UI |
| `colors` | object | Map of CSS variable names to color values |

### Optional Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `description` | string | "" | Brief description of the theme |
| `author` | string | "User" | Theme creator's name |
| `version` | string | "1.0.0" | Semantic version number |
| `isBuiltIn` | boolean | false | Whether this is a built-in theme |

---

## CSS Variables Reference

### Background Colors

| Variable | Description | Default (Dark) |
|----------|-------------|----------------|
| `--color-bg-primary` | Main background | `#16151d` |
| `--color-bg-secondary` | Secondary surfaces | `#1e1e2e` |
| `--color-bg-tertiary` | Toolbar, elevated areas | `#232330` |
| `--color-bg-elevated` | Cards, modals | `#282839` |
| `--color-bg-hover` | Hover state backgrounds | `#343446` |
| `--color-bg-active` | Active/pressed states | `#3d3d5c` |
| `--color-bg-overlay` | Modal overlays | `rgba(22, 21, 29, 0.95)` |

### Text Colors

| Variable | Description | Default (Dark) |
|----------|-------------|----------------|
| `--color-text-primary` | Main text | `#e4e4ef` |
| `--color-text-secondary` | Secondary text | `#c4c2d0` |
| `--color-text-tertiary` | Tertiary/muted text | `#9999b3` |
| `--color-text-muted` | Very muted text | `#71718a` |
| `--color-text-disabled` | Disabled text | `#636074` |

### Border Colors

| Variable | Description | Default (Dark) |
|----------|-------------|----------------|
| `--color-border-primary` | Main borders | `#313146` |
| `--color-border-secondary` | Subtle borders | `#252532` |
| `--color-border-hover` | Hover state borders | `#404060` |
| `--color-border-focus` | Focus ring color | `#4a4a6a` |

### Accent Colors

| Variable | Description | Default (Dark) |
|----------|-------------|----------------|
| `--color-accent-primary` | Primary accent | `#6C63FF` |
| `--color-accent-secondary` | Secondary accent | `#7c6aef` |
| `--color-accent-hover` | Accent hover state | `#8a83ff` |
| `--color-accent-light` | Light accent background | `rgba(108, 99, 255, 0.15)` |

### Status Colors

| Variable | Description | Default (Dark) |
|----------|-------------|----------------|
| `--color-success` | Success/positive | `#6aef8a` |
| `--color-warning` | Warning/caution | `#f0b866` |
| `--color-danger` | Error/destructive | `#ef6a6a` |
| `--color-info` | Informational | `#6ac0ef` |

### Component Colors

| Variable | Description | Default (Dark) |
|----------|-------------|----------------|
| `--toolbar-bg` | Toolbar gradient | `linear-gradient(...)` |
| `--menu-bg` | Menu background | `#2b2b38` |
| `--card-bg` | Card background | `#282839` |
| `--btn-primary-bg` | Primary button | `#6C63FF` |
| `--input-bg` | Input background | `#32324a` |
| `--scrollbar-thumb` | Scrollbar | `#3d3d5c` |
| `--tooltip-bg` | Tooltip background | `rgba(43, 43, 56, 0.95)` |

---

## JavaScript API

The theme system exposes a global `ThemeManager` object:

```javascript
// Get all available themes (built-in + custom)
const themes = ThemeManager.getAllThemes();

// Apply a theme by ID
ThemeManager.applyTheme('dark');

// Get the current theme
const current = ThemeManager.getCurrentTheme();

// Create a new custom theme
const newTheme = ThemeManager.createTheme({
    name: 'My Theme',
    description: 'A custom theme',
    colors: { ... }
});

// Update an existing custom theme
ThemeManager.updateTheme('theme-id', {
    name: 'Updated Name',
    colors: { ... }
});

// Delete a custom theme
ThemeManager.deleteTheme('theme-id');

// Duplicate a theme for customization
const copy = ThemeManager.duplicateTheme('dark');

// Export theme as JSON string
const json = ThemeManager.exportTheme('theme-id');

// Import theme from JSON string
const imported = ThemeManager.importTheme(jsonString);
```

### Events

Listen for theme changes:

```javascript
window.addEventListener('themeChanged', (e) => {
    console.log('Theme changed to:', e.detail.themeId);
    console.log('Theme data:', e.detail.theme);
});
```

---

## Native persistence

`theme.js` is the entire theme engine — it owns the theme objects, the CSS
variable generation, and applying a theme. Persistence is the only part that
reaches native code.

It probes for four globals on every page that loads it:

| Global | Purpose |
|--------|---------|
| `NativeGetSetting('theme')` | Returns the saved theme id, or `""` if none is stored |
| `NativeSetSetting('theme', id)` | Saves the active theme id |
| `NativeGetThemes()` | Returns the custom-themes JSON object, or `"{}"` |
| `NativeSaveThemes(json)` | Saves the custom-themes object verbatim |
| `NativeGetThemeOverrides()` | Returns the built-in override object, or `"{}"` |
| `NativeSaveThemeOverrides(json)` | Saves the override object verbatim |
| `NativeGetSeedThemes()` | Returns the legacy `assets/themes/*.json` definitions, keyed by file stem |

These are bound on **every** UI-owned view in `UI::OnDOMReady`. That matters
because `theme.js` is loaded by nine separate pages and each one checks for the
globals independently — binding them only on the chrome overlay would leave
every internal page on the `localStorage` fallback and the theme would differ
between pages.

### Storage

Three files in the browser's settings directory:

- `active_theme.txt` — the active theme id as a bare string
- `custom_themes.json` — user-created themes
- `theme_overrides.json` — user edits layered on the shipped palettes

The two blobs are stored and returned verbatim: the engine builds them with
`JSON.stringify` and expects them back unchanged, so re-serialising them here
would need a parser this class has no other use for.

Writes go through a temporary file and a rename, so an interrupted write cannot
leave a truncated file that would break theme loading on every page.

### Validation

Both bridges take untrusted input — theme ids come from the settings UI and from
imported JSON — so values are checked before they reach disk:

- Theme ids must be 1–64 characters of `[A-Za-z0-9_-]`. No quote, newline or path
  separator can reach the file.
- The custom-themes blob must be a non-blank `{...}` object within 4 MB. It is
  produced by `JSON.stringify`, so requiring an object catches empty strings and
  truncated writes without a full parser.

Rejected values are logged to stderr and discarded; the previous stored value is
left intact rather than being cleared.

### Migration from `localStorage`

`theme.js` still writes to `localStorage` as a backup, but reads prefer native
storage **only when it actually holds a value**:

- `getSavedThemeId()` returns the native id if present, otherwise falls back to
  `localStorage`, otherwise `'dark'`.
- `loadCustomThemes()` parses the native blob unless it is `"{}"`, and only then
  falls back to `localStorage`.

An earlier revision returned `NativeGetSetting('theme') || 'dark'` directly,
which would have reset every existing user's theme selection to dark the first
time an updated page loaded. The fall-through avoids that.

---

## Editing themes

Every theme is editable, including the built-ins. Open the theme management page
and use **Edit** on any card.

The editor builds its inputs from the theme's own colour keys rather than a
hardcoded list, so built-in, legacy and user themes all get an editor without a
separate key list per palette. Values matching `#rrggbb` get a native colour
picker; `rgba()`, `hsl()` and non-colour tokens such as radii are edited as text,
because a picker cannot represent them.

### Built-ins: override, never mutate

Editing a built-in does **not** modify `DEFAULT_THEMES`. The change is stored as
an entry in `theme_overrides.json`, and `getAllThemes()` merges it on top:

```
built-ins  <  legacy variants  <  overrides  <  custom themes
```

Keeping the shipped object untouched is what makes **Reset** reliable: dropping
the override restores the original palette exactly, with no need to keep a
backup copy of the defaults. A modified built-in shows an *Edited* badge.

### Legacy variants

The five `assets/themes/*.json` files predate the engine and were never read by
any code. They are close to, but not identical to, the five built-ins they share
names with — and for `nord` and `monokai` the JSON is actually **richer** (54
colour keys against 38 in `theme.js`).

They are therefore surfaced as selectable `* (Classic)` variants under
`classic_*` ids rather than being discarded. The ids are namespaced on purpose:
the file stems collide with built-in ids, so an un-namespaced load would silently
replace the in-engine palettes in the merge and make Reset meaningless.

Editing a variant promotes it to one of your own themes; it then behaves like any
other custom theme.

---

## Best Practices

### Color Contrast

Ensure sufficient contrast between text and backgrounds:
- Text on backgrounds: Minimum 4.5:1 contrast ratio
- Large text: Minimum 3:1 contrast ratio
- Interactive elements: Clearly distinguishable states

### Consistency

- Use the accent color consistently for interactive elements
- Maintain visual hierarchy with proper use of text colors
- Keep hover/active states visually connected to their base states

### Testing

Before sharing a theme:
1. Test on all major browser pages (history, bookmarks, settings, etc.)
2. Check readability of all text elements
3. Verify button and input visibility
4. Test in different lighting conditions

### Naming

- Use descriptive names that hint at the color palette
- Include version numbers when making significant changes
- Credit original themes if creating variations

---

## Troubleshooting

### Theme Not Applying

1. Check browser console for errors
2. Verify the theme JSON is valid
3. Try reloading the page
4. Clear browser data and re-import the theme

### Colors Not Updating

1. Some pages may cache styles - try hard refresh
2. Check if the variable name matches exactly
3. Verify the color value format (hex, rgb, rgba)

### Import Failures

1. Ensure the file is valid JSON
2. Check that required fields (`name`, `colors`) exist
3. Verify color values are properly quoted strings

---

## Contributing

To add a new built-in theme:

1. Add the theme definition to the `DEFAULT_THEMES` object in
   `assets/themes/theme.js`. That object is the single source of truth — there is
   no C++ registry and no separate theme file to keep in sync.
2. Confirm the theme exposes the colour keys the engine reads. `applyTheme()`
   derives most of the ~40 CSS variables from a small set of base keys and falls
   back to hardcoded values for the rest.
3. Register `assets/themes.html` and `assets/bookmarks.html` awareness if you add
   a *new* internal page that loads `theme.js`, so it is listed in
   `IsBrowserInternalPage()` and `IsInternalBrowserPage()` in `UI.cpp`. Skipping
   this applies the invert-filter dark-mode hack on top of a correctly themed
   page.
4. Test on all pages, including the ones with their own dark styling.
5. Submit a pull request with screenshots.

---

*Last updated: December 2024*
