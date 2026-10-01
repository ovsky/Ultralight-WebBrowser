#pragma once

#include <filesystem>
#include <set>
#include <string>

/**
 * Hosts that cannot render video in the main engine.
 *
 * The bundled engine ships no media pipeline and no codecs, so <video> and
 * <audio> do not play: the elements render as empty boxes. Rather than pretend
 * otherwise, this browser can hand those sites to the platform's own webview
 * (WebView2 / WKWebView / WebKit2GTK), which already exists for DRM and already
 * works. YouTube in a real browser beats a black rectangle.
 *
 * This is deliberately opt-in. Silently sending a site to a different engine is
 * a behaviour change the user should choose, and the setting lives alongside the
 * DRM toggle for the same reason.
 */
namespace media
{

    class MediaFallback
    {
    public:
        explicit MediaFallback(std::filesystem::path assets_path = {});

        // Reads assets/media_sites.json. A missing or unreadable file is not an
        // error: it just means no host is listed and nothing is redirected.
        bool Load();

        bool IsMediaSite(const std::string &url) const;

        const std::set<std::string> &hosts() const { return hosts_; }
        bool loaded() const { return loaded_; }

    private:
        static std::string ExtractHost(const std::string &url);
        // Accepts "youtube.com" matching "www.youtube.com", "m.youtube.com" and
        // "music.youtube.com", but not "notyoutube.com".
        static bool HostMatches(const std::string &host, const std::string &rule);

        std::filesystem::path assets_path_;
        std::set<std::string> hosts_;
        bool loaded_ = false;
    };

} // namespace media
