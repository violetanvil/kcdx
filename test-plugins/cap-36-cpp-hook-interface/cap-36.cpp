// CAP-36 — kcdxHookInterface (C++ mirror of kcdx.hook.*) end-to-end.
//
// Phase 3 sub-1 step 5-main chunk 5. The verification chunk that proves
// kcdxHookInterface v1 works for a real C++ DLL author. See kcdx.toml in
// this folder for the full row-by-row plan + the per-row falsifiability
// contract; this file is the as-built proof.
//
// === Idiomatic shape (chunk 6's Kcdx.h will take notes from this) =====
//
// 1. Cache (api, self) at kcdxPlugin_Load. `self` is api->GetPluginHandle
//    of the [plugin].name (the BARE name, NOT the prefixed form — see
//    src/plugin_loader.cpp::FindByName which compares manifest.name
//    directly). Every kcdxHookOptions this DLL builds threads `self`
//    into opts.owningPlugin so the self-tier of self>engine>other
//    resolves the calling plugin's identity (precedence is self > engine >
//    other-plugin). The raw-floor row PROVES this works without a wrapper to stash it
//    for the author.
//
// 2. QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version) at
//    Load. The returned vtable's lifetime is the engine's; cache the
//    pointer at file scope. Returns null on version mismatch — every
//    row reports FAIL with a clear reason in that case (loud-on-failure
//    rather than silent skipping — fix/report at the cause).
//
// 3. Build kcdxHookOptions on the stack as a POD aggregate, default-zero
//    every field by `kcdxHookOptions opts = {};`, then set ONLY the
//    fields this hook uses. Always set opts.owningPlugin = g_self;
//    always set opts.address = (uintptr_t)&YourStub; for a raw-address
//    locator always set opts.signature to the DSL string matching the
//    stub's ABI (the engine substitutes nothing for a raw `address` —
//    no library entry to read it from).
//
// 4. Cast the C callback to void* on the install call (kcdxHookHandle h
//    = api_hook->Before(nullptr, (void*)&MyCallback, &opts);). target
//    is null/"" when using the [advanced] `address` locator (per
//    ValidateBaseline at src/hook_interface.cpp:106-147 — a non-empty
//    target OR a non-empty advanced locator is required).
//
// 5. Check h != 0 (registration succeeded; the engine logs the teaching
//    error to both engine + plugin logs on failure per Interfaces.h
//    :1562-1568). Optionally call api_hook->IsApplied(h) after the apply
//    pass (PostGameLoad is post-apply, so IsApplied is final here).
//
// 6. The callback signature MUST match the per-mode shape from
//    src/dynamic_call_jit.cpp::BuildCDispatchThunk:
//      Before:        void cFn(uintptr_t args[], int* outCount,
//                              /* typed args... */)
//      After void:    void cFn(/* typed args... */)
//      After non-void:<typed_return> cFn(<typed_return> origReturn,
//                                        /* typed args... */)
//      Around:        <typed_return> cFn(<typed_return>(*call_original)(
//                                            /* typed args... */),
//                                        /* typed args... */)
//      Replace:       <typed_return> cFn(/* typed args... */)
//    A wrong-shape callback is undefined behavior (the JIT thunk casts
//    void* to whatever the per-mode signature derived from cSig
//    expects). Match it exactly.
//
// === Lifecycle ========================================================
//
// kcdxPlugin_Load:
//   - cache (api, self, logger)
//   - QueryInterface for Hook + Messaging + Scripting
//   - install all 7 hooks on the per-row stub targets
//   - register the kcdx.cap36.addr_crosslang() Lua accessor (so the
//     sibling Lua plugin can install its half of the crosslang chain)
//
// kcdxPlugin_PostGameLoad (after_game, AFTER ApplyZone(AfterGame),
// BEFORE InputLoaded — src/hooks.cpp:431-462; see cap-29 for the
// established prior art for both-phase exports):
//   - for each row: invoke the stub, observe the return, assert the
//     value matches the row's PASS contract, call api->ReportTestResult.
//
// All 7 rows report from PostGameLoad. An InputLoaded backstop (mirror
// of cap-29's design) reports a loud FAIL if PostGameLoad never fired,
// so a missing after-phase export does not leave silent PENDING rows.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_36_cpp_hook_interface";

// Cached at kcdxPlugin_Load. Every kcdxHookOptions opts.owningPlugin =
// g_self; raw-floor row included (no wrapper to stash this for the
// author). The other surfaces (logger, scripting RegisterFunction,
// ReportTestResult) all key on g_self too.
const kcdxInterface*       g_api      = nullptr;
const kcdxHookInterface*   g_hook     = nullptr;  // primary path (Floor 4)
kcdxPluginHandle           g_self     = kcdxInvalidPluginHandle;
kcdxLogger                 g_log;

// Cross-language sibling-Lua signal. The Lua sibling sets _G.cap36_lua_
// hook_fired = true in its before-hook; the C++ plugin reads it post-
// invoke to corroborate that the Lua entry on the chain fired alongside
// the C entry. (Value-distinguishable observed-return is the primary
// proof; the Lua-side flag is the corroborator surfacing in the FAIL
// reason when something went wrong.)
//
// The Lua plugin writes this via a kcdx C function (cap36.set_lua_fired)
// registered by THIS DLL, so the read is in-process via a C global —
// the same lightuserdata-handoff pattern cap-20 uses for addresses.
bool g_lua_hook_fired = false;
int  g_crosslang_observed_seed_in_lua = -1;  // for diagnostics on FAIL

// One-shot guard so an InputLoaded backstop fires only if PostGameLoad
// never reported (the cap-29 design). If PostGameLoad reports, it sets
// g_post_ran = true; the InputLoaded listener no-ops in that case.
bool g_post_ran = false;

// === Stub targets — one per row, ICF-defeated ========================
//
// Each stub is a noinline real C-ABI function with a unique volatile
// tag (defeats /OPT:ICF folding). Semantics: seed + 100 (unique tag is
// a discarded volatile read; the value contract is unchanged).
// The stub VAs are this DLL's own — opts.address = (uintptr_t)&Stub.

#define TARGET(name, tag)                                                 \
    extern "C" __declspec(noinline) int name(int seed) {                  \
        volatile int s = seed; volatile int unique = (tag); (void)unique; \
        return s + 100;                                                   \
    }
TARGET(Cap36_Add_Before,    0x3601)
TARGET(Cap36_Add_After,     0x3602)
TARGET(Cap36_Add_Around,    0x3603)
TARGET(Cap36_Add_Replace,   0x3604)
TARGET(Cap36_Add_Uninstall, 0x3605)
TARGET(Cap36_Add_RawFloor,  0x3606)
TARGET(Cap36_Crosslang,     0x3607)
#undef TARGET

// === Hook callbacks — per-mode signatures (BuildCDispatchThunk) ======

// BEFORE shape: void cFn(uintptr_t args[], int* outCount, /* typed
// args... */). Mutate args[i] (8-byte slot) and set *outCount = N to
// commit the first N slots back to params. Untouched slots (outCount
// < cSig.args.size()) are left as-is by the thunk (args[] is pre-
// populated with current slot values at Before entry per
// dynamic_call_jit.cpp:604-611, so a callback that touches nothing
// produces zero net change). The typed-arg pass-through (`int seed`
// trailing) is the engine's typed view of the same slot, given to the
// callback for ergonomic reading; the AUTHORITATIVE mutation channel
// is args[]/outCount.
extern "C" void Cap36_Before_Cb(uintptr_t args[], int* outCount,
                                int seed) {
    (void)seed;  // typed pass-through, not used here
    args[0]     = static_cast<uintptr_t>(static_cast<int32_t>(args[0]) + 1);
    *outCount   = 1;
}

// AFTER non-void shape: <typed_return> cFn(<typed_return> origReturn,
// /* typed args... */). Receive the original return as the first arg,
// return the new value. seed is the pass-through (the original function
// already ran with it; we don't get to change it).
extern "C" int Cap36_After_Cb(int origReturn, int seed) {
    (void)seed;
    return origReturn + 1000;
}

// AROUND shape: <typed_return> cFn(<typed_return>(*call_original)(
// /* typed args... */), /* typed args... */). The call_original
// parameter arrives as a pointer-width register (engine-side signature
// uses TypeId::kUIntPtr per dynamic_call_jit.cpp:417-418, D-c-fn-abi-2
// Option B); the C source-level typedef declares the typed function
// pointer so the call site can invoke it with the typed args directly.
typedef int (*Cap36_Around_CallOrig)(int);
extern "C" int Cap36_Around_Cb(Cap36_Around_CallOrig call_original,
                               int seed) {
    return 2 * call_original(seed);
}

// REPLACE shape: <typed_return> cFn(/* typed args... */). No prepended
// args, no origReturn (the original never runs).
extern "C" int Cap36_Replace_Cb(int seed) {
    (void)seed;
    return 42;
}

// UNINSTALL — use a vanilla Before for the install half; Uninstall
// later strips it. The before adds 5000 to make the hooked vs un-hooked
// values trivially distinguishable (110 un-hooked → 110+5000=5110
// hooked? No — Before mutates the ARG, not the return. seed+5000
// → original adds 100 → 5110 observed).
extern "C" void Cap36_Uninstall_Before_Cb(uintptr_t args[], int* outCount,
                                          int seed) {
    (void)seed;
    args[0]   = static_cast<uintptr_t>(static_cast<int32_t>(args[0]) + 5000);
    *outCount = 1;
}

// RAW FLOOR — same shape as Cap36_Before_Cb but a distinct function so
// the raw-floor row's hook is wholly its own. Same mutation (+1) for
// the same value expectation (111).
extern "C" void Cap36_RawFloor_Before_Cb(uintptr_t args[], int* outCount,
                                         int seed) {
    (void)seed;
    args[0]   = static_cast<uintptr_t>(static_cast<int32_t>(args[0]) + 1);
    *outCount = 1;
}

// CROSSLANG (C side) — add 1 to seed. With load-order priority C++=30,
// Lua=70, this fires FIRST; the Lua sibling's before fires SECOND and
// multiplies by 2. Stub adds 100. Seed 10 → ((10+1)*2)+100 = 122.
extern "C" void Cap36_Crosslang_Before_Cb(uintptr_t args[], int* outCount,
                                          int seed) {
    (void)seed;
    args[0]   = static_cast<uintptr_t>(static_cast<int32_t>(args[0]) + 1);
    *outCount = 1;
}

// === Hand the cross-language hook target's VA + flag-setter to Lua ===
//
// The sibling Lua plugin can't see this DLL's symbols. The cap-20
// pattern: register a kcdx C function returning the stub VA as
// lightuserdata (exact for pointer-magnitude values — pointers push as
// light userdata; PushInteger would truncate at 2^24 under LUA_NUMBER=float).

static int Lua_AddrCrosslang(struct lua_State* L, void* ud) {
    static_cast<const kcdxLuaApi*>(ud)->PushLightUserdata(
        L, reinterpret_cast<void*>(&Cap36_Crosslang));
    return 1;
}

// The Lua sibling's before-hook calls this to flip the corroborator
// flag. The Lua side passes the seed it observed (must equal C++'s
// post-mutation 11 — second-in-chain sees the first-in-chain's mutated
// arg, which proves both Lua AND C are on the same chain at the SAME
// site rather than each running on a separate detour).
static int Lua_NotifyLuaFired(struct lua_State* L, void* ud) {
    auto* lua = static_cast<const kcdxLuaApi*>(ud);
    g_lua_hook_fired = true;
    if (lua->IsNumber(L, 1)) {
        g_crosslang_observed_seed_in_lua =
            static_cast<int>(lua->ToInteger(L, 1));
    }
    return 0;
}

// === Tiny per-row PASS/FAIL helper (mirrors cap-20 / cap-29 idiom) ===

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP36", "PASS %s: %s", row, reason);
    else      g_log.Error("CAP36", "FAIL %s: %s", row, reason);
    g_api->ReportTestResult(g_self, row, pass ? 1 : 0, reason);
}

// === InputLoaded backstop — loud FAIL if PostGameLoad never fired ====
//
// PostGameLoad runs BEFORE InputLoaded by design (src/hooks.cpp:455 →
// FireEngineMessage(InputLoaded) at :462). If by InputLoaded time
// g_post_ran is still false, the after-phase C++ export was not
// dispatched — every row reports loud FAIL with the same reason rather
// than sitting silent-PENDING. This is the cap-29 design lifted.

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;  // PostGameLoad already reported every row.

    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_"
        "InputLoaded — the after-phase C++ export was not dispatched; "
        "all 7 rows reported FAIL via the InputLoaded backstop";
    g_log.Error("CAP36", "FAIL backstop: %s", reason);
    const char* rows[] = {
        "CAP-36-cpp-hook-before",
        "CAP-36-cpp-hook-after",
        "CAP-36-cpp-hook-around",
        "CAP-36-cpp-hook-replace",
        "CAP-36-cpp-hook-uninstall",
        "CAP-36-cpp-hook-raw-floor",
        "CAP-36-cpp-hook-crosslang",
    };
    for (const char* r : rows) {
        g_api->ReportTestResult(g_self, r, 0, reason);
    }
}

// === Install state held between Load and PostGameLoad ================
//
// Handles from Load's install pass; PostGameLoad reads IsApplied + uses
// the Uninstall handle for the uninstall row.

kcdxHookHandle g_h_before    = 0;
kcdxHookHandle g_h_after     = 0;
kcdxHookHandle g_h_around    = 0;
kcdxHookHandle g_h_replace   = 0;
kcdxHookHandle g_h_uninstall = 0;
kcdxHookHandle g_h_rawfloor  = 0;
kcdxHookHandle g_h_crosslang = 0;

// The raw-floor row uses a SEPARATELY-fetched kcdxHookInterface pointer
// — same vtable, freshly resolved via api->QueryInterface, NOT going
// through the cached g_hook. This is the floor-4 demo: no wrapper, no
// helper, the author types api->QueryInterface + cast + call.
const kcdxHookInterface* g_hook_raw_floor = nullptr;

// === Install all 7 hooks ============================================

bool InstallHooks() {
    // --- Row 1: Before -------------------------------------------------
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Add_Before);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_before";
        g_h_before = g_hook->Before(/*target=*/nullptr,
                                    (void*)&Cap36_Before_Cb, &opts);
        if (g_h_before == 0) {
            g_log.Error("CAP36",
                "InstallHooks: Before install returned 0 (see HOOK_"
                "INTERFACE engine log for the teaching error)");
            return false;
        }
    }

    // --- Row 2: After -------------------------------------------------
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Add_After);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_after";
        g_h_after = g_hook->After(/*target=*/nullptr,
                                  (void*)&Cap36_After_Cb, &opts);
        if (g_h_after == 0) {
            g_log.Error("CAP36", "InstallHooks: After install returned 0");
            return false;
        }
    }

    // --- Row 3: Around -------------------------------------------------
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Add_Around);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_around";
        g_h_around = g_hook->Around(/*target=*/nullptr,
                                    (void*)&Cap36_Around_Cb, &opts);
        if (g_h_around == 0) {
            g_log.Error("CAP36", "InstallHooks: Around install returned 0");
            return false;
        }
    }

    // --- Row 4: Replace ------------------------------------------------
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Add_Replace);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_replace";
        g_h_replace = g_hook->Replace(/*target=*/nullptr,
                                      (void*)&Cap36_Replace_Cb, &opts);
        if (g_h_replace == 0) {
            g_log.Error("CAP36", "InstallHooks: Replace install returned 0");
            return false;
        }
    }

    // --- Row 5: Uninstall (install a vanilla Before; strip it in
    //          PostGameLoad and re-invoke to prove the callback no
    //          longer fires) ---------------------------------------------
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Add_Uninstall);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_uninstall";
        g_h_uninstall = g_hook->Before(/*target=*/nullptr,
                                       (void*)&Cap36_Uninstall_Before_Cb,
                                       &opts);
        if (g_h_uninstall == 0) {
            g_log.Error("CAP36",
                "InstallHooks: Uninstall row's install returned 0");
            return false;
        }
    }

    // --- Row 6: Raw floor — fetch the interface AFRESH (no wrapper) ---
    //
    // Demonstrates the floor-4 contract: author writes the literal
    // QueryInterface call, casts the returned void*, threads everything
    // (including owningPlugin) by hand.
    {
        g_hook_raw_floor = static_cast<const kcdxHookInterface*>(
            g_api->QueryInterface(kcdxInterface_Hook,
                                  kcdxHookInterface_Version));
        if (!g_hook_raw_floor) {
            g_log.Error("CAP36",
                "InstallHooks: raw-floor QueryInterface(Hook, v%u) "
                "returned null (engine version mismatch?)",
                kcdxHookInterface_Version);
            return false;
        }
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Add_RawFloor);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_raw_floor";
        g_h_rawfloor = g_hook_raw_floor->Before(/*target=*/nullptr,
                                                (void*)&Cap36_RawFloor_Before_Cb,
                                                &opts);
        if (g_h_rawfloor == 0) {
            g_log.Error("CAP36",
                "InstallHooks: raw-floor Before install returned 0");
            return false;
        }
    }

    // --- Row 7: Crosslang (C++ half) ----------------------------------
    {
        kcdxHookOptions opts = {};
        opts.owningPlugin   = g_self;
        opts.address        = reinterpret_cast<uintptr_t>(&Cap36_Crosslang);
        opts.signature      = "i32 (i32 seed)";
        opts.name           = "cap36_crosslang_cpp";
        g_h_crosslang = g_hook->Before(/*target=*/nullptr,
                                       (void*)&Cap36_Crosslang_Before_Cb,
                                       &opts);
        if (g_h_crosslang == 0) {
            g_log.Error("CAP36",
                "InstallHooks: Crosslang (C++ half) install returned 0");
            return false;
        }
    }

    g_log.Info("CAP36", "InstallHooks: all 7 install calls returned non-zero "
               "handles; final IsApplied verdicts read in PostGameLoad");
    return true;
}

}  // namespace

// === kcdxPlugin_Load ==================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    // --- Fetch interfaces: Hook (primary), Messaging (backstop),
    //     Scripting (cross-language address handoff) ------------------
    g_hook = static_cast<const kcdxHookInterface*>(
        api->QueryInterface(kcdxInterface_Hook,
                            kcdxHookInterface_Version));
    if (!g_hook) {
        g_log.Error("INIT",
            "QueryInterface(Hook, v%u) returned null — every row will "
            "FAIL", kcdxHookInterface_Version);
        const char* rows[] = {
            "CAP-36-cpp-hook-before",   "CAP-36-cpp-hook-after",
            "CAP-36-cpp-hook-around",   "CAP-36-cpp-hook-replace",
            "CAP-36-cpp-hook-uninstall","CAP-36-cpp-hook-raw-floor",
            "CAP-36-cpp-hook-crosslang"
        };
        for (const char* r : rows) {
            api->ReportTestResult(g_self, r, 0,
                "QueryInterface(Hook) returned null at Plugin_Load");
        }
        return true;
    }

    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (messaging) {
        messaging->RegisterListener(g_self, nullptr, OnMessage);
    } else {
        g_log.Warn("INIT",
            "QueryInterface(Messaging) returned null — InputLoaded "
            "backstop disabled (if PostGameLoad never fires the rows "
            "sit silent-PENDING rather than loud-FAIL)");
    }

    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    if (scripting) {
        void* luaApi = (void*)scripting->lua;
        scripting->RegisterFunction(g_self, "cap36", "addr_crosslang",
                                    Lua_AddrCrosslang, luaApi);
        scripting->RegisterFunction(g_self, "cap36", "set_lua_fired",
                                    Lua_NotifyLuaFired, luaApi);
        g_log.Info("INIT",
            "registered kcdx.cap36.addr_crosslang + .set_lua_fired for "
            "the sibling Lua plugin");
    } else {
        g_log.Warn("INIT",
            "QueryInterface(Scripting) returned null — sibling Lua "
            "plugin cannot reach the crosslang stub VA; CAP-36-cpp-"
            "hook-crosslang will FAIL");
    }

    // Install all 7 hooks now (Load wave). Apply pass runs after Load
    // returns; PostGameLoad observes the final applied state.
    if (!InstallHooks()) {
        // InstallHooks already logged the specific failure; report a
        // best-effort row-level FAIL for every row that has a null
        // handle by the end of Load. PostGameLoad's per-row checks will
        // catch the rest.
        g_log.Error("INIT",
            "InstallHooks reported a binder failure; PostGameLoad will "
            "report per-row verdicts based on observed values");
    }

    return true;
}

// === kcdxPlugin_PostGameLoad ==========================================
//
// Fires AFTER ApplyZone(AfterGame) (the second drain — src/hooks.cpp
// :440) and BEFORE FireEngineMessage(InputLoaded) (:462). Every hook
// installed in Load is LIVE by now. Re-invoke each stub directly; the
// MinHook detour fires for every caller including us; observe the
// returned value; compare to the per-row expected.

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // we cached g_api in Load; the api pointer is the same.
    g_log.Info("CAP36",
               "kcdxPlugin_PostGameLoad — every hook applied; running "
               "the 7 falsifiable assertions");
    g_post_ran = true;

    // --- Row 1: Before ------------------------------------------------
    {
        bool applied = g_hook->IsApplied(g_h_before);
        int  r       = Cap36_Add_Before(10);
        char reason[200];
        const bool pass = (r == 111) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap36_Add_Before(10)=%d (expected 111, the "
            "before-callback wrote args[0]=seed+1, *outCount=1; "
            "original then +100); IsApplied=%d",
            pass ? "before-arg-mutation took effect" :
                   "before-arg-mutation did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-36-cpp-hook-before", pass, reason);
    }

    // --- Row 2: After -------------------------------------------------
    {
        bool applied = g_hook->IsApplied(g_h_after);
        int  r       = Cap36_Add_After(10);
        char reason[200];
        const bool pass = (r == 1110) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap36_Add_After(10)=%d (expected 1110, original "
            "returned 110 and after-callback returned origReturn+1000); "
            "IsApplied=%d",
            pass ? "after origReturn-mutation took effect" :
                   "after origReturn-mutation did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-36-cpp-hook-after", pass, reason);
    }

    // --- Row 3: Around ------------------------------------------------
    {
        bool applied = g_hook->IsApplied(g_h_around);
        int  r       = Cap36_Add_Around(10);
        char reason[200];
        const bool pass = (r == 220) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap36_Add_Around(10)=%d (expected 220, around "
            "callback received typed call_original and returned 2 * "
            "call_original(10)=2*110); IsApplied=%d",
            pass ? "around call_original wrap took effect" :
                   "around call_original wrap did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-36-cpp-hook-around", pass, reason);
    }

    // --- Row 4: Replace -----------------------------------------------
    {
        bool applied = g_hook->IsApplied(g_h_replace);
        int  r       = Cap36_Add_Replace(10);
        char reason[200];
        const bool pass = (r == 42) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap36_Add_Replace(10)=%d (expected 42; original "
            "never runs); IsApplied=%d",
            pass ? "replace overrode the result; original skipped" :
                   "replace did NOT override the result",
            r, applied ? 1 : 0);
        Report("CAP-36-cpp-hook-replace", pass, reason);
    }

    // --- Row 5: Uninstall ---------------------------------------------
    //
    // Two-phase assertion: (a) install applied → invoke gives 5110;
    // (b) Uninstall flips IsApplied + the callback no longer fires →
    // invoke gives un-hooked 110.
    {
        bool applied_pre = g_hook->IsApplied(g_h_uninstall);
        int  r_pre       = Cap36_Add_Uninstall(10);  // expected 5110
        bool uninstall_ok = g_hook->Uninstall(g_h_uninstall);
        bool applied_post = g_hook->IsApplied(g_h_uninstall);
        int  r_post       = Cap36_Add_Uninstall(10);  // expected 110

        char reason[400];
        const bool pass = applied_pre && (r_pre == 5110) && uninstall_ok &&
                          !applied_post && (r_post == 110);
        snprintf(reason, sizeof(reason),
            "%s — pre: IsApplied=%d Cap36_Add_Uninstall(10)=%d "
            "(expected applied=1 r=5110); Uninstall returned %d; post: "
            "IsApplied=%d Cap36_Add_Uninstall(10)=%d (expected applied=0 "
            "r=110, the un-hooked path)",
            pass ? "Uninstall lifecycle PASS" :
                   "Uninstall lifecycle FAIL (one of the four checks "
                   "did not match)",
            applied_pre ? 1 : 0, r_pre, uninstall_ok ? 1 : 0,
            applied_post ? 1 : 0, r_post);
        Report("CAP-36-cpp-hook-uninstall", pass, reason);
    }

    // --- Row 6: Raw floor (no wrapper) --------------------------------
    {
        // Re-resolve the interface AGAIN through QueryInterface to
        // prove the raw-floor caller's path. Lifetime is process so
        // both pointers are the same vtable; the test is that an
        // author who never includes Kcdx.h can do this end-to-end.
        const kcdxHookInterface* rh = static_cast<const kcdxHookInterface*>(
            g_api->QueryInterface(kcdxInterface_Hook,
                                  kcdxHookInterface_Version));
        bool got_iface = (rh != nullptr);
        bool applied   = got_iface ? rh->IsApplied(g_h_rawfloor) : false;
        int  r         = Cap36_Add_RawFloor(10);
        char reason[300];
        const bool pass = got_iface && applied && (r == 111);
        snprintf(reason, sizeof(reason),
            "%s — raw api->QueryInterface(Hook, v%u) returned %s; "
            "IsApplied=%d Cap36_Add_RawFloor(10)=%d (expected 111, the "
            "same +1 mutation Row 1 uses but routed through a freshly-"
            "fetched interface ptr with NO wrapper); the install half "
            "happened in Load via a separately-fetched g_hook_raw_floor",
            pass ? "raw-floor PASS" : "raw-floor FAIL",
            kcdxHookInterface_Version,
            got_iface ? "non-null" : "null",
            applied ? 1 : 0, r);
        Report("CAP-36-cpp-hook-raw-floor", pass, reason);
    }

    // --- Row 7: Crosslang ---------------------------------------------
    //
    // Value-distinguishable expected = 122:
    //   seed=10
    //   C++ before (load-order priority 30, fires first):  10 -> 11
    //   Lua before (load-order priority 70, fires second): 11 -> 22
    //   original Cap36_Crosslang(22): 22 + 100 = 122
    //
    // Distinguishable from:
    //   C++ only (Lua never fired):     11 + 100 = 111
    //   Lua only (C++ never fired):     20 + 100 = 120
    //   reversed order:                 (10*2)+1 + 100 = 121
    //   no hooks:                                  = 110
    {
        g_lua_hook_fired = false;  // reset before the invoke
        g_crosslang_observed_seed_in_lua = -1;
        bool applied = g_hook->IsApplied(g_h_crosslang);
        int  r       = Cap36_Crosslang(10);
        char reason[500];
        const bool pass = (r == 122) && applied && g_lua_hook_fired &&
                          (g_crosslang_observed_seed_in_lua == 11);
        snprintf(reason, sizeof(reason),
            "%s — Cap36_Crosslang(10)=%d (expected 122 = ((seed+1)*2)+100); "
            "C++ IsApplied=%d; Lua before fired=%d; seed observed in Lua "
            "callback=%d (expected 11 — C++ before mutated 10->11, Lua "
            "sees the post-C++ value, proving both entries on ONE chain "
            "at the same site)",
            pass ? "crosslang chain PASS — C and Lua entries coexist on "
                   "one ChainEntry vector, fire in load order"
                 : "crosslang chain FAIL — observed value does not match "
                   "C-before-then-Lua-before-then-original (compare: 111 "
                   "C-only, 120 Lua-only, 121 reversed, 110 no hooks)",
            r, applied ? 1 : 0, g_lua_hook_fired ? 1 : 0,
            g_crosslang_observed_seed_in_lua);
        Report("CAP-36-cpp-hook-crosslang", pass, reason);
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
