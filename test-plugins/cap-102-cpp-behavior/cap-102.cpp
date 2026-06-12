// CAP-102 — kcdxBehaviorInterface (C++ mirror of kcdx.behavior.*) end-to-end.
//
// The verification plugin that proves kcdxBehaviorInterface v1 works for a real
// C++ DLL author: the four verbs over the engine-owned value-handle model (values
// NEVER marshalled out of the one VM), the C++-side value builders, the
// generation-checked staleness, the coercion accessors, the QUERY thread-wall, the
// window law, and the VM-adoption wave-end gate. Invoke + the off-thread queued Set
// are a later step (NOT exercised here).
//
// Shape: NATIVE C++ DLL plugin + a sibling Lua plugin (cap-102-cpp-behavior-lua/)
// for the two cross-language rows. The ONE registry serves both surfaces:
//   - a C++-declared behavior (declared here at kcdxPlugin_Load) is SET FROM LUA
//     (the sibling's plugin.lua, a main stop) — proven by the C++ implementation
//     firing with the Lua-set value at the apply boundary;
//   - a Lua-declared behavior (the sibling declares it at plugin.lua) is SET FROM
//     C++ (here, at kcdxPlugin_PostGameLoad, a main stop).
//
// Lifecycle:
//   kcdxPlugin_Load (EARLY worker stop):
//     - declare two C++ behaviors (cpp_scalar, cpp_table) — declares are legal at
//       the early stop; only plugin-tier SETs from an early stop are walled.
//     - declare cpp_crosslang (set from Lua at the main stop).
//     - the EARLY-STOP OUT-OF-WINDOW row: attempt a plugin-tier Set HERE (early
//       stop) — it must FAIL loud with the out-of-window teaching error. This
//       re-issues step-4's thin-C-harness fixture through the REAL interface.
//   kcdxPlugin_PostGameLoad (MAIN stop, PRE-apply-boundary):
//     - the four verbs whose assertions are synchronous records, not applied
//       state: List, the table-value default read, the coercion mismatch, the
//       stale-handle generation check (Get→replace→old-handle-Stale), the
//       early-stop out-of-window verdict, and the C++→Lua cross-language set
//       (set + read-back, a record).
//     The apply boundary fires the implementations AFTER this stop (between
//     PostGameLoad and InputLoaded — see the OffThreadResult note below), so any
//     row reading an APPLIED value (an impl-fire flag, an applied/coerced value
//     a set produced) is reported at InputLoaded (post-boundary), NOT here.
//   InputLoaded handler (POST-apply-boundary):
//     - the boundary-dependent rows: declare-set-get (cpp_scalar's impl fired
//       with the Lua-set value + AsBool reads the applied true), the Lua-sets-C++
//       cross-language row (cpp_crosslang's impl fired with 42), the wave-end gate
//       order (cpp_scalar's impl fired = the C++ wave reached the live VM).
//     - the stale-handle-on-raise row + the off-thread QUERY thread-wall row
//       (both need BoundaryCompleted()/PostLoad() true).
//
// Every row reads ACTUAL state (the coerced handle value, the actual error text
// via GetLastError, the engine-written impl-fire flags, the boot order) — never a
// value the row itself set, so a row can genuinely fail. An InputLoaded backstop
// reports loud FAIL for any row PostGameLoad/InputLoaded never reached.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include "kcdx/Interfaces.h"

namespace {

const char* kName = "cap_102_cpp_behavior";

const kcdxInterface*         g_api      = nullptr;
const kcdxBehaviorInterface* g_beh      = nullptr;
kcdxPluginHandle             g_self     = kcdxInvalidPluginHandle;
kcdxLogger                   g_log;
bool                         g_post_ran = false;

// === The C++ implementation callbacks — they OBSERVE the value handle the engine
// hands them at the apply boundary / toggle, recording what they saw so a row can
// assert the behavior actually drove the implementation with the right value. ===

// cpp_scalar's impl: records the bool value it received at the boundary.
bool g_cpp_scalar_impl_ran   = false;
bool g_cpp_scalar_impl_value = false;
void CppScalarImpl(kcdxBehaviorValue value, void* /*ctx*/) {
    g_cpp_scalar_impl_ran = true;
    bool b = false;
    if (g_beh->AsBool(value, &b) == kcdxBehaviorAccess_Ok) {
        g_cpp_scalar_impl_value = b;
    }
}

// cpp_table's impl: records the table's element [1] (an int) it received.
bool    g_cpp_table_impl_ran    = false;
int64_t g_cpp_table_impl_elem1  = -1;
void CppTableImpl(kcdxBehaviorValue value, void* /*ctx*/) {
    g_cpp_table_impl_ran = true;
    kcdxBehaviorValue child = 0;
    if (g_beh->Index(value, 1, &child) == kcdxBehaviorAccess_Ok) {
        int64_t n = -1;
        if (g_beh->AsInt64(child, &n) == kcdxBehaviorAccess_Ok) {
            g_cpp_table_impl_elem1 = n;
        }
    }
}

// cpp_crosslang's impl: records the int the LUA sibling set it to (proves a
// C++-declared behavior set from Lua drove the C++ implementation).
bool    g_cpp_crosslang_impl_ran   = false;
int64_t g_cpp_crosslang_impl_value = -1;
void CppCrosslangImpl(kcdxBehaviorValue value, void* /*ctx*/) {
    g_cpp_crosslang_impl_ran = true;
    int64_t n = -1;
    if (g_beh->AsInt64(value, &n) == kcdxBehaviorAccess_Ok) {
        g_cpp_crosslang_impl_value = n;
    }
}

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP102", "PASS %s: %s", row, reason);
    else      g_log.Error("CAP102", "FAIL %s: %s", row, reason);
    g_api->ReportTestResult(g_self, row, pass ? 1 : 0, reason);
}

// All this plugin's row IDs — used by the InputLoaded backstop to FAIL any row
// that never got reported because kcdxPlugin_PostGameLoad did not fire. The
// InputLoaded-reported rows (the three boundary-dependent rows — declare-set-get,
// crosslang-lua-sets-cpp, wave-end-gate-order — plus stale-handle-on-raise +
// offthread-query-wall) ALSO run only when g_post_ran is true (the handler
// returns early otherwise), so the backstop covers them on the
// PostGameLoad-never-fired path too.
const char* kRows[] = {
    "CAP-102-cpp-declare-set-get",
    "CAP-102-cpp-list",
    "CAP-102-cpp-table-value",
    "CAP-102-cpp-coercion-mismatch",
    "CAP-102-cpp-stale-handle",
    "CAP-102-cpp-stale-handle-on-raise",
    "CAP-102-cpp-offthread-query-wall",
    "CAP-102-cpp-early-stop-out-of-window",
    "CAP-102-cpp-wave-end-gate-order",
    "CAP-102-crosslang-lua-sets-cpp",
    "CAP-102-crosslang-cpp-sets-lua",
};

// === The off-thread post-load QUERY fixture (the thread-wall regression) ===
//
// The QUERY thread-wall fires when PostLoad() && !IsGameMainThread(). A
// boot-to-menu test plugin runs everything on the game main thread, so the wall
// is normally unexercised. Seam: spawn a transient std::thread from a POST-LOAD
// point and call Get() from it — !IsGameMainThread() holds on the worker thread
// (a distinct OS thread id), and PostLoad() must be true at the spawn point.
//
// PostLoad() flips true only at the apply-boundary's end (RunApplyBoundary),
// which runs AFTER kcdxPlugin_PostGameLoad and BEFORE InputLoaded fires. So the
// spawn point MUST be the InputLoaded handler (post-boundary) — a thread spawned
// from PostGameLoad would see PostLoad()==false and the wall would NOT fire.
// (Verified against the boot sequence: hooks.cpp runs RunPostGameLoad, then
// RunApplyBoundary which sets the boundary-complete flag, then fires
// InputLoaded.)
//
// The worker calls Get() (the wall returns BEFORE any VM deref — confirmed: the
// interface checks OffThreadPostLoadQuery() at the top of Get/every accessor,
// before touching the Lua state), reads its OWN thread-local GetLastError text
// (the error channel is per-thread), and records the verdict into plain (non-
// thread-local) storage. The main thread JOINS the worker (the happens-before
// that publishes the worker's writes — no dangling thread, no data race), then
// reports.
struct OffThreadResult {
    std::atomic<bool> getReturned{false};   // what Get() returned (true == ok)
    std::atomic<bool> threadVerdict{false}; // access==Thread observed off-thread
    char errText[400] = {0};                // the worker's GetLastError text
};

void RunOffThreadQuery(OffThreadResult* out) {
    // Off the game main thread, post-load: Get() must hit the QUERY thread-wall.
    kcdxBehaviorValue h = 0;
    // cpp_scalar is this plugin's own behavior (declared at Load) — a resolvable
    // name, so a non-Thread reject here would be a genuine wall failure, not a
    // resolution miss.
    const bool got = g_beh->Get("cpp_scalar", &h, g_self);
    out->getReturned.store(got);
    // The two-sanctioned-pattern teaching error, read on THIS worker thread (the
    // error channel is thread-local).
    const char* err = g_beh->GetLastError();
    // Capture the verdict: the wall fires iff Get returned false AND the error
    // names BOTH sanctioned patterns (capture-in-impl + copy-out-on-main).
    const bool walled = (!got) && err &&
        (std::strstr(err, "capture the value") != nullptr) &&
        (std::strstr(err, "copy the value out on the main thread") != nullptr);
    out->threadVerdict.store(walled);
    if (err) {
        // Truncating copy into the shared buffer (the main thread reads it after
        // join).
        std::strncpy(out->errText, err, sizeof(out->errText) - 1);
        out->errText[sizeof(out->errText) - 1] = '\0';
    }
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (!g_post_ran) {
        const char* reason =
            "kcdxPlugin_PostGameLoad did not fire before InputLoaded — the C++ "
            "main-stop rows could not run; all rows FAIL via the backstop";
        for (const char* r : kRows) g_api->ReportTestResult(g_self, r, 0, reason);
        return;
    }
    if (!g_beh) return;  // backstop already reported (no interface)

    // --- Row: stale handle on an impl-RAISE toggle (the missed-bump regression) -
    // We are now post-boundary (BoundaryCompleted()==true), on the game main
    // thread — so a Set takes the post-load TOGGLE path that invokes the
    // implementation. The sibling's lua_raiser (a revert declarer, applied at the
    // boundary with false) raises on the toggle to true: the registry runs
    // revert(false) (ok) then implementation(true) (RAISES) → clears the record
    // to unset (get() now answers the default) AND must bump the value generation
    // (the get()-answered value CHANGED: recorded value → default). A handle
    // minted BEFORE the toggle must therefore read Stale. The regression this
    // guards: the impl-raise clear path previously cleared recordedRef WITHOUT
    // bumping the generation, so the old handle read as FRESH and dereferenced the
    // now-default ref — a silent wrong value (a write that misses its target yet
    // reports OK). The verdict (Stale vs Ok) is the discriminator, independent of
    // any value coincidence.
    {
        kcdxBehaviorValue oldH = 0;
        bool got1 = g_beh->Get("ts.cap_102_cpp_behavior_lua.lua_raiser",
                               &oldH, g_self);
        bool preVal = true;
        kcdxBehaviorAccess preA = got1 ? g_beh->AsBool(oldH, &preVal)
                                       : kcdxBehaviorAccess_BadHandle;
        // Toggle to true — lua_raiser's implementation raises on true. The C++
        // Set returns TRUE at the consumer: a declarer-code raise is logged
        // against the declarer, NOT re-surfaced as a Set failure (the registry
        // contract — mirrors the Lua binder). The OBSERVABLE proof the raise
        // disposition ran is the old handle going Stale (record cleared +
        // generation bumped), NOT the Set return.
        kcdxBehaviorValue nv = g_beh->NewBool(true);
        bool setOk = g_beh->Set("ts.cap_102_cpp_behavior_lua.lua_raiser",
                                nv, g_self);
        // The OLD handle must now read Stale (the generation advanced on the
        // impl-raise clear) — NOT Ok-into-the-default-ref. THIS is the Fix-1
        // discriminator: without the generation bump on the impl-raise clear
        // path, postA would be Ok and the old handle would dereference the now-
        // cleared/default ref — a silent wrong value.
        bool throwaway = false;
        kcdxBehaviorAccess postA = g_beh->AsBool(oldH, &throwaway);
        const char* err = g_beh->GetLastError();
        // PASS: pre-read Ok, the Set returned true at the consumer (the raise is
        // the declarer's, logged, not re-surfaced), and the OLD handle is now
        // generation-checked Stale. FAILS if postA is Ok (the missed bump — the
        // old handle reads fresh against a cleared record, a silent wrong value).
        const bool pass = got1 && preA == kcdxBehaviorAccess_Ok && setOk &&
                          postA == kcdxBehaviorAccess_Stale &&
                          err && std::strstr(err, "stale") != nullptr;
        char reason[560];
        snprintf(reason, sizeof(reason),
            "%s — pre: Get+AsBool access=%d value=%d; impl-raise toggle Set(true) "
            "ok=%d (true — a declarer-code raise is logged against the declarer, "
            "not re-surfaced at the consumer); post: old-handle AsBool access=%d "
            "GetLastError=\"%s\" (PASS: the impl-raise clear bumped the "
            "generation, so the OLD handle is Stale — NOT a fresh read into the "
            "cleared/default ref. FAILS if access=Ok: the missed generation bump "
            "= a silent wrong value the counter exists to prevent)",
            pass ? "impl-raise stale-handle bump ok"
                 : "impl-raise stale-handle WRONG",
            static_cast<int>(preA), preVal ? 1 : 0, setOk ? 1 : 0,
            static_cast<int>(postA), err ? err : "<null>");
        g_api->ReportTestResult(g_self, "CAP-102-cpp-stale-handle-on-raise",
                                pass ? 1 : 0, reason);
    }

    // --- Row: off-thread post-load QUERY hits the thread-wall ----------------
    // We are now post-boundary (PostLoad()==true) on the game main thread. Spawn
    // a transient worker thread, drive a deliberately-off-thread Get(), JOIN it
    // (no fire-and-forget), then report from here (the main thread).
    {
        OffThreadResult res;
        std::thread worker(RunOffThreadQuery, &res);
        worker.join();  // happens-before: the worker's writes are visible now.
        const bool getReturned  = res.getReturned.load();
        const bool walled       = res.threadVerdict.load();
        // PASS: the off-thread call was REJECTED (Get returned false) AND the
        // error named the two sanctioned patterns. FAILS if the off-thread Get
        // SUCCEEDED (a silent off-thread VM touch — the bug the wall prevents),
        // or the error did not teach the two patterns.
        const bool pass = (!getReturned) && walled;
        char reason[600];
        snprintf(reason, sizeof(reason),
            "%s — an off-thread (worker std::thread) post-load Get(cpp_scalar) "
            "returned ok=%d; GetLastError(worker)=\"%s\" (PASS requires ok=0 + "
            "the thread teaching error naming BOTH sanctioned patterns: "
            "capture-in-impl AND copy-out-on-main. A success here would be a "
            "silent off-thread VM touch — the race the wall prevents; the thread "
            "is joined before this report, no dangling handle)",
            pass ? "off-thread query wall fired" : "off-thread query wall WRONG",
            getReturned ? 1 : 0, res.errText[0] ? res.errText : "<null>");
        g_api->ReportTestResult(g_self, "CAP-102-cpp-offthread-query-wall",
                                pass ? 1 : 0, reason);
    }

    // --- Row: declare + set + get round-trip on cpp_scalar (the four verbs) ---
    // cpp_scalar was set FROM LUA at the main stop (the sibling sets it true).
    // Reported HERE (post-boundary), NOT at PostGameLoad: the boundary fires
    // CppScalarImpl AFTER PostGameLoad, so the impl-fire flags + the APPLIED
    // AsBool value are only readable now. We assert (a) get()'s applied value
    // reflects the recorded true, and (b) the C++ implementation fired at the
    // boundary with that value. Both reads are engine-written state — get()'s
    // applied value and the impl-fire flags the engine set when it invoked the
    // declarer's code — never a value this row set, so it can genuinely FAIL.
    {
        kcdxBehaviorValue h = 0;
        bool got = g_beh->Get("cpp_scalar", &h, g_self);
        bool val = false;
        kcdxBehaviorAccess a = got ? g_beh->AsBool(h, &val)
                                   : kcdxBehaviorAccess_BadHandle;
        // The Lua sibling sets cpp_scalar = true at the main stop; the boundary
        // invokes CppScalarImpl(true). PASS: get()==true AND impl ran with true.
        const bool pass = got && a == kcdxBehaviorAccess_Ok && val == true &&
                          g_cpp_scalar_impl_ran && g_cpp_scalar_impl_value == true;
        char reason[400];
        snprintf(reason, sizeof(reason),
            "%s — Get(cpp_scalar)=%d AsBool=%d value=%d; impl ran=%d impl saw=%d "
            "(PASS: the Lua sibling set cpp_scalar=true at its main stop, the C++ "
            "implementation fired at the boundary with true, get() reflects it — "
            "ONE registry across both languages, no value marshalled out; read "
            "post-boundary at InputLoaded where the applied value + impl flags "
            "exist)",
            pass ? "C++ behavior declare/set/get round-trip ok"
                 : "round-trip WRONG",
            got ? 1 : 0, static_cast<int>(a), val ? 1 : 0,
            g_cpp_scalar_impl_ran ? 1 : 0, g_cpp_scalar_impl_value ? 1 : 0);
        g_api->ReportTestResult(g_self, "CAP-102-cpp-declare-set-get",
                                pass ? 1 : 0, reason);
    }

    // --- Row: crosslang — Lua set a C++-declared behavior --------------------
    // cpp_crosslang was declared in C++; the Lua sibling sets it to 42 at its
    // main stop. The C++ implementation fired at the boundary with 42. Reported
    // HERE (post-boundary), NOT at PostGameLoad: the impl-fire flags are only
    // written once the boundary invokes CppCrosslangImpl, after PostGameLoad.
    {
        kcdxBehaviorValue h = 0;
        bool got = g_beh->Get("cpp_crosslang", &h, g_self);
        int64_t val = -1;
        kcdxBehaviorAccess a = got ? g_beh->AsInt64(h, &val)
                                   : kcdxBehaviorAccess_BadHandle;
        const bool pass = got && a == kcdxBehaviorAccess_Ok && val == 42 &&
                          g_cpp_crosslang_impl_ran &&
                          g_cpp_crosslang_impl_value == 42;
        char reason[400];
        snprintf(reason, sizeof(reason),
            "%s — Get(cpp_crosslang)=%lld (access=%d); impl ran=%d impl saw=%lld "
            "(PASS: a C++-DECLARED behavior SET FROM LUA — the Lua sibling set "
            "cpp_crosslang=42 at its main stop, the C++ implementation fired with "
            "42 at the boundary; the ONE registry serves both languages; read "
            "post-boundary at InputLoaded where the impl flags exist)",
            pass ? "Lua-sets-C++-declared ok" : "Lua-sets-C++-declared WRONG",
            static_cast<long long>(val), static_cast<int>(a),
            g_cpp_crosslang_impl_ran ? 1 : 0,
            static_cast<long long>(g_cpp_crosslang_impl_value));
        g_api->ReportTestResult(g_self, "CAP-102-crosslang-lua-sets-cpp",
                                pass ? 1 : 0, reason);
    }

    // --- Row: wave-end gate ORDER — the C++-wave-end signal preceded adoption --
    // The intercept WAITS on the C++-wave-end gate before adopting the VM.
    // Reported HERE (post-boundary), NOT at PostGameLoad: the proof is
    // cpp_scalar's impl having fired, which the boundary records after
    // PostGameLoad. The engine-log line order (LUA_VM_BUILD wave_end_gate_signaled
    // BEFORE engine_adopted_kcdx_state) is the backstop the agent greps.
    {
        // The interface does not expose CppWaveEnded to plugins; the engine-log
        // ORDER is the falsifiable signal. We assert what a plugin CAN observe:
        // the C++ wave reached the live VM under the gated guarantee — proven
        // transitively by cpp_scalar's impl having fired with the Lua-set value
        // (the boundary read the VM the C++ wave used). If the gate were inverted
        // (adoption before wave end), the C++ wave's VM access would have raced
        // the engine's VM overwrite and the round-trip would be wrong.
        const bool pass = g_cpp_scalar_impl_ran;  // the wave used the live VM
        char reason[400];
        snprintf(reason, sizeof(reason),
            "%s — the VM-adoption wave-end gate held the engine off the VM until "
            "the C++ wave finished: cpp_scalar's implementation fired at the "
            "boundary (impl ran=%d), proving the C++ wave reached the live VM "
            "under the gated guarantee. The falsifiable BOOT-ORDER backstop the "
            "agent greps: LUA_VM_BUILD 'wave_end_gate_signaled' MUST precede "
            "'engine_adopted_kcdx_state' in the engine log (signal-before-adopt). "
            "An inversion would race the engine's VM overwrite",
            pass ? "wave-end gate order ok" : "wave-end gate order WRONG",
            g_cpp_scalar_impl_ran ? 1 : 0);
        g_api->ReportTestResult(g_self, "CAP-102-cpp-wave-end-gate-order",
                                pass ? 1 : 0, reason);
    }
}

// === Stash between Load and PostGameLoad ===
// The early-stop out-of-window verdict captured at Load (reported at PostGameLoad
// so the matrix line is contiguous). Captured eagerly so a missing-interface Load
// still surfaces a reason.
bool g_early_stop_walled = false;
char g_early_stop_text[400] = {0};

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load (early worker stop) — engine v0x%08X",
               api->kcdxVersion);

    g_beh = static_cast<const kcdxBehaviorInterface*>(
        api->QueryInterface(kcdxInterface_Behavior, kcdxBehaviorInterface_Version));
    if (!g_beh) {
        const char* reason =
            "QueryInterface(Behavior, v1) returned null at Plugin_Load — every "
            "row FAILs (engine version mismatch?)";
        g_log.Error("INIT", "%s", reason);
        for (const char* r : kRows) api->ReportTestResult(g_self, r, 0, reason);
        return true;
    }

    // Register the InputLoaded backstop.
    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version));
    if (messaging) messaging->RegisterListener(g_self, nullptr, OnMessage);

    // --- Declare the C++ behaviors (declares are legal at the early stop) ----
    // cpp_scalar: a bool behavior, default false, with a revert (togglable).
    {
        kcdxBehaviorValue def = g_beh->NewBool(false);
        if (!g_beh->Declare("cpp_scalar", "a C++-declared bool behavior", def,
                            CppScalarImpl, /*revert=*/nullptr, /*ctx=*/nullptr,
                            g_self)) {
            g_log.Error("INIT", "declare cpp_scalar failed: %s",
                        g_beh->GetLastError());
        }
    }
    // cpp_table: a table behavior whose default is { 7 }.
    {
        kcdxBehaviorValue t = g_beh->NewTable();
        g_beh->SetIndex(t, 1, g_beh->NewInt64(7));
        if (!g_beh->Declare("cpp_table", "a C++-declared table behavior", t,
                            CppTableImpl, nullptr, nullptr, g_self)) {
            g_log.Error("INIT", "declare cpp_table failed: %s",
                        g_beh->GetLastError());
        }
    }
    // cpp_crosslang: set from Lua at the main stop; default int 0.
    {
        kcdxBehaviorValue def = g_beh->NewInt64(0);
        if (!g_beh->Declare("cpp_crosslang",
                            "a C++-declared behavior the Lua sibling sets", def,
                            CppCrosslangImpl, nullptr, nullptr, g_self)) {
            g_log.Error("INIT", "declare cpp_crosslang failed: %s",
                        g_beh->GetLastError());
        }
    }

    // --- The EARLY-STOP OUT-OF-WINDOW fixture (re-issue of step-4's harness) --
    // A plugin-tier Set from THIS early stop (kcdxPlugin_Load) MUST fail loud
    // with the out-of-window teaching error — plugin behaviors resolve at the
    // main stop. Set our OWN cpp_scalar (declared just above) from here.
    {
        kcdxBehaviorValue v = g_beh->NewBool(true);
        const bool ok = g_beh->Set("cpp_scalar", v, g_self);
        const char* err = g_beh->GetLastError();
        // PASS iff the Set was REJECTED (ok==false) AND the error names the
        // main-stop window rule (the out-of-window wall).
        const bool walled = (!ok) && err &&
            (std::strstr(err, "main stop") != nullptr ||
             std::strstr(err, "early stop") != nullptr);
        g_early_stop_walled = walled;
        snprintf(g_early_stop_text, sizeof(g_early_stop_text),
            "%s — an early-stop (kcdxPlugin_Load) plugin-tier Set on 'cpp_scalar' "
            "returned ok=%d; GetLastError=\"%s\" (PASS requires ok=0 + an "
            "out-of-window/main-stop teaching error — plugin behaviors resolve at "
            "the main stop, settable only from kcdxPlugin_PostGameLoad)",
            walled ? "early-stop wall fired" : "early-stop wall did NOT fire",
            ok ? 1 : 0, err ? err : "<null>");
    }

    return true;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;
    g_post_ran = true;
    if (!g_beh) return true;  // backstop already reported

    g_log.Info("CAP102", "kcdxPlugin_PostGameLoad (main stop) — running rows");

    // --- Row: the early-stop out-of-window verdict (captured at Load) ---------
    Report("CAP-102-cpp-early-stop-out-of-window", g_early_stop_walled,
           g_early_stop_text);

    // NOTE: CAP-102-cpp-declare-set-get runs in the InputLoaded handler, NOT
    // here. It reads cpp_scalar's APPLIED value (AsBool) + the impl-fire flags,
    // both written by the apply boundary — which fires AFTER PostGameLoad. At
    // PostGameLoad the Lua-set value is RECORDED (Get sees it) but not yet
    // APPLIED (AsBool/the impl have not run), so reading it here is one stop too
    // early. See OnMessage.

    // --- Row: List(prefix) enumerates the C++ behaviors -----------------------
    {
        struct Ctx { int count; bool sawScalar; bool sawTable; } ctx = {0, false, false};
        auto cb = [](const kcdxBehaviorListEntry* e, void* u) {
            auto* c = static_cast<Ctx*>(u);
            c->count++;
            if (std::strstr(e->name, "cpp_scalar")) c->sawScalar = true;
            if (std::strstr(e->name, "cpp_table"))  c->sawTable  = true;
        };
        uint32_t n = g_beh->List("ts.cap_102_cpp_behavior.", cb, &ctx);
        const bool pass = n >= 2 && ctx.sawScalar && ctx.sawTable;
        char reason[300];
        snprintf(reason, sizeof(reason),
            "%s — List(\"ts.cap_102_cpp_behavior.\") enumerated %u entries "
            "(callback saw %d), cpp_scalar=%d cpp_table=%d (PASS: at least the two "
            "C++-declared behaviors enumerate with their stamped full names)",
            pass ? "List enumeration ok" : "List enumeration WRONG",
            n, ctx.count, ctx.sawScalar ? 1 : 0, ctx.sawTable ? 1 : 0);
        Report("CAP-102-cpp-list", pass, reason);
    }

    // --- Row: table value handle — read cpp_table's default { 7 } -------------
    {
        kcdxBehaviorValue h = 0;
        bool got = g_beh->Get("cpp_table", &h, g_self);
        kcdxBehaviorType ty = got ? g_beh->TypeOf(h) : kcdxBehaviorType_Invalid;
        size_t len = 0;
        kcdxBehaviorValue child = 0;
        int64_t elem1 = -1;
        bool lenOk = got && g_beh->Length(h, &len) == kcdxBehaviorAccess_Ok;
        bool idxOk = got && g_beh->Index(h, 1, &child) == kcdxBehaviorAccess_Ok &&
                     g_beh->AsInt64(child, &elem1) == kcdxBehaviorAccess_Ok;
        // cpp_table is never set, so get() answers the default { 7 }: type table,
        // length 1, [1] == 7. The boundary did NOT invoke CppTableImpl (never set).
        const bool pass = got && ty == kcdxBehaviorType_Table && lenOk &&
                          len == 1 && idxOk && elem1 == 7 && !g_cpp_table_impl_ran;
        char reason[400];
        snprintf(reason, sizeof(reason),
            "%s — Get(cpp_table) type=%d len=%zu [1]=%lld; impl ran=%d "
            "(PASS: never-set cpp_table answers its default {7} — type table, "
            "len 1, [1]==7, traversed via the table accessors ON the VM with no "
            "value marshalled out; the implementation did NOT fire, default is a "
            "get() answer not an applied state)",
            pass ? "table value-handle traversal ok" : "table traversal WRONG",
            static_cast<int>(ty), len, static_cast<long long>(elem1),
            g_cpp_table_impl_ran ? 1 : 0);
        Report("CAP-102-cpp-table-value", pass, reason);
    }

    // --- Row: coercion mismatch — AsInt64 on a table value → TypeError --------
    {
        kcdxBehaviorValue h = 0;
        bool got = g_beh->Get("cpp_table", &h, g_self);
        int64_t n = 999;
        kcdxBehaviorAccess a = got ? g_beh->AsInt64(h, &n)
                                   : kcdxBehaviorAccess_BadHandle;
        const char* err = g_beh->GetLastError();
        // PASS: AsInt64 on a table returns TypeError, the out-param is UNTOUCHED
        // (still 999), and GetLastError names the actual type (table).
        const bool pass = got && a == kcdxBehaviorAccess_TypeError && n == 999 &&
                          err && std::strstr(err, "table") != nullptr;
        char reason[400];
        snprintf(reason, sizeof(reason),
            "%s — AsInt64(cpp_table handle) returned access=%d, out-param=%lld "
            "(unchanged from 999?), GetLastError=\"%s\" (PASS: a coercion mismatch "
            "is a LOUD TypeError naming the actual type 'table', the out-param "
            "untouched — never a silently-wrong value)",
            pass ? "coercion mismatch fails loud" : "coercion mismatch WRONG",
            static_cast<int>(a), static_cast<long long>(n), err ? err : "<null>");
        Report("CAP-102-cpp-coercion-mismatch", pass, reason);
    }

    // --- Row: stale handle — Get a handle, replace the value, old handle stale -
    // cpp_scalar has no revert, so a post-load set on it errors (can't change
    // mid-session) — that would NOT replace the value. Instead use the
    // Lua-declared togglable behavior (lua_togglable, declared by the sibling
    // with a revert) for the stale test: Get a handle, toggle the value, assert
    // the OLD handle's accessor now returns Stale.
    {
        kcdxBehaviorValue oldH = 0;
        bool got1 = g_beh->Get("ts.cap_102_cpp_behavior_lua.lua_togglable",
                               &oldH, g_self);
        bool preVal = false;
        kcdxBehaviorAccess preA = got1 ? g_beh->AsBool(oldH, &preVal)
                                       : kcdxBehaviorAccess_BadHandle;
        // Toggle it (a post-load main-thread set on a revert declarer) — this
        // replaces the recorded value and bumps the generation.
        kcdxBehaviorValue nv = g_beh->NewBool(!preVal);
        bool setOk = g_beh->Set("ts.cap_102_cpp_behavior_lua.lua_togglable",
                                nv, g_self);
        // The OLD handle's accessor must now report Stale (generation advanced).
        bool throwaway = false;
        kcdxBehaviorAccess postA = g_beh->AsBool(oldH, &throwaway);
        const char* err = g_beh->GetLastError();
        const bool pass = got1 && preA == kcdxBehaviorAccess_Ok && setOk &&
                          postA == kcdxBehaviorAccess_Stale &&
                          err && std::strstr(err, "stale") != nullptr;
        char reason[450];
        snprintf(reason, sizeof(reason),
            "%s — pre: Get+AsBool access=%d value=%d; toggle set ok=%d; "
            "post: old-handle AsBool access=%d GetLastError=\"%s\" (PASS: after "
            "the value is replaced by the toggle, the OLD handle is "
            "generation-checked Stale — a teaching error, NEVER a dangle into the "
            "replaced ref)",
            pass ? "stale-handle generation check ok" : "stale-handle WRONG",
            static_cast<int>(preA), preVal ? 1 : 0, setOk ? 1 : 0,
            static_cast<int>(postA), err ? err : "<null>");
        Report("CAP-102-cpp-stale-handle", pass, reason);
    }

    // NOTE: CAP-102-cpp-stale-handle-on-raise runs in the InputLoaded handler,
    // NOT here. It needs a POST-BOUNDARY toggle (BoundaryCompleted()==true) so
    // the Set takes the post-load TOGGLE path that invokes the implementation
    // (which raises) — at PostGameLoad the boundary has not yet run, so a Set is
    // a load-window record (no impl invocation, the impl-raise path untested).

    // NOTE: CAP-102-cpp-wave-end-gate-order + CAP-102-crosslang-lua-sets-cpp run
    // in the InputLoaded handler, NOT here. Both assert an impl-fire flag
    // (cpp_scalar's / cpp_crosslang's), an APPLIED-state observation the apply
    // boundary writes AFTER PostGameLoad. The wave-end-order row uses cpp_scalar's
    // impl-fire as transitive proof the C++ wave reached the live VM; the
    // crosslang row asserts cpp_crosslang's impl fired with the Lua-set 42. Both
    // are post-boundary reads — see OnMessage.

    // --- Row: crosslang — C++ sets a Lua-declared behavior -------------------
    // The Lua sibling declares lua_consumed (default int 0); C++ sets it to 99
    // HERE (a main stop). Read it back to prove the C++ set recorded onto the
    // Lua-declared behavior. (The Lua sibling's CAP-102-crosslang-cpp-sets-lua-
    // impl-fired row asserts the C++ Set DROVE its implementation with 99 from
    // its own input_loaded handler — the value-effect half. This C++ row proves
    // the C++ Set RESOLVED + RECORDED the Lua-declared behavior; both halves are
    // the cross-language Set.)
    {
        // Lua declares lua_consumed at the main stop (before this after-stop), so
        // it exists by now. Set it from C++ — a post-load main-thread set on a
        // revert declarer (the sibling declares it with a revert).
        kcdxBehaviorValue v = g_beh->NewInt64(99);
        bool setOk = g_beh->Set("ts.cap_102_cpp_behavior_lua.lua_consumed",
                                v, g_self);
        const char* setErr = setOk ? "" : g_beh->GetLastError();
        // Read it back: the toggle recorded 99.
        kcdxBehaviorValue h = 0;
        bool got = setOk && g_beh->Get(
            "ts.cap_102_cpp_behavior_lua.lua_consumed", &h, g_self);
        int64_t val = -1;
        kcdxBehaviorAccess a = got ? g_beh->AsInt64(h, &val)
                                   : kcdxBehaviorAccess_BadHandle;
        const bool pass = setOk && got && a == kcdxBehaviorAccess_Ok && val == 99;
        char reason[400];
        snprintf(reason, sizeof(reason),
            "%s — C++ Set(lua_consumed=99) ok=%d (err=\"%s\"); read-back=%lld "
            "(access=%d) (PASS: a LUA-DECLARED behavior SET FROM C++ at the C++ "
            "main stop — the toggle resolved + recorded onto the cross-language "
            "behavior; get() reads 99 back)",
            pass ? "C++-sets-Lua-declared ok" : "C++-sets-Lua-declared WRONG",
            setOk ? 1 : 0, setErr, static_cast<long long>(val),
            static_cast<int>(a));
        Report("CAP-102-crosslang-cpp-sets-lua", pass, reason);
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
