// CAP-09 task-interface test.
//
// The task runs on the main thread on the next update tick. It calls
// ReportTestResult from inside Run(). The aggregator picks up that
// report on the next lifecycle message after the tick — typically
// kInputLoaded.

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "kcdx/Interfaces.h"

namespace {

const char* kName = "cap_09_task_interface";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;
DWORD                g_loadThreadId = 0;

struct TestTask : kcdxTask {
    void Run() override {
        gLog.Info("TASK", "TestTask::Run fired; comparing thread to Plugin_Load");
        // The feature under test is that AddTask DEFERS the callback to the
        // main-thread update-tick drain (src/task.cpp DrainQueue, inside the
        // per-frame update hook) rather than running it inline on the caller.
        // There is no plugin-facing main-thread-ID accessor; the only
        // comparison available is Run()-thread vs Plugin_Load-thread. These
        // are different threads by construction: kcdxPlugin_Load runs on the
        // injector/bootstrap thread, the drain runs on the game main thread.
        // So runThreadId != g_loadThreadId is the deterministic, falsifiable
        // signal that the task correctly hopped to the main-thread drain.
        DWORD runThreadId = GetCurrentThreadId();
        bool  pass        = (runThreadId != g_loadThreadId);
        char  msg[256];
        if (pass) {
            snprintf(msg, sizeof(msg),
                "task marshaled to the main-thread drain: ran on thread %lu, "
                "a thread different from Plugin_Load's (%lu)",
                runThreadId, g_loadThreadId);
            gLog.Info("TASK", "PASS: %s", msg);
        } else {
            snprintf(msg, sizeof(msg),
                "task ran INLINE on the Plugin_Load thread %lu (run thread %lu) "
                "— AddTask did NOT marshal to the main-thread update-tick drain",
                g_loadThreadId, runThreadId);
            gLog.Error("TASK", "FAIL: %s", msg);
        }
        g_api->ReportTestResult(g_self, "CAP-09", pass ? 1 : 0, msg);
    }
    void Dispose() override { delete this; }
};

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");
    g_loadThreadId = GetCurrentThreadId();

    auto* task = static_cast<kcdxTaskInterface*>(
        api->QueryInterface(kcdxInterface_Task,
                            kcdxTaskInterface_Version));
    if (!task) {
        gLog.Error("INIT", "QueryInterface(Task) returned null");
        api->ReportTestResult(g_self, "CAP-09", 0,
            "QueryInterface(Task) returned null");
        return true;
    }

    task->AddTask(new TestTask());
    // No ReportTestResult here — the task's Run() reports when it fires.
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
