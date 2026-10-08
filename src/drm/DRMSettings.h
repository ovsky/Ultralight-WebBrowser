#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace drm
{
    struct SiteRule
    {
        bool force = true;
    };

    class DRMSettings
    {
    public:
        explicit DRMSettings(std::filesystem::path storage_path = {});

        bool Load();
        bool Save() const;
        void ResetToDefaults();

        bool IsEnabled() const { return enabled_; }
        void SetEnabled(bool enabled) { enabled_ = enabled; }

        const std::map<std::string, SiteRule> &site_rules() const { return site_rules_; }
        void SetSiteRule(const std::string &host, const SiteRule &rule);

        bool IsDRMRequired(const std::string &url) const;
        
        // Check if URL matches a DRM site (ignores enabled_ flag)
        bool IsDrmSite(const std::string &url) const;

        const std::filesystem::path &storage_path() const { return storage_path_; }

    private:
        static bool ParseBoolField(const std::string &buffer, const std::string &key, bool fallback);
        static size_t FindMatchingBrace(const std::string &buffer, size_t open_pos);
        static bool ParseSiteRules(const std::string &buffer, std::map<std::string, SiteRule> &out);
        static std::string ExtractHost(const std::string &url);
        static std::string NormalizeHost(std::string host);
        static bool HostMatchesRule(const std::string &host, const std::string &rule);

        std::filesystem::path storage_path_;
        bool enabled_ = true;
        std::map<std::string, SiteRule> site_rules_;
    };

} // namespace drm
