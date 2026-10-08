#pragma once
#include <Ultralight/Listener.h>
#include <Ultralight/NetworkRequest.h>
#include <unordered_set>
#include <vector>
#include <string>
#include <mutex>

// Basic ad/tracker blocker implementing Ultralight's NetworkListener.
//
// Features:
// - Domain-based blocking from a simple hosts/filter list
// - Optional URL-substring rules (very simple)
// - Allows file:// and data:// schemes unconditionally
//
// Blocklist format (any of the following per line):
// - "example.com"              (blocks example.com and all subdomains)
// - "0.0.0.0 example.com"     (hosts file style; ignores IP)
// - "127.0.0.1 example.com"   (hosts file style; ignores IP)
// - "||example.com^"          (Adblock-style domain rule; ^ terminator optional)
// - "*ads.js"                 (treated as a simple URL substring match)
// - Lines starting with '#' are comments; blank lines ignored.
class AdBlocker : public ultralight::NetworkListener
{
public:
    AdBlocker() = default;
    ~AdBlocker() override = default;

    // Load a blocklist from the given file path. When append=false, clears existing rules first.
    bool LoadBlocklist(const std::string &path, bool append = false);

    // Backwards-compat wrapper
    bool LoadDefaultBlocklist(const std::string &path) { return LoadBlocklist(path, false); }

    // Load all .txt blocklists in a directory (non-recursive), appending to current rules.
    // Returns count of files successfully loaded.
    int LoadBlocklistsInDirectory(const std::string &dir_path);

    // Clear all rules
    void Clear();

    // NetworkListener override (purely decides to allow or block).
    bool OnNetworkRequest(ultralight::View *caller, ultralight::NetworkRequest &request) override;

    // Add a blocked host (suffix-match, case-insensitive)
    void AddBlockedHost(const std::string &host);

    // Add a URL substring rule (case-insensitive)
    void AddURLSubstring(const std::string &needle);
    // Add a simple glob pattern (supports '*' wildcard), case-insensitive
    void AddURLGlob(const std::string &pattern);

    // Enable/disable blocking at runtime
    void set_enabled(bool enabled)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        enabled_ = enabled;
    }
    bool enabled() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return enabled_;
    }

private:
    bool IsBlockedHost(const std::string &host) const;
    bool IsBlockedURL(const std::string &url) const;
    static bool GlobMatch(const char *text, const char *pattern);

    static std::string ToLower(std::string s);
    static std::string Trim(const std::string &s);

    // Data structures
    std::unordered_set<std::string> blocked_hosts_; // host suffixes in lowercase (eg, "doubleclick.net")
    std::vector<std::string> url_substrings_;       // lowercase substrings
    std::vector<std::string> url_globs_;            // lowercase glob patterns with '*'

    mutable std::mutex mtx_;
    bool enabled_ = true;
    bool log_blocked_ = false;
    bool log_all_requests_ = false;  // Debug: log every request

public:
    void set_log_blocked(bool v)
    {
        std::lock_guard<std::mutex> l(mtx_);
        log_blocked_ = v;
    }
    void set_log_all_requests(bool v)
    {
        std::lock_guard<std::mutex> l(mtx_);
        log_all_requests_ = v;
    }
};
