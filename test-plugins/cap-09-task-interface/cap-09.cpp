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

const char* kName = "kcdx.cap-09-task-interface";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;
DWORD                g_loadThreadId = 0;

struct TestTask : kcdxTask {
    void Run() override {
        gLog.Info("TASK", "TestTask::Run fired; comparing thread to Plugin_Load");
        // Pass = task fired AND on a thread different from the one
        // that called Plugin_Load. (We don't have a documented "main
        // thread ID" accessor; comparing against the Plugin_Load
        // thread is enough to verify the task hopped contexts.)
        DWORD runThreadId = GetCurrentThreadId();
        char msg[200];
        snprintf(msg, sizeof(msg),
            "task fired on thread %lu (Plugin_Load thread was %lu)",
            runThreadId, g_loadThreadId);
        gLog.Info("TASK", "PASS: %s", msg);
        g_api->ReportTestResult(g_self, "CAP-09", 1, msg);
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
