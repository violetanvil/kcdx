// hello-plugin.cpp — minimal kcdx plugin
//
// Demonstrates the smallest possible kcdx plugin: a DLL with the required
// kcdxPluginVersionData export and a kcdxPlugin_Load function that:
//
//   - subscribes to engine lifecycle messages (Phase 3 messaging)
//   - submits a task to the main thread (Phase 3 task queue)
//   - allocates from both trampoline pools and confirms branch-pool
//     proximity to WHGame.dll (Phase 4 trampoline interface)
//
// All log output goes through api->Log, which writes to this plugin's own
// log file at plugins/hello-plugin/hello-plugin.log. No DebugView required.
//
// Build with the example CMakeLists.txt in this folder.

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

// NOTE: plugins do NOT #include "lua.h" directly. kcdx's vendored
// Lua 5.1 is statically linked inside kcdx.asi with no exported
// symbols (matches SKSE's pattern — see Interfaces.h commentary on
// kcdxLuaApi). All Lua C API calls go through the kcdxLuaApi*
// function-pointer struct.

// kcdxPluginVersionData no longer exported — plugin metadata moved to
// kcdx.toml's [plugin] section in this plugin folder. See ./kcdx.toml.

// Static references to the engine interface + our handle, captured at Load
// time. Used by the engine-message callback and the task callback, both of
// which are called outside the kcdxPlugin_Load scope.
static const kcdxInterface* g_api  = nullptr;
static kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;

// Tiny helper so callbacks don't repeat themselves.
static void LogInfo(const char* msg) {
    if (g_api) g_api->Log(g_self, kcdxLog_Info, msg);
}
static void LogInfoF(const char* fmt, ...) {
    if (!g_api) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_api->Log(g_self, kcdxLog_Info, buf);
}

// Lifecycle callback: fires for each engine-originated message we subscribed
// to. The engine fires these with sender == nullptr.
static void OnEngineMessage(kcdxMessage* msg) {
    const char* name = "?";
    switch (msg->messageType) {
        case kcdxMessage_PostLoad:      name = "PostLoad";      break;
        case kcdxMessage_PostPostLoad:  name = "PostPostLoad";  break;
        case kcdxMessage_InputLoaded:   name = "InputLoaded";   break;
        case kcdxMessage_NewGame:       name = "NewGame";       break;
        case kcdxMessage_PreLoadGame:   name = "PreLoadGame";   break;
        case kcdxMessage_PostLoadGame:  name = "PostLoadGame";  break;
        case kcdxMessage_SaveGame:      name = "SaveGame";      break;
        case kcdxMessage_DeleteGame:    name = "DeleteGame";    break;
    }
    LogInfoF("engine message: %s (type=%u)", name, msg->messageType);
}

// Example task: prints from the main thread. Plugins extend kcdxTask, override
// Run() (which gets called on the main thread next update tick), and Dispose()
// (which typically just `delete this`).
struct HelloTask : kcdxTask {
    void Run() override {
        LogInfo("HelloTask::Run on main thread");
    }
    void Dispose() override {
        delete this;
    }
};

// Stashed at Load time so the registered functions have access
// without a global lookup each call.
static const kcdxLuaApi* g_lua = nullptr;

// A Lua-callable native function. Signature:
//   kcdx.hello.greet(name) -> string
// Demonstrates the kcdxScriptingInterface::RegisterFunction round-trip.
static int Lua_Greet(struct lua_State* L, void* /*user_data*/) {
    const char* name = g_lua->ToString(L, 1);
    if (!name) name = "stranger";
    char buf[256];
    snprintf(buf, sizeof(buf), "hello, %s, from hello-plugin", name);
    g_lua->PushString(L, buf);
    return 1;
}

// kcdx.hello.add(a, b) -> number. Variation on the same theme.
static int Lua_Add(struct lua_State* L, void* /*user_data*/) {
    double a = g_lua->ToNumber(L, 1);
    double b = g_lua->ToNumber(L, 2);
    g_lua->PushNumber(L, a + b);
    return 1;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle("violetanvil.hello-plugin");

    LogInfo("kcdxPlugin_Load called");
    LogInfoF("my handle is %u, engine version 0x%08X, game version 0x%08X",
             g_self, api->kcdxVersion, api->runtimeGameVersion);

    uint32_t count = api->EnumeratePlugins(nullptr, 0);
    LogInfoF("%u plugin(s) loaded total", count);

    // Subscribe to engine lifecycle messages.
    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version));
    if (msg) {
        msg->RegisterListener(g_self, /*sender=*/nullptr, OnEngineMessage);
        LogInfo("subscribed to engine messages");
    } else {
        api->Log(g_self, kcdxLog_Warn, "Messaging interface unavailable");
    }

    // Submit a task to the main thread.
    auto* task = static_cast<kcdxTaskInterface*>(
        api->QueryInterface(kcdxInterface_Task, kcdxTaskInterface_Version));
    if (task) {
        task->AddTask(new HelloTask());
        LogInfo("submitted a HelloTask");
    } else {
        api->Log(g_self, kcdxLog_Warn, "Task interface unavailable");
    }

    // Exercise the trampoline interface. Allocates 64 bytes from each pool
    // and reports the resulting addresses. For the branch pool we also
    // compute |alloc - WHGame_base| and confirm it's < 2 GB, proving
    // proximity for a future 5-byte rel32 jump.
    auto* tramp = static_cast<kcdxTrampolineInterface*>(
        api->QueryInterface(kcdxInterface_Trampoline, kcdxTrampolineInterface_Version));
    if (tramp) {
        HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
        uintptr_t whBase = reinterpret_cast<uintptr_t>(whgame);

        void* branch = tramp->AllocateFromBranchPool(g_self, 64);
        if (branch) {
            uintptr_t b = reinterpret_cast<uintptr_t>(branch);
            int64_t offset = (b > whBase) ? int64_t(b - whBase) : -int64_t(whBase - b);
            bool inRange = (offset > -int64_t(0x80000000ll)) && (offset < int64_t(0x7FFFFFFFll));
            LogInfoF("branch-pool alloc OK: 0x%p (offset from WHGame.dll = %lld, in rel32 range = %s)",
                     branch, static_cast<long long>(offset), inRange ? "YES" : "NO");
        } else {
            api->Log(g_self, kcdxLog_Warn, "branch-pool alloc failed");
        }

        void* local = tramp->AllocateFromLocalPool(g_self, 64);
        if (local) {
            LogInfoF("local-pool alloc OK: 0x%p", local);
        } else {
            api->Log(g_self, kcdxLog_Warn, "local-pool alloc failed");
        }
    } else {
        api->Log(g_self, kcdxLog_Warn, "Trampoline interface unavailable");
    }

    // Phase 5e: register Lua-callable native functions under
    // kcdx.hello.*. The actual application to the live lua_State
    // happens later (after kcdx creates the kcdx global at
    // first-update-tick); these calls queue.
    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    if (scripting) {
        g_lua = scripting->lua;
        if (scripting->RegisterFunction(g_self, "hello", "greet", Lua_Greet, nullptr)) {
            LogInfo("registered kcdx.hello.greet");
        } else {
            api->Log(g_self, kcdxLog_Warn, "RegisterFunction(greet) failed");
        }
        if (scripting->RegisterFunction(g_self, "hello", "add", Lua_Add, nullptr)) {
            LogInfo("registered kcdx.hello.add");
        } else {
            api->Log(g_self, kcdxLog_Warn, "RegisterFunction(add) failed");
        }
    } else {
        api->Log(g_self, kcdxLog_Warn, "Scripting interface unavailable");
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
