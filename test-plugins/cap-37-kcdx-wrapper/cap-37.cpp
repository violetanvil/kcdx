// CAP-37 — Kcdx.h empowered wrapper end-to-end.
//
// cap-36 is the RAW kcdxHookInterface floor
// regression net; THIS plugin is the WRAPPER's regression net. It exercises
// the include/kcdx/Kcdx.h empowered helpers (kcdx::hook::Before/After/Around/
// Replace<Sig,&fn> + the Try* handle path) and the type->DSL trait that
// derives opts.signature from <Sig> on the no-name (raw-address) path.
//
// Every row's PASS is a value-distinguishable assertion observed at
// kcdxPlugin_PostGameLoad (the after_game export — by then every hook is
// LIVE). The wrapper is behavior-preserving sugar over the raw floor, so the
// 4 sub-verb rows produce the SAME observable values cap-36's raw rows do
// (Before 111, After 1110, Around 220, Replace 42) — but here the author wrote
// NATURAL callbacks and the wrapper carried the mangled cFn ABI.
//
// === What each row proves (the wrapper machinery under test) ==========
//
//   * CAP-37-wrapper-before    by-ref Before write-back + auto outCount.
//                              Natural `void cb(int& seed){ seed += 1; }`;
//                              Kcdx.h's before_adapter writes the mutated
//                              local back to args[0] and sets *outCount.
//                              Cap37_Before(10) → 111.
//
//   * CAP-37-wrapper-after     non-void After origReturn-prepend. Natural
//                              `int cb(int origReturn, int seed)` returns
//                              origReturn+1000. Cap37_After(10) → 1110.
//
//   * CAP-37-wrapper-around    typed call_original fn-ptr. Natural
//                              `int cb(int(*call_original)(int), int seed)`
//                              returns 2*call_original(seed).
//                              Cap37_Around(10) → 220.
//
//   * CAP-37-wrapper-replace   return-only Replace; original never runs.
//                              Natural `int cb(int seed){ return 42; }`.
//                              Cap37_Replace(10) → 42.
//
//   * CAP-37-wrapper-try-handle  the Try* handle path. TryBefore returns a
//                              NON-ZERO kcdxHookHandle; K.hook->IsApplied(h)
//                              is true after the apply pass. Proves the
//                              wrapper threads the handle back (vs the void
//                              Before/etc forms that swallow it).
//
//   * CAP-37-wrapper-typemap   the type->DSL trait on MULTIPLE arg types.
//                              Stub is `i32 (i32, f32, ptr)`; the wrapper
//                              must derive that DSL string from
//                              <int(int, float, void*)> for the no-name
//                              install to succeed and the slots to marshal
//                              correctly. A wrong type->token mapping (f32
//                              emitted as i32, ptr mishandled) fails the
//                              install or mis-marshals → the observed value
//                              diverges from the expected 1016. This is the
//                              row that catches a future type-trait
//                              regression.
//
// All rows use a DLL-internal stub target (raw opts.address locator, no named
// target) — so the wrapper's no-name path (B2) derives opts.signature from
// <Sig> and threads opts.owningPlugin = K.self automatically. The author never
// hand-writes the "i32 (...)" string nor sets owningPlugin.
//
// An InputLoaded backstop (the cap-29 / cap-36 design) reports a loud FAIL on
// every row if PostGameLoad never fired.

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest bare name + author — must match [plugin] in kcdx.toml.
const char* kName   = "cap_37_kcdx_wrapper";
const char* kAuthor = "ts";

// The empowered floor. K.Init at Load fetches the sub-interfaces, resolves
// K.self = GetPluginHandle(kName), builds K.log. Every helper threads K.self
// into opts.owningPlugin for us.
Kcdx K;

// One-shot guard so the InputLoaded backstop fires only if PostGameLoad never
// reported (the cap-36 design).
bool g_post_ran = false;

// === Stub targets — one per row, ICF-defeated ========================
//
// Each stub is a noinline real C-ABI function with a unique volatile tag
// (defeats /OPT:ICF folding so each row's hook chain is its own). The four
// single-arg stubs are `int(int)`: seed + 100. The typemap stub is
// `int(int, float, void*)`: a + (int)b + (c ? 1000 : 0) — see the typemap row.

#define TARGET(name, tag)                                                 \
    extern "C" __declspec(noinline) int name(int seed) {                  \
        volatile int s = seed; volatile int unique = (tag); (void)unique; \
        return s + 100;                                                   \
    }
TARGET(Cap37_Before,   0x3701)
TARGET(Cap37_After,    0x3702)
TARGET(Cap37_Around,   0x3703)
TARGET(Cap37_Replace,  0x3704)
TARGET(Cap37_TryHandle,0x3705)
#undef TARGET

// Multi-type stub for the typemap row: i32 + f32 + ptr in one signature.
// Folds the float and pointer into the integer return so a marshaling error
// in ANY of the three arg positions perturbs the single observed value:
//   a + (int)b + (c ? 1000 : 0)
extern "C" __declspec(noinline) int Cap37_TypeMap(int a, float b, void* c) {
    volatile int unique = 0x3706; (void)unique;
    return a + static_cast<int>(b) + (c ? 1000 : 0);
}

// === Natural callbacks (Kcdx.h carries the mangled cFn ABI) ===========
//
// All NON-CAPTURING free functions (decay to a plain fn ptr — the wrapper's
// constraint). The author writes the ORIGINAL target's natural shape; the
// per-mode adapter unpacks the engine's JIT-thunk ABI for us.

// BEFORE — by-reference arg mutation. The adapter writes the mutated `seed`
// back to args[0] and sets *outCount automatically.
void Cap37_Before_Cb(int& seed) { seed += 1; }

// AFTER non-void — receive origReturn + (by-value) args; return the new return.
int Cap37_After_Cb(int origReturn, int seed) { (void)seed; return origReturn + 1000; }

// AROUND — call_original is a plain typed fn ptr the author invokes naturally.
int Cap37_Around_Cb(int (*call_original)(int), int seed) {
    return 2 * call_original(seed);
}

// REPLACE — original never runs; return the replacement.
int Cap37_Replace_Cb(int seed) { (void)seed; return 42; }

// TRY-HANDLE — a vanilla by-ref Before; the row asserts the RETURNED HANDLE,
// not a value mutation (so this callback's mutation is incidental). +0 keeps
// the observed value the un-mutated 110 (irrelevant — the handle is the proof).
void Cap37_TryHandle_Cb(int& seed) { (void)seed; }

// TYPEMAP — natural by-ref Before over the multi-type stub. Mutate the i32
// arg; observe the f32 + ptr survived marshaling via the stub's fold. The
// adapter must round-trip all three slots; we only mutate `a`.
void Cap37_TypeMap_Cb(int& a, float& b, void*& c) {
    (void)b; (void)c;  // observe-only; mutating a proves the i32 slot writes back
    a += 1;
}

// === Install state held between Load and PostGameLoad ================

kcdxHookHandle g_h_before    = 0;
kcdxHookHandle g_h_after     = 0;
kcdxHookHandle g_h_around    = 0;
kcdxHookHandle g_h_replace   = 0;
kcdxHookHandle g_h_tryhandle = 0;
kcdxHookHandle g_h_typemap   = 0;

// A non-null sentinel for the typemap row's ptr arg (its identity is what the
// stub's `c ? 1000 : 0` fold observes — a null-marshaled ptr would drop 1000).
int g_typemap_sentinel = 0;

// === Tiny per-row PASS/FAIL helper ===================================

void Report(const char* row, bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP37", "PASS %s: %s", row, reason);
    else      K.log.Error("CAP37", "FAIL %s: %s", row, reason);
    K.api->ReportTestResult(K.self, row, pass ? 1 : 0, reason);
}

// === InputLoaded backstop — loud FAIL if PostGameLoad never fired ====

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;

    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_InputLoaded "
        "— the after-phase C++ export was not dispatched; all 6 rows reported "
        "FAIL via the InputLoaded backstop";
    K.log.Error("CAP37", "FAIL backstop: %s", reason);
    const char* rows[] = {
        "CAP-37-wrapper-before",
        "CAP-37-wrapper-after",
        "CAP-37-wrapper-around",
        "CAP-37-wrapper-replace",
        "CAP-37-wrapper-try-handle",
        "CAP-37-wrapper-typemap",
    };
    for (const char* r : rows) {
        K.api->ReportTestResult(K.self, r, 0, reason);
    }
}

// === Install all 6 hooks via the empowered wrapper ===================
//
// Every install is the no-name (raw opts.address) path: the wrapper derives
// opts.signature from <Sig> and threads opts.owningPlugin = K.self. The author
// writes ONLY the natural callback + opts.address + opts.name.

bool InstallHooks() {
    // --- Row 1: Before (by-ref write-back + auto outCount) ----------------
    {
        kcdxHookOptions opts = {};
        opts.address = reinterpret_cast<uintptr_t>(&Cap37_Before);
        opts.name    = "cap37_before";
        g_h_before = kcdx::hook::TryBefore<int(int), &Cap37_Before_Cb>(
            K, /*target=*/nullptr, &opts);
        if (g_h_before == 0) {
            K.log.Error("CAP37", "InstallHooks: Before install returned 0");
            return false;
        }
    }

    // --- Row 2: After (non-void origReturn-prepend) -----------------------
    {
        kcdxHookOptions opts = {};
        opts.address = reinterpret_cast<uintptr_t>(&Cap37_After);
        opts.name    = "cap37_after";
        g_h_after = kcdx::hook::TryAfter<int(int), &Cap37_After_Cb>(
            K, /*target=*/nullptr, &opts);
        if (g_h_after == 0) {
            K.log.Error("CAP37", "InstallHooks: After install returned 0");
            return false;
        }
    }

    // --- Row 3: Around (typed call_original fn-ptr) -----------------------
    {
        kcdxHookOptions opts = {};
        opts.address = reinterpret_cast<uintptr_t>(&Cap37_Around);
        opts.name    = "cap37_around";
        g_h_around = kcdx::hook::TryAround<int(int), &Cap37_Around_Cb>(
            K, /*target=*/nullptr, &opts);
        if (g_h_around == 0) {
            K.log.Error("CAP37", "InstallHooks: Around install returned 0");
            return false;
        }
    }

    // --- Row 4: Replace (return-only; original never runs) ----------------
    {
        kcdxHookOptions opts = {};
        opts.address = reinterpret_cast<uintptr_t>(&Cap37_Replace);
        opts.name    = "cap37_replace";
        g_h_replace = kcdx::hook::TryReplace<int(int), &Cap37_Replace_Cb>(
            K, /*target=*/nullptr, &opts);
        if (g_h_replace == 0) {
            K.log.Error("CAP37", "InstallHooks: Replace install returned 0");
            return false;
        }
    }

    // --- Row 5: Try-handle (the Try* handle return) -----------------------
    {
        kcdxHookOptions opts = {};
        opts.address = reinterpret_cast<uintptr_t>(&Cap37_TryHandle);
        opts.name    = "cap37_try_handle";
        g_h_tryhandle = kcdx::hook::TryBefore<int(int), &Cap37_TryHandle_Cb>(
            K, /*target=*/nullptr, &opts);
        if (g_h_tryhandle == 0) {
            K.log.Error("CAP37", "InstallHooks: Try-handle install returned 0");
            return false;
        }
    }

    // --- Row 6: Typemap (i32 + f32 + ptr -> derived DSL) ------------------
    {
        kcdxHookOptions opts = {};
        opts.address = reinterpret_cast<uintptr_t>(&Cap37_TypeMap);
        opts.name    = "cap37_typemap";
        g_h_typemap = kcdx::hook::TryBefore<int(int, float, void*),
                                            &Cap37_TypeMap_Cb>(
            K, /*target=*/nullptr, &opts);
        if (g_h_typemap == 0) {
            K.log.Error("CAP37",
                "InstallHooks: Typemap install returned 0 (the derived DSL "
                "string for <int(int, float, void*)> may be malformed — a "
                "type->token regression)");
            return false;
        }
    }

    K.log.Info("CAP37", "InstallHooks: all 6 wrapper installs returned non-zero "
               "handles; final verdicts read in PostGameLoad");
    return true;
}

}  // namespace

// === kcdxPlugin_Load ==================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    // K.Init fetches Hook (+ the other shipped sub-interfaces), resolves
    // K.self, builds K.log. Returns false (after logging) iff Hook is
    // unavailable — in which case every row reports FAIL below.
    if (!K.Init(api, kAuthor, kName)) {
        const char* rows[] = {
            "CAP-37-wrapper-before",   "CAP-37-wrapper-after",
            "CAP-37-wrapper-around",   "CAP-37-wrapper-replace",
            "CAP-37-wrapper-try-handle","CAP-37-wrapper-typemap",
        };
        for (const char* r : rows) {
            api->ReportTestResult(api->GetPluginHandle(kName), r, 0,
                "Kcdx::Init failed (Hook interface unavailable) at Plugin_Load");
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    // InputLoaded backstop (best-effort — a null Messaging just disables it).
    if (K.messaging) {
        K.messaging->RegisterListener(K.self, nullptr, OnMessage);
    } else {
        K.log.Warn("INIT",
            "QueryInterface(Messaging) returned null — InputLoaded backstop "
            "disabled (rows sit silent-PENDING if PostGameLoad never fires)");
    }

    if (!InstallHooks()) {
        K.log.Error("INIT",
            "InstallHooks reported a binder failure; PostGameLoad reports "
            "per-row verdicts based on observed values + handles");
    }

    return true;
}

// === kcdxPlugin_PostGameLoad ==========================================
//
// Fires AFTER ApplyZone(AfterGame) and BEFORE InputLoaded — every hook is LIVE.
// Re-invoke each stub directly; the MinHook detour fires; observe + assert.

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;
    K.log.Info("CAP37",
               "kcdxPlugin_PostGameLoad — every wrapper hook applied; running "
               "the 6 falsifiable assertions");
    g_post_ran = true;

    // --- Row 1: Before ------------------------------------------------
    {
        bool applied = K.hook->IsApplied(g_h_before);
        int  r       = Cap37_Before(10);
        char reason[256];
        const bool pass = (r == 111) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap37_Before(10)=%d (expected 111; the NATURAL by-ref "
            "callback `void(int& seed){ seed += 1; }` had its mutated slot "
            "written back + *outCount auto-set by the wrapper; original +100); "
            "IsApplied=%d",
            pass ? "wrapper by-ref Before write-back took effect"
                 : "wrapper by-ref Before write-back did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-37-wrapper-before", pass, reason);
    }

    // --- Row 2: After -------------------------------------------------
    {
        bool applied = K.hook->IsApplied(g_h_after);
        int  r       = Cap37_After(10);
        char reason[256];
        const bool pass = (r == 1110) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap37_After(10)=%d (expected 1110; original returned 110, "
            "the NATURAL `int(int origReturn, int seed)` callback returned "
            "origReturn+1000 — the wrapper's non-void After origReturn-prepend "
            "adapter); IsApplied=%d",
            pass ? "wrapper non-void After took effect"
                 : "wrapper non-void After did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-37-wrapper-after", pass, reason);
    }

    // --- Row 3: Around ------------------------------------------------
    {
        bool applied = K.hook->IsApplied(g_h_around);
        int  r       = Cap37_Around(10);
        char reason[256];
        const bool pass = (r == 220) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap37_Around(10)=%d (expected 220; the NATURAL "
            "`int(int(*call_original)(int), int seed)` callback received the "
            "typed call_original fn-ptr and returned 2*call_original(10)=2*110 "
            "— the wrapper's pass-through Around adapter); IsApplied=%d",
            pass ? "wrapper typed call_original Around took effect"
                 : "wrapper typed call_original Around did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-37-wrapper-around", pass, reason);
    }

    // --- Row 4: Replace -----------------------------------------------
    {
        bool applied = K.hook->IsApplied(g_h_replace);
        int  r       = Cap37_Replace(10);
        char reason[256];
        const bool pass = (r == 42) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap37_Replace(10)=%d (expected 42; the NATURAL "
            "`int(int seed){ return 42; }` callback's return replaced the "
            "result, original never ran); IsApplied=%d",
            pass ? "wrapper return-only Replace took effect"
                 : "wrapper return-only Replace did NOT take effect",
            r, applied ? 1 : 0);
        Report("CAP-37-wrapper-replace", pass, reason);
    }

    // --- Row 5: Try-handle --------------------------------------------
    //
    // The proof is the HANDLE the Try* form returns: non-zero at install
    // (held in g_h_tryhandle) AND IsApplied true after the apply pass. The
    // void Before/After/etc forms swallow the handle; TryBefore hands it back.
    {
        bool nonzero_handle = (g_h_tryhandle != 0);
        bool applied        = nonzero_handle && K.hook->IsApplied(g_h_tryhandle);
        char reason[256];
        const bool pass = nonzero_handle && applied;
        snprintf(reason, sizeof(reason),
            "%s — TryBefore<int(int), &cb> returned handle=0x%llX (expected "
            "non-zero — the wrapper's Try* path hands the kcdxHookHandle back, "
            "unlike the void Before form); IsApplied=%d (expected 1)",
            pass ? "wrapper Try* handle path took effect"
                 : "wrapper Try* handle path did NOT return a usable handle",
            static_cast<unsigned long long>(g_h_tryhandle), applied ? 1 : 0);
        Report("CAP-37-wrapper-try-handle", pass, reason);
    }

    // --- Row 6: Typemap (i32 + f32 + ptr) -----------------------------
    //
    // Cap37_TypeMap(a, b, c) = a + (int)b + (c ? 1000 : 0). The by-ref Before
    // mutates a 10->11; b=5.0f and c=&sentinel must survive marshaling. So
    // 11 + 5 + 1000 = 1016. A wrong type->DSL mapping (f32 emitted as i32, ptr
    // mishandled) either fails the install (caught at Load) or mis-marshals a
    // slot here → the observed value diverges from 1016.
    {
        bool applied = K.hook->IsApplied(g_h_typemap);
        int  r       = Cap37_TypeMap(10, 5.0f, &g_typemap_sentinel);
        char reason[320];
        const bool pass = (r == 1016) && applied;
        snprintf(reason, sizeof(reason),
            "%s — Cap37_TypeMap(10, 5.0f, &sentinel)=%d (expected 1016 = "
            "(10 mutated to 11 by the by-ref Before) + (int)5.0f + 1000 for the "
            "non-null ptr); the wrapper derived the DSL `i32 (i32, f32, ptr)` "
            "from <int(int, float, void*)> and marshaled all three arg types "
            "correctly; IsApplied=%d. A wrong type->token mapping would fail "
            "the install or perturb this value.",
            pass ? "wrapper type->DSL trait mapped i32+f32+ptr correctly"
                 : "wrapper type->DSL trait MIS-mapped one of i32/f32/ptr",
            r, applied ? 1 : 0);
        Report("CAP-37-wrapper-typemap", pass, reason);
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
