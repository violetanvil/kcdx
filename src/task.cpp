#include "task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "crash_guard.h"
#include "log.h"

namespace kcdx::task {

namespace {

constexpr const char* kCat = "TASK_PUMP";

// Pending-task queue. Mutex-protected because AddTask is callable from any
// thread; DrainQueue runs on the game's main thread once per update tick.
std::mutex          g_mutex;
std::vector<kcdxTask*> g_pending;

// === High-water teaching warn (design §5.4 ruling 2026-06-11) ===
// The pump is UNBOUNDED — the engine never rations authors: no cap, no rejection,
// no coalescing. A high-water warn makes a RUNAWAY producer diagnosable. The warn
// covers ALL pump producers (behavior commands, hook off-thread marshaling, plugin
// AddTask), since the depth is the SHARED queue's depth at enqueue.
//
// Threshold is GENEROUS — a legitimate boot/tick burst never trips it; only a
// producer flooding the pump does. The warn fires once per THRESHOLD CROSSING, not
// per enqueue (a per-iteration log on the pump is forbidden — logging.md): g_warned
// arms on the first crossing and re-arms only after the queue drains back under the
// threshold, so a single runaway logs ONE line, not one per task.
constexpr size_t kHighWaterThreshold = 512;

// Warn-once latch: true once the threshold warn has fired for the current
// over-threshold episode; cleared (re-armed) when DrainQueue empties the queue.
// relaxed ordering: g_warned is a DEBOUNCER for a teaching warn — it guards no
// shared data and establishes no happens-before edge with any other state (the
// enqueue itself is serialized by g_mutex). A relaxed race on it can at worst emit
// one extra or one fewer diagnostic line; it never affects correctness.
std::atomic<bool> g_warned{false};

void Thunk_AddTask(kcdxTask* task) {
    if (!task) {
        log::Warn("Task::AddTask: null task pointer");
        return;
    }
    size_t depth;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending.push_back(task);
        depth = g_pending.size();  // the depth at this enqueue (under the lock)
    }
    // Unbounded — the task was already enqueued above; the warn is purely
    // diagnostic, never a rejection. Fire ONE line on the threshold crossing (the
    // depth reached the high-water mark and we have not warned for this episode);
    // re-arm only after DrainQueue brings the depth back under the threshold.
    if (depth >= kHighWaterThreshold &&
        !g_warned.exchange(true, std::memory_order_relaxed)) {
        LOG_WARN_KV(kCat, "high_water",
            ::kcdx::log::KV("pending", static_cast<uint64_t>(depth)),
            ::kcdx::log::KV("threshold", static_cast<uint64_t>(kHighWaterThreshold)),
            ::kcdx::log::KV("note",
                "the main-thread task pump is unbounded and never rations authors; "
                "this depth means a producer (a behavior off-thread Set, a hook "
                "off-thread marshal, or a plugin AddTask) is enqueueing far faster "
                "than the per-tick drain — check for a tight off-thread loop issuing "
                "Set/AddTask. Warned once per crossing; re-arms after the queue "
                "drains below the threshold."));
    }
}

const kcdxTaskInterface g_iface = {
    /*AddTask=*/ Thunk_AddTask,
};

}  // namespace

const kcdxTaskInterface* GetInterface() {
    return &g_iface;
}

void DrainQueue() {
    // Snapshot under the lock, then run + dispose without the lock held.
    // A task's Run() may legitimately call AddTask to re-schedule itself
    // (it just won't fire again until the NEXT tick) — locking through
    // Run() would deadlock.
    std::vector<kcdxTask*> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        snapshot.swap(g_pending);
    }
    // The queue is now empty until a Run() re-schedules — re-arm the high-water warn
    // for the next over-threshold episode now that the pump has drained. relaxed:
    // g_warned is a teaching-warn debouncer, not a synchronization signal.
    g_warned.store(false, std::memory_order_relaxed);
    if (snapshot.empty()) return;

    for (kcdxTask* t : snapshot) {
        if (!t) continue;
        guard::Call("task.run", nullptr,
            [](void* ud) { static_cast<kcdxTask*>(ud)->Run(); },
            t);
        guard::Call("task.dispose", nullptr,
            [](void* ud) { static_cast<kcdxTask*>(ud)->Dispose(); },
            t);
    }
}

}  // namespace kcdx::task
