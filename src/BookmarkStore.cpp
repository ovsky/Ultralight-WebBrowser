#include "BookmarkStore.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <exception>

BookmarkStore::BookmarkStore() = default;
BookmarkStore::~BookmarkStore() = default;

void BookmarkStore::Initialize(const std::filesystem::path &storage_dir)
{
    namespace fs = std::filesystem;

    // Ensure directory exists
    if (!fs::exists(storage_dir))
    {
        std::error_code ec;
        fs::create_directories(storage_dir, ec);
    }

    storage_path_ = storage_dir / "bookmarks.json";
    LoadFromDisk();
}

uint64_t BookmarkStore::AddBookmark(const std::string &url, const std::string &title,
                                    const std::string &favicon, bool show_on_bar)
{
    // Check if already bookmarked
    std::string normalized = NormalizeUrl(url);
    for (const auto &bm : bookmarks_)
    {
        if (NormalizeUrl(bm.url) == normalized)
            return bm.id; // Already exists, return existing ID
    }

    // Find the highest position among bar items
    int max_position = -1;
    for (const auto &bm : bookmarks_)
    {
        if (bm.show_on_bar && bm.position > max_position)
            max_position = bm.position;
    }

    Bookmark bm;
    bm.id = next_id_++;
    bm.url = url;
    bm.title = title.empty() ? url : title;
    bm.favicon = favicon;
    bm.created_at = GetCurrentTimestamp();
    bm.show_on_bar = show_on_bar;
    bm.position = show_on_bar ? (max_position + 1) : 0;

    bookmarks_.push_back(bm);
    SaveToDisk();

    return bm.id;
}

bool BookmarkStore::RemoveBookmark(uint64_t id)
{
    auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                           [id](const Bookmark &bm)
                           { return bm.id == id; });

    if (it != bookmarks_.end())
    {
        bookmarks_.erase(it);
        SaveToDisk();
        return true;
    }
    return false;
}

bool BookmarkStore::UpdateBookmark(uint64_t id, const std::string &url, const std::string &title,
                                   const std::string &favicon, bool show_on_bar)
{
    auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                           [id](const Bookmark &bm)
                           { return bm.id == id; });

    if (it != bookmarks_.end())
    {
        it->url = url;
        it->title = title.empty() ? url : title;
        if (!favicon.empty())
            it->favicon = favicon;
        it->show_on_bar = show_on_bar;
        SaveToDisk();
        return true;
    }
    return false;
}

bool BookmarkStore::IsBookmarked(const std::string &url) const
{
    std::string normalized = NormalizeUrl(url);
    for (const auto &bm : bookmarks_)
    {
        if (NormalizeUrl(bm.url) == normalized)
            return true;
    }
    return false;
}

const BookmarkStore::Bookmark *BookmarkStore::GetBookmarkByUrl(const std::string &url) const
{
    std::string normalized = NormalizeUrl(url);
    for (const auto &bm : bookmarks_)
    {
        if (NormalizeUrl(bm.url) == normalized)
            return &bm;
    }
    return nullptr;
}

const BookmarkStore::Bookmark *BookmarkStore::GetBookmarkById(uint64_t id) const
{
    for (const auto &bm : bookmarks_)
    {
        if (bm.id == id)
            return &bm;
    }
    return nullptr;
}

std::vector<BookmarkStore::Bookmark> BookmarkStore::GetBookmarkBarItems() const
{
    std::vector<Bookmark> bar_items;
    for (const auto &bm : bookmarks_)
    {
        if (bm.show_on_bar)
            bar_items.push_back(bm);
    }
    // Sort by position
    std::sort(bar_items.begin(), bar_items.end(),
              [](const Bookmark &a, const Bookmark &b)
              { return a.position < b.position; });
    return bar_items;
}

bool BookmarkStore::ReorderBookmarks(const std::vector<uint64_t> &ordered_ids)
{
    // Update positions based on the order in ordered_ids
    for (size_t i = 0; i < ordered_ids.size(); ++i)
    {
        for (auto &bm : bookmarks_)
        {
            if (bm.id == ordered_ids[i])
            {
                bm.position = static_cast<int>(i);
                break;
            }
        }
    }
    return SaveToDisk();
}

// Helper to escape JSON strings
static std::string EscapeJSON(const std::string &s)
{
    std::ostringstream o;
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\b':
            o << "\\b";
            break;
        case '\f':
            o << "\\f";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if ('\x00' <= c && c <= '\x1f')
            {
                o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
            }
            else
            {
                o << c;
            }
        }
    }
    return o.str();
}

std::string BookmarkStore::ToJSON() const
{
    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto &bm : bookmarks_)
    {
        if (!first)
            ss << ",";
        first = false;
        ss << "{";
        ss << "\"id\":" << bm.id << ",";
        ss << "\"url\":\"" << EscapeJSON(bm.url) << "\",";
        ss << "\"title\":\"" << EscapeJSON(bm.title) << "\",";
        ss << "\"favicon\":\"" << EscapeJSON(bm.favicon) << "\",";
        ss << "\"created_at\":" << bm.created_at << ",";
        ss << "\"show_on_bar\":" << (bm.show_on_bar ? "true" : "false") << ",";
        ss << "\"position\":" << bm.position;
        ss << "}";
    }
    ss << "]";
    return ss.str();
}

std::string BookmarkStore::BookmarkBarToJSON() const
{
    // Get sorted bar items
    auto bar_items = GetBookmarkBarItems();

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto &bm : bar_items)
    {
        if (!first)
            ss << ",";
        first = false;
        ss << "{";
        ss << "\"id\":" << bm.id << ",";
        ss << "\"url\":\"" << EscapeJSON(bm.url) << "\",";
        ss << "\"title\":\"" << EscapeJSON(bm.title) << "\",";
        ss << "\"favicon\":\"" << EscapeJSON(bm.favicon) << "\"";
        ss << "}";
    }
    ss << "]";
    return ss.str();
}

bool BookmarkStore::SaveToDisk()
{
    if (storage_path_.empty())
        return false;

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"next_id\": " << next_id_ << ",\n";
    ss << "  \"bookmarks\": [\n";

    bool first = true;
    for (const auto &bm : bookmarks_)
    {
        if (!first)
            ss << ",\n";
        first = false;
        ss << "    {\n";
        ss << "      \"id\": " << bm.id << ",\n";
        ss << "      \"url\": \"" << EscapeJSON(bm.url) << "\",\n";
        ss << "      \"title\": \"" << EscapeJSON(bm.title) << "\",\n";
        ss << "      \"favicon\": \"" << EscapeJSON(bm.favicon) << "\",\n";
        ss << "      \"created_at\": " << bm.created_at << ",\n";
        ss << "      \"show_on_bar\": " << (bm.show_on_bar ? "true" : "false") << ",\n";
        ss << "      \"position\": " << bm.position << "\n";
        ss << "    }";
    }

    ss << "\n  ]\n";
    ss << "}\n";

    std::ofstream file(storage_path_);
    if (!file.is_open())
        return false;

    file << ss.str();
    return true;
}

// Simple JSON value extraction helpers
static std::string ExtractString(const std::string &json, const std::string &key)
{
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return "";

    pos += search.length();
    // Skip whitespace
    while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos])))
        pos++;

    if (pos >= json.length() || json[pos] != '"')
        return "";
    pos++; // Skip opening quote

    std::string result;
    while (pos < json.length() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.length())
        {
            pos++;
            switch (json[pos])
            {
            case '"':
                result += '"';
                break;
            case '\\':
                result += '\\';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            default:
                result += json[pos];
                break;
            }
        }
        else
        {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

static uint64_t ExtractUint64(const std::string &json, const std::string &key)
{
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return 0;

    pos += search.length();
    // Skip whitespace
    while (pos < json.length() && std::isspace(static_cast<unsigned char>(json[pos])))
        pos++;

    std::string num;
    while (pos < json.length() && std::isdigit(static_cast<unsigned char>(json[pos])))
    {
        num += json[pos++];
    }

    if (num.empty())
        return 0;

    // bookmarks.json is on-disk state; a corrupted or oversized value must not
    // propagate std::out_of_range out of the parser.
    try
    {
        return std::stoull(num);
    }
    catch (const std::exception &)
    {
        return 0;
    }
}

static bool ExtractBool(const std::string &json, const std::string &key, bool default_val = false)
{
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return default_val;

    pos += search.length();
    // Skip whitespace
    while (pos < json.length() && std::isspace(json[pos]))
        pos++;

    if (pos + 4 <= json.length() && json.substr(pos, 4) == "true")
        return true;
    if (pos + 5 <= json.length() && json.substr(pos, 5) == "false")
        return false;

    return default_val;
}

bool BookmarkStore::LoadFromDisk()
{
    if (storage_path_.empty() || !std::filesystem::exists(storage_path_))
        return false;

    std::ifstream file(storage_path_);
    if (!file.is_open())
        return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    bookmarks_.clear();

    // Extract next_id
    next_id_ = ExtractUint64(json, "next_id");
    if (next_id_ == 0)
        next_id_ = 1;

    // Find bookmarks array
    size_t arr_start = json.find("\"bookmarks\":");
    if (arr_start == std::string::npos)
        return true; // No bookmarks yet

    arr_start = json.find('[', arr_start);
    if (arr_start == std::string::npos)
        return true;

    // Parse each bookmark object
    size_t pos = arr_start + 1;
    while (pos < json.length())
    {
        // Find next object start
        size_t obj_start = json.find('{', pos);
        if (obj_start == std::string::npos)
            break;

        // Find object end
        int brace_count = 1;
        size_t obj_end = obj_start + 1;
        while (obj_end < json.length() && brace_count > 0)
        {
            if (json[obj_end] == '{')
                brace_count++;
            else if (json[obj_end] == '}')
                brace_count--;
            obj_end++;
        }

        if (brace_count != 0)
            break;

        std::string obj_json = json.substr(obj_start, obj_end - obj_start);

        Bookmark bm;
        bm.id = ExtractUint64(obj_json, "id");
        bm.url = ExtractString(obj_json, "url");
        bm.title = ExtractString(obj_json, "title");
        bm.favicon = ExtractString(obj_json, "favicon");
        bm.created_at = ExtractUint64(obj_json, "created_at");
        bm.show_on_bar = ExtractBool(obj_json, "show_on_bar", true);
        bm.position = static_cast<int>(ExtractUint64(obj_json, "position"));

        if (!bm.url.empty())
        {
            bookmarks_.push_back(bm);
            if (bm.id >= next_id_)
                next_id_ = bm.id + 1;
        }

        pos = obj_end;

        // Check for end of array
        size_t next_comma = json.find(',', pos);
        size_t arr_end = json.find(']', pos);
        if (arr_end != std::string::npos && (next_comma == std::string::npos || arr_end < next_comma))
            break;
    }

    return true;
}

uint64_t BookmarkStore::GetCurrentTimestamp()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string BookmarkStore::NormalizeUrl(const std::string &url)
{
    if (url.empty())
        return url;

    std::string result = url;

    // Remove trailing slash
    while (!result.empty() && result.back() == '/')
        result.pop_back();

    // Lowercase the scheme and host part
    size_t scheme_end = result.find("://");
    if (scheme_end != std::string::npos)
    {
        // Lowercase scheme
        for (size_t i = 0; i < scheme_end; i++)
            result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));

        // Find end of host (start of path, query, or fragment)
        size_t host_start = scheme_end + 3;
        size_t host_end = result.find_first_of("/?#", host_start);
        if (host_end == std::string::npos)
            host_end = result.length();

        // Lowercase host
        for (size_t i = host_start; i < host_end; i++)
            result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    }

    return result;
}
