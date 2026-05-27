// CAP-13 — kcdxConsoleInterface dispatch self-test.
//
// Test plan (auto-run on every game launch, no user input needed):
//   1. At kcdxPlugin_Load: query the interfaces, register a listener.
//   2. At kcdxMessage_InputLoaded (when kcdx::console::Init() has armed
//      the surface):
//        a. Call RegisterCommand("kcdx_test_cap13", ..., OnCap13Command).
//        b. Call ExecuteString("kcdx_test_cap13 hello world").
//        c. The callback (synchronous) records argc + arg values into
//           globals.
//        d. After ExecuteString returns, check the recorded values.
//        e. Report PASS only if argc==3 AND arg[0]=="kcdx_test_cap13"
//           AND arg[1]=="hello" AND arg[2]=="world". Otherwise PASS
//           with a "registration ok, dispatch unverified" reason
//           never — always either PASS-on-everything or FAIL.
//
// Any future regression in:
//   - Address Library id 2000/2001/2002 resolution
//   - vtable[33] semantic (the slot-32-vs-33 bug)
//   - IConsoleCmdArgs vtable layout (GetArgCount/GetArg/GetCommandLine)
//   - kcdxConsoleInterface ABI
// will fail this test on the next game launch.

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_13_console_command";

const kcdxInterface*           g_api     = nullptr;
const kcdxConsoleInterface*    g_console = nullptr;
kcdxPluginHandle               g_self    = kcdxInvalidPluginHandle;
kcdxLogger                     gLog;

bool g_reported   = false;

// Self-test verification state. Populated by OnCap13Command during
// the synchronous ExecuteString call.
bool        g_callbackFired = false;
int         g_argc           = -1;
char        g_argv0[64]      = {0};
char        g_argv1[64]      = {0};
char        g_argv2[64]      = {0};
char        g_commandLine[256] = {0};

void OnCap13Command(const kcdxConsoleCmdArgs* args) {
    g_callbackFired = true;
    if (!args || !g_console) return;
    g_argc = g_console->GetArgCount(args);
    if (const char* line = g_console->GetCommandLine(args)) {
        strncpy_s(g_commandLine, line, sizeof(g_commandLine) - 1);
    }
    if (g_argc > 0) if (const char* a = g_console->GetArg(args, 0)) strncpy_s(g_argv0, a, sizeof(g_argv0) - 1);
    if (g_argc > 1) if (const char* a = g_console->GetArg(args, 1)) strncpy_s(g_argv1, a, sizeof(g_argv1) - 1);
    if (g_argc > 2) if (const char* a = g_console->GetArg(args, 2)) strncpy_s(g_argv2, a, sizeof(g_argv2) - 1);
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_reported) return;
    g_reported = true;

    gLog.Info("CONSOLE", "InputLoaded received; running RegisterCommand+ExecuteString self-test");

    char reason[512];

    if (!g_console) {
        snprintf(reason, sizeof(reason),
            "QueryInterface(Console) returned null at Plugin_Load");
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }

    // (a) Register the command.
    bool registered = g_console->RegisterCommand(
        g_self,
        "kcdx_test_cap13",
        "Auto-fired by CAP-13 self-test at kInputLoaded; args echo to "
        "kcdx-dev.log via CONSOLE.DISPATCH.",
        OnCap13Command);
    if (!registered) {
        snprintf(reason, sizeof(reason),
            "RegisterCommand returned false (console surface not "
            "armed, or name collision)");
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }

    // (b) Self-fire via ExecuteString. ExecuteString is synchronous,
    // so OnCap13Command will have run by the time this returns.
    bool executed = g_console->ExecuteString("kcdx_test_cap13 hello world");
    if (!executed) {
        snprintf(reason, sizeof(reason),
            "ExecuteString refused (g_ExecuteString null or IConsole "
            "not ready) — registration worked, dispatch can't be "
            "tested");
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }

    // (c)+(d) Verify everything the callback observed.
    if (!g_callbackFired) {
        snprintf(reason, sizeof(reason),
            "ExecuteString returned but callback never fired "
            "(CryEngine dispatcher rejected the command — possibly "
            "a regression in vtable[33]/AddCommand registration)");
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }
    if (g_argc != 3) {
        snprintf(reason, sizeof(reason),
            "callback fired but argc=%d (expected 3); "
            "command_line='%s'", g_argc, g_commandLine);
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }
    if (strcmp(g_argv0, "kcdx_test_cap13") != 0) {
        snprintf(reason, sizeof(reason),
            "arg[0]='%s' (expected 'kcdx_test_cap13')", g_argv0);
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }
    if (strcmp(g_argv1, "hello") != 0) {
        snprintf(reason, sizeof(reason),
            "arg[1]='%s' (expected 'hello')", g_argv1);
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }
    if (strcmp(g_argv2, "world") != 0) {
        snprintf(reason, sizeof(reason),
            "arg[2]='%s' (expected 'world')", g_argv2);
        gLog.Error("CONSOLE", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, "CAP-13", 0, reason);
        return;
    }

    // All checks passed.
    snprintf(reason, sizeof(reason),
        "register+ExecuteString+callback roundtrip ok "
        "(argc=3, command_line='%s')", g_commandLine);
    gLog.Info("CONSOLE", "PASS: %s", reason);
    g_api->ReportTestResult(g_self, "CAP-13", 1, reason);
}
}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    g_console = static_cast<const kcdxConsoleInterface*>(
        api->QueryInterface(kcdxInterface_Console,
                            kcdxConsoleInterface_Version));
    // null here is reported from OnMessage when kInputLoaded fires.

    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-13", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
