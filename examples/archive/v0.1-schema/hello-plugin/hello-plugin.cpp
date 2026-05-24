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

// One kcdxLogger holds the api pointer + this plugin's handle. Wired up
// during kcdxPlugin_Load and reused by every callback that fires later
// (engine messages, tasks, Lua bindings). The wrapper is in
// kcdx/Interfaces.h; see kcdx/docs/logging.md for the routing model.
static kcdxLogger gLog;

// Engine interface + Lua API pointers — same pattern as gLog but for
// callbacks that need more than just logging (RegisterFunction etc.).
static const kcdxInterface* g_api  = nullptr;

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
    gLog.Info("MESSAGING", "engine message: %s (type=%u)", name, msg->messageType);
}

// Example task: prints from the main thread. Plugins extend kcdxTask, override
// Run() (which gets called on the main thread next update tick), and Dispose()
// (which typically just `delete this`).
struct HelloTask : kcdxTask {
    void Run() override {
        gLog.Info("TASK", "HelloTask::Run on main thread");
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
    g_api = api;
    kcdxPluginHandle self = api->GetPluginHandle("hello_plugin");
    gLog  = kcdxLogger(api, self);

    gLog.Info("INIT", "kcdxPlugin_Load called");
    gLog.Info("INIT", "handle=%u engine=0x%08X game=0x%08X",
              self, api->kcdxVersion, api->runtimeGameVersion);

    uint32_t count = api->EnumeratePlugins(nullptr, 0);
    gLog.Info("INIT", "%u plugin(s) loaded total", count);

    // Subscribe to engine lifecycle messages.
    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version));
    if (msg) {
        msg->RegisterListener(self, /*sender=*/nullptr, OnEngineMessage);
        gLog.Info("MESSAGING", "subscribed to engine messages");
    } else {
        gLog.Warn("MESSAGING", "Messaging interface unavailable");
    }

    // Submit a task to the main thread.
    auto* task = static_cast<kcdxTaskInterface*>(
        api->QueryInterface(kcdxInterface_Task, kcdxTaskInterface_Version));
    if (task) {
        task->AddTask(new HelloTask());
        gLog.Info("TASK", "submitted a HelloTask");
    } else {
        gLog.Warn("TASK", "Task interface unavailable");
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

        void* branch = tramp->AllocateFromBranchPool(self, 64);
        if (branch) {
            uintptr_t b = reinterpret_cast<uintptr_t>(branch);
            int64_t offset = (b > whBase) ? int64_t(b - whBase) : -int64_t(whBase - b);
            bool inRange = (offset > -int64_t(0x80000000ll)) && (offset < int64_t(0x7FFFFFFFll));
            gLog.Info("TRAMPOLINE",
                "branch-pool alloc OK: 0x%p (offset from WHGame.dll = %lld, in rel32 range = %s)",
                branch, static_cast<long long>(offset), inRange ? "YES" : "NO");
        } else {
            gLog.Warn("TRAMPOLINE", "branch-pool alloc failed");
        }

        void* local = tramp->AllocateFromLocalPool(self, 64);
        if (local) {
            gLog.Info("TRAMPOLINE", "local-pool alloc OK: 0x%p", local);
        } else {
            gLog.Warn("TRAMPOLINE", "local-pool alloc failed");
        }
    } else {
        gLog.Warn("TRAMPOLINE", "Trampoline interface unavailable");
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
        if (scripting->RegisterFunction(self, "hello", "greet", Lua_Greet, nullptr)) {
            gLog.Info("SCRIPTING", "registered kcdx.hello.greet");
        } else {
            gLog.Warn("SCRIPTING", "RegisterFunction(greet) failed");
        }
        if (scripting->RegisterFunction(self, "hello", "add", Lua_Add, nullptr)) {
            gLog.Info("SCRIPTING", "registered kcdx.hello.add");
        } else {
            gLog.Warn("SCRIPTING", "RegisterFunction(add) failed");
        }
    } else {
        gLog.Warn("SCRIPTING", "Scripting interface unavailable");
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
