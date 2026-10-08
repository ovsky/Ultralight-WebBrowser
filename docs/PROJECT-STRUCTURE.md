# Ultralight WebBrowser - Project Structure

This document describes the organization and structure of the Ultralight WebBrowser codebase.

## Table of Contents

1. [Root Directory](#root-directory)
2. [Source Code (`src/`)](#source-code-src)
3. [Assets (`assets/`)](#assets-assets)
4. [Build & Configuration](#build--configuration)
5. [Documentation (`docs/`)](#documentation-docs)
6. [Tests (`tests/`)](#tests-tests)
7. [File Naming Conventions](#file-naming-conventions)
8. [Adding New Features](#adding-new-features)

---

## Root Directory

```
Ultralight-alt/
├── .github/               # GitHub Actions workflows and templates
├── .vscode/               # VS Code workspace settings and tasks
├── assets/                # Frontend assets (HTML, CSS, JS, images)
├── build/                 # CMake build output (generated)
├── cmake/                 # CMake helper modules and scripts
├── crashdumps/            # Crash dump files for debugging
├── data/                  # Ultralight SDK binaries and resources
├── docs/                  # Project documentation
├── downloads/             # Default downloads directory
├── libs/                  # External libraries and dependencies
├── scripts/               # Build and utility scripts
├── setup/                 # Installation and setup files
├── src/                   # C++ source code
├── tests/                 # Unit tests
├── CMakeLists.txt         # Main CMake configuration
├── LICENSE                # Project license
└── README.md              # Project readme
```

---

## Source Code (`src/`)

The C++ backend source code is organized by component:

```
src/
├── main.cpp               # Application entry point
├── Browser.h/cpp          # Main browser controller
├── Tab.h/cpp              # Tab management and state
├── UI.h/cpp               # UI rendering and interactions
│
├── # Feature Modules
├── AdBlocker.h/cpp        # Ad and content blocking
├── BookmarkStore.h/cpp    # Bookmark storage and management
├── DownloadManager.h/cpp  # Download handling
├── ExtensionManager.h/cpp # Browser extensions
├── PasswordManager.h/cpp  # Password storage (encrypted)
├── Settings.h/cpp         # User settings management
├── ThemeManager.h/cpp     # Theme application and storage
│
├── # Utilities
├── Utils.h/cpp            # General utility functions
├── resource.h             # Windows resource definitions
│
└── drm/                   # DRM (Widevine) integration
    ├── DRMSettings.h/cpp  # DRM configuration
    └── ...                # DRM implementation files
```

### Key Classes

| Class | Purpose |
|-------|---------|
| `Browser` | Main application controller, manages windows and tabs |
| `Tab` | Represents a browser tab with its own web view |
| `UI` | Renders the browser chrome (toolbar, tabs, panels) |
| `AdBlocker` | Blocks ads and trackers using filter lists |
| `BookmarkStore` | SQLite-based bookmark storage |
| `DownloadManager` | Handles file downloads |
| `ThemeManager` | Applies and persists themes |

---

## Assets (`assets/`)

Frontend assets are organized by function:

```
assets/
├── # Core Browser UI
├── ui.html                # Main browser UI shell
├── ui.css                 # Browser chrome styling (uses CSS variables)
├── ui.js                  # Browser UI JavaScript
├── chrome-tabs.css        # Tab bar styling
├── chrome-tabs.js         # Tab bar functionality
│
├── # Browser Pages
├── about.html             # About page
├── bookmarks.html         # Bookmarks management
├── downloads.html         # Downloads page
├── extensions.html        # Extensions page
├── history.html           # Browsing history
├── new_tab_page.html      # New tab page with search
├── passwords.html         # Password manager
├── release_notes.html     # Release notes
├── settings.html          # Settings page
├── themes.html            # Theme management
│
├── # Panels & Components
├── contextmenu.html       # Right-click context menu
├── downloads-panel.html   # Downloads toolbar panel
├── menu.html              # Main menu dropdown
├── quick-inspector.html   # Developer tools panel
├── suggestions.html       # Address bar suggestions
│
├── # Theme System
├── themes/
│   ├── theme-variables.css  # CSS custom properties
│   ├── theme.js             # Theme manager JavaScript
│   └── *.json               # Theme definitions
│
├── # Libraries
├── anchorme.js            # URL detection library
├── draggabilly.pkgd.min.js # Drag functionality
│
├── # Data Files
├── blocklist.txt          # Ad blocking filter list
├── drm_sites.json         # DRM-enabled site list
├── popular_sites.json     # Popular sites for new tab
├── settings_catalog.json  # Settings definitions
├── shortcuts.json         # Keyboard shortcut mappings
│
├── # Images
├── icon.png               # Browser icon
├── logo.png               # Logo image
├── spinner.svg            # Loading spinner
├── earth.svg              # Globe icon
│
├── # Special Directories
├── extensions/            # Installed extensions
├── filters/               # Content filter lists
└── static-sites/          # Cached static site data
```

### Theme System Files

The theme system spans multiple files:

| File | Purpose |
|------|---------|
| `themes/theme-variables.css` | Defines all CSS custom properties for theming |
| `themes/theme.js` | JavaScript theme manager (applies themes, stores preferences) |
| `themes/*.json` | Individual theme definitions |
| `themes.html` | Theme management UI |
| `ui.css` | Uses CSS variables for toolbar, address bar, menus |
| `chrome-tabs.css` | Uses CSS variables for tabs |

### CSS Variable Categories

The theme system uses these variable categories:

- `--color-bg-*` - Background colors (primary, secondary, elevated, hover)
- `--color-text-*` - Text colors (primary, secondary, muted)
- `--color-border-*` - Border colors
- `--color-accent-*` - Accent/brand colors
- `--color-success/warning/danger/info` - Status colors
- `--toolbar-*` - Toolbar-specific colors
- `--tab-*` - Tab-specific colors
- `--menu-*` - Dropdown menu colors
- `--btn-*` - Button colors
- `--input-*` - Form input colors
- `--radius-*` - Border radius values
- `--spacing-*` - Spacing values
- `--font-*` - Typography values

---

## Build & Configuration

```
├── CMakeLists.txt         # Main CMake configuration
├── cmake/
│   └── *.cmake            # CMake modules
├── browser.rc             # Windows resource script
├── browser.ico            # Application icon
├── .vscode/
│   └── tasks.json         # VS Code build tasks
└── scripts/
    ├── compilation_*.ps1  # PowerShell build scripts
    └── ...
```

### Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run
./build/Release/Ultralight-WebBrowser.exe
```

---

## Documentation (`docs/`)

```
docs/
├── PROJECT-STRUCTURE.md   # This file
├── Theme-System.md        # Theme system documentation
├── Session-Restore.md     # Session restore feature
├── DRMManualChecklist.md  # DRM testing checklist
├── CI-CD-ENHANCEMENTS.md  # CI/CD improvements
├── CI-CD-PERFORMANCE.md   # CI/CD performance optimizations
└── General-Codebase-Refactor.md  # Code refactoring notes
```

---

## Tests (`tests/`)

```
tests/
├── BookmarkStoreTest.cpp  # Bookmark storage tests
├── DRMSettingsTest.cpp    # DRM settings tests
├── UtilsTest.cpp          # Utility function tests
└── ...
```

Tests use Google Test framework. Run with:

```bash
./build/Release/UtilsTest.exe
./build/Release/BookmarkStoreTest.exe
```

---

## File Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| C++ Headers | PascalCase.h | `BookmarkStore.h` |
| C++ Source | PascalCase.cpp | `BookmarkStore.cpp` |
| HTML Pages | lowercase-dashes.html | `new-tab-page.html` |
| CSS Files | lowercase-dashes.css | `chrome-tabs.css` |
| JavaScript | lowercase.js or camelCase.js | `theme.js` |
| JSON Data | lowercase_underscores.json | `drm_sites.json` |

---

## Adding New Features

### Adding a New Browser Page

1. Create the HTML file in `assets/`:
   ```
   assets/my-feature.html
   ```

2. Include theme system:
   ```html
   <link rel="stylesheet" href="themes/theme-variables.css">
   <script src="themes/theme.js"></script>
   ```

3. Add navigation from menu or settings

### Adding a New C++ Component

1. Create header and source files in `src/`:
   ```
   src/MyFeature.h
   src/MyFeature.cpp
   ```

2. Add to `CMakeLists.txt`:
   ```cmake
   set(BROWSER_SOURCES
       src/MyFeature.cpp
       ...
   )
   ```

3. Register any JavaScript bindings in `Browser.cpp`

### Adding a New Theme

1. Create theme definition in `assets/themes/`:
   ```json
   {
     "id": "my-theme",
     "name": "My Theme",
     "colors": { ... }
   }
   ```

2. Or add to built-in themes in `theme.js` under `DEFAULT_THEMES`

---

## Architecture Notes

### Communication Flow

```
User Input
    ↓
ui.js (JavaScript)
    ↓
Native* functions (JS→C++ bridge)
    ↓
Browser.cpp / Tab.cpp (C++ handlers)
    ↓
Ultralight WebKit (rendering)
```

### Theme System Flow

```
theme.js (loads theme)
    ↓
CSS Variables (:root { --color-*: ... })
    ↓
ui.css / chrome-tabs.css (use var(--*))
    ↓
ThemeManager.cpp (persists to settings)
```

---

## Dependencies

- **Ultralight SDK**: Lightweight web rendering engine
- **SQLite**: Local database storage
- **Google Test**: Unit testing framework (dev)

---

*Last updated: 2025*
