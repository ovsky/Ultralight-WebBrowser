#pragma once

#include <Ultralight/Listener.h>
#include <Ultralight/String.h>
#include <Ultralight/Buffer.h>
#include <functional>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <map>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <fstream>

namespace ultralight
{
    class View;
}

class DownloadManager : public ultralight::DownloadListener
{
public:
    using DownloadId = ultralight::DownloadId;

    DownloadManager();
    explicit DownloadManager(std::filesystem::path download_dir);
    ~DownloadManager() override;

    void SetOnChangeCallback(std::function<void()> callback);
    void SetWebPConversionCallback(std::function<bool()> callback);

    std::string GetDownloadsJSON();
    void ClearFinishedDownloads();
    bool OpenDownload(DownloadId id) const;
    bool RevealDownload(DownloadId id) const;
    bool CancelDownload(DownloadId id);
    bool RemoveDownload(DownloadId id);
    bool HasActiveDownloads() const;
    void PruneStaleRequests();
    uint64_t last_started_sequence() const;

    // DownloadListener overrides
    DownloadId NextDownloadId(ultralight::View *caller) override;
    bool OnRequestDownload(ultralight::View *caller, DownloadId id, const ultralight::String &url) override;
    void OnBeginDownload(ultralight::View *caller, DownloadId id, const ultralight::String &url,
                         const ultralight::String &filename, int64_t expected_content_length) override;
    void OnReceiveDataForDownload(ultralight::View *caller, DownloadId id, ultralight::RefPtr<ultralight::Buffer> data) override;
    void OnFinishDownload(ultralight::View *caller, DownloadId id) override;
    void OnFailDownload(ultralight::View *caller, DownloadId id) override;

    const std::filesystem::path &download_directory() const { return download_dir_; }

private:
    enum class Status
    {
        Requested,
        InProgress,
        Completed,
        Failed,
        Cancelled
    };

    struct DownloadRecord
    {
        DownloadId id = 0;
        std::string url;
        std::string display_name;
        std::string preferred_name;
        std::filesystem::path path;
        int64_t expected_bytes = -1;
        int64_t received_bytes = 0;
        Status status = Status::Requested;
        std::chrono::system_clock::time_point started_at;
        std::chrono::system_clock::time_point finished_at;
        std::string error;
        uint64_t sequence = 0;
        bool suppress_ui = false;
        bool placeholder = false;
    };

    struct ActiveDownload
    {
        DownloadRecord *record = nullptr;
        std::unique_ptr<std::ofstream> stream;
    };

    static std::filesystem::path DetermineDefaultDirectory();
    static std::string DeriveFilename(const std::string &url, const std::string &suggested);
    static std::string SanitizeFilename(const std::string &filename);
    std::filesystem::path EnsureUniquePath(const std::string &base_name) const;
    std::string StatusToString(Status status) const;
    static std::string JsonEscape(const std::string &input);

    void EnsureDirectoryExists();
    void NotifyChangeLocked(std::unique_lock<std::mutex> &lock);
    DownloadRecord &GetOrCreateRecordLocked(DownloadId id);
    DownloadRecord *FindRecordLocked(DownloadId id);
    void CloseStreamLocked(DownloadId id, bool remove_file);
    void LoadHistoryFromDisk();
    void SaveHistorySnapshotUnlocked(const std::string &snapshot);
    std::string BuildHistorySnapshotLocked(size_t max_entries) const;
    void TrimHistoryLocked(size_t max_entries);
    static std::string EncodeField(const std::string &value);
    static bool DecodeField(const std::string &value, std::string &out);
    static Status StatusFromString(const std::string &value);
    static int64_t TimePointToMillis(const std::chrono::system_clock::time_point &tp);
    static std::chrono::system_clock::time_point MillisToTimePoint(int64_t ms);

    std::filesystem::path download_dir_;
    std::filesystem::path history_file_;
    mutable std::mutex mutex_;
    DownloadId next_id_ = 1;
    std::map<DownloadId, DownloadRecord> records_;
    std::unordered_map<DownloadId, ActiveDownload> active_;
    std::unordered_map<DownloadId, DownloadId> external_to_internal_id_; // Maps Ultralight's ID to our internal ID
    uint64_t start_sequence_counter_ = 0;
    uint64_t last_started_sequence_ = 0;
    std::function<void()> on_change_;
    std::function<bool()> should_convert_webp_;
    static constexpr size_t kMaxHistoryEntries = 200;

    bool PruneStaleRequestsLocked(std::unique_lock<std::mutex> &lock, std::chrono::system_clock::time_point now, bool notify);
    DownloadId GetInternalIdLocked(DownloadId external_id) const;
};
