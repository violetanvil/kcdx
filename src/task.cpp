#include "task.h"

#include <mutex>
#include <vector>

#include "crash_guard.h"
#include "log.h"

namespace kcdx::task {

namespace {

// Pending-task queue. Mutex-protected because AddTask is callable from any
// thread; DrainQueue runs on the game's main thread once per update tick.
std::mutex          g_mutex;
std::vector<kcdxTask*> g_pending;

void Thunk_AddTask(kcdxTask* task) {
    if (!task) {
        log::Warn("Task::AddTask: null task pointer");
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending.push_back(task);
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
