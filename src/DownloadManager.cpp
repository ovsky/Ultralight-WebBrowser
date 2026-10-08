#include "DownloadManager.h"

#include <Ultralight/platform/Platform.h>
#include "Utils.h"
#include "UI.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <comdef.h>
#pragma comment(lib, "windowscodecs.lib")
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <cstdlib>
#else
#include <cstdlib>
#endif

#include <system_error>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cstdio>

namespace
{
    constexpr const char *kDefaultFilename = "download";
    constexpr const char *kDownloadsFolderName = "downloads";

    bool ShouldIgnoreDownloadURL(const std::string &url)
    {
        auto scheme_end = url.find(':');
        if (scheme_end == std::string::npos)
            return false;

        std::string scheme = url.substr(0, scheme_end);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        return scheme == "blob" || scheme == "data";
    }

    bool IsGuidLike(const std::string &name)
    {
        if (name.size() != 36)
            return false;

        for (size_t i = 0; i < name.size(); ++i)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (name[i] != '-')
                    return false;
                continue;
            }

            if (!std::isxdigit(static_cast<unsigned char>(name[i])))
                return false;
        }

        return true;
    }

    // Use util::ToStdString from Utils.h

    std::wstring ToWide(const std::string &input)
    {
#ifdef _WIN32
        if (input.empty())
            return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.size(), nullptr, 0);
        if (size_needed <= 0)
            return std::wstring();
        std::wstring result(static_cast<size_t>(size_needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.size(), result.data(), size_needed);
        return result;
#else
        return std::wstring(input.begin(), input.end());
#endif
    }

    std::string::size_type FindLastPathSeparator(const std::string &s)
    {
        auto pos = s.find_last_of("/\\");
        return pos == std::string::npos ? 0 : pos + 1;
    }

    std::string StripQueryAndFragment(const std::string &s)
    {
        auto qpos = s.find_first_of("?#");
        if (qpos == std::string::npos)
            return s;
        return s.substr(0, qpos);
    }

    std::string TrimTrailingDigitsAfterExtension(const std::string &name)
    {
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= name.size())
            return name;

        size_t end = name.size();
        while (end > dot + 1 && std::isdigit(static_cast<unsigned char>(name[end - 1])))
        {
            --end;
        }

        if (end == name.size())
            return name; // nothing trimmed

        // Ensure we still have at least one character in extension
        if (end <= dot + 1)
            return name;

        return name.substr(0, end);
    }

    bool IsWebPFile(const std::filesystem::path &path)
    {
        std::string ext = path.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return ext == ".webp";
    }

#ifdef _WIN32
    // Convert WebP to PNG using Windows Imaging Component (WIC)
    bool ConvertWebPToPNG(const std::filesystem::path &webp_path, std::filesystem::path &out_png_path)
    {
        // Initialize COM
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool com_initialized = SUCCEEDED(hr) || hr == S_FALSE;

        IWICImagingFactory *factory = nullptr;
        IWICBitmapDecoder *decoder = nullptr;
        IWICBitmapFrameDecode *frame = nullptr;
        IWICStream *stream = nullptr;
        IWICBitmapEncoder *encoder = nullptr;
        IWICBitmapFrameEncode *frame_encode = nullptr;
        IWICFormatConverter *converter = nullptr;

        bool success = false;

        // Create WIC factory
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr))
            goto cleanup;

        // Create decoder from file
        hr = factory->CreateDecoderFromFilename(
            webp_path.wstring().c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder);
        if (FAILED(hr))
            goto cleanup;

        // Get first frame
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr))
            goto cleanup;

        // Create format converter to convert to 32bppBGRA
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr))
            goto cleanup;

        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
            goto cleanup;

        // Create output PNG path
        out_png_path = webp_path;
        out_png_path.replace_extension(".png");

        // Create stream for output
        hr = factory->CreateStream(&stream);
        if (FAILED(hr))
            goto cleanup;

        hr = stream->InitializeFromFilename(out_png_path.wstring().c_str(), GENERIC_WRITE);
        if (FAILED(hr))
            goto cleanup;

        // Create PNG encoder
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr))
            goto cleanup;

        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (FAILED(hr))
            goto cleanup;

        // Create frame
        hr = encoder->CreateNewFrame(&frame_encode, nullptr);
        if (FAILED(hr))
            goto cleanup;

        hr = frame_encode->Initialize(nullptr);
        if (FAILED(hr))
            goto cleanup;

        // Get image dimensions
        UINT width, height;
        hr = converter->GetSize(&width, &height);
        if (FAILED(hr))
            goto cleanup;

        hr = frame_encode->SetSize(width, height);
        if (FAILED(hr))
            goto cleanup;

        WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
        hr = frame_encode->SetPixelFormat(&pixel_format);
        if (FAILED(hr))
            goto cleanup;

        // Write the converted bitmap
        hr = frame_encode->WriteSource(converter, nullptr);
        if (FAILED(hr))
            goto cleanup;

        hr = frame_encode->Commit();
        if (FAILED(hr))
            goto cleanup;

        hr = encoder->Commit();
        if (FAILED(hr))
            goto cleanup;

        success = true;

    cleanup:
        if (frame_encode)
            frame_encode->Release();
        if (encoder)
            encoder->Release();
        if (stream)
            stream->Release();
        if (converter)
            converter->Release();
        if (frame)
            frame->Release();
        if (decoder)
            decoder->Release();
        if (factory)
            factory->Release();
        if (com_initialized)
            CoUninitialize();

        return success;
    }
#endif
} // namespace

DownloadManager::DownloadManager()
    : DownloadManager(DetermineDefaultDirectory()) {}

DownloadManager::DownloadManager(std::filesystem::path download_dir)
    : download_dir_(std::move(download_dir))
{
    EnsureDirectoryExists();
    history_file_ = download_dir_ / ".download_history";
    LoadHistoryFromDisk();
    TrimHistoryLocked(kMaxHistoryEntries);
}

DownloadManager::~DownloadManager()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &entry : active_)
    {
        if (entry.second.stream && entry.second.stream->is_open())
            entry.second.stream->close();
    }
    active_.clear();
}

void DownloadManager::SetOnChangeCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    on_change_ = std::move(callback);
}

void DownloadManager::SetWebPConversionCallback(std::function<bool()> callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    should_convert_webp_ = std::move(callback);
}

DownloadManager::DownloadId DownloadManager::NextDownloadId(ultralight::View *caller)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return next_id_++;
}

bool DownloadManager::OnRequestDownload(ultralight::View *caller, DownloadId id, const ultralight::String &url)
{
    std::string url_str = util::ToStdString(url);
    if (ShouldIgnoreDownloadURL(url_str))
        return false;

    std::unique_lock<std::mutex> lock(mutex_);

    // Generate our own unique internal ID to avoid collisions
    // (Ultralight may reuse external IDs across different downloads)
    DownloadId internal_id = next_id_++;
    external_to_internal_id_[id] = internal_id;

    auto &record = GetOrCreateRecordLocked(internal_id);
    record.url = std::move(url_str);
    record.status = Status::Requested;
    record.error.clear();
    record.finished_at = {};
    record.path.clear();
    record.expected_bytes = -1;
    record.received_bytes = 0;
    record.preferred_name = TrimTrailingDigitsAfterExtension(SanitizeFilename(DeriveFilename(record.url, "")));
    if (record.preferred_name.empty())
        record.preferred_name = kDefaultFilename;
    record.display_name = record.preferred_name;

    bool looks_guid = IsGuidLike(record.preferred_name);
    record.placeholder = looks_guid;
    record.suppress_ui = true;
    record.sequence = 0;
    record.started_at = std::chrono::system_clock::now();
    NotifyChangeLocked(lock);
    return true;
}

void DownloadManager::OnBeginDownload(ultralight::View *caller, DownloadId id, const ultralight::String &url,
                                      const ultralight::String &filename, int64_t expected_content_length)
{
    std::string url_str = util::ToStdString(url);
    if (ShouldIgnoreDownloadURL(url_str))
        return;

    std::unique_lock<std::mutex> lock(mutex_);

    // Get or create internal ID mapping
    DownloadId internal_id;
    auto map_it = external_to_internal_id_.find(id);
    if (map_it != external_to_internal_id_.end())
    {
        internal_id = map_it->second;
    }
    else
    {
        // OnRequestDownload wasn't called, create mapping now
        internal_id = next_id_++;
        external_to_internal_id_[id] = internal_id;
    }

    auto &record = GetOrCreateRecordLocked(internal_id);
    if (record.url.empty())
        record.url = url_str;

    record.status = Status::InProgress;
    record.expected_bytes = expected_content_length;
    record.received_bytes = 0;
    if (record.sequence == 0)
    {
        record.sequence = ++start_sequence_counter_;
        last_started_sequence_ = record.sequence;
    }
    record.started_at = std::chrono::system_clock::now();
    record.error.clear();
    record.finished_at = {};

    EnsureDirectoryExists();

    std::string suggested = util::ToStdString(filename);
    std::string derived = DeriveFilename(record.url, suggested);
    std::string sanitized = TrimTrailingDigitsAfterExtension(SanitizeFilename(derived));
    std::string base_name = sanitized;
    if (base_name.empty())
        base_name = record.preferred_name;
    if (base_name.empty())
        base_name = kDefaultFilename;

    base_name = TrimTrailingDigitsAfterExtension(base_name);

    auto full_path = EnsureUniquePath(base_name);

    record.display_name = full_path.filename().u8string();
    record.path = full_path;
    record.error.clear();
    record.placeholder = false;
    record.suppress_ui = false;

    auto stream = std::make_unique<std::ofstream>(full_path, std::ios::binary | std::ios::out);
    if (!stream->is_open())
    {
        record.status = Status::Failed;
        record.error = "Failed to open file for writing";
        record.path.clear();
        NotifyChangeLocked(lock);
        return;
    }

    ActiveDownload active;
    active.record = &record;
    active.stream = std::move(stream);
    active_[id] = std::move(active);

    if (record.sequence == 0)
    {
        record.sequence = ++start_sequence_counter_;
        last_started_sequence_ = record.sequence;
    }

    NotifyChangeLocked(lock);
}

void DownloadManager::OnReceiveDataForDownload(ultralight::View *caller, DownloadId id, ultralight::RefPtr<ultralight::Buffer> data)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = active_.find(id);
    if (it == active_.end())
        return;

    auto &active = it->second;
    if (active.record && active.record->status == Status::Requested)
        active.record->status = Status::InProgress;
    if (active.record && active.record->display_name.empty())
        active.record->display_name = SanitizeFilename(DeriveFilename(active.record->url, ""));
    if (active.stream && active.stream->is_open() && data && data->size())
    {
        active.stream->write(reinterpret_cast<const char *>(data->data()), static_cast<std::streamsize>(data->size()));
        active.record->received_bytes += static_cast<int64_t>(data->size());
    }

    NotifyChangeLocked(lock);
}

void DownloadManager::OnFinishDownload(ultralight::View *caller, DownloadId id)
{
    std::unique_lock<std::mutex> lock(mutex_);
    DownloadId internal_id = GetInternalIdLocked(id);
    auto rec = FindRecordLocked(internal_id);
    std::filesystem::path original_path;
    bool should_convert = false;

    if (rec)
    {
        if (rec->status != Status::Failed && rec->status != Status::Cancelled)
            rec->status = Status::Completed;
        if (rec->expected_bytes >= 0 && rec->received_bytes < rec->expected_bytes)
            rec->received_bytes = rec->expected_bytes;
        if (rec->display_name.empty())
            rec->display_name = SanitizeFilename(DeriveFilename(rec->url, ""));
        // Ensure the record is visible in the UI even if OnBeginDownload
        // was not called previously (some downloads may not trigger Begin).
        rec->suppress_ui = false;
        rec->placeholder = false;
        rec->finished_at = std::chrono::system_clock::now();

        // Check if we should convert WebP to PNG
        original_path = rec->path;
        if (should_convert_webp_ && should_convert_webp_() && IsWebPFile(original_path))
        {
            should_convert = true;
        }
    }

    CloseStreamLocked(id, false);

#ifdef _WIN32
    // Perform WebP to PNG conversion after releasing the file stream
    if (should_convert && rec && !original_path.empty())
    {
        std::filesystem::path png_path;
        if (ConvertWebPToPNG(original_path, png_path))
        {
            // Update the record with the new PNG path
            rec->path = png_path;
            rec->display_name = png_path.filename().u8string();

            // Delete the original WebP file
            std::error_code ec;
            std::filesystem::remove(original_path, ec);
        }
    }
#endif

    NotifyChangeLocked(lock);
}

void DownloadManager::OnFailDownload(ultralight::View *caller, DownloadId id)
{
    std::unique_lock<std::mutex> lock(mutex_);
    DownloadId internal_id = GetInternalIdLocked(id);
    auto rec = FindRecordLocked(internal_id);
    if (rec)
    {
        rec->status = Status::Failed;
        rec->error = "Download failed";
        if (rec->display_name.empty())
            rec->display_name = SanitizeFilename(DeriveFilename(rec->url, ""));
        // Make sure failed downloads are shown so user can inspect/ retry
        rec->suppress_ui = false;
        rec->placeholder = false;
        rec->finished_at = std::chrono::system_clock::now();
        rec->path.clear();
    }

    CloseStreamLocked(id, true);
    NotifyChangeLocked(lock);
}

std::string DownloadManager::GetDownloadsJSON()
{
    std::unique_lock<std::mutex> lock(mutex_);
    bool pruned = PruneStaleRequestsLocked(lock, std::chrono::system_clock::now(), false);
    std::string json = "{\"items\":[";
    bool first = true;
    for (auto it = records_.rbegin(); it != records_.rend(); ++it)
    {
        const auto &rec = it->second;
        if (rec.suppress_ui)
            continue;
        if (!first)
            json += ',';
        first = false;

        json += "{\"id\":" + std::to_string(rec.id);
        json += ",\"url\":\"" + util::EscapeJsonString(rec.url) + "\"";
        json += ",\"filename\":\"" + util::EscapeJsonString(rec.display_name) + "\"";
        json += ",\"path\":\"" + util::EscapeJsonString(rec.path.u8string()) + "\"";
        json += ",\"status\":\"" + util::EscapeJsonString(StatusToString(rec.status)) + "\"";
        json += ",\"received\":" + std::to_string(rec.received_bytes);
        json += ",\"total\":" + std::to_string(rec.expected_bytes);
        json += ",\"canOpen\":" + std::string((rec.status == Status::Completed && !rec.path.empty()) ? "true" : "false");
        json += ",\"canReveal\":" + std::string((rec.status == Status::Completed && !rec.path.empty()) ? "true" : "false");
        json += ",\"startedAt\":" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(rec.started_at.time_since_epoch()).count());
        json += ",\"finishedAt\":" + std::to_string(rec.finished_at.time_since_epoch().count() ? std::chrono::duration_cast<std::chrono::milliseconds>(rec.finished_at.time_since_epoch()).count() : 0);
        json += ",\"error\":\"" + util::EscapeJsonString(rec.error) + "\"";
        json += "}";
    }
    json += "]}";
    auto callback = on_change_;
    lock.unlock();
    if (pruned && callback)
        callback();
    return json;
}

void DownloadManager::ClearFinishedDownloads()
{
    std::unique_lock<std::mutex> lock(mutex_);
    bool any_removed = false;
    for (auto it = records_.begin(); it != records_.end();)
    {
        if (it->second.status == Status::Completed || it->second.status == Status::Failed || it->second.status == Status::Cancelled)
        {
            if (active_.find(it->first) == active_.end())
            {
                it = records_.erase(it);
                any_removed = true;
                continue;
            }
        }
        ++it;
    }
    if (any_removed)
        NotifyChangeLocked(lock);
}

bool DownloadManager::OpenDownload(DownloadId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto rec = records_.find(id);
    if (rec == records_.end())
        return false;
    if (rec->second.status != Status::Completed || rec->second.path.empty())
        return false;

    const auto path_str = rec->second.path.u8string();
#ifdef _WIN32
    auto wpath = ToWide(path_str);
    if (wpath.empty())
        return false;
    auto result = ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#elif defined(__APPLE__)
    std::string cmd = "open " + util::EscapeShellArg(path_str);
    return std::system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open " + util::EscapeShellArg(path_str);
    return std::system(cmd.c_str()) == 0;
#endif
}

bool DownloadManager::RevealDownload(DownloadId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto rec = records_.find(id);
    if (rec == records_.end())
        return false;
    if (rec->second.path.empty())
        return false;

    auto folder = rec->second.path.parent_path();
    const auto folder_str = folder.u8string();
#ifdef _WIN32
    std::wstring params = L"/select,\"" + rec->second.path.wstring() + L"\"";
    auto result = ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#elif defined(__APPLE__)
    std::string cmd = "open -R " + util::EscapeShellArg(rec->second.path.u8string());
    return std::system(cmd.c_str()) == 0;
#else
    std::string cmd = "xdg-open " + util::EscapeShellArg(folder_str);
    return std::system(cmd.c_str()) == 0;
#endif
}

bool DownloadManager::CancelDownload(DownloadId id)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto active = active_.find(id);
    if (active == active_.end())
        return false;
    if (auto rec = FindRecordLocked(id))
        rec->status = Status::Cancelled;
    CloseStreamLocked(id, true);
    NotifyChangeLocked(lock);
    return true;
}

bool DownloadManager::RemoveDownload(DownloadId id)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto active = active_.find(id);
    if (active != active_.end())
    {
        if (auto rec = FindRecordLocked(id))
            rec->status = Status::Cancelled;
        CloseStreamLocked(id, true);
    }

    auto rec_it = records_.find(id);
    if (rec_it == records_.end())
        return false;

    records_.erase(rec_it);
    NotifyChangeLocked(lock);
    return true;
}

bool DownloadManager::HasActiveDownloads() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &entry : active_)
    {
        if (entry.second.record && !entry.second.record->suppress_ui)
            return true;
    }
    for (const auto &entry : records_)
    {
        if (entry.second.suppress_ui)
            continue;
        if (entry.second.status == Status::Requested || entry.second.status == Status::InProgress)
            return true;
    }
    return false;
}

uint64_t DownloadManager::last_started_sequence() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_started_sequence_;
}

bool DownloadManager::PruneStaleRequestsLocked(std::unique_lock<std::mutex> &lock, std::chrono::system_clock::time_point now, bool notify)
{
    constexpr auto kMaxPendingAge = std::chrono::seconds(2);
    bool removed = false;
    for (auto it = records_.begin(); it != records_.end();)
    {
        bool is_requested = it->second.status == Status::Requested;
        bool has_active_stream = active_.find(it->first) != active_.end();
        bool is_stale = (now - it->second.started_at) > kMaxPendingAge;

        if (it->second.placeholder && is_requested && !has_active_stream && is_stale)
        {
            it = records_.erase(it);
            removed = true;
            continue;
        }

        ++it;
    }

    if (removed && notify)
    {
        auto callback = on_change_;
        lock.unlock();
        if (callback)
            callback();
        lock.lock();
    }

    return removed;
}

void DownloadManager::PruneStaleRequests()
{
    std::unique_lock<std::mutex> lock(mutex_);
    PruneStaleRequestsLocked(lock, std::chrono::system_clock::now(), true);
}

std::filesystem::path DownloadManager::DetermineDefaultDirectory()
{
    std::filesystem::path base;
#ifdef _WIN32
    base = std::filesystem::current_path();
#else
    auto home = util::GetEnvVar("HOME");
    if (!home.empty())
        base = std::filesystem::path(home);
    else
        base = std::filesystem::current_path();
#endif
    return base / kDownloadsFolderName;
}

std::string DownloadManager::DeriveFilename(const std::string &url, const std::string &suggested)
{
    if (!suggested.empty())
        return suggested;
    std::string trimmed = StripQueryAndFragment(url);
    auto start = FindLastPathSeparator(trimmed);
    std::string name = trimmed.substr(start);
    if (name.empty())
        return kDefaultFilename;
    return name;
}

std::string DownloadManager::SanitizeFilename(const std::string &filename)
{
    std::string result;
    result.reserve(filename.size());
    const std::string invalid = "\\/:*?\"<>|";
    for (char c : filename)
    {
        if (static_cast<unsigned char>(c) < 32)
            continue;
        if (invalid.find(c) != std::string::npos)
            continue;
        result.push_back(c);
    }
    if (result.empty())
        result = kDefaultFilename;
    return result;
}

std::filesystem::path DownloadManager::EnsureUniquePath(const std::string &base_name) const
{
    auto path = download_dir_ / base_name;
    if (!std::filesystem::exists(path))
        return path;

    auto stem = path.stem().u8string();
    auto extension = path.extension().u8string();
    for (int i = 1; i < 1000; ++i)
    {
        std::string candidate = stem + " (" + std::to_string(i) + ")" + extension;
        auto candidate_path = download_dir_ / candidate;
        if (!std::filesystem::exists(candidate_path))
            return candidate_path;
    }
    return path;
}

std::string DownloadManager::StatusToString(Status status) const
{
    switch (status)
    {
    case Status::Requested:
        return "requested";
    case Status::InProgress:
        return "in-progress";
    case Status::Completed:
        return "completed";
    case Status::Failed:
        return "failed";
    case Status::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

// use util::EscapeJsonString

void DownloadManager::EnsureDirectoryExists()
{
    std::error_code ec;
    if (!std::filesystem::exists(download_dir_, ec))
    {
        std::filesystem::create_directories(download_dir_, ec);
    }
}

void DownloadManager::NotifyChangeLocked(std::unique_lock<std::mutex> &lock)
{
    TrimHistoryLocked(kMaxHistoryEntries);
    std::string history_snapshot;
    bool has_visible_records = false;
    if (!history_file_.empty())
    {
        history_snapshot = BuildHistorySnapshotLocked(kMaxHistoryEntries);
        has_visible_records = !history_snapshot.empty();
    }
    auto callback = on_change_;
    lock.unlock();
    if (!history_file_.empty())
    {
        if (has_visible_records)
        {
            SaveHistorySnapshotUnlocked(history_snapshot);
        }
        else
        {
            std::error_code ec;
            std::filesystem::remove(history_file_, ec);
        }
    }
    if (callback)
        callback();
    lock.lock();
}

DownloadManager::DownloadRecord &DownloadManager::GetOrCreateRecordLocked(DownloadId id)
{
    auto it = records_.find(id);
    if (it == records_.end())
    {
        DownloadRecord rec;
        rec.id = id;
        rec.started_at = std::chrono::system_clock::now();
        it = records_.emplace(id, std::move(rec)).first;
    }
    return it->second;
}

DownloadManager::DownloadId DownloadManager::GetInternalIdLocked(DownloadId external_id) const
{
    auto it = external_to_internal_id_.find(external_id);
    if (it != external_to_internal_id_.end())
        return it->second;
    return external_id; // Fallback to external ID if no mapping exists
}

DownloadManager::DownloadRecord *DownloadManager::FindRecordLocked(DownloadId id)
{
    auto it = records_.find(id);
    if (it == records_.end())
        return nullptr;
    return &it->second;
}

void DownloadManager::CloseStreamLocked(DownloadId id, bool remove_file)
{
    auto it = active_.find(id);
    if (it != active_.end())
    {
        if (it->second.stream && it->second.stream->is_open())
            it->second.stream->close();
        active_.erase(it);
    }

    DownloadId internal_id = GetInternalIdLocked(id);
    // Clean up the ID mapping for this external ID (after we've used it)
    external_to_internal_id_.erase(id);

    auto rec = records_.find(internal_id);
    if (remove_file && rec != records_.end() && !rec->second.path.empty())
    {
        std::error_code ec;
        std::filesystem::remove(rec->second.path, ec);
    }
}

std::string DownloadManager::EncodeField(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value)
    {
        if (c == '\t' || c == '\n' || c == '\r' || c == '%' || c == '\\')
        {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
        else
        {
            out += static_cast<char>(c);
        }
    }
    return out;
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

bool DownloadManager::DecodeField(const std::string &value, std::string &out)
{
    out.clear();
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%' && i + 2 < value.size())
        {
            int hi = HexValue(value[i + 1]);
            int lo = HexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return true;
}

DownloadManager::Status DownloadManager::StatusFromString(const std::string &value)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    if (lower == "requested")
        return Status::Requested;
    if (lower == "in-progress")
        return Status::InProgress;
    if (lower == "completed")
        return Status::Completed;
    if (lower == "failed")
        return Status::Failed;
    if (lower == "cancelled")
        return Status::Cancelled;
    return Status::Requested;
}

int64_t DownloadManager::TimePointToMillis(const std::chrono::system_clock::time_point &tp)
{
    if (tp.time_since_epoch().count() == 0)
        return 0;
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point DownloadManager::MillisToTimePoint(int64_t ms)
{
    if (ms <= 0)
        return std::chrono::system_clock::time_point{};
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

void DownloadManager::LoadHistoryFromDisk()
{
    if (history_file_.empty())
        return;

    std::ifstream in(history_file_, std::ios::binary);
    if (!in.is_open())
        return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::vector<std::string> fields;
        size_t pos = 0;
        while (pos <= line.size())
        {
            size_t next = line.find('\t', pos);
            if (next == std::string::npos)
            {
                fields.emplace_back(line.substr(pos));
                break;
            }
            fields.emplace_back(line.substr(pos, next - pos));
            pos = next + 1;
        }

        if (fields.size() < 10)
            continue;

        DownloadRecord rec;
        rec.id = static_cast<DownloadId>(std::strtoull(fields[0].c_str(), nullptr, 10));
        rec.status = StatusFromString(fields[1]);
        DecodeField(fields[2], rec.url);
        DecodeField(fields[3], rec.display_name);
        std::string path_str;
        DecodeField(fields[4], path_str);
        if (!path_str.empty())
            rec.path = std::filesystem::path(path_str);
        rec.expected_bytes = std::strtoll(fields[5].c_str(), nullptr, 10);
        rec.received_bytes = std::strtoll(fields[6].c_str(), nullptr, 10);
        rec.started_at = MillisToTimePoint(std::strtoll(fields[7].c_str(), nullptr, 10));
        rec.finished_at = MillisToTimePoint(std::strtoll(fields[8].c_str(), nullptr, 10));
        DecodeField(fields[9], rec.error);
        rec.suppress_ui = false;
        rec.placeholder = false;

        records_[rec.id] = std::move(rec);
    }

    if (!records_.empty())
    {
        next_id_ = (std::max)(next_id_, records_.rbegin()->first + 1);
    }
}

void DownloadManager::SaveHistorySnapshotUnlocked(const std::string &snapshot)
{
    if (history_file_.empty())
        return;
    std::ofstream out(history_file_, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return;
    out << snapshot;
}

std::string DownloadManager::BuildHistorySnapshotLocked(size_t max_entries) const
{
    if (history_file_.empty())
        return {};
    std::string out;
    size_t count = 0;
    for (auto it = records_.rbegin(); it != records_.rend(); ++it)
    {
        const auto &rec = it->second;
        if (rec.suppress_ui)
            continue;
        if (max_entries && count >= max_entries)
            break;
        if (!out.empty())
            out += '\n';
        out += std::to_string(rec.id);
        out += '\t';
        out += EncodeField(StatusToString(rec.status));
        out += '\t';
        out += EncodeField(rec.url);
        out += '\t';
        out += EncodeField(rec.display_name);
        out += '\t';
        out += EncodeField(rec.path.string());
        out += '\t';
        out += std::to_string(rec.expected_bytes);
        out += '\t';
        out += std::to_string(rec.received_bytes);
        out += '\t';
        out += std::to_string(TimePointToMillis(rec.started_at));
        out += '\t';
        out += std::to_string(TimePointToMillis(rec.finished_at));
        out += '\t';
        out += EncodeField(rec.error);
        ++count;
    }
    return out;
}

void DownloadManager::TrimHistoryLocked(size_t max_entries)
{
    if (!max_entries)
        return;

    size_t visible = 0;
    for (auto it = records_.rbegin(); it != records_.rend(); ++it)
    {
        const auto &rec = it->second;
        if (rec.suppress_ui)
            continue;
        ++visible;
    }

    if (visible <= max_entries)
        return;

    size_t to_remove = visible - max_entries;
    for (auto it = records_.begin(); it != records_.end() && to_remove > 0;)
    {
        auto status = it->second.status;
        bool is_finished = (status == Status::Completed || status == Status::Failed || status == Status::Cancelled);
        if (!it->second.suppress_ui && is_finished)
        {
            it = records_.erase(it);
            --to_remove;
        }
        else
        {
            ++it;
        }
    }
}
