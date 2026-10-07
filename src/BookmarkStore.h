#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

/**
 * Simple bookmark storage system.
 * Stores bookmarks as JSON in the settings directory.
 */
class BookmarkStore
{
public:
    struct Bookmark
    {
        uint64_t id;
        std::string url;
        std::string title;
        std::string favicon; // Data URL or empty
        uint64_t created_at; // Timestamp in milliseconds
        bool show_on_bar;    // Whether to show on bookmark bar
        int position;        // Position in bookmark bar (for ordering)
    };

    BookmarkStore();
    ~BookmarkStore();

    // Initialize with storage directory path
    void Initialize(const std::filesystem::path &storage_dir);

    // Add a new bookmark, returns the assigned ID
    uint64_t AddBookmark(const std::string &url, const std::string &title,
                         const std::string &favicon = "", bool show_on_bar = true);

    // Remove a bookmark by ID, returns true if found and removed
    bool RemoveBookmark(uint64_t id);

    // Update an existing bookmark
    bool UpdateBookmark(uint64_t id, const std::string &url, const std::string &title,
                        const std::string &favicon = "", bool show_on_bar = true);

    // Check if a URL is bookmarked
    bool IsBookmarked(const std::string &url) const;

    // Get bookmark by URL (returns nullptr if not found)
    const Bookmark *GetBookmarkByUrl(const std::string &url) const;

    // Get bookmark by ID (returns nullptr if not found)
    const Bookmark *GetBookmarkById(uint64_t id) const;

    // Get all bookmarks
    const std::vector<Bookmark> &GetAllBookmarks() const { return bookmarks_; }

    // Get bookmarks for the bookmark bar only (sorted by position)
    std::vector<Bookmark> GetBookmarkBarItems() const;

    // Reorder bookmarks on the bar - takes a vector of bookmark IDs in new order
    bool ReorderBookmarks(const std::vector<uint64_t> &ordered_ids);

    // Save bookmarks to disk
    bool SaveToDisk();

    // Load bookmarks from disk
    bool LoadFromDisk();

    // Get bookmarks as JSON string
    std::string ToJSON() const;

    // Get bookmark bar items as JSON string
    std::string BookmarkBarToJSON() const;

    // Export/Import
    bool ExportToJSON(const std::filesystem::path &filepath) const;
    bool ImportFromJSON(const std::filesystem::path &filepath);

private:
    std::vector<Bookmark> bookmarks_;
    std::filesystem::path storage_path_;
    uint64_t next_id_ = 1;

    // Get current timestamp in milliseconds
    static uint64_t GetCurrentTimestamp();

    // Normalize URL for comparison (remove trailing slash, lowercase host)
    static std::string NormalizeUrl(const std::string &url);
};
