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
│   ├── theme.js              # JavaScript theme manager (15 built-in themes)
│   ├── dark.json             # Dark theme definition
│   ├── light.json            # Light theme definition
│   ├── midnight.json         # Midnight Blue theme
│   └── ...                   # Additional theme files
├── themes.html               # Theme management page
├── ui.html                   # Browser UI (imports theme system)
├── ui.css                    # Browser chrome styling (uses CSS variables)
├── chrome-tabs.css           # Tab bar styling (uses CSS variables)
└── ...

src/
├── ThemeManager.h            # C++ theme manager header
├── ThemeManager.cpp          # C++ theme manager implementation
└── ...
```

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

## C++ Backend

The `ThemeManager` class handles theme persistence and provides native bindings.

### Header (ThemeManager.h)

```cpp
class ThemeManager {
public:
    struct Theme {
        std::string id;
        std::string name;
        std::string description;
        std::string author;
        std::string version;
        bool is_builtin;
        std::map<std::string, std::string> colors;
    };

    void Initialize(const std::filesystem::path& storage_dir);
    std::string GetActiveThemeId() const;
    bool SetActiveTheme(const std::string& theme_id);
    std::string GetCustomThemesJSON() const;
    bool SaveCustomThemes(const std::string& json);
    // ... more methods
};
```

### Storage

Themes are stored in the browser's settings directory:
- `custom_themes.json` - Array of custom theme objects
- `theme_settings.json` - Active theme preference

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

1. Create a theme JSON file in `assets/themes/`
2. Add the theme definition to `ThemeManager::LoadBuiltinThemes()` in `ThemeManager.cpp`
3. Add the theme to the `DEFAULT_THEMES` object in `theme.js`
4. Test thoroughly on all pages
5. Submit a pull request with screenshots

---

*Last updated: December 2024*
