#include <gtest/gtest.h>
#include <filesystem>
#include <random>
#include <string>

#include "../src/drm/DRMSettings.h"

namespace
{
    class TempSettingsFile
    {
    public:
        TempSettingsFile()
        {
            auto tmp_dir = std::filesystem::temp_directory_path();
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<uint64_t> dist;
            path_ = tmp_dir / ("drm_settings_test_" + std::to_string(dist(gen)) + ".json");
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }

        ~TempSettingsFile()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }

        const std::filesystem::path &path() const { return path_; }

    private:
        std::filesystem::path path_;
    };
}

TEST(DRMSettingsTest, DefaultsPersistAndReload)
{
    TempSettingsFile temp;
    drm::DRMSettings settings(temp.path());
    EXPECT_TRUE(settings.Load());
    EXPECT_TRUE(settings.IsEnabled());

    settings.SetEnabled(true);
    settings.SetSiteRule("example.com", drm::SiteRule{true});
    EXPECT_TRUE(settings.Save());

    drm::DRMSettings reloaded(temp.path());
    EXPECT_TRUE(reloaded.Load());
    EXPECT_TRUE(reloaded.IsEnabled());
    EXPECT_TRUE(reloaded.IsDRMRequired("https://example.com/watch"));
    EXPECT_TRUE(reloaded.IsDRMRequired("https://sub.example.com/play"));
}

TEST(DRMSettingsTest, RuleCanDisableBuiltInSite)
{
    TempSettingsFile temp;
    drm::DRMSettings settings(temp.path());
    ASSERT_TRUE(settings.Load());
    settings.SetEnabled(true);

    drm::SiteRule disabled{false};
    settings.SetSiteRule("netflix.com", disabled);
    ASSERT_TRUE(settings.Save());

    drm::DRMSettings reloaded(temp.path());
    ASSERT_TRUE(reloaded.Load());
    EXPECT_TRUE(reloaded.IsEnabled());
    EXPECT_FALSE(reloaded.IsDRMRequired("https://www.netflix.com/title/80025678"));

    drm::SiteRule forced{true};
    reloaded.SetSiteRule("drm.svc.local", forced);
    EXPECT_TRUE(reloaded.IsDRMRequired("https://drm.svc.local/content"));
    EXPECT_FALSE(reloaded.IsDRMRequired("https://unrelated.test"));
}
