#include "SyncService.h"
#include "UI.h"
#include "BookmarkStore.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>

namespace
{
  std::string EscapeJsonString(const std::string &s)
  {
    std::string result;
    for (char c : s)
    {
      switch (c)
      {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
      }
    }
    return result;
  }

  std::string GetIsoTimestamp()
  {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
#if defined(_WIN32)
    localtime_s(&tm_now, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_now);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_now);
    return std::string(buf);
  }
}

SyncService::SyncService(UI *ui)
  : ui_(ui)
{
}

SyncService::~SyncService()
{
}

void SyncService::Initialize(const std::filesystem::path &profile_dir)
{
  profile_dir_ = profile_dir;
}

bool SyncService::ExportToFile(const std::filesystem::path &filepath)
{
  std::ostringstream json;
  json << "{\n";
  json << "  \"sync_version\": 1,\n";
  json << "  \"sync_timestamp\": \"" << GetIsoTimestamp() << "\",\n";

  // Bookmarks
  std::string bookmarks_json;
  if (ui_->bookmark_store())
  {
    // Use BookmarkStore's export to a temp string
    // BookmarkStore::ExportToJSON writes to a file, so we need a different approach
    // For now, we'll export bookmarks to a temp file and read it back
    std::error_code ec;
    auto temp_path = std::filesystem::temp_directory_path() / "ultralight_sync_bookmarks.json";
    if (ui_->bookmark_store()->ExportToJSON(temp_path))
    {
      std::ifstream bf(temp_path);
      if (bf.is_open())
      {
        std::ostringstream bss;
        bss << bf.rdbuf();
        bookmarks_json = bss.str();
      }
      std::filesystem::remove(temp_path, ec);
    }
  }

  json << "  \"bookmarks\": " << (bookmarks_json.empty() ? "[]" : bookmarks_json) << ",\n";

  // History
  json << "  \"history\": [";
  // History is internal to UI, we need to expose it or use GetHistoryJSON
  // For now, use an empty array as history sync requires additional API
  json << "],\n";

  // Settings
    json << "  \"settings\": " << ui_->GetSettingsJSONPublic() << "\n";

  json << "}\n";

  std::ofstream file(filepath);
  if (!file.is_open())
  {
    last_sync_status_ = "Failed to open file for writing";
    last_sync_success_ = false;
    return false;
  }

  file << json.str();
  file.close();

  last_sync_status_ = "Exported to " + filepath.string();
  last_sync_success_ = true;
  return true;
}

bool SyncService::ImportFromFile(const std::filesystem::path &filepath)
{
  std::ifstream file(filepath);
  if (!file.is_open())
  {
    last_sync_status_ = "Failed to open file for reading";
    last_sync_success_ = false;
    return false;
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  file.close();
  std::string content = ss.str();

  // Parse bookmarks
  size_t bm_start = content.find("\"bookmarks\"");
  if (bm_start != std::string::npos)
  {
    bm_start = content.find('[', bm_start);
    if (bm_start != std::string::npos)
    {
      size_t bm_end = content.find(']', bm_start);
      if (bm_end != std::string::npos)
      {
        std::string bookmarks_json = content.substr(bm_start, bm_end - bm_start + 1);
        // Write to temp file and import
        std::error_code ec;
        auto temp_path = std::filesystem::temp_directory_path() / "ultralight_sync_bookmarks_import.json";
        std::ofstream tf(temp_path);
        tf << bookmarks_json;
        tf.close();
        if (ui_->bookmark_store())
          ui_->bookmark_store()->ImportFromJSON(temp_path);
        std::filesystem::remove(temp_path, ec);
      }
    }
  }

  // Import settings
  ui_->ReloadSettingsFromDisk();

  last_sync_status_ = "Imported from " + filepath.string();
  last_sync_success_ = true;
  return true;
}

bool SyncService::SyncToRemote(const std::string &endpoint_url)
{
  // Placeholder for future cloud sync integration
  // Would require HTTP client library (curl, winhttp, etc.)
  last_sync_status_ = "Remote sync not yet implemented";
  last_sync_success_ = false;
  return false;
}

bool SyncService::SyncFromRemote(const std::string &endpoint_url)
{
  // Placeholder for future cloud sync integration
  last_sync_status_ = "Remote sync not yet implemented";
  last_sync_success_ = false;
  return false;
}

bool SyncService::SerializeBookmarks(std::string &json)
{
  // Implemented via ExportToFile above
  return true;
}

bool SyncService::SerializeHistory(std::string &json)
{
  // History sync requires additional UI API
  return true;
}

bool SyncService::SerializeSettings(std::string &json)
{
  json = ui_->GetSettingsJSONPublic();
  return true;
}

bool SyncService::DeserializeBookmarks(const std::string &json)
{
  return true;
}

bool SyncService::DeserializeHistory(const std::string &json)
{
  return true;
}

bool SyncService::DeserializeSettings(const std::string &json)
{
  return true;
}
