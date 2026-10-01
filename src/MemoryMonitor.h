#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

/**
 * Memory accounting for low-RAM mode.
 *
 * The browser runs on everything from a 128 MB phone to a desktop workstation, so
 * the resident-set size has to be observable at runtime rather than assumed. This
 * module is deliberately free of any Ultralight dependency: it only samples the
 * host process, which keeps it unit-testable and safe to call from any thread.
 */
namespace memory {

// A single reading of the process footprint.
struct MemorySample
{
    // Bytes currently resident in physical RAM. Zero when `valid` is false.
    uint64_t resident_bytes = 0;
    // High-water mark of `resident_bytes` for the lifetime of the process.
    uint64_t peak_resident_bytes = 0;
    // False when the platform could not be queried. Callers must treat the byte
    // counts as meaningless rather than assuming zero means "no memory used".
    bool valid = false;
};

// Source of memory readings. Abstracted so the budget policy can be tested
// against deterministic values instead of whatever the CI machine happens to use.
class MemorySampler
{
public:
    virtual ~MemorySampler() = default;

    // Fills `out` with the current footprint. Returns false (and leaves `out`
    // untouched) when the platform provides no usable measurement.
    virtual bool Sample(MemorySample &out) = 0;

    // Monotonic process peak RSS, or 0 when unavailable. Unlike `Sample` this is
    // cached by the OS, so it is safe to call even when `Sample` fails.
    virtual uint64_t PeakResidentBytes() const { return 0; }
};

// Reads the real process footprint for the host platform.
class ProcessMemorySampler : public MemorySampler
{
public:
    bool Sample(MemorySample &out) override;
    uint64_t PeakResidentBytes() const override;
};

// Immutable point-in-time view handed to the UI layer.
struct MemorySnapshot
{
    uint64_t resident_bytes = 0;
    uint64_t peak_resident_bytes = 0;
    uint64_t budget_bytes = 0;
    int open_tab_count = 0;
    int total_tab_count = 0;
    bool over_budget = false;
    // True when the platform refused to report usage. The UI should hide the
    // numbers rather than display a misleading zero.
    bool valid = false;

    // Resident set expressed in mebibytes, rounded down. Returns 0 when invalid.
    uint64_t ResidentMiB() const;
    uint64_t PeakResidentMiB() const;
    uint64_t BudgetMiB() const;

    // Percentage of the budget consumed, clamped to [0, 100]. Returns 0 when
    // invalid or when no budget is configured.
    double BudgetUsagePercent() const;

    // Compact JSON payload for the settings page.
    std::string ToJSON() const;
};

// Tracks footprint against a configured budget and decides when the browser
// should shed background work. All methods are safe to call from any thread.
class MemoryMonitor
{
public:
    // Budget bounds, in bytes. Callers work in MiB; these keep the arithmetic in
    // one place so a zero or inverted range can never be stored.
    static constexpr uint64_t kMinBudgetBytes = 64ull * 1024 * 1024;
    static constexpr uint64_t kMaxBudgetBytes = 16384ull * 1024 * 1024;

    // Once the budget is exceeded, usage must fall back below this fraction of it
    // before the monitor reports healthy again. Without hysteresis, a workload
    // sitting exactly at the limit would flip the flag every sample.
    static constexpr double kRecoveryRatio = 0.85;

    // How long usage must stay over budget before eviction is advised. A single
    // spike (a heavy page, a large image) should not immediately cost a tab.
    static constexpr std::chrono::seconds kOverBudgetGrace{5};

    explicit MemoryMonitor(std::shared_ptr<MemorySampler> sampler = nullptr);
    ~MemoryMonitor();

    MemoryMonitor(const MemoryMonitor &) = delete;
    MemoryMonitor &operator=(const MemoryMonitor &) = delete;

    // Enables or disables enforcement. When disabled, IsOverBudget() is always
    // false and the sampler is not polled.
    void SetLowRamModeEnabled(bool enabled);
    bool IsLowRamModeEnabled() const;

    // Sets the budget in bytes. Values outside [kMinBudgetBytes, kMaxBudgetBytes]
    // are clamped. A budget of 0 (the default) disables the check.
    void SetBudgetBytes(uint64_t bytes);
    uint64_t BudgetBytes() const;

    // Overrides how long usage must stay above the budget before pressure is
    // reported. Defaults to kOverBudgetGrace. Exposed so the policy can be
    // tuned per device class and exercised deterministically in tests.
    void SetGracePeriod(std::chrono::seconds grace);
    std::chrono::seconds GracePeriod() const;

    void SetTabCounts(int open_tabs, int total_tabs);

    // Records that a tab was created, so the snapshot can report the load.
    void NoteTabOpened();
    void NoteTabClosed();

    // Polls the sampler and updates the over-budget state. Returns the resulting
    // snapshot. Cheap and non-blocking; safe to call from the render loop.
    MemorySnapshot Poll();

    // Returns the last computed snapshot without re-sampling the platform.
    MemorySnapshot Snapshot() const;

    // True only when the monitor is enabled, the platform reported usage, a
    // budget is set, and usage has stayed above it for the grace period.
    bool IsOverBudget() const;

    // Advisory signal for the caller to discard a background tab. Requires
    // IsOverBudget() and at least one non-active tab to be worth reclaiming.
    bool ShouldEvictBackgroundTab() const;

    // Clears the over-budget latch. Call after reclaiming memory so the monitor
    // does not immediately re-trigger.
    void NotifyMemoryReclaimed();

    // Best-effort hint to the allocator that a large amount of memory was freed.
    // No-op on platforms where this is not meaningful.
    static void ReleaseFreedMemory();

private:
    void RecomputeOverBudgetLocked(uint64_t resident_bytes, bool sample_valid,
                                   std::chrono::steady_clock::time_point now);

    std::shared_ptr<MemorySampler> sampler_;
    mutable std::mutex mutex_;
    MemorySample last_sample_;
    uint64_t budget_bytes_ = 0;
    std::chrono::seconds grace_period_{kOverBudgetGrace};
    bool low_ram_mode_enabled_ = false;
    bool over_budget_ = false;
    // When usage first went over budget, used for the grace period.
    std::chrono::steady_clock::time_point over_budget_since_{};
    int open_tab_count_ = 0;
    int total_tab_count_ = 0;
};

// Helpers for formatting byte counts for display (e.g. "412.0 MB").
std::string FormatBytes(uint64_t bytes);

} // namespace memory
