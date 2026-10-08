# Ultralight Web Browser ✨
> Ultra‑fast / Ultra‑light / Ultra‑portable

[![Build - All (x64 + ARM64)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-all.yml/badge.svg?branch=dev)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-all.yml?query=branch%3Adev)
[![Build - Linux (x64)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-linux.yml/badge.svg?branch=dev)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-linux.yml?query=branch%3Adev)
[![Build - Linux (ARM64)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-linux-arm64.yml/badge.svg?branch=dev)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-linux-arm64.yml?query=branch%3Adev)
[![Build - macOS (x64)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-macos.yml/badge.svg?branch=dev)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-macos.yml?query=branch%3Adev)
[![Build - macOS (ARM64)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-macos-arm64.yml/badge.svg?branch=dev)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-macos-arm64.yml?query=branch%3Adev)
[![Build - Windows (x64)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-windows.yml/badge.svg?branch=dev)](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-windows.yml?query=branch%3Adev)

<p align="center">
  <img src="https://github.com/ultralight-ux/Ultralight/raw/master/media/logo.png" width="200" alt="Ultralight Logo">
</p>

<strong>A native C++ proof‑of‑concept browser focused on minimal overhead, cold‑start speed, and resource efficiency.</strong><br/>
No multi‑process bloat, no background daemons, no gigabytes of RAM for a handful of tabs—just a lean renderer + native UI.

> Download: [Ultralight Web → Browser Releases Page](https://github.com/ovsky/Ultralight-WebBrowser/releases) | Status: Development / Experimental / Educational
---

<img width="1366" height="768" alt="ultralight-downloads" src="https://github.com/user-attachments/assets/fe2c4609-6930-483c-9fa3-eab64664b539" />

---

## 🧭 Table of Contents
1. [Why Ultralight?](#-why-ultralight-ditch-the-bloat)
2. [Project Philosophy & Goals](#-project-philosophy--goals)
3. [Supported Platforms & Architectures](#-supported-platforms--architectures)
4. [Get the App](#-get-the-app)
5. [Installation](#-installation)
6. [Features](#-features)
7. [Recent Updates](#-recent-updates)
8. [Tech Stack](#-tech-stack)
9. [Build From Source](#-build-from-source)
10. [ARM64 Build Notes](#-arm64-build-notes)
11. [JavaScript Bridge API](#-javascript-bridge-api-window__ul)
12. [Create Packages Locally](#-create-packages-locally-optional)
13. [CI / Automation](#-ci--automation)
14. [Roadmap](#-roadmap--ideas)
15. [Troubleshooting](#-troubleshooting)
16. [Contributing](#-contributing)
17. [Security & Privacy](#-security--privacy)
18. [License](#-license)
19. [Acknowledgements](#-acknowledgements)
20. [Disclaimer](#-disclaimer)

---

## 🚀 Why Ultralight? Ditch the Bloat
Traditional browsers (and desktop web stacks like Electron / CEF) embed full, sandboxed operating systems (Chromium). They are powerful—but heavy. This project explores how far you can go by combining a lightweight GPU renderer with a native shell for a dramatically smaller footprint and near‑instant startup.

<p align="center">
  <img src="https://ultralig.ht/media/base-memory-usage.webp" width="600" alt="Memory Comparison">
</p>

Result: lower memory pressure, near‑instant cold starts, smaller footprint, simple embedding, and much more.

---

## 🎯 Project Philosophy & Goals
| Feature | Ultralight (This Project) | Electron / CEF |
|--------|---------------------------|----------------|
| Performance | ⚡ Up to 6× faster in simple page render ops | Chromium baseline CEF |
| Memory Usage | 🧠 ~1/10 RAM (no multi-process sandbox) | High (multi-process JS + GPU + Extensions) |
| Startup | 🚀 < 1s typical | 3–5s cold start |
| Disk Footprint | 📦 ~30–50 MB packaged | 1+ GB (runtime + Cache) |
| Rendering | 🎨 Lightweight GPU | Full Chromium CEF Stack |
| Architecture | 🧱 Native C++ + pixel buffer compositing | Node.js + Chromium + Interop Bridge |

Goals:
- Showcase minimal native browser shell design.
- Provide reference for Ultralight SDK Browser.
- Preliminary lightweight content / ad-block / tracking-block.
- Highlight performance vs conventional heavy frameworks.

---

## 🖥️ Supported Platforms & Architectures
| Platform | Architectures | CI Artifacts | Notes |
|----------|---------------|--------------|-------|
| Windows 10/11+ | x64 | Portable ZIP, optional NSIS installer | ARM64 SDK not yet published by Ultralight |
| macOS 12+ | x64, arm64 | TGZ, optional DMG | ARM64 auto‑detected when runner host is ARM64 |
| Linux (Ubuntu/Fedora etc.) | x64, arm64 | TGZ / DEB / RPM | ARM64 requires aarch64 runner; workflow automatically includes detection & fallback |

ARM64 archives are probed automatically when available in the `base-sdk` branch (eg: `ultralight-free-sdk-<ver>-linux-arm64.7z`, `...-mac-arm64.7z`). Current public CI uses x64 runners; arm64 builds may require:
- Self‑hosted runner (Apple Silicon / aarch64 Linux)
- Future strategy matrix addition (see CI section)

---

## 📥 Get the App
Official tagged releases:
[🎉 Ultralight Web → Browser Releases Page](https://github.com/ovsky/Ultralight-WebBrowser/releases)

Development (continuous) artifacts (latest successful `dev` workflow runs):

| Platform | Architectures | Packages | Latest Runs |
|----------|----------------|----------|-------------|
| Linux | x64, arm64* | TGZ, DEB, RPM | [Open runs](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-linux.yml?query=branch%3Adev) |
| macOS | x64, arm64 | TGZ, DMG | [Open runs](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-macos.yml?query=branch%3Adev) |
| Windows | x64 | ZIP (portable) / optional Installer | [Open runs](https://github.com/ovsky/Ultralight-WebBrowser/actions/workflows/build-windows.yml?query=branch%3Adev) |

Notes:
- ✅ Linux AMD64 artifacts appear when an x64 runner is used or when `ULTRALIGHT_SDK_URL` points to an AMD64 SDK.
- ✅ Linux ARM64 artifacts appear when an aarch64 runner is used or when `ULTRALIGHT_SDK_URL` points to an arm64 SDK.
- ✅ Windows AMD64 artifacts appear when an aarch64 runner is used or when `ULTRALIGHT_SDK_URL` points to an arm64 SDK.
- ✅ macOS ARM64 artifacts are produced automatically when the CI runner is Apple Silicon.
- ✅ macOS AMD64 artifacts appear when an x64 runner is used or when `ULTRALIGHT_SDK_URL` points to an AMD64 SDK.
- 🕒 Windows ARM64 artifacts are not yet published by Ultralight [SOON™].

How to fetch artifacts:
1. Open the workflow link for your platform/arch.
2. Click the latest green run (success).
3. Scroll to “Artifacts” (bottom) and download the desired archive.

---

## 📥 Installation

<details>
<summary><b>🟦  Windows</b></summary>

---

**Installer**:
1. Download `Ultralight-WebBrowser-Installer.exe` from [Releases](https://github.com/ovsky/Ultralight-WebBrowser/releases)
2. Install it and get happy!

---

**Portable**:
1. Download `c` from [Releases](https://github.com/ovsky/Ultralight-WebBrowser/releases)
2. Extract to any folder
3. Run `Ultralight-WebBrowser.exe`

*Optional: Use the NSIS installer (`*-Windows-Installer.exe`) if available.*
</details>

---

<details>
<summary><b>🍎 macOS</b></summary>

---
**TGZ Archive:**
```bash
tar -xzf Ultralight-WebBrowser-*.tar.gz
./Ultralight-WebBrowser
```

---

**DMG Image:**
1. Mount the DMG
2. Drag to Applications
3. Right-click → Open (first launch, to bypass Gatekeeper)

</details>

---

<details>
<summary><b>🐧 Linux</b></summary>

---

**Debian/Ubuntu (DEB):**
```bash
sudo apt install ./Ultralight-WebBrowser-*.deb
ultralight-webbrowser
```

---

**Fedora/RHEL (RPM):**
```bash
sudo dnf install ./Ultralight-WebBrowser-*.rpm
ultralight-webbrowser
```

---

**Portable (TGZ):**
```bash
tar -xzf Ultralight-WebBrowser-*.tar.gz -C ~/.local/opt
~/.local/opt/UltralightWebBrowser/Ultralight-WebBrowser
```

</details>

---

## ✨ Features

### Core Browser Functionality
- **GPU‑Accelerated Rendering** – Powered by Ultralight core engine
- **Low Memory Footprint** – Single process shell architecture (~1/10 RAM vs. Electron)
- **HTML5 / CSS3 / Modern JS Support** – Full web standards compliance
- **Multi‑Tab Interface** – Chrome-style draggable tabs with smooth animations
- **Navigation Controls** – Back / Forward / Reload / Stop / Address bar
- **Dynamic Page Indicators** – Real-time title updates and loading states
- **Responsive Resize** – Fluid layout adaptation

### Privacy & Security
- **Lightweight Ad & Tracker Filtering**
  - Domain + substring + glob pattern matching
  - Rule sources: `assets/blocklist.txt` + all `.txt` in `assets/filters/`
  - Formats: `example.com`, `0.0.0.0 example.com`, `||example.com^`, `/ads.js`, `*://*/*analytics*.js`
  - Always allowed: `file://`, `data:`
  - Toggle via toolbar icon or Settings
  - Requires SDK network interception capabilities
- **Location Spoofing** – Override geolocation with custom coordinates
  - Configurable latitude/longitude values
  - Preset city buttons (New York, London, Tokyo, Sydney, Paris)
  - Per-site geolocation override via JavaScript injection
- **Do Not Track (DNT)** – Configurable header setting
- **Clear History on Exit** – Optional automatic cleanup
- **Web Security Controls** – JavaScript, cookies, storage permissions

### User Interface & Experience
- **Modern Glassmorphic Design** – Semi-transparent overlays with backdrop blur
- **Dark Mode** – Global theme toggle with persistent preferences
- **Compact Tabs Mode** – Space-saving layout (60px UI height, 12em tab width)
- **Toolbar Icons** – Quick access to Inspector, Downloads, AdBlock, Menu
- **Bookmark System** – Save favorite sites with toolbar star icon and bookmarks bar
- **Bookmark Manager** – Organize bookmarks with folder support and quick access
- **Download Manager** – Full-featured UI with progress tracking and notifications
  - WebP to PNG conversion – Automatic conversion of downloaded WebP images
  - Configurable download location prompts
  - Download history and status tracking
- **Settings Panel** – Comprehensive configuration across 7+ categories:
  - Appearance (Dark Mode, Vibrant Window, Transparent Toolbar, Compact Tabs)
  - Privacy & Security (AdBlock, Trackers, JavaScript, Web Security, Cookies, DNT, History, Location Spoofing)
  - Address Bar & Suggestions (Autocompletion, Favicons)
  - Downloads (Badge, Auto-open Panel, Ask Location, WebP to PNG Conversion)
  - Performance (Smooth Scrolling, Hardware Acceleration, Local Storage, Database)
  - Accessibility (Reduce Motion, High Contrast, Caret Browsing)
  - Developer (Remote Inspector, Performance Overlay)
  - DRM Content (DRM WebView toggle for protected content)
- **Settings Search** – Quick search to find specific settings
- **Auto-Save Settings** – Automatic persistence of preference changes
- **Custom User Agent** – Configurable browser identification string
- **Persistent Settings** – JSON-based storage with runtime updates
- **Context Menu** – Right-click actions and shortcuts
- **Keyboard Shortcuts** – Customizable shortcut mapping system
- **Favicon Support** – Site icons in tabs and suggestions
- **Autosuggestion** – Intelligent URL/search completions with popular sites

### DRM Content Support (Experimental)
- **DRM WebView Subsystem** – Platform-native WebView for DRM-protected content
  - Windows: WebView2 (Edge/Chromium) integration
  - macOS: Native WKWebView with Cocoa/WebKit frameworks
  - Linux: WebKit2GTK integration
- **Automatic Fallback** – Seamless switching between Ultralight and native WebView
- **DRM Status Indicators** – Visual feedback for DRM content detection
- **Per-Platform Optimization** – Native framework integration for best performance

### Developer Features
- **JavaScript ↔ Native Bridge** – `window.__ul` API for deep integration
- **Local History API** – In-memory browsing history (non-persistent by default)
- **Shortcut Mapping** – JSON-based keyboard shortcut configuration
- **Debug Panel** – Runtime settings inspection and diagnostics

---

## 🆕 Recent Updates

### v0.9.6 (In Development)

#### New Features
- **Location Spoofing** – Override browser geolocation with custom coordinates
  - Configurable latitude/longitude in Settings → Privacy
  - Preset city buttons: New York, London, Tokyo, Sydney, Paris
  - JavaScript geolocation API override for privacy protection
- **WebP to PNG Conversion** – Automatic conversion of downloaded WebP images
  - Windows Imaging Component (WIC) based conversion
  - Toggle in Settings → Downloads
  - Preserves original filename with PNG extension
- **Session Restore** – Restore tabs and state from previous browsing session
- **Password Manager** – Secure credential storage and autofill

#### Improvements
- Enhanced download manager with format conversion support
- Expanded settings catalog (30+ options)
- Improved privacy controls with geolocation override

### v0.9.5 (Previous Release)

#### Major Features
- **Dark Mode** – Global theme toggle with persistent preferences
- **Settings Panel** – 26+ configurable options across 7 categories
- **Download Manager** – Full-featured UI with progress tracking
- **ARM64 Support** – Native builds for Apple Silicon and ARM Linux
- **URL Suggestions** – Intelligent autocompletion with popular sites
- **Quick Inspector** – Built-in development tools
- **Keyboard Shortcuts** – Customizable shortcut mapping system

### Earlier Merged Features (dev → main)

#### PR #45: Implement Features from Dev into Main
Major feature sync bringing all development improvements to the stable branch.

#### PR #43: Fully Functional Compact Tabs
- Dynamic compact tabs mode with live toggle
- No restart required for layout changes
- Improved tab reload and settings mutation handling

#### PR #42: Settings Auto-Save Functionality
- Automatic saving of settings changes
- Toggle option for auto-save behavior
- Seamless preference persistence

#### PR #41: Custom User Agent Setting
- Configurable browser User-Agent string
- New UI handling for setting changes
- Enhanced request customization

#### PR #40: Icons Bar Styles Fix
- Fixed toolbar icon styles and transitions
- Improved download icon behavior
- Polished visual feedback

#### PR #39: General Project Optimization
- NSIS path detection improvements
- PowerShell environment variable fixes
- Windows CI workflow enhancements

### Current Development (feature/drm-subsystem-implementation)
- **DRM WebView Subsystem** – Cross-platform native WebView integration
- **Bookmark System** – Full bookmark management with toolbar integration
- **Settings Search** – Quick-find functionality for settings panel
- **CI/CD Improvements** – Full ARM64 support for Linux and macOS

---

## 🛠️ Tech Stack

| Component | Technology |
|-----------|------------|
| **Renderer** | [Ultralight SDK](https://ultralig.ht/) |
| **Language** | C++17, Objective-C++ (macOS) |
| **Window/Input** | [GLFW](https://www.glfw.org/) |
| **Graphics** | OpenGL 3.3 |
| **Build System** | CMake + CPack |
| **CI/CD** | GitHub Actions (x64 + ARM64) |
| **DRM (Windows)** | WebView2 (Edge/Chromium) |
| **DRM (macOS)** | WKWebView (Cocoa/WebKit) |
| **DRM (Linux)** | WebKit2GTK |

---

## 🗺️ Roadmap

<table>
<tr>
<td width="33%" valign="top">

### ✅ Complete
- GPU-Accelerated Rendering
- Multi-Tab Interface
- Ad & Tracker Blocking
- Download Manager
- Settings Panel (30+ options)
- Dark Mode & Themes
- DRM WebView (all platforms)
- ARM64 Support
- Auto-Save Settings
- Custom User Agent
- Settings Search
- Compact Tabs Mode
- Location Spoofing
- WebP to PNG Conversion
- Session Restore
- Password Manager
- Extension/Plugin API
- Persistent History

</td>
<td width="33%" valign="top">

### 🚧 In Progress
- Bookmark System + Import/Export
- Directories Organization
- Accessibility Enhancements
- Performance Optimizations
- Tab Groups

</td>
<td width="33%" valign="top">

### 🔮 Planned

- Multi-Profile Support (?)
- Reader Mode
- Screenshot Tool (?)
- Custom Themes
- Sync Service (?)
- DRM Detection Improvements


</td>
</tr>
</table>

---

## 🔧 Troubleshooting

<details>
<summary><b>Common Issues</b></summary>

| Issue | Solution |
|-------|----------|
| Blank window | Verify OpenGL 3.3 support; check library files |
| Settings not loading | Check `setup/settings.json` exists and is valid JSON |
| AdBlock not working | Ensure SDK supports network interception |
| DRM not playing | Enable in Settings → DRM Content |
| DRM WebView crash (macOS) | Ensure Cocoa/WebKit frameworks available |
| DRM WebView crash (Linux) | Install `libwebkit2gtk-4.1-dev` |
| macOS Gatekeeper | Right-click → Open (first launch) |
| Bookmarks not saving | Check write permissions for storage directory |

</details>

<details>
<summary><b>Debug Tips</b></summary>

1. **Launch from terminal** to see initialization logs
2. **Enable Performance Overlay** in Settings → Developer
3. **Delete `setup/settings.json`** to reset to defaults
4. **Check `assets/blocklist.txt`** for filter syntax errors
5. **Verify SDK presence** in `data/` or `ULTRALIGHT_SDK_ROOT`

</details>

---

## 🔄 CI / Automation

### Active Workflows

| Workflow | Platform | Features |
|----------|----------|----------|
| `build-all.yml` | All | Meta-workflow (main badge) |
| `build-windows.yml` | Windows x64 | Optional NSIS installer |
| `build-macos.yml` | macOS x64 | Cocoa/WebKit linking |
| `build-macos-arm64.yml` | macOS ARM64 | Apple Silicon |
| `build-linux.yml` | Linux x64 | GTK3, WebKit2GTK |
| `build-linux-arm64.yml` | Linux ARM64 | Self-hosted/emulated |

### Environment Variables

| Variable | Purpose |
|----------|---------|
| `ULTRALIGHT_SDK_URL` | Override auto-detected SDK URL |
| `ULTRALIGHT_VERSION` | Override SDK version |
| `WEBBROWSER_VERSION` | App version label |
| `PACKAGE_GENERATORS` | CPack generators (`TGZ;DEB;RPM`) |
| `CREATE_INSTALLER` | Build NSIS installer (Windows) |

---

## 🔐 DRM WebView Notes

The DRM WebView subsystem provides native WebView integration for playing DRM-protected content (Netflix, Disney+, etc.) that cannot be rendered by the Ultralight engine.

### How It Works
1. **Detection**: Browser detects DRM-protected content via content type or site rules
2. **Fallback**: Automatically switches from Ultralight to native WebView
3. **Integration**: Native WebView overlays the main window with proper z-ordering
4. **Return**: User can switch back to Ultralight for regular browsing

### Platform Implementations

| Platform | Native WebView | DRM Support | Notes |
|----------|---------------|-------------|-------|
| Windows | WebView2 (Edge/Chromium) | Widevine, PlayReady | Requires Edge runtime |
| macOS | WKWebView | FairPlay, Widevine | Native Cocoa/WebKit |
| Linux | WebKit2GTK | Limited | WebKit2GTK 4.1 preferred |

### Enabling DRM WebView
1. Open **Settings** → **DRM Content**
2. Toggle **Enable DRM WebView**
3. Navigate to DRM-protected content
4. Browser will automatically use native WebView when needed

### Build Requirements
- **Windows**: WebView2 SDK (auto-detected by CMake)
- **macOS**: Xcode with Cocoa/WebKit frameworks
- **Linux**: `libwebkit2gtk-4.1-dev` or `libwebkit2gtk-4.0-dev`

---

## 📚 Documentation

<details>
<summary><b>🧩 JavaScript Bridge API</b></summary>

The `window.__ul` API is injected into the main frame for native integration:

```javascript
// Navigation
__ul.navigate("https://example.com");
__ul.newTab("https://example.org");
__ul.back(); __ul.forward(); __ul.reload();

// Settings
const settings = __ul.getSettingsSnapshot();
__ul.updateSetting("enable_adblock", true);
__ul.saveSettings();

// Theme
__ul.toggleDarkMode();
if (__ul.isDarkModeEnabled()) { /* ... */ }

// History
const history = __ul.getHistory();
__ul.clearHistory();

// App Info
const info = __ul.getAppInfo();
// { name: "Ultralight WebBrowser", version: "1.4.0" }
```

</details>

<details>
<summary><b>🔐 DRM WebView System</b></summary>

For DRM-protected content (Netflix, Disney+, etc.):

| Platform | WebView | DRM Support |
|----------|---------|-------------|
| Windows | WebView2 | Widevine, PlayReady |
| macOS | WKWebView | FairPlay, Widevine |
| Linux | WebKit2GTK | Limited |

**Enable:** Settings → DRM Content → Enable DRM WebView

The DRM WebView subsystem provides native WebView integration for playing protected content that cannot be rendered by the Ultralight engine.

**How It Works:**
1. Browser detects DRM-protected content via content type or site rules
2. Automatically switches from Ultralight to native WebView
3. Native WebView overlays the main window with proper z-ordering
4. User can switch back to Ultralight for regular browsing

</details>

<details>
<summary><b>📍 Location Spoofing</b></summary>

Override the browser's geolocation API with custom coordinates for privacy protection.

**Configuration:**
1. Open **Settings** → **Privacy & Security**
2. Enable **Location Spoofing**
3. Enter custom **Latitude** and **Longitude** values
4. Or use preset city buttons: New York, London, Tokyo, Sydney, Paris

**How It Works:**
- Overrides `navigator.geolocation.getCurrentPosition()`
- Overrides `navigator.geolocation.watchPosition()`
- Returns spoofed coordinates to all web pages
- Does not affect actual device location

**Preset Coordinates:**
| City | Latitude | Longitude |
|------|----------|-----------|
| New York | 40.7128 | -74.0060 |
| London | 51.5074 | -0.1278 |
| Tokyo | 35.6762 | 139.6503 |
| Sydney | -33.8688 | 151.2093 |
| Paris | 48.8566 | 2.3522 |

</details>

<details>
<summary><b>🛡️ Ad Blocking</b></summary>

Rules loaded from:
- `assets/blocklist.txt`
- `assets/filters/*.txt`

Supported formats:
```
example.com
0.0.0.0 example.com
||example.com^
/ads.js
*://*/*analytics*.js
```

Always allowed: `file://`, `data:` URLs

Toggle via toolbar icon or Settings → Privacy → Enable AdBlock

</details>

<details>
<summary><b>⚙️ Settings Categories</b></summary>

| Category | Options |
|----------|---------|
| **Appearance** | Dark Mode, Vibrant Window, Transparent Toolbar, Compact Tabs |
| **Privacy & Security** | AdBlock, Tracker Blocking, JavaScript, Cookies, DNT, Clear History, Location Spoofing |
| **Address Bar** | Autocompletion, Favicons, Suggestions |
| **Downloads** | Badge, Auto-open Panel, Location Prompt, WebP to PNG Conversion |
| **Performance** | Smooth Scrolling, Hardware Acceleration, Local Storage |
| **Accessibility** | Reduce Motion, High Contrast, Caret Browsing |
| **Developer** | Remote Inspector, Performance Overlay |
| **DRM Content** | Enable DRM WebView |

</details>

<details>
<summary><b>⌨️ Keyboard Shortcuts</b></summary>

Shortcuts are customizable via `assets/shortcuts.json`:

| Action | Default Shortcut |
|--------|------------------|
| New Tab | `Ctrl+T` |
| Close Tab | `Ctrl+W` |
| Reload | `Ctrl+R` / `F5` |
| Back | `Alt+Left` |
| Forward | `Alt+Right` |
| Address Bar | `Ctrl+L` |
| Find | `Ctrl+F` |
| Settings | `Ctrl+,` |
| Developer Tools | `F12` |

</details>

---

## 🔄 CI / Automation

### Active Workflows

| Workflow | Platform | Features |
|----------|----------|----------|
| `build-all.yml` | All | Meta-workflow (main badge) |
| `build-windows.yml` | Windows x64 | Optional NSIS installer |
| `build-macos.yml` | macOS x64 | Cocoa/WebKit linking |
| `build-macos-arm64.yml` | macOS ARM64 | Apple Silicon |
| `build-linux.yml` | Linux x64 | GTK3, WebKit2GTK |
| `build-linux-arm64.yml` | Linux ARM64 | Self-hosted/emulated |

### Environment Variables

| Variable | Purpose |
|----------|---------|
| `ULTRALIGHT_SDK_URL` | Override auto-detected SDK URL |
| `ULTRALIGHT_VERSION` | Override SDK version |
| `WEBBROWSER_VERSION` | App version label |
| `PACKAGE_GENERATORS` | CPack generators (`TGZ;DEB;RPM`) |
| `CREATE_INSTALLER` | Build NSIS installer (Windows) |

---

## 🛠️ Tech Stack

| Component | Technology |
|-----------|------------|
| **Renderer** | [Ultralight SDK](https://ultralig.ht/) |
| **Language** | C++17, Objective-C++ (macOS) |
| **Window/Input** | [GLFW](https://www.glfw.org/) |
| **Graphics** | OpenGL 3.3 |
| **Build System** | CMake + CPack |
| **CI/CD** | GitHub Actions (x64 + ARM64) |
| **DRM (Windows)** | WebView2 (Edge/Chromium) |
| **DRM (macOS)** | WKWebView (Cocoa/WebKit) |
| **DRM (Linux)** | WebKit2GTK |

---

## 🤝 Contributing

1. **Fork** the repository
2. **Create** feature branch: `git checkout -b feature/amazing-feature`
3. **Commit** changes: `git commit -m "Add amazing feature"`
4. **Push**: `git push origin feature/amazing-feature`
5. **Open** a Pull Request

**Guidelines:**
- Keep PRs focused and small
- Include performance notes if relevant
- Add tests for core logic changes
- Follow existing code style

---

## 🔒 Security & Privacy

This project:
- Does **NOT** implement hardened sandboxing found in mainstream browsers
- Stores only **ephemeral in-memory history** (no disk persistence by default)
- Performs **lightweight filtering**—no advanced tracker heuristics
- Should **not** be used for security-critical tasks without further auditing

---

## 📄 License

MIT License — see [LICENSE](./LICENSE) for details.

---

## 🙏 Acknowledgements

- [Ultralight](https://ultralig.ht/) — Lightweight HTML renderer
- [GLFW](https://www.glfw.org/) — Cross-platform windowing
- Open-source community contributors

---

<p align="center">
  <sub>
    ⚠️ <b>Disclaimer:</b> This is an educational project demonstrating minimal browser architecture.
    Not recommended for security-critical tasks without additional hardening.
  </sub>
</p>

<p align="center">
  Made with ❤️ for <a href="https://ultralig.ht/">Ultralight SDK</a> by <a href="https://github.com/ovsky">@ovsky</a>
</p>
