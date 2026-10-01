# Low-RAM Mode

Low-RAM mode keeps the browser's resident working set under a budget you choose,
by closing background tabs when usage stays high for long enough to be real.

It is **off by default**. Nothing in this document applies until you enable it in
Settings → Performance.

## Contents

- [Turning it on](#turning-it-on)
- [How pressure is detected](#how-pressure-is-detected)
- [What reclamation does](#what-reclamation-does)
- [What is never reclaimed](#what-is-never-reclaimed)
- [Threading model](#threading-model)
- [Live readout](#live-readout)
- [Settings persistence](#settings-persistence)
- [Platform support](#platform-support)
- [Testing](#testing)
- [Tuning](#tuning)
- [Troubleshooting](#troubleshooting)

## Turning it on

| Setting | Key | Default | Range |
|---------|-----|---------|-------|
| Low-RAM mode | `low_ram_mode` | `false` | on / off |
| Memory budget (MB) | `memory_budget_mb` | `512` | 64 – 16384 |

The budget is a whole number of megabytes. The settings UI renders it as a
bounded number field, and the value is clamped on load, on edit and on read, so
a hand-edited `settings.json` cannot put the monitor into a nonsensical state.

A lower budget is not automatically better. Each tab costs a real WebView plus
its DOM, JavaScript heap and image cache, so a budget below roughly 200 MB on a
multi-tab session will reclaim aggressively.

## How pressure is detected

`src/MemoryMonitor.cpp` reads the **process working set** — the pages actually
resident in RAM right now, not the virtual address space, which is always large
and therefore useless as a signal.

| Platform | Source |
|----------|--------|
| Windows | `GetProcessMemoryInfo` → `WorkingSetSize` / `PeakWorkingSetSize` |
| macOS | `task_info(mach_task_self(), MACH_TASK_BASIC_INFO)` |
| Linux / Android / BSD | `/proc/self/statm` resident pages × `sysconf(_SC_PAGESIZE)` |

Three rules keep the signal from flapping:

1. **Grace period.** Usage must stay above the budget for 5 continuous seconds
   before anything is closed. The window starts once and is *not* restarted by
   further over-budget polls — an earlier version reset the timer on every poll,
   which meant the latch could never trip at all.
2. **Hysteresis.** After usage drops back below the budget, the monitor stays
   latched until usage falls to 85% of the budget (`kRecoveryRatio`). A workload
   hovering on the limit does not cause repeated evict/reopen cycles.
3. **No budget, no reading, no action.** If the platform sampler fails, or the
   mode is off, or the budget is unset, the monitor reports "not over budget"
   rather than guessing.

## What reclamation does

Reclamation runs **only** on the UI thread, and only at points where the tab map
is not mid-mutation:

- a new tab is created
- a tab is closed
- the user switches tabs

Those are also the points where memory actually grows, because each one
allocates or releases a WebView and its DOM and JavaScript heap. The settings
page deliberately does **not** trigger reclamation: it is a read-only diagnostic
surface, and reclaiming from inside a JavaScript bridge callback would mean
closing a tab from the middle of the call that is displaying memory figures.

At each of those points, if the monitor is latched over budget, the browser
repeatedly evicts the **least-recently-activated background tab** and re-measures
between evictions. Closing one view rarely brings a process that is far over
budget back under the line, so the loop re-polls rather than assuming one close
was enough. The loop stops as soon as usage is back under budget, or when only
one tab would remain.

Tab recency is a monotonic counter (`tab_last_active_seq_`) incremented on every
activation. A tab that has never been activated sorts as the coldest, which is
the desired behaviour for a freshly restored session.

Each eviction also:

1. calls `NoteTabClosed()` so the reported tab count stays accurate,
2. routes through the same JavaScript close path a user-initiated close uses, so
   session bookkeeping, history and window state are handled once, by one code
   path,
3. calls `MemoryMonitor::ReleaseFreedMemory()`, which is `malloc_trim(0)` on
   Linux and Android. Without this, glibc/Bionic keep the freed pages and the RSS
   figure never actually moves, which would make the whole loop useless,
4. calls `NotifyMemoryReclaimed()` to clear the latch and restart the grace
   window against the new footprint.

Every eviction is logged to stderr:

```
[UI] low-RAM: closed background tab 3, now 498.2 MB of 512.0 MB budget
```

## What is never reclaimed

Two exclusions are deliberate, not oversights:

- **The active tab.** Closing the page the user is looking at is never an
  acceptable outcome for a memory optimisation. `ShouldEvictBackgroundTab()`
  additionally refuses to act when fewer than two tabs are open.
- **DRM tabs.** A DRM tab holds a live media session the user is watching. Tearing
  it down mid-playback is far more disruptive than the memory it costs, so DRM
  tabs are skipped as eviction candidates.

If memory pressure can only be relieved by closing one of those, the browser
stops and stays over budget rather than doing something the user would resent.

## Threading model

Ultralight is not thread-safe, so **no view, tab or DOM state is ever touched off
the UI thread.**

`StartMemoryWatchdog()` spawns one detached worker that does nothing but:

- sleep in 100 ms steps for 2 seconds,
- call `MemoryMonitor::Poll()` (fully mutex-protected, reads only process memory),
- set `std::atomic<bool> memory_pressure_pending_` if the result is over budget.

That is the worker's entire vocabulary. It cannot create, close or navigate a
tab, because it has no reference to any of them.

`ConsumeMemoryPressureSignal()` does an `exchange(false)` and is called only from
UI-thread safe points. If the flag was never set, the safe point costs one
relaxed atomic read and nothing else. Polling at 2 s against a 5 s grace period
guarantees a transient spike is always resolved before the signal is even
raised.

The worker is stopped and joined in `~UI()`, before the monitor it polls is
destroyed.

## Live readout

With low-RAM mode enabled, Settings → Performance shows:

```
Memory: 341.7 MB used (peak 512.4 MB) of 512 MB budget · 7 open tabs
```

The panel turns red and appends `OVER BUDGET` when the monitor is latched. It
refreshes every 2 seconds, and immediately when you toggle the mode or change the
budget, rather than waiting for the next tick.

If the platform sampler reports no usable reading, the panel hides itself rather
than displaying a misleading `0 MB`.

## Settings persistence

`low_ram_mode` and `memory_budget_mb` are persisted in the settings directory
alongside every other setting, and are seeded from the catalog in
`assets/settings_catalog.json`.

`memory_budget_mb` is parsed strictly: fractional values, exponent notation
(`5e2`), and non-numeric junk are rejected in favour of the last good value,
rather than being silently coerced to 0. That matters because a budget of 0
would mean "always over budget".

## Platform support

| Platform | Sampling | `malloc_trim` | Notes |
|----------|----------|---------------|-------|
| Windows | `GetProcessMemoryInfo` | n/a | `psapi` is linked explicitly |
| macOS | `task_info` | n/a | System allocator returns pages on its own |
| Linux | `/proc/self/statm` | yes | `malloc_trim(0)` after each eviction |
| Android | `/proc/self/statm` | yes | Reachable, but see below |

Android is cross-compiled and the monitor compiles for it, but an Android build is
a cross-compiled binary rather than a running app — see the Android section of
`README.md`.

## Testing

`tests/MemoryMonitorTest.cpp` runs the monitor against an injected fake sampler,
so the behaviour above is verified deterministically with no dependency on the
machine's real memory usage. It covers sampling validity, byte formatting, budget
clamping, the grace period, sustained pressure, hysteresis, mode transitions,
tab counters, JSON serialization, and the eviction policy.

```bash
ctest --test-dir build -R MemoryMonitorTest --output-on-failure
```

The test needs no Ultralight libraries, so it builds and runs in environments
where the application itself cannot be linked. It is skipped for Android
cross-builds, since a test binary for `aarch64-linux-android` cannot execute on
the CI runner; test coverage runs in the native platform workflows.

## Tuning

- **Sudden closes while browsing.** Raise the budget, or raise the grace period
  in `MemoryMonitor` if you routinely load many heavy pages at once.
- **No effect at all.** Confirm the mode is on and that the readout shows a real
  number. On Linux, if the readout drops after an eviction but the process RSS
  reported by `ps` does not, `malloc_trim` is not returning pages — that would be
  a bug in `ReleaseFreedMemory`.
- **Want to know what it chose.** Every eviction logs the tab id and the
  before/after footprint to stderr.

## Troubleshooting

**The readout is hidden.** Either the mode is off or the platform sampler failed.
The panel deliberately hides rather than showing `0 MB`, because a fake zero would
look like a working monitor reporting no usage.

**Nothing is evicted but the readout says `OVER BUDGET`.** Expected when the only
open tab is the active one, or all other tabs are DRM tabs. The monitor reports
pressure honestly and the browser declines to act.

**Memory stays high after an eviction.** On Linux, a process can hold resident
pages that no longer back any live allocation. `ReleaseFreedMemory()` addresses
this, but only for the allocator it can reach.
