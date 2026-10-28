#pragma once
#include <string>
#include <filesystem>
#include <functional>

class UI;

class SyncService
{
public:
  SyncService(UI *ui);
  ~SyncService();

  // Initialize with the profile data directory
  void Initialize(const std::filesystem::path &profile_dir);

  // Export all browser data to a JSON file
  bool ExportToFile(const std::filesystem::path &filepath);

  // Import browser data from a JSON file
  bool ImportFromFile(const std::filesystem::path &filepath);

  // Sync to a remote endpoint (placeholder for future cloud integration)
  bool SyncToRemote(const std::string &endpoint_url);

  // Sync from a remote endpoint
  bool SyncFromRemote(const std::string &endpoint_url);

  // Get last sync status
  std::string GetLastSyncStatus() const { return last_sync_status_; }
  bool GetLastSyncSuccess() const { return last_sync_success_; }

  // Set sync endpoint URL
  void SetSyncEndpoint(const std::string &url) { sync_endpoint_ = url; }
  const std::string &GetSyncEndpoint() const { return sync_endpoint_; }

  // Enable/disable auto-sync
  void SetAutoSync(bool enabled) { auto_sync_ = enabled; }
  bool GetAutoSync() const { return auto_sync_; }

private:
  UI *ui_;
  std::filesystem::path profile_dir_;
  std::string last_sync_status_;
  bool last_sync_success_ = false;
  std::string sync_endpoint_;
  bool auto_sync_ = false;

  bool SerializeBookmarks(std::string &json);
  bool SerializeHistory(std::string &json);
  bool SerializeSettings(std::string &json);
  bool DeserializeBookmarks(const std::string &json);
  bool DeserializeHistory(const std::string &json);
  bool DeserializeSettings(const std::string &json);
};
