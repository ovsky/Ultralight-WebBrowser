#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "../src/MediaFallback.h"

namespace
{
    class TempMediaList
    {
    public:
        // `contents` is written verbatim to <dir>/media_sites.json.
        explicit TempMediaList(const std::string &contents)
        {
            std::random_device rd;
            std::mt19937_64 gen(rd());
            std::uniform_int_distribution<uint64_t> dist;
            dir_ = std::filesystem::temp_directory_path() /
                   ("media_fallback_test_" + std::to_string(dist(gen)));
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
            std::filesystem::create_directories(dir_, ec);

            std::ofstream out(dir_ / "media_sites.json", std::ios::binary | std::ios::trunc);
            out << contents;
        }

        ~TempMediaList()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        const std::filesystem::path &path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    media::MediaFallback Loaded(const std::string &contents)
    {
        TempMediaList list(contents);
        media::MediaFallback fallback(list.path() / "media_sites.json");
        fallback.Load();
        return fallback;
    }
} // namespace

TEST(MediaFallbackTest, MatchesExactHost)
{
    auto fb = Loaded("youtube.com\nvimeo.com\n");

    EXPECT_TRUE(fb.IsMediaSite("https://youtube.com/watch?v=abc"));
    EXPECT_TRUE(fb.IsMediaSite("https://vimeo.com/12345"));
}

TEST(MediaFallbackTest, MatchesSubdomainsOnLabelBoundary)
{
    auto fb = Loaded("youtube.com\n");

    // The engine cannot decode video, so the common subdomains have to match.
    EXPECT_TRUE(fb.IsMediaSite("https://www.youtube.com/watch"));
    EXPECT_TRUE(fb.IsMediaSite("https://m.youtube.com/watch"));
    EXPECT_TRUE(fb.IsMediaSite("https://music.youtube.com/watch"));
}

TEST(MediaFallbackTest, DoesNotMatchLookalikeHosts)
{
    auto fb = Loaded("youtube.com\n");

    // A plain substring or suffix test would wrongly catch all of these, which
    // is the classic way a host allowlist turns into an open redirect.
    EXPECT_FALSE(fb.IsMediaSite("https://notyoutube.com/"));
    EXPECT_FALSE(fb.IsMediaSite("https://youtube.com.evil.test/"));
    EXPECT_FALSE(fb.IsMediaSite("https://evilyoutube.com/"));
    EXPECT_FALSE(fb.IsMediaSite("https://myyoutube.com/"));
}

TEST(MediaFallbackTest, HandlesSchemePortPathAndCase)
{
    auto fb = Loaded("YouTube.com\n");

    EXPECT_TRUE(fb.IsMediaSite("http://youtube.com/"));
    EXPECT_TRUE(fb.IsMediaSite("HTTPS://YOUTUBE.COM/watch"));
    EXPECT_TRUE(fb.IsMediaSite("https://youtube.com:8443/watch?v=1"));
    EXPECT_TRUE(fb.IsMediaSite("https://user:pass@youtube.com/watch"));
    EXPECT_TRUE(fb.IsMediaSite("youtubE.com"));
}

TEST(MediaFallbackTest, IgnoresCommentsBlankLinesAndWildcards)
{
    auto fb = Loaded("# a comment\n\n  \n*.example.com\nhttps://vimeo.com/123\n");

    EXPECT_TRUE(fb.IsMediaSite("https://www.example.com/x"));
    EXPECT_TRUE(fb.IsMediaSite("https://vimeo.com/999"));
    EXPECT_EQ(fb.hosts().size(), 2u);
}

TEST(MediaFallbackTest, EmptyOrMissingListRedirectsNothing)
{
    // The dangerous failure mode is an empty list that somehow matches
    // everything, so both cases are asserted explicitly.
    auto empty = Loaded("");
    EXPECT_FALSE(empty.IsMediaSite("https://youtube.com/"));

    std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "media_fallback_absent_dir_xyz";
    media::MediaFallback absent(missing / "media_sites.json");
    EXPECT_FALSE(absent.Load());
    EXPECT_FALSE(absent.IsMediaSite("https://youtube.com/"));
}

TEST(MediaFallbackTest, RejectsMalformedInput)
{
    auto fb = Loaded("youtube.com\n");
    EXPECT_FALSE(fb.IsMediaSite(""));
    EXPECT_FALSE(fb.IsMediaSite("not a url"));
    EXPECT_FALSE(fb.IsMediaSite("file:///youtube.com"));
    EXPECT_FALSE(fb.IsMediaSite("mailto:youtube.com"));
}
