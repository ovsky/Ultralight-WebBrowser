// Regression coverage for download bookkeeping.
//
// The bug these tests exist for: active_ (the map holding the open file stream)
// was keyed by the SDK's external DownloadId, while records_ was keyed by the
// internal id we allocate. The UI only ever sees internal ids -- GetDownloadsJSON
// emits rec.id and the UI forwards it straight back to CancelDownload and
// RemoveDownload -- so every cancel and remove missed active_ entirely. Cancel
// became a silent no-op for all in-progress downloads, and remove erased the
// record while active_ kept a dangling raw pointer plus an open file handle.
//
// PruneStaleRequestsLocked already assumed the internal key space
// (active_.find(it->first) while iterating records_), which is what made the
// inconsistency visible: two call sites in the same file disagreed about the key.

#include <gtest/gtest.h>

#include "DownloadManager.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Pulls the first "id" field out of the downloads snapshot. Good enough for a
// one-record list; this is a test helper, not a parser.
long long FirstIdFromJson(const std::string &json)
{
    size_t key = json.find("\"id\":");
    if (key == std::string::npos)
        return -1;
    key += 5;
    long long value = 0;
    bool any = false;
    while (key < json.size() && json[key] >= '0' && json[key] <= '9')
    {
        value = value * 10 + (json[key] - '0');
        ++key;
        any = true;
    }
    return any ? value : -1;
}

class DownloadManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dir_ = std::filesystem::temp_directory_path() /
               ("ul_dl_test_" + std::to_string(::testing::UnitTest::GetInstance()
                                                    ->current_test_info()
                                                    ->line()));
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
        manager_ = std::make_unique<DownloadManager>(dir_);
    }

    void TearDown() override
    {
        manager_.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    // Uses an external id far from the internal id range on purpose. Internal
    // ids start at 1, so an external id of 9001 guarantees the two key spaces
    // differ and the bug cannot hide behind them coinciding.
    static constexpr ultralight::DownloadId kExternalId = 9001;

    std::filesystem::path dir_;
    std::unique_ptr<DownloadManager> manager_;
};

TEST_F(DownloadManagerTest, InternalAndExternalIdsDiffer)
{
    // Establishes the precondition every other test here depends on: if these
    // two ever matched, the key-space bug would be invisible.
    ultralight::DownloadId internal_id =
        manager_->OnRequestDownload(nullptr, kExternalId, "https://example.com/file.zip");
    ASSERT_NE(internal_id, 0u);
    EXPECT_NE(internal_id, kExternalId)
        << "test is invalid unless the internal id differs from the external id";
}

TEST_F(DownloadManagerTest, CancelDownloadWorksWithInternalId)
{
    manager_->OnRequestDownload(nullptr, kExternalId, "https://example.com/file.zip");
    manager_->OnBeginDownload(nullptr, kExternalId, "https://example.com/file.zip",
                              "file.zip", 1024);

    ASSERT_TRUE(manager_->HasActiveDownloads());

    const long long id = FirstIdFromJson(manager_->GetDownloadsJSON());
    ASSERT_GT(id, 0) << "download should be visible in the snapshot";

    // The id the UI holds. Before the fix this always returned false.
    EXPECT_TRUE(manager_->CancelDownload(static_cast<ultralight::DownloadId>(id)))
        << "CancelDownload missed active_ because it was keyed by the external id";

    EXPECT_FALSE(manager_->HasActiveDownloads())
        << "cancelled download should no longer count as active";
}

TEST_F(DownloadManagerTest, RemoveDownloadClosesStreamBeforeErasingRecord)
{
    manager_->OnRequestDownload(nullptr, kExternalId, "https://example.com/file.zip");
    manager_->OnBeginDownload(nullptr, kExternalId, "https://example.com/file.zip",
                              "file.zip", 1024);

    const long long id = FirstIdFromJson(manager_->GetDownloadsJSON());
    ASSERT_GT(id, 0);

    // OnBeginDownload creates the partial file on disk. Locating it lets us
    // assert that remove actually tore the stream down rather than just
    // dropping the record.
    std::filesystem::path partial;
    for (const auto &entry : std::filesystem::directory_iterator(dir_))
    {
        if (entry.path().filename() == ".download_history")
            continue;
        partial = entry.path();
    }
    ASSERT_FALSE(partial.empty()) << "OnBeginDownload should have created a partial file";

    ASSERT_TRUE(manager_->RemoveDownload(static_cast<ultralight::DownloadId>(id)));

    // Before the fix, RemoveDownload missed active_, never reached
    // CloseStreamLocked, and left the partially written file on disk alongside
    // a dangling record pointer and an open handle. This is the deterministic
    // symptom; the use-after-free on a later data callback is the undefined one.
    EXPECT_FALSE(std::filesystem::exists(partial))
        << "remove should delete the partial file, which requires closing the stream";

    // A late data callback must now be a harmless no-op.
    manager_->OnReceiveDataForDownload(nullptr, kExternalId, nullptr);
    manager_->OnFinishDownload(nullptr, kExternalId);

    EXPECT_EQ(FirstIdFromJson(manager_->GetDownloadsJSON()), -1)
        << "removed download should be gone from the snapshot";
}

TEST_F(DownloadManagerTest, UnknownIdIsRejected)
{
    EXPECT_FALSE(manager_->CancelDownload(424242u));
    EXPECT_FALSE(manager_->RemoveDownload(424242u));
}

TEST_F(DownloadManagerTest, RepeatedCancelIsIdempotent)
{
    manager_->OnRequestDownload(nullptr, kExternalId, "https://example.com/file.zip");
    manager_->OnBeginDownload(nullptr, kExternalId, "https://example.com/file.zip",
                              "file.zip", 1024);

    const long long id = FirstIdFromJson(manager_->GetDownloadsJSON());
    ASSERT_GT(id, 0);

    EXPECT_TRUE(manager_->CancelDownload(static_cast<ultralight::DownloadId>(id)));
    // The stream is already gone, so a second cancel reports false instead of
    // double-closing or crashing.
    EXPECT_FALSE(manager_->CancelDownload(static_cast<ultralight::DownloadId>(id)));
}

} // namespace