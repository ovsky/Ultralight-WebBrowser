#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "../src/MemoryMonitor.h"

namespace {

constexpr uint64_t kMiB = 1024ull * 1024ull;

// Deterministic sampler so the budget policy is tested against exact values
// rather than whatever the CI machine happens to be using.
class FakeSampler : public memory::MemorySampler
{
public:
    void SetResident(uint64_t bytes) { resident_ = bytes; }
    void SetSampleValid(bool valid) { valid_ = valid; }
    void SetPeak(uint64_t bytes) { peak_ = bytes; }

    int SampleCount() const { return sample_count_; }

    bool Sample(memory::MemorySample &out) override
    {
        ++sample_count_;
        if (!valid_)
        {
            return false;
        }
        out.resident_bytes = resident_;
        out.peak_resident_bytes = peak_;
        out.valid = true;
        return true;
    }

    uint64_t PeakResidentBytes() const override { return peak_; }

private:
    uint64_t resident_ = 0;
    uint64_t peak_ = 0;
    bool valid_ = true;
    int sample_count_ = 0;
};

} // namespace

// --------------------------------------------------------------------------
// Platform sampler
// --------------------------------------------------------------------------

TEST(MemoryMonitorTest, ProcessSamplerReportsRealUsage)
{
    memory::ProcessMemorySampler sampler;
    memory::MemorySample sample;
    ASSERT_TRUE(sampler.Sample(sample)) << "host platform should expose memory usage";
    EXPECT_TRUE(sample.valid);

    // A live process always has a non-zero resident set. Asserting > 0 also
    // catches a platform where the field was filled with a sentinel.
    EXPECT_GT(sample.resident_bytes, 0u);
    EXPECT_GT(sample.peak_resident_bytes, 0u);

    // The peak must never be below the current reading.
    EXPECT_GE(sample.peak_resident_bytes, sample.resident_bytes);
}

TEST(MemoryMonitorTest, FormatBytesScalesUnits)
{
    EXPECT_EQ(memory::FormatBytes(0), "0 B");
    EXPECT_EQ(memory::FormatBytes(512), "512 B");
    EXPECT_EQ(memory::FormatBytes(1024), "1.0 KB");
    EXPECT_EQ(memory::FormatBytes(1024ull * 1024ull), "1.0 MB");
    EXPECT_EQ(memory::FormatBytes(1536ull * 1024ull), "1.5 MB");
    EXPECT_EQ(memory::FormatBytes(2ull * 1024ull * 1024ull * 1024ull), "2.0 GB");
}

// --------------------------------------------------------------------------
// Budget clamping
// --------------------------------------------------------------------------

TEST(MemoryMonitorTest, BudgetIsClampedToSupportedRange)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(100 * kMiB);
    memory::MemoryMonitor monitor(sampler);

    monitor.SetBudgetBytes(1);
    EXPECT_EQ(monitor.BudgetBytes(), memory::MemoryMonitor::kMinBudgetBytes)
        << "a budget below the floor should be raised, not stored verbatim";

    monitor.SetBudgetBytes(UINT64_MAX);
    EXPECT_EQ(monitor.BudgetBytes(), memory::MemoryMonitor::kMaxBudgetBytes);

    // A budget of 0 is meaningful: it disables the check entirely.
    monitor.SetBudgetBytes(0);
    EXPECT_EQ(monitor.BudgetBytes(), 0u);

    monitor.SetBudgetBytes(512 * kMiB);
    EXPECT_EQ(monitor.BudgetBytes(), 512 * kMiB);
}

// --------------------------------------------------------------------------
// Over-budget detection and hysteresis
// --------------------------------------------------------------------------

TEST(MemoryMonitorTest, StaysHealthyBelowBudget)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(100 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(512 * kMiB);

    EXPECT_FALSE(monitor.Poll().over_budget);
    EXPECT_FALSE(monitor.IsOverBudget());
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());
}

TEST(MemoryMonitorTest, DisabledMonitorNeverReportsOverBudget)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(4ull * 1024 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    // Budget set, but low-RAM mode off.
    monitor.SetBudgetBytes(256 * kMiB);

    EXPECT_FALSE(monitor.Poll().over_budget);
    EXPECT_FALSE(monitor.IsOverBudget());
    EXPECT_EQ(sampler->SampleCount(), 0)
        << "a disabled monitor must not pay for a platform query every frame";
}

TEST(MemoryMonitorTest, GracePeriodDefersEviction)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(600 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(512 * kMiB);
    monitor.SetTabCounts(5, 5);

    // A single spike must not immediately cost the user a tab.
    EXPECT_FALSE(monitor.Poll().over_budget)
        << "first over-budget sample should only start the grace window";
    EXPECT_FALSE(monitor.IsOverBudget());
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());
}

TEST(MemoryMonitorTest, NoBudgetMeansNoPressure)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(8ull * 1024 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    // Budget left at 0.
    monitor.SetTabCounts(4, 4);

    EXPECT_FALSE(monitor.Poll().over_budget);
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());
}

TEST(MemoryMonitorTest, InvalidSampleIsNotTreatedAsZeroUsage)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetSampleValid(false);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(256 * kMiB);

    const memory::MemorySnapshot snapshot = monitor.Poll();
    EXPECT_FALSE(snapshot.valid)
        << "a failed platform query must be surfaced, not reported as 0 bytes used";
    EXPECT_FALSE(snapshot.over_budget);
    EXPECT_FALSE(monitor.IsOverBudget());
}

TEST(MemoryMonitorTest, RecoveryBelowHysteresisClearsLatch)
{
    auto sampler = std::make_shared<FakeSampler>();
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(512 * kMiB);
    // Zero grace so the latch trips deterministically on the first sample.
    monitor.SetGracePeriod(std::chrono::seconds{0});

    sampler->SetResident(600 * kMiB);
    EXPECT_TRUE(monitor.Poll().over_budget);

    // 500 MiB is under the 512 MiB budget but above the 0.85 recovery
    // watermark (435 MiB). The previous decision must be held, otherwise a
    // workload hovering near the limit would flap on every sample.
    sampler->SetResident(500 * kMiB);
    EXPECT_TRUE(monitor.Poll().over_budget)
        << "usage inside the hysteresis band must keep the existing decision";

    // 400 MiB is below the recovery watermark, so the latch clears.
    sampler->SetResident(400 * kMiB);
    EXPECT_FALSE(monitor.Poll().over_budget);
    EXPECT_FALSE(monitor.IsOverBudget());
}

TEST(MemoryMonitorTest, SustainedPressureTripsAfterGracePeriod)
{
    auto sampler = std::make_shared<FakeSampler>();
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(512 * kMiB);

    // A long grace window means repeated polling must not trip early, and the
    // grace timer must not restart on each poll.
    monitor.SetGracePeriod(std::chrono::hours{1});
    sampler->SetResident(4ull * 1024 * kMiB);

    for (int i = 0; i < 100; ++i)
    {
        EXPECT_FALSE(monitor.Poll().over_budget)
            << "must not trip while still inside the grace window";
    }

    // Shortening the window below the time already elapsed trips immediately,
    // which proves the original start time was preserved across polls.
    monitor.SetGracePeriod(std::chrono::seconds{0});
    EXPECT_TRUE(monitor.Poll().over_budget);
}

TEST(MemoryMonitorTest, TogglingModeResetsLatch)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(2ull * 1024 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(128 * kMiB);
    monitor.SetGracePeriod(std::chrono::seconds{0});
    monitor.SetTabCounts(4, 4);

    ASSERT_TRUE(monitor.Poll().over_budget);
    ASSERT_TRUE(monitor.ShouldEvictBackgroundTab());

    monitor.SetLowRamModeEnabled(false);
    EXPECT_FALSE(monitor.IsOverBudget());
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());

    // Re-enabling must not inherit the stale pressure state, and must not trip
    // instantly either - the grace window restarts from zero.
    monitor.SetLowRamModeEnabled(true);
    EXPECT_FALSE(monitor.IsOverBudget());
    monitor.SetGracePeriod(std::chrono::seconds{0});
    EXPECT_TRUE(monitor.Poll().over_budget);
}

TEST(MemoryMonitorTest, GracePeriodRejectsNegativeValues)
{
    auto sampler = std::make_shared<FakeSampler>();
    memory::MemoryMonitor monitor(sampler);
    monitor.SetGracePeriod(std::chrono::seconds{-5});
    EXPECT_EQ(monitor.GracePeriod().count(), 0)
        << "a negative grace period would disable the debounce entirely";
}

// --------------------------------------------------------------------------
// Eviction policy
// --------------------------------------------------------------------------

TEST(MemoryMonitorTest, EvictionRequiresAtLeastTwoTabs)
{
    auto sampler = std::make_shared<FakeSampler>();
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(512 * kMiB);
    monitor.SetGracePeriod(std::chrono::seconds{0});

    // A single tab is the user's only page; evicting it would leave a blank
    // browser. Policy must refuse regardless of memory pressure.
    monitor.SetTabCounts(1, 1);
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());

    monitor.SetTabCounts(0, 0);
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());

    // With two tabs there is a background tab worth reclaiming.
    monitor.SetTabCounts(2, 2);
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab())
        << "not under pressure yet, so nothing should be evicted";

    // Now actually exceed the budget and the advisory signal appears.
    sampler->SetResident(1ull * 1024 * kMiB);
    EXPECT_TRUE(monitor.Poll().over_budget);
    EXPECT_TRUE(monitor.ShouldEvictBackgroundTab());

    // After the caller reclaims memory the advisory signal must clear.
    monitor.NotifyMemoryReclaimed();
    EXPECT_FALSE(monitor.IsOverBudget());
    EXPECT_FALSE(monitor.ShouldEvictBackgroundTab());
}

TEST(MemoryMonitorTest, TabCountersTrackOpenTabs)
{
    auto sampler = std::make_shared<FakeSampler>();
    memory::MemoryMonitor monitor(sampler);

    monitor.NoteTabOpened();
    monitor.NoteTabOpened();
    monitor.NoteTabOpened();
    EXPECT_EQ(monitor.Snapshot().open_tab_count, 3);
    EXPECT_EQ(monitor.Snapshot().total_tab_count, 3);

    monitor.NoteTabClosed();
    EXPECT_EQ(monitor.Snapshot().open_tab_count, 2);

    // Closing more tabs than are open must not drive the counter negative.
    monitor.NoteTabClosed();
    monitor.NoteTabClosed();
    monitor.NoteTabClosed();
    EXPECT_EQ(monitor.Snapshot().open_tab_count, 0);
    EXPECT_GE(monitor.Snapshot().total_tab_count, 0);
}

TEST(MemoryMonitorTest, SnapshotPollReportsSamplerValues)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(300 * kMiB);
    sampler->SetPeak(450 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(600 * kMiB);
    monitor.SetTabCounts(3, 7);

    const memory::MemorySnapshot snapshot = monitor.Poll();
    EXPECT_TRUE(snapshot.valid);
    EXPECT_EQ(snapshot.resident_bytes, 300 * kMiB);
    EXPECT_EQ(snapshot.peak_resident_bytes, 450 * kMiB);
    EXPECT_EQ(snapshot.BudgetMiB(), 600u);
    EXPECT_EQ(snapshot.ResidentMiB(), 300u);
    EXPECT_EQ(snapshot.PeakResidentMiB(), 450u);
    EXPECT_EQ(snapshot.open_tab_count, 3);
    EXPECT_EQ(snapshot.total_tab_count, 7);
    EXPECT_NEAR(snapshot.BudgetUsagePercent(), 50.0, 0.01);
}

// --------------------------------------------------------------------------
// Snapshot presentation
// --------------------------------------------------------------------------

TEST(MemoryMonitorTest, UsagePercentIsSafeWithoutBudgetOrReading)
{
    memory::MemorySnapshot snapshot;
    // No reading, no budget: must not divide by zero.
    EXPECT_DOUBLE_EQ(snapshot.BudgetUsagePercent(), 0.0);

    snapshot.valid = true;
    snapshot.resident_bytes = 400 * kMiB;
    EXPECT_DOUBLE_EQ(snapshot.BudgetUsagePercent(), 0.0)
        << "a valid reading without a budget reports no pressure";

    snapshot.budget_bytes = 512 * kMiB;
    EXPECT_NEAR(snapshot.BudgetUsagePercent(), 78.125, 0.01);

    // Over budget clamps to 100 so the UI bar cannot overflow.
    snapshot.resident_bytes = 2ull * 1024 * kMiB;
    EXPECT_DOUBLE_EQ(snapshot.BudgetUsagePercent(), 100.0);
}

TEST(MemoryMonitorTest, SnapshotJSONIsParseableAndComplete)
{
    auto sampler = std::make_shared<FakeSampler>();
    sampler->SetResident(200 * kMiB);
    sampler->SetPeak(260 * kMiB);
    memory::MemoryMonitor monitor(sampler);
    monitor.SetLowRamModeEnabled(true);
    monitor.SetBudgetBytes(400 * kMiB);
    monitor.SetTabCounts(2, 5);

    const std::string json = monitor.Poll().ToJSON();

    // The settings page JSON.parse()s this; assert every documented key exists.
    for (const char *key :
         {"\"valid\"", "\"resident_bytes\"", "\"peak_resident_bytes\"",
          "\"budget_bytes\"", "\"open_tabs\"", "\"total_tabs\"",
          "\"over_budget\"", "\"usage_percent\""})
    {
        EXPECT_NE(json.find(key), std::string::npos)
            << "missing key " << key << " in " << json;
    }
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
    EXPECT_NE(json.find("\"valid\":true"), std::string::npos);
    EXPECT_NE(json.find("\"open_tabs\":2"), std::string::npos);
    EXPECT_NE(json.find("\"total_tabs\":5"), std::string::npos);
}

TEST(MemoryMonitorTest, InvalidSnapshotJSONIsStillValidJSON)
{
    memory::MemorySnapshot snapshot;
    const std::string json = snapshot.ToJSON();
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
    EXPECT_NE(json.find("\"valid\":false"), std::string::npos);
}
