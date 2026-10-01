#include "ThemeManager.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace themes
{

namespace
{
    // The legacy definition files. Fixed and small: they ship with the app, so
    // there is no reason to discover them at runtime, and naming them keeps the
    // set from silently growing if a stray file lands in assets/themes/.
    constexpr const char *const kSeedStems[] = {"dark", "light", "midnight", "nord", "monokai"};

    std::string Trim(const std::string &text)
    {
        const size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return std::string();
        const size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    }
} // namespace

ThemeManager::ThemeManager(std::filesystem::path settings_dir)
    : settings_dir_(std::move(settings_dir))
{
}

bool ThemeManager::Initialize()
{
    std::error_code ec;
    std::filesystem::create_directories(settings_dir_, ec);
    if (ec)
    {
        std::fprintf(stderr, "[ThemeManager] cannot create settings dir '%s': %s\n",
                     settings_dir_.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Filesystem primitives
// ---------------------------------------------------------------------------

std::string ThemeManager::ReadWholeFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
        return std::string();

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool ThemeManager::WriteWholeFile(const std::filesystem::path &path, const std::string &contents)
{
    // Write to a sibling temp file, then rename over the target. A rename within
    // a directory is atomic on POSIX and on Windows, so a crash or a full disk
    // part-way through leaves the previous good file intact instead of a
    // truncated one that would break theme loading on every page.
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            std::fprintf(stderr, "[ThemeManager] cannot open '%s' for writing\n", tmp.string().c_str());
            return false;
        }

        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out.good())
        {
            std::fprintf(stderr, "[ThemeManager] write failed for '%s'\n", tmp.string().c_str());
            out.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        // POSIX rename replaces the destination silently; Windows fails when it
        // already exists. Removing and retrying is safe because the temp file is
        // already fully written, so the only window without a file is one
        // rename wide.
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::rename(tmp, path, ec);
    }

    if (ec)
    {
        std::fprintf(stderr, "[ThemeManager] cannot replace '%s': %s\n",
                     path.string().c_str(), ec.message().c_str());
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool ThemeManager::IsValidThemeId(const std::string &theme_id)
{
    if (theme_id.empty() || theme_id.size() > kMaxThemeIdLength)
        return false;

    for (const char c : theme_id)
    {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '-' || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

bool ThemeManager::IsValidBlob(const std::string &json)
{
    if (json.empty() || json.size() > kMaxThemeBlobBytes)
        return false;

    // Both blobs are produced by JSON.stringify on an object, so they always
    // look like { ... }. Requiring a non-blank object literal rejects empty
    // strings, truncated writes and accidental non-object values without
    // needing a full parser.
    const std::string trimmed = Trim(json);
    if (trimmed.size() < 2)
        return false;
    return trimmed.front() == '{' && trimmed.back() == '}';
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::filesystem::path ThemeManager::ActiveThemeFilePath() const
{
    return settings_dir_ / "active_theme.txt";
}

std::filesystem::path ThemeManager::CustomThemesFilePath() const
{
    return settings_dir_ / "custom_themes.json";
}

std::filesystem::path ThemeManager::OverridesFilePath() const
{
    return settings_dir_ / "theme_overrides.json";
}

// ---------------------------------------------------------------------------
// Active theme
// ---------------------------------------------------------------------------

std::string ThemeManager::GetActiveThemeId() const
{
    // Only a file this class wrote can hold a value here, and it only ever writes
    // ids that passed IsValidThemeId. Trimming anyway costs nothing and means a
    // hand-edited file degrades to the default instead of to an unusable id.
    const std::string stored = Trim(ReadWholeFile(ActiveThemeFilePath()));
    if (IsValidThemeId(stored))
        return stored;

    return "dark";
}

bool ThemeManager::SetActiveThemeId(const std::string &theme_id)
{
    if (!IsValidThemeId(theme_id))
    {
        std::fprintf(stderr, "[ThemeManager] rejected invalid theme id '%s'\n", theme_id.c_str());
        return false;
    }

    if (!WriteWholeFile(ActiveThemeFilePath(), theme_id))
    {
        // The engine also keeps a localStorage copy, so the selection is not lost
        // when the disk write fails. Leaving the previous stored id in place is
        // the right failure mode: a stale id still selects a real theme, whereas
        // clearing it would discard the user's choice entirely.
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Custom themes and overrides
// ---------------------------------------------------------------------------

std::string ThemeManager::GetCustomThemesJSON() const
{
    const std::string stored = ReadWholeFile(CustomThemesFilePath());
    if (IsValidBlob(stored))
        return stored;

    // "{}" rather than an empty string: the engine distinguishes the two and uses
    // "{}" to mean "nothing stored yet" and fall back to localStorage.
    return "{}";
}

bool ThemeManager::SaveCustomThemesJSON(const std::string &json)
{
    if (!IsValidBlob(json))
    {
        std::fprintf(stderr, "[ThemeManager] rejected malformed custom themes payload (%zu bytes)\n", json.size());
        return false;
    }

    return WriteWholeFile(CustomThemesFilePath(), json);
}

std::string ThemeManager::GetOverridesJSON() const
{
    const std::string stored = ReadWholeFile(OverridesFilePath());
    if (IsValidBlob(stored))
        return stored;

    return "{}";
}

bool ThemeManager::SaveOverridesJSON(const std::string &json)
{
    if (!IsValidBlob(json))
    {
        std::fprintf(stderr, "[ThemeManager] rejected malformed theme overrides payload (%zu bytes)\n", json.size());
        return false;
    }

    return WriteWholeFile(OverridesFilePath(), json);
}

// ---------------------------------------------------------------------------
// Legacy seed definitions
// ---------------------------------------------------------------------------

std::string ThemeManager::GetSeedThemesJSON(const std::filesystem::path &assets_dir) const
{
    std::string out = "{";
    bool wrote_any = false;

    for (const char *stem : kSeedStems)
    {
        const std::filesystem::path path =
            assets_dir / "themes" / (std::string(stem) + ".json");

        const std::string body = Trim(ReadWholeFile(path));
        if (!IsValidBlob(body))
        {
            std::fprintf(stderr, "[ThemeManager] skipping unreadable seed theme '%s'\n", path.string().c_str());
            continue;
        }

        if (wrote_any)
            out += ",";
        out += "\"" + std::string(stem) + "\":" + body;
        wrote_any = true;
    }

    out += "}";

    // An empty object is still valid input when nothing could be read: the engine
    // merges it and simply offers no legacy variants.
    (void)wrote_any;
    return out;
}

} // namespace themes
