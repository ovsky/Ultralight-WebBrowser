#include "MemoryMonitor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
// psapi.h must follow windows.h.
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#include <sys/resource.h>
#else
// Linux/Android/BSD. Android needs no separate branch: Bionic provides
// /proc/self/statm, getrusage (with ru_maxrss in kilobytes) and malloc_trim.
#include <cstdlib>
#include <malloc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace memory {

namespace {

constexpr uint64_t kBytesPerMiB = 1024ull * 1024ull;

} // namespace

// ---------------------------------------------------------------------------
// ProcessMemorySampler
// ---------------------------------------------------------------------------

bool ProcessMemorySampler::Sample(MemorySample &out)
{
    MemorySample sample;

#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        sample.resident_bytes = static_cast<uint64_t>(pmc.WorkingSetSize);
        sample.peak_resident_bytes = static_cast<uint64_t>(pmc.PeakWorkingSetSize);
        sample.valid = true;
        out = sample;
        return true;
    }
    return false;

#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                 reinterpret_cast<task_info_t>(&info), &count);
    if (kr != KERN_SUCCESS)
    {
        return false;
    }

    sample.resident_bytes = static_cast<uint64_t>(info.resident_size);
    sample.peak_resident_bytes = static_cast<uint64_t>(info.resident_size_max);
    sample.valid = true;
    out = sample;
    return true;

#else
    // Linux/BSD: /proc/self/statm reports sizes in pages, field 2 is the
    // resident set. It is markedly cheaper than getrusage and is available
    // without extra privileges.
    std::FILE *statm = std::fopen("/proc/self/statm", "r");
    if (!statm)
    {
        return false;
    }

    unsigned long long total_pages = 0;
    unsigned long long resident_pages = 0;
    int matched = std::fscanf(statm, "%llu %llu", &total_pages, &resident_pages);
    std::fclose(statm);

    if (matched != 2)
    {
        return false;
    }

    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
    {
        return false;
    }

    sample.resident_bytes = static_cast<uint64_t>(resident_pages) *
                            static_cast<uint64_t>(page_size);
    sample.valid = true;
    out = sample;
    return true;
#endif
}

uint64_t ProcessMemorySampler::PeakResidentBytes() const
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<uint64_t>(pmc.PeakWorkingSetSize);
    }
    return 0;
#else
    // ru_maxrss is in kilobytes on Linux and in bytes on macOS/BSD.
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
#endif
}

// ---------------------------------------------------------------------------
// MemorySnapshot
// ---------------------------------------------------------------------------

uint64_t MemorySnapshot::ResidentMiB() const
{
    return valid ? resident_bytes / kBytesPerMiB : 0;
}

uint64_t MemorySnapshot::PeakResidentMiB() const
{
    return valid ? peak_resident_bytes / kBytesPerMiB : 0;
}

uint64_t MemorySnapshot::BudgetMiB() const
{
    return budget_bytes / kBytesPerMiB;
}

double MemorySnapshot::BudgetUsagePercent() const
{
    // A missing reading or a missing budget must not produce a divide-by-zero or
    // a misleading "100% used" figure in the settings UI.
    if (!valid || budget_bytes == 0)
    {
        return 0.0;
    }

    const double percent = (static_cast<double>(resident_bytes) /
                            static_cast<double>(budget_bytes)) *
                           100.0;
    if (percent < 0.0)
    {
        return 0.0;
    }
    if (percent > 100.0)
    {
        return 100.0;
    }
    return percent;
}

std::string MemorySnapshot::ToJSON() const
{
    char buffer[320];
    const int written = std::snprintf(
        buffer, sizeof(buffer),
        "{\"valid\":%s,\"resident_bytes\":%llu,\"peak_resident_bytes\":%llu,"
        "\"budget_bytes\":%llu,\"open_tabs\":%d,\"total_tabs\":%d,"
        "\"over_budget\":%s,\"usage_percent\":%.2f}",
        valid ? "true" : "false",
        static_cast<unsigned long long>(resident_bytes),
        static_cast<unsigned long long>(peak_resident_bytes),
        static_cast<unsigned long long>(budget_bytes), open_tab_count,
        total_tab_count, over_budget ? "true" : "false", BudgetUsagePercent());

    if (written <= 0)
    {
        // Fall back to a minimal, always-valid payload so the UI can parse it
        // rather than throwing on an empty response.
        return "{\"valid\":false,\"resident_bytes\":0,\"peak_resident_bytes\":0,"
               "\"budget_bytes\":0,\"open_tabs\":0,\"total_tabs\":0,"
               "\"over_budget\":false,\"usage_percent\":0.00}";
    }
    return std::string(buffer, static_cast<size_t>(written));
}

// ---------------------------------------------------------------------------
// MemoryMonitor
// ---------------------------------------------------------------------------

MemoryMonitor::MemoryMonitor(std::shared_ptr<MemorySampler> sampler)
    : sampler_(sampler ? std::move(sampler)
                       : std::make_shared<ProcessMemorySampler>())
{
}

MemoryMonitor::~MemoryMonitor() = default;

void MemoryMonitor::SetLowRamModeEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (low_ram_mode_enabled_ == enabled)
    {
        return;
    }

    low_ram_mode_enabled_ = enabled;

    // Reset the latch so re-enabling does not inherit a stale grace timer.
    over_budget_ = false;
    over_budget_since_ = std::chrono::steady_clock::time_point{};
}

bool MemoryMonitor::IsLowRamModeEnabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return low_ram_mode_enabled_;
}

void MemoryMonitor::SetBudgetBytes(uint64_t bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Clamp instead of rejecting: a user who types an absurd number in the
    // settings box should get a sane bound, not a silently ignored edit.
    uint64_t clamped = bytes;
    if (clamped > 0)
    {
        clamped = std::max(kMinBudgetBytes, std::min(kMaxBudgetBytes, clamped));
    }

    if (clamped != budget_bytes_)
    {
        budget_bytes_ = clamped;
        over_budget_ = false;
        over_budget_since_ = std::chrono::steady_clock::time_point{};
    }
}

uint64_t MemoryMonitor::BudgetBytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return budget_bytes_;
}

void MemoryMonitor::SetGracePeriod(std::chrono::seconds grace)
{
    std::lock_guard<std::mutex> lock(mutex_);
    grace_period_ = grace.count() < 0 ? std::chrono::seconds{0} : grace;
}

std::chrono::seconds MemoryMonitor::GracePeriod() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return grace_period_;
}

void MemoryMonitor::SetTabCounts(int open_tabs, int total_tabs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    open_tab_count_ = std::max(0, open_tabs);
    total_tab_count_ = std::max(open_tab_count_, total_tabs);
}

void MemoryMonitor::NoteTabOpened()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++open_tab_count_;
    if (total_tab_count_ < open_tab_count_)
    {
        total_tab_count_ = open_tab_count_;
    }
}

void MemoryMonitor::NoteTabClosed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_tab_count_ > 0)
    {
        --open_tab_count_;
    }
    if (total_tab_count_ > open_tab_count_ + 1)
    {
        --total_tab_count_;
    }
}

void MemoryMonitor::RecomputeOverBudgetLocked(uint64_t resident_bytes, bool sample_valid,
                                              std::chrono::steady_clock::time_point now)
{
    // Without a budget, an enabled monitor, or a usable reading, nothing is over.
    if (!low_ram_mode_enabled_ || budget_bytes_ == 0 || !sample_valid)
    {
        over_budget_since_ = std::chrono::steady_clock::time_point{};
        over_budget_ = false;
        return;
    }

    const uint64_t recovery_bytes =
        static_cast<uint64_t>(static_cast<double>(budget_bytes_) * kRecoveryRatio);

    const bool previously_tracking = over_budget_since_ != std::chrono::steady_clock::time_point{};

    if (resident_bytes > budget_bytes_)
    {
        // Start the grace window once, and keep the original start time while
        // usage stays high. Resetting it on every poll would restart the grace
        // period indefinitely and the latch could never trip.
        if (!previously_tracking)
        {
            over_budget_since_ = now;
        }
    }
    else if (previously_tracking && resident_bytes <= recovery_bytes)
    {
        // Usage has genuinely recovered, so the next excursion starts fresh.
        over_budget_since_ = std::chrono::steady_clock::time_point{};
    }

    // Between the budget and the recovery watermark we deliberately hold the
    // previous decision. A workload hovering near the limit must not flap.
    over_budget_ = previously_tracking ||
                   (over_budget_since_ != std::chrono::steady_clock::time_point{} &&
                    (now - over_budget_since_) >= grace_period_);
}

MemorySnapshot MemoryMonitor::Poll()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const bool should_sample = low_ram_mode_enabled_ || budget_bytes_ != 0;
    if (should_sample && sampler_)
    {
        MemorySample sample;
        if (sampler_->Sample(sample))
        {
            last_sample_ = sample;
            if (last_sample_.peak_resident_bytes == 0)
            {
                last_sample_.peak_resident_bytes = sampler_->PeakResidentBytes();
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    RecomputeOverBudgetLocked(last_sample_.resident_bytes, last_sample_.valid, now);

    MemorySnapshot snapshot;
    snapshot.resident_bytes = last_sample_.resident_bytes;
    snapshot.peak_resident_bytes = last_sample_.peak_resident_bytes;
    snapshot.budget_bytes = budget_bytes_;
    snapshot.open_tab_count = open_tab_count_;
    snapshot.total_tab_count = total_tab_count_;
    snapshot.over_budget = over_budget_;
    snapshot.valid = last_sample_.valid;
    return snapshot;
}

MemorySnapshot MemoryMonitor::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    MemorySnapshot snapshot;
    snapshot.resident_bytes = last_sample_.resident_bytes;
    snapshot.peak_resident_bytes = last_sample_.peak_resident_bytes;
    snapshot.budget_bytes = budget_bytes_;
    snapshot.open_tab_count = open_tab_count_;
    snapshot.total_tab_count = total_tab_count_;
    snapshot.over_budget = over_budget_;
    snapshot.valid = last_sample_.valid;
    return snapshot;
}

bool MemoryMonitor::IsOverBudget() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return over_budget_;
}

bool MemoryMonitor::ShouldEvictBackgroundTab() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Evicting the only remaining tab would leave the user with no page at all.
    // Require at least two open tabs before advising the caller to act.
    if (!over_budget_ || open_tab_count_ < 2)
    {
        return false;
    }
    return true;
}

void MemoryMonitor::NotifyMemoryReclaimed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    over_budget_ = false;
    over_budget_since_ = std::chrono::steady_clock::time_point{};
}

void MemoryMonitor::ReleaseFreedMemory()
{
#if !defined(_WIN32) && !defined(__APPLE__)
    // malloc_trim(0) returns unused heap pages to the OS immediately, which is
    // what actually moves the RSS figure. It is Linux-specific.
    malloc_trim(0);
#endif
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::string FormatBytes(uint64_t bytes)
{
    static const char *const kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }

    char buffer[64];
    if (unit == 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%llu %s",
                      static_cast<unsigned long long>(bytes), kUnits[unit]);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
    }
    return std::string(buffer);
}

} // namespace memory
