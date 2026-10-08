#include "AdBlocker.h"
#include "Utils.h"
#include <Ultralight/Ultralight.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstdio>

using namespace ultralight;

namespace
{
    bool ends_with_dot_or_exact(const std::string &host, const std::string &rule)
    {
        // Match exact domain or subdomain ("example.com" matches example.com and a.example.com but not badexample.com)
        if (host == rule)
            return true;
        if (host.size() <= rule.size())
            return false;
        // Ensure boundary on the left is a dot
        size_t pos = host.size() - rule.size();
        if (host.compare(pos, std::string::npos, rule) == 0 && pos > 0 && host[pos - 1] == '.')
            return true;
        return false;
    }
}

bool AdBlocker::LoadBlocklist(const std::string &path, bool append)
{
    std::ifstream in(path);
    if (!in.is_open())
        return false;

    std::lock_guard<std::mutex> lock(mtx_);
    if (!append)
    {
        blocked_hosts_.clear();
        url_substrings_.clear();
        url_globs_.clear();
    }

    std::string line;
    while (std::getline(in, line))
    {
        line = util::Trim(line);
        if (line.empty())
            continue;
        if (line[0] == '#')
            continue;

        // Adblock-style domain pattern: ||example.com^
        if (line.rfind("||", 0) == 0)
        {
            std::string dom = line.substr(2);
            // Trim at first '^' if present
            auto hat = dom.find('^');
            if (hat != std::string::npos)
                dom = dom.substr(0, hat);
            dom = util::Trim(dom);
            if (!dom.empty())
                AddBlockedHost(dom);
            continue;
        }

        // Hosts-file style: IP then domain
        {
            std::istringstream iss(line);
            std::string tok1, tok2;
            if (iss >> tok1)
            {
                if (tok1.find('.') != std::string::npos || tok1 == "::1")
                {
                    if (iss >> tok2)
                    {
                        AddBlockedHost(tok2);
                        continue;
                    }
                }
            }
        }

        // Plain domain without IP
        if (line.find('.') != std::string::npos && line.find(' ') == std::string::npos)
        {
            AddBlockedHost(line);
            continue;
        }

        // If contains wildcard, treat as glob, else as substring
        if (line.find('*') != std::string::npos || line.find('?') != std::string::npos)
            AddURLGlob(line);
        else
            AddURLSubstring(line);
    }

    return true;
}

int AdBlocker::LoadBlocklistsInDirectory(const std::string &dir_path)
{
    int count = 0;
#if __cplusplus >= 201703L
    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(dir_path))
        {
            if (!entry.is_regular_file())
                continue;
            auto path = entry.path();
            if (path.extension() == ".txt")
            {
                if (LoadBlocklist(path.string(), true))
                    ++count;
            }
        }
    }
    catch (...)
    {
        // ignore directory errors
    }
#endif
    return count;
}

void AdBlocker::Clear()
{
    std::lock_guard<std::mutex> lock(mtx_);
    blocked_hosts_.clear();
    url_substrings_.clear();
}

bool AdBlocker::OnNetworkRequest(View * /*caller*/, NetworkRequest &request)
{
    bool enabled_local = true;
    bool log_all = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        enabled_local = enabled_;
        log_all = log_all_requests_;
    }

    auto proto = request.urlProtocol().utf8();
    auto host_ul = request.urlHost();
    auto url_ul = request.url();
    auto method = request.httpMethod().utf8();
    auto host = util::ToLower(std::string(host_ul.utf8().data()));
    auto url = std::string(url_ul.utf8().data());

    // Debug logging of all requests
    if (log_all)
    {
        std::fprintf(stderr, "[NET] %s %s (host: %s)\n", 
                     method.data() ? method.data() : "GET",
                     url.c_str(), 
                     host.c_str());
    }

    // If disabled, allow all traffic.
    if (!enabled_local)
        return true;
    // Always allow file/data schemes and about:blank, etc.
    if (proto == "file" || proto == "data" || proto == "about")
        return true;

    auto url_lower = util::ToLower(url);

    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!host.empty() && IsBlockedHost(host))
        {
            if (log_blocked_ || log_all)
                std::fprintf(stderr, "AdBlock: BLOCKED host: %s\n", host.c_str());
            return false; // Block by domain
        }
        if (!url_lower.empty() && IsBlockedURL(url_lower))
        {
            if (log_blocked_ || log_all)
                std::fprintf(stderr, "AdBlock: BLOCKED url: %s\n", url.c_str());
            return false; // Block by simple substring
        }
    }

    return true; // Allow
}

void AdBlocker::AddBlockedHost(const std::string &host_raw)
{
    std::string h = util::ToLower(util::Trim(host_raw));
    if (h.empty())
        return;
    // strip leading dots
    while (!h.empty() && h.front() == '.')
        h.erase(h.begin());
    blocked_hosts_.insert(h);
}

void AdBlocker::AddURLSubstring(const std::string &needle_raw)
{
    std::string n = util::ToLower(util::Trim(needle_raw));
    if (n.empty())
        return;
    url_substrings_.push_back(n);
}

void AdBlocker::AddURLGlob(const std::string &pattern_raw)
{
    std::string p = util::ToLower(util::Trim(pattern_raw));
    if (p.empty())
        return;
    url_globs_.push_back(p);
}

bool AdBlocker::IsBlockedHost(const std::string &host) const
{
    for (const auto &rule : blocked_hosts_)
    {
        if (ends_with_dot_or_exact(host, rule))
            return true;
    }
    return false;
}

bool AdBlocker::IsBlockedURL(const std::string &url) const
{
    for (const auto &needle : url_substrings_)
    {
        if (url.find(needle) != std::string::npos)
            return true;
    }
    for (const auto &glob : url_globs_)
    {
        if (GlobMatch(url.c_str(), glob.c_str()))
            return true;
    }
    return false;
}

// Use util::ToLower and util::Trim from Utils.h

// Very simple glob matcher supporting '*' and '?'
bool AdBlocker::GlobMatch(const char *text, const char *pattern)
{
    // Based on recursive backtracking; fine for small pattern counts
    if (!pattern || !text)
        return false;
    const char *p = pattern;
    const char *t = text;
    const char *star = nullptr;
    const char *star_text = nullptr;
    while (*t)
    {
        if (*p == '?' || *p == *t)
        {
            ++p;
            ++t;
            continue;
        }
        if (*p == '*')
        {
            star = p++;
            star_text = t;
            continue;
        }
        if (star)
        {
            p = star + 1;
            t = ++star_text;
            continue;
        }
        return false;
    }
    while (*p == '*')
        ++p;
    return *p == '\0';
}
