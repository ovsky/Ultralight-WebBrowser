#include "drm/DRMSettings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>

namespace drm
{
    namespace
    {
        constexpr const char kCatalogFilename[] = "drm_site_catalog.json";
        // Minimal embedded fallback so that the application can still function if the
        // JSON catalog is missing. The full list now lives in assets/drm_sites.json.
        constexpr const char kEmbeddedCatalog[] = R"({
  "drm_sites": {
    "netflix.com": { "force": true },
    "disneyplus.com": { "force": true }
  }
}
)";

        std::string ReadTextFile(const std::filesystem::path &path)
        {
            if (path.empty())
                return {};
            std::ifstream in(path, std::ios::in | std::ios::binary);
            if (!in.is_open())
                return {};
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::filesystem::path ResolveStorageDirectory(const DRMSettings &settings)
        {
            const auto &storage_path = settings.storage_path();
            if (!storage_path.empty())
            {
                auto parent = storage_path.parent_path();
                if (!parent.empty())
                    return parent;
            }
            return std::filesystem::path("data");
        }

        std::string LoadCatalogDocument(const DRMSettings &settings)
        {
            std::vector<std::filesystem::path> candidates;
            auto storage_dir = ResolveStorageDirectory(settings);
            candidates.push_back(storage_dir / kCatalogFilename);
            candidates.push_back(std::filesystem::path("assets") / "drm_sites.json");

            for (const auto &candidate : candidates)
            {
                auto buffer = ReadTextFile(candidate);
                if (!buffer.empty())
                    return buffer;
            }
            return kEmbeddedCatalog;
        }

        void EnsureCatalogSnapshot(const DRMSettings &settings, const std::string &document)
        {
            auto storage_dir = ResolveStorageDirectory(settings);
            auto dest = storage_dir / kCatalogFilename;
            if (dest.empty() || document.empty())
                return;
            if (std::filesystem::exists(dest))
                return;
            std::error_code ec;
            std::filesystem::create_directories(storage_dir, ec);
            std::ofstream out(dest, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return;
            out << document;
        }

        void MergeCatalog(std::map<std::string, SiteRule> &target,
                          const std::map<std::string, SiteRule> &catalog)
        {
            // Always include all catalog entries (catalog takes precedence)
            // This ensures new sites added to the catalog are picked up
            for (const auto &entry : catalog)
            {
                target[entry.first] = entry.second;
            }
        }
    }

    DRMSettings::DRMSettings(std::filesystem::path storage_path)
        : storage_path_(std::move(storage_path))
    {
    }

    bool DRMSettings::Load()
    {
        if (storage_path_.empty())
        {
            storage_path_ = std::filesystem::path("data") / "drm_settings.json";
        }

        std::error_code ec;
        std::filesystem::create_directories(storage_path_.parent_path(), ec);

        const std::string catalog_document = LoadCatalogDocument(*this);
        std::map<std::string, SiteRule> catalog_rules;
        ParseSiteRules(catalog_document, catalog_rules);
        EnsureCatalogSnapshot(*this, catalog_document);

        std::ifstream in(storage_path_, std::ios::in | std::ios::binary);
        if (!in.is_open())
        {
            ResetToDefaults();
            return Save();
        }

        std::ostringstream ss;
        ss << in.rdbuf();
        std::string buffer = ss.str();
        in.close();
        if (buffer.empty())
        {
            ResetToDefaults();
            return Save();
        }

        enabled_ = ParseBoolField(buffer, "enabled", true);
        std::map<std::string, SiteRule> parsed_rules;
        if (!ParseSiteRules(buffer, parsed_rules) || parsed_rules.empty())
        {
            ResetToDefaults();
            return Save();
        }
        site_rules_ = std::move(parsed_rules);
        if (!catalog_rules.empty())
            MergeCatalog(site_rules_, catalog_rules);
        return true;
    }

    bool DRMSettings::Save() const
    {
        if (storage_path_.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(storage_path_.parent_path(), ec);

        std::ofstream out(storage_path_, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;

        out << "{\n";
        out << "  \"enabled\": " << (enabled_ ? "true" : "false") << ",\n";
        out << "  \"drm_sites\": {\n";
        bool first = true;
        for (const auto &entry : site_rules_)
        {
            if (!first)
                out << ",\n";
            out << "    \"" << entry.first << "\": { \"force\": " << (entry.second.force ? "true" : "false") << " }";
            first = false;
        }
        out << "\n  }\n";
        out << "}\n";
        out.flush();
        if (!out.good())
            return false;
        return true;
    }

    void DRMSettings::ResetToDefaults()
    {
        enabled_ = false; // Default to disabled - user must opt-in
        site_rules_.clear();

        const std::string catalog_document = LoadCatalogDocument(*this);
        std::map<std::string, SiteRule> catalog_rules;
        if (!ParseSiteRules(catalog_document, catalog_rules) || catalog_rules.empty())
        {
            ParseSiteRules(kEmbeddedCatalog, catalog_rules);
        }

        if (catalog_rules.empty())
        {
            catalog_rules["netflix.com"] = SiteRule{true};
            catalog_rules["disneyplus.com"] = SiteRule{true};
        }

        site_rules_ = catalog_rules;
        EnsureCatalogSnapshot(*this, catalog_document.empty() ? std::string(kEmbeddedCatalog) : catalog_document);
    }

    void DRMSettings::SetSiteRule(const std::string &host, const SiteRule &rule)
    {
        if (host.empty())
            return;
        site_rules_[NormalizeHost(host)] = rule;
    }

    bool DRMSettings::IsDRMRequired(const std::string &url) const
    {
        if (!enabled_)
            return false;
        return IsDrmSite(url);
    }

    bool DRMSettings::IsDrmSite(const std::string &url) const
    {
        // Check if URL matches a DRM site (ignores enabled_ flag)
        std::string host = NormalizeHost(ExtractHost(url));
        if (host.empty())
            return false;
        for (const auto &entry : site_rules_)
        {
            if (entry.second.force && HostMatchesRule(host, entry.first))
                return true;
        }
        return false;
    }

    bool DRMSettings::ParseBoolField(const std::string &buffer, const std::string &key, bool fallback)
    {
        if (key.empty())
            return fallback;
        std::string needle = std::string("\"") + key + "\"";
        auto pos = buffer.find(needle);
        if (pos == std::string::npos)
            return fallback;
        pos = buffer.find(':', pos + needle.size());
        if (pos == std::string::npos)
            return fallback;
        ++pos;
        while (pos < buffer.size() && std::isspace(static_cast<unsigned char>(buffer[pos])))
            ++pos;
        if (pos >= buffer.size())
            return fallback;
        if (buffer.compare(pos, 4, "true") == 0)
            return true;
        if (buffer.compare(pos, 5, "false") == 0)
            return false;
        if (buffer[pos] == '1')
            return true;
        if (buffer[pos] == '0')
            return false;
        return fallback;
    }

    size_t DRMSettings::FindMatchingBrace(const std::string &buffer, size_t open_pos)
    {
        int depth = 0;
        for (size_t i = open_pos; i < buffer.size(); ++i)
        {
            if (buffer[i] == '{')
                ++depth;
            else if (buffer[i] == '}')
            {
                --depth;
                if (depth == 0)
                    return i;
            }
        }
        return std::string::npos;
    }

    bool DRMSettings::ParseSiteRules(const std::string &buffer, std::map<std::string, SiteRule> &out)
    {
        out.clear();
        auto pos = buffer.find("\"drm_sites\"");
        if (pos == std::string::npos)
            return false;
        auto open = buffer.find('{', pos);
        if (open == std::string::npos)
            return false;
        auto close = FindMatchingBrace(buffer, open);
        if (close == std::string::npos || close <= open)
            return false;
        std::string object = buffer.substr(open + 1, close - open - 1);
        std::regex entry_re("\\\"([^\\\"]+)\\\"\\s*:\\s*\\{([^}]*)\\}");
        auto begin = std::sregex_iterator(object.begin(), object.end(), entry_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            std::string key = NormalizeHost((*it)[1]);
            std::string body = (*it)[2];
            bool force = ParseBoolField(body, "force", true);
            out[key] = SiteRule{force};
        }
        return !out.empty();
    }

    std::string DRMSettings::ExtractHost(const std::string &url)
    {
        if (url.empty())
            return {};
        std::string work = url;
        auto scheme_end = work.find("://");
        if (scheme_end != std::string::npos)
            work = work.substr(scheme_end + 3);
        auto slash = work.find_first_of("/ ?#");
        if (slash != std::string::npos)
            work = work.substr(0, slash);
        auto at = work.rfind('@');
        if (at != std::string::npos)
            work = work.substr(at + 1);
        auto colon = work.find(':');
        if (colon != std::string::npos)
            work = work.substr(0, colon);
        return work;
    }

    std::string DRMSettings::NormalizeHost(std::string host)
    {
        std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        if (!host.empty() && host.back() == '.')
            host.pop_back();
        return host;
    }

    bool DRMSettings::HostMatchesRule(const std::string &host, const std::string &rule)
    {
        if (host == rule)
            return true;
        if (host.size() <= rule.size())
            return false;
        auto suffix_pos = host.size() - rule.size();
        if (host.compare(suffix_pos, std::string::npos, rule) != 0)
            return false;
        if (suffix_pos == 0)
            return true;
        return host[suffix_pos - 1] == '.';
    }

} // namespace drm
