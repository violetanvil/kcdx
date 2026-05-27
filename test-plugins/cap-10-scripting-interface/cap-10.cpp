// CAP-10 — kcdxScriptingInterface: a C++-registered function is CALLABLE
// from Lua and the arg+return round-trips.
//
// This is the C++ HALF of the CAP-10 test (the registration side). It
// registers kcdx.cap10test.stub — a genuinely-callable function that reads
// an int arg and returns arg+42. The PASS VERDICT for CAP-10 is owned by
// this plugin's sibling plugin.lua, which CALLS kcdx.cap10test.stub(100)
// and asserts it returns 142. Registration succeeding is only a
// PRECONDITION here — a successful RegisterFunction whose function is
// unreachable or wrong from Lua would still flunk the Lua round-trip
// (the false-PASS this strengthening closes — a PASS that asserts nothing
// falsifiable). So this side reports
// FAIL only on a real registration failure (null Scripting interface, or
// RegisterFunction returning 0); on success it stays silent and defers the
// verdict to plugin.lua.
//
// DLL-load-before-own-plugin.lua: src/config.cpp ParsePluginManifest
// accepts both a dll and a lua entrypoint on ONE plugin; the DLL load wave
// runs before RunAll executes plugin.lua, so the function this DLL
// registers in kcdxPlugin_Load is reachable when the SAME plugin's
// plugin.lua runs.

#include <windows.h>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_10_scripting_interface";
kcdxLogger  gLog;
}  // namespace

// kcdx.cap10test.stub(n) — reads integer arg 1, returns n+42. The +42
// (an arbitrary non-identity transform) makes the round-trip falsifiable:
// a Lua caller passing 100 must see exactly 142 back, proving the arg
// reached C++ AND the return reached Lua. user_data is the kcdxLuaApi*
// passed at RegisterFunction. 100/142 are well under 2^24, so they
// round-trip losslessly under CryEngine's LUA_NUMBER=float (integers beyond
// 2^24 lose precision).
static int Lua_Cap10Stub(struct lua_State* L, void* user_data) {
    const kcdxLuaApi* lua = static_cast<const kcdxLuaApi*>(user_data);
    long long arg = lua->LCheckInteger(L, 1);
    lua->PushInteger(L, arg + 42);
    return 1;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);
    gLog = kcdxLogger(api, self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    if (!scripting) {
        gLog.Error("INIT", "QueryInterface(Scripting) returned null");
        api->ReportTestResult(self, "CAP-10", 0,
            "QueryInterface(Scripting) returned null");
        return true;
    }

    // Register under our own namespace (kcdx.cap10test.stub). Pass the
    // kcdxLuaApi* as user_data so Lua_Cap10Stub can read the arg + push
    // the return without a global.
    int ok = scripting->RegisterFunction(self,
                                         "cap10test",
                                         "stub",
                                         Lua_Cap10Stub,
                                         (void*)scripting->lua);
    if (!ok) {
        // PRECONDITION failure — a real failure (invalid args, name
        // collision, OOM). Report it; plugin.lua's guard would otherwise
        // report a less-specific "not registered" once it can't find the fn.
        const char* reason = "RegisterFunction('cap10test.stub') returned 0 "
                             "(invalid args / name collision / OOM)";
        gLog.Error("SCRIPTING", "FAIL: %s", reason);
        api->ReportTestResult(self, "CAP-10", 0, reason);
        return true;
    }

    // Registration succeeded — this is NOT the verdict. plugin.lua owns the
    // PASS by calling kcdx.cap10test.stub(100) and asserting it returns 142.
    gLog.Info("SCRIPTING",
        "registered kcdx.cap10test.stub; plugin.lua will call it and own "
        "the CAP-10 verdict (the arg+return round-trip)");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
