#pragma once

#include <filesystem>
#include <map>
#include <string>

/**
 * Native backing store for the theme engine.
 *
 * The theme engine itself is JavaScript (assets/themes/theme.js). It owns the
 * theme objects, the CSS-variable generation, and applying a theme to a page.
 * This class owns everything that has to survive a restart and that JavaScript
 * cannot be trusted with: where the data lives, whether it is well-formed, and
 * how it is written.
 *
 * An earlier revision shipped a second, complete theme engine in C++ that was
 * never constructed anywhere, so it duplicated theme.js while doing nothing.
 * That duplication is gone. What remains is the part theme.js genuinely cannot
 * do for itself — disk access with validation — exposed over a small surface
 * that the engine binds as a handful of JavaScript globals.
 *
 * Storage (under the browser settings directory):
 *   active_theme.txt     - the selected theme id
 *   custom_themes.json   - user-created themes
 *   theme_overrides.json - user edits layered on the shipped palettes
 *
 * assets/themes/*.json are legacy definitions that no code used to read. They
 * are surfaced as selectable themes instead of being discarded; see
 * GetSeedThemesJSON().
 */
namespace themes
{

    // Bounds for untrusted values crossing the JavaScript bridge.
    constexpr size_t kMaxThemeIdLength = 64;
    constexpr size_t kMaxThemeBlobBytes = 4u * 1024u * 1024u;

    class ThemeManager
    {
    public:
        explicit ThemeManager(std::filesystem::path settings_dir);

        // Creates the settings directory if needed. Safe to call more than once.
        bool Initialize();

        // Active theme id. Returns "dark" when nothing valid is stored, which is
        // the engine's own default, so callers never have to special-case it.
        std::string GetActiveThemeId() const;
        bool SetActiveThemeId(const std::string &theme_id);

        // Opaque JSON blobs for the custom-theme and override layers. They are
        // stored and returned verbatim: the engine builds them with
        // JSON.stringify and expects them back unchanged, so re-serialising here
        // would need a parser this class has no other use for.
        std::string GetCustomThemesJSON() const;
        bool SaveCustomThemesJSON(const std::string &json);

        std::string GetOverridesJSON() const;
        bool SaveOverridesJSON(const std::string &json);

        // Returns the legacy assets/themes/*.json definitions as a single JSON
        // object keyed by file stem, or "{}" when none can be read.
        std::string GetSeedThemesJSON(const std::filesystem::path &assets_dir) const;

        // Validation helpers, exposed because the UI layer logs the reason a
        // value was refused and the tests exercise them directly.
        static bool IsValidThemeId(const std::string &theme_id);
        static bool IsValidBlob(const std::string &json);

        // Filesystem primitives, exposed for the same reason.
        static std::string ReadWholeFile(const std::filesystem::path &path);
        static bool WriteWholeFile(const std::filesystem::path &path, const std::string &contents);

    private:
        std::filesystem::path ActiveThemeFilePath() const;
        std::filesystem::path CustomThemesFilePath() const;
        std::filesystem::path OverridesFilePath() const;

        std::filesystem::path settings_dir_;
    };

} // namespace themes
