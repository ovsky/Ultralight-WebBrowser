#include "MediaFallback.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace media
{

namespace
{
    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string Trim(const std::string &value)
    {
        const size_t begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return std::string();
        const size_t end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }
} // namespace

MediaFallback::MediaFallback(std::filesystem::path assets_path)
    : assets_path_(std::move(assets_path))
{
}

bool MediaFallback::Load()
{
    hosts_.clear();
    loaded_ = false;

    std::filesystem::path path = assets_path_;
    if (path.empty())
        path = std::filesystem::path("assets") / "media_sites.json";

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
        return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();

    // One host per line. A JSON array would be tidier, but this avoids adding a
    // JSON parser to a list that ships with the app and never changes at
    // runtime; comment lines starting with '#' keep it self-documenting.
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line))
    {
        std::string host = Trim(line);
        if (host.empty() || host[0] == '#')
            continue;

        // Strip a scheme and path if someone pasted a full URL, and drop a
        // leading "*." wildcard so the stored form is always a bare host.
        const size_t scheme = host.find("://");
        if (scheme != std::string::npos)
            host = host.substr(scheme + 3);
        const size_t slash = host.find('/');
        if (slash != std::string::npos)
            host = host.substr(0, slash);
        if (host.rfind("*.", 0) == 0)
            host = host.substr(2);

        host = ToLower(Trim(host));
        if (host.empty())
            continue;

        hosts_.insert(host);
    }

    loaded_ = true;
    return !hosts_.empty();
}

std::string MediaFallback::ExtractHost(const std::string &url)
{
    const size_t scheme = url.find("://");
    std::string rest = (scheme == std::string::npos) ? url : url.substr(scheme + 3);

    // Credentials in the authority are legal but must not be mistaken for the host.
    const size_t at = rest.find('@');
    if (at != std::string::npos)
        rest = rest.substr(at + 1);

    // Host ends at '/', '?' or '#'.
    size_t end = rest.find_first_of("/?#");
    if (end != std::string::npos)
        rest = rest.substr(0, end);

    // Drop the port.
    const size_t colon = rest.rfind(':');
    if (colon != std::string::npos && rest.find(']') == std::string::npos)
        rest = rest.substr(0, colon);

    return ToLower(rest);
}

bool MediaFallback::HostMatches(const std::string &host, const std::string &rule)
{
    if (host == rule)
        return true;

    // Suffix match on a label boundary, so "youtube.com" covers
    // "www.youtube.com" and "music.youtube.com" but not "notyoutube.com".
    if (host.size() > rule.size() &&
        host.compare(host.size() - rule.size(), rule.size(), rule) == 0 &&
        host[host.size() - rule.size() - 1] == '.')
    {
        return true;
    }

    return false;
}

bool MediaFallback::IsMediaSite(const std::string &url) const
{
    if (!loaded_ || hosts_.empty())
        return false;

    const std::string host = ExtractHost(url);
    if (host.empty())
        return false;

    for (const std::string &rule : hosts_)
    {
        if (HostMatches(host, rule))
            return true;
    }

    return false;
}

} // namespace media
