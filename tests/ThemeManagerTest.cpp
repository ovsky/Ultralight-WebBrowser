#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "../src/ThemeManager.h"

namespace
{
    // Each test gets its own directory so they can run in any order and cannot
    // observe each other's files.
    class TempThemeDir
    {
    public:
        TempThemeDir()
        {
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<uint64_t> dist;
            dir_ = std::filesystem::temp_directory_path() /
                   ("theme_manager_test_" + std::to_string(dist(gen)));
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ~TempThemeDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        const std::filesystem::path &path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };
} // namespace

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, AcceptsPlainIdentifiers)
{
    EXPECT_TRUE(themes::ThemeManager::IsValidThemeId("dark"));
    EXPECT_TRUE(themes::ThemeManager::IsValidThemeId("classic_nord"));
    EXPECT_TRUE(themes::ThemeManager::IsValidThemeId("custom-1735689600"));
    EXPECT_TRUE(themes::ThemeManager::IsValidThemeId("A1"));
}

TEST(ThemeManagerTest, RejectsUnsafeThemeIds)
{
    // These values all reach the bridge from the settings UI and from imported
    // JSON, so anything that could corrupt the file layout or escape the
    // settings directory has to be refused.
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId(""));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId("has space"));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId("quote\""));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId("new\nline"));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId("../escape"));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId("semi;colon"));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId("back\\slash"));
}

TEST(ThemeManagerTest, RejectsOverlongThemeId)
{
    EXPECT_TRUE(themes::ThemeManager::IsValidThemeId(std::string(themes::kMaxThemeIdLength, 'a')));
    EXPECT_FALSE(themes::ThemeManager::IsValidThemeId(std::string(themes::kMaxThemeIdLength + 1, 'a')));
}

TEST(ThemeManagerTest, BlobValidationRequiresAnObject)
{
    EXPECT_TRUE(themes::ThemeManager::IsValidBlob("{}"));
    EXPECT_TRUE(themes::ThemeManager::IsValidBlob("  {\"a\":\"#fff\"}  "));

    // An empty string would make the engine's `json && json !== '{}'` fallback
    // misparse, and a truncated object is exactly what an interrupted write
    // would leave behind.
    EXPECT_FALSE(themes::ThemeManager::IsValidBlob(""));
    EXPECT_FALSE(themes::ThemeManager::IsValidBlob("   "));
    EXPECT_FALSE(themes::ThemeManager::IsValidBlob("{\"a\":\"#fff\""));
    EXPECT_FALSE(themes::ThemeManager::IsValidBlob("[1,2,3]"));
    EXPECT_FALSE(themes::ThemeManager::IsValidBlob("\"just a string\""));
}

// ---------------------------------------------------------------------------
// Active theme
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, DefaultsToDarkWhenNothingStored)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    EXPECT_EQ(manager.GetActiveThemeId(), "dark");
}

TEST(ThemeManagerTest, RoundTripsActiveThemeId)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    ASSERT_TRUE(manager.SetActiveThemeId("classic_nord"));
    EXPECT_EQ(manager.GetActiveThemeId(), "classic_nord");

    ASSERT_TRUE(manager.SetActiveThemeId("gruvbox"));
    EXPECT_EQ(manager.GetActiveThemeId(), "gruvbox");
}

TEST(ThemeManagerTest, RefusesInvalidActiveThemeId)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    ASSERT_TRUE(manager.SetActiveThemeId("nord"));

    EXPECT_FALSE(manager.SetActiveThemeId("bad id"));
    EXPECT_FALSE(manager.SetActiveThemeId(""));

    // A refused write must leave the previous valid selection intact rather than
    // clearing it, which would discard the user's choice.
    EXPECT_EQ(manager.GetActiveThemeId(), "nord");
}

TEST(ThemeManagerTest, IgnoresCorruptActiveThemeFile)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    // Simulate a hand-edited file that no longer holds a valid id.
    std::filesystem::path active = dir.path() / "active_theme.txt";
    {
        std::ofstream out(active, std::ios::binary | std::ios::trunc);
        out << "  not a valid id \n";
    }

    EXPECT_EQ(manager.GetActiveThemeId(), "dark");
}

// ---------------------------------------------------------------------------
// Custom themes and overrides
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, DefaultsBlobsToEmptyObject)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    EXPECT_EQ(manager.GetCustomThemesJSON(), "{}");
    EXPECT_EQ(manager.GetOverridesJSON(), "{}");
}

TEST(ThemeManagerTest, RoundTripsCustomThemesAndOverrides)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    const std::string custom = "{\"mine\":{\"id\":\"mine\",\"colors\":{\"color-bg-primary\":\"#123456\"}}}";
    const std::string overrides = "{\"dark\":{\"colors\":{\"color-bg-primary\":\"#010203\"}}}";

    ASSERT_TRUE(manager.SaveCustomThemesJSON(custom));
    ASSERT_TRUE(manager.SaveOverridesJSON(overrides));

    EXPECT_EQ(manager.GetCustomThemesJSON(), custom);
    EXPECT_EQ(manager.GetOverridesJSON(), overrides);
}

TEST(ThemeManagerTest, RefusesMalformedBlobs)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    ASSERT_TRUE(manager.SaveCustomThemesJSON("{\"ok\":1}"));

    EXPECT_FALSE(manager.SaveCustomThemesJSON(""));
    EXPECT_FALSE(manager.SaveCustomThemesJSON("{\"truncated\""));
    EXPECT_FALSE(manager.SaveOverridesJSON("not json"));

    // The previously stored blob survives a rejected write.
    EXPECT_EQ(manager.GetCustomThemesJSON(), "{\"ok\":1}");
}

TEST(ThemeManagerTest, OverwrittenBlobIsReplacedNotAppended)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    ASSERT_TRUE(manager.SaveCustomThemesJSON("{\"a\":1}"));
    ASSERT_TRUE(manager.SaveCustomThemesJSON("{\"b\":2}"));

    // Guards the atomic replace path: on Windows rename() fails when the target
    // exists, so the retry-after-remove branch is what makes this work.
    EXPECT_EQ(manager.GetCustomThemesJSON(), "{\"b\":2}");
}

TEST(ThemeManagerTest, SurvivesReopen)
{
    TempThemeDir dir;

    {
        themes::ThemeManager manager(dir.path());
        ASSERT_TRUE(manager.Initialize());
        ASSERT_TRUE(manager.SetActiveThemeId("dracula"));
        ASSERT_TRUE(manager.SaveOverridesJSON("{\"dracula\":{\"colors\":{\"color-bg-primary\":\"#000000\"}}}"));
    }

    themes::ThemeManager reopened(dir.path());
    ASSERT_TRUE(reopened.Initialize());

    EXPECT_EQ(reopened.GetActiveThemeId(), "dracula");
    EXPECT_EQ(reopened.GetOverridesJSON(), "{\"dracula\":{\"colors\":{\"color-bg-primary\":\"#000000\"}}}");
}

// ---------------------------------------------------------------------------
// Seed themes
// ---------------------------------------------------------------------------

TEST(ThemeManagerTest, MissingSeedDirectoryYieldsEmptyObject)
{
    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    const std::string json = manager.GetSeedThemesJSON(dir.path() / "does-not-exist");

    EXPECT_EQ(json, "{}");
    // The result still has to be a valid blob, because the engine parses it
    // directly and an unparseable return would break theme loading entirely.
    EXPECT_TRUE(themes::ThemeManager::IsValidBlob(json));
}

TEST(ThemeManagerTest, ReadsSeedThemesFromAssets)
{
    // Locate the repository's assets directory by walking up from the test
    // binary's working directory, so this works from both build/ and build
    // subdirectories used by the CI workflows.
    std::filesystem::path assets;
    std::error_code ec;

    std::filesystem::path probe = std::filesystem::current_path();
    for (int i = 0; i < 6 && assets.empty(); ++i)
    {
        if (std::filesystem::exists(probe / "assets" / "themes" / "dark.json", ec))
            assets = probe / "assets";
        else
            probe = probe.parent_path();
    }

    if (assets.empty())
    {
        GTEST_SKIP() << "assets/themes not reachable from " << std::filesystem::current_path();
    }

    TempThemeDir dir;
    themes::ThemeManager manager(dir.path());
    ASSERT_TRUE(manager.Initialize());

    const std::string json = manager.GetSeedThemesJSON(assets);

    EXPECT_TRUE(themes::ThemeManager::IsValidBlob(json));
    // Every seed file that ships should appear as a key.
    EXPECT_NE(json.find("\"dark\""), std::string::npos);
    EXPECT_NE(json.find("\"nord\""), std::string::npos);
    EXPECT_NE(json.find("\"monokai\""), std::string::npos);
    EXPECT_NE(json.find("\"light\""), std::string::npos);
    EXPECT_NE(json.find("\"midnight\""), std::string::npos);
}
