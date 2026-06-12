// CAP-102 — kcdxBehaviorInterface (C++ mirror of kcdx.behavior.*) end-to-end.
//
// The verification plugin that proves kcdxBehaviorInterface works for a real C++
// DLL author: the four verbs over the engine-owned value-handle model (values NEVER
// marshalled out of the one VM), the C++-side value builders, the
// generation-checked staleness, the coercion accessors, the QUERY thread-wall, the
// window law, and the VM-adoption wave-end gate (v1) — plus (v2, P2 s2) Invoke
// (calling a callable value, both a C++-registered fn-pointer and a Lua-declared
// function value) and the off-thread QUEUED Set (a post-load Set from a worker
// thread queues + executes its toggle on the game main thread at the next
// DrainQueue, with off-thread value construction staged engine-side + per-
// disposition async attribution).
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
const kcdxTaskInterface*     g_task     = nullptr;  // schedules the deferred queued-check
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

// === The C++ NewCallable target (the cpp-callable Invoke row) ===
// A kcdxBehaviorImplFn-shaped function registered AS a callable value via
// NewCallable. When Invoked, the engine's C-impl trampoline hands it the FIRST
// pcall arg as `value` (the trampoline forwards arg 1; the callable returns no Lua
// value, so Invoke's result is a no-result handle). It records that it fired + the
// int it received so the row can assert the call reached it with the arg.
bool    g_cpp_callable_ran      = false;
int64_t g_cpp_callable_arg      = -1;
void CppCallable(kcdxBehaviorValue value, void* /*ctx*/) {
    g_cpp_callable_ran = true;
    int64_t n = -1;
    if (g_beh->AsInt64(value, &n) == kcdxBehaviorAccess_Ok) g_cpp_callable_arg = n;
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
    // P2 s2 — Invoke + the off-thread queued Set.
    "CAP-102-cpp-invoke-cpp-callable",
    "CAP-102-cpp-invoke-lua-callable",
    "CAP-102-cpp-offthread-set-queues",
    "CAP-102-cpp-queued-misuse-attribution",
    "CAP-102-cpp-queued-declarer-raise-attribution",
    "CAP-102-cpp-offthread-table-payload",
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

// === The off-thread QUEUED-SET fixtures (P2 s2) ===
//
// A post-load Set from a NON-main thread QUEUES (returns having queued) and its
// toggle executes on the game main thread at the next DrainQueue. Each worker below
// runs ON a transient std::thread spawned post-load, builds its value OFF-THREAD
// (the builder stages it — no live VM off-thread), issues an off-thread Set (which
// queues), records whether the Set returned true (queued, not errored on thread),
// and is JOINED before the report (no fire-and-forget). The toggle's EFFECT (get()
// flipped / the impl fired / the value attribution) is observed by a DEFERRED main-
// thread task that runs on a LATER tick — AFTER the queued behavior commands have
// drained (FIFO: the behavior commands are enqueued from these workers, the deferred
// check is enqueued after the joins, so it drains after them).

const char* kLuaOffthread        = "ts.cap_102_cpp_behavior_lua.lua_offthread";
const char* kLuaOffthreadRevless = "ts.cap_102_cpp_behavior_lua.lua_offthread_revertless";
const char* kLuaOffthreadRaiser  = "ts.cap_102_cpp_behavior_lua.lua_offthread_raiser";
const char* kLuaOffthreadTable   = "ts.cap_102_cpp_behavior_lua.lua_offthread_table";

// Each worker's "Set returned having queued" verdict (read by the deferred check
// after all joins — plain storage, published by the join happens-before).
std::atomic<bool> g_q_offthread_queued{false};
std::atomic<bool> g_q_misuse_queued{false};
std::atomic<bool> g_q_raiser_queued{false};
std::atomic<bool> g_q_table_queued{false};

// Worker: off-thread Set lua_offthread = 7 (a revert togglable int). Queues.
void WkSetOffthread() {
    kcdxBehaviorValue v = g_beh->NewInt64(7);            // staged off-thread
    g_q_offthread_queued.store(g_beh->Set(kLuaOffthread, v, g_self));
}
// Worker: off-thread Set lua_offthread_revertless = 9 — a CONSUMER-MISUSE (a
// revert-less post-load set). The Set still QUEUES off-thread (it never errors on
// thread); the queued command then logs the failure attributed to the SETTER and
// leaves the value unchanged (asserted by the deferred check: get() stays 5).
void WkSetMisuse() {
    kcdxBehaviorValue v = g_beh->NewInt64(9);
    g_q_misuse_queued.store(g_beh->Set(kLuaOffthreadRevless, v, g_self));
}
// Worker: off-thread Set lua_offthread_raiser = true — a DECLARER-CODE raise (the
// impl raises on true). Queues; the queued command runs the toggle, the impl raises,
// the registry clears the record + logs attributed to the DECLARER (the deferred
// check asserts get() reverted to the default false).
void WkSetRaiser() {
    kcdxBehaviorValue v = g_beh->NewBool(true);
    g_q_raiser_queued.store(g_beh->Set(kLuaOffthreadRaiser, v, g_self));
}
// Worker: off-thread Set lua_offthread_table = { 77 } — a STAGED TABLE built
// off-thread (NewTable + SetIndex stage the description), materialized on the main
// thread at the queued command's execution. The impl records element [1] (77).
void WkSetTable() {
    kcdxBehaviorValue t = g_beh->NewTable();             // staged table off-thread
    kcdxBehaviorValue e = g_beh->NewInt64(77);           // staged child off-thread
    g_beh->SetIndex(t, 1, e);                            // stage into the table desc
    g_q_table_queued.store(g_beh->Set(kLuaOffthreadTable, t, g_self));
}

// The DEFERRED check — a kcdxTask scheduled (from the InputLoaded handler, after the
// off-thread Sets queued + joined) to run on a LATER main-thread tick, by which time
// the queued behavior commands have drained. It reports the four off-thread-queued
// rows by reading ACTUAL post-toggle state (get() values / the revert dispositions),
// then self-disposes.
struct DeferredQueuedCheck : kcdxTask {
    void Run() override {
        if (!g_beh) return;  // backstop already reported

        // --- Row: off-thread Set queued → executed on main → get() flipped -------
        // lua_offthread (default 0, applied at load with 0). The off-thread Set
        // queued 7; after the drain the toggle applied 7. get()==7 is the proof the
        // queued command executed on the main thread (an off-thread setter never
        // touched the VM). FAILS if the Set errored on thread (queued=false), or the
        // toggle never executed (get() still 0).
        {
            kcdxBehaviorValue h = 0;
            bool got = g_beh->Get(kLuaOffthread, &h, g_self);
            int64_t val = -1;
            kcdxBehaviorAccess a = got ? g_beh->AsInt64(h, &val)
                                       : kcdxBehaviorAccess_BadHandle;
            const bool queued = g_q_offthread_queued.load();
            const bool pass = queued && got && a == kcdxBehaviorAccess_Ok && val == 7;
            char reason[400];
            snprintf(reason, sizeof(reason),
                "%s — off-thread Set(lua_offthread=7) queued=%d; after the next "
                "DrainQueue get()=%lld (access=%d) (PASS: the off-thread Set QUEUED "
                "(returned true on thread, never errored) and the toggle executed on "
                "the game main thread at the next apply point — get() flipped 0->7. "
                "FAILS if the Set errored on thread, or the toggle never ran (get() "
                "still 0): the queued command path is broken)",
                pass ? "off-thread queued Set executed on main"
                     : "off-thread queued Set WRONG",
                queued ? 1 : 0, static_cast<long long>(val), static_cast<int>(a));
            g_api->ReportTestResult(g_self, "CAP-102-cpp-offthread-set-queues",
                                    pass ? 1 : 0, reason);
        }

        // --- Row: queued misuse → attributed to the SETTER (async) ---------------
        // lua_offthread_revertless (revert-LESS, applied at load = 5). The off-thread
        // queued Set(9) is a consumer-misuse (a revert-less post-load set). It QUEUED
        // off-thread; the queued command rejected the toggle, logged the failure
        // attributed to the SETTING plugin (ts.cap_102_cpp_behavior), and left the
        // value UNCHANGED. The observable proof here is get()==5 (unchanged); the
        // attribution itself is the engine-log line the agent greps
        // (BEHAVIOR_INTERFACE queued_set_failed setter=cap_102_cpp_behavior). FAILS
        // if the value CHANGED (the revert-less post-load set was wrongly applied) or
        // the Set errored on thread instead of queuing.
        {
            kcdxBehaviorValue h = 0;
            bool got = g_beh->Get(kLuaOffthreadRevless, &h, g_self);
            int64_t val = -1;
            kcdxBehaviorAccess a = got ? g_beh->AsInt64(h, &val)
                                       : kcdxBehaviorAccess_BadHandle;
            const bool queued = g_q_misuse_queued.load();
            const bool pass = queued && got && a == kcdxBehaviorAccess_Ok && val == 5;
            char reason[460];
            snprintf(reason, sizeof(reason),
                "%s — off-thread Set(lua_offthread_revertless=9) queued=%d; after the "
                "drain get()=%lld (access=%d) (PASS: a revert-less post-load Set is a "
                "CONSUMER-MISUSE — it queued off-thread, the queued command rejected "
                "the toggle, logged the failure attributed to the SETTER "
                "(cap_102_cpp_behavior), and left the value at its load value 5. The "
                "attribution is the engine-log 'queued_set_failed setter=...' line. "
                "FAILS if the value changed (misuse wrongly applied) or the Set "
                "errored on thread)",
                pass ? "queued misuse attributed to setter (value unchanged)"
                     : "queued misuse WRONG",
                queued ? 1 : 0, static_cast<long long>(val), static_cast<int>(a));
            g_api->ReportTestResult(g_self,
                                    "CAP-102-cpp-queued-misuse-attribution",
                                    pass ? 1 : 0, reason);
        }

        // --- Row: queued declarer-raise → attributed to the DECLARER (async) -----
        // lua_offthread_raiser (revert togglable bool, applied clean at load with
        // false). The off-thread queued Set(true) toggles it → the impl RAISES → the
        // registry clears the record (get() reverts to the default false) and logs
        // the raise attributed to the DECLARER (cap_102_cpp_behavior_lua). The
        // observable proof is get()==false (reverted); the attribution is the engine
        // log line (BEHAVIOR registry, declarer=...). FAILS if get() reads true (the
        // raise disposition did not clear the record) or the Set errored on thread.
        {
            kcdxBehaviorValue h = 0;
            bool got = g_beh->Get(kLuaOffthreadRaiser, &h, g_self);
            bool val = true;
            kcdxBehaviorAccess a = got ? g_beh->AsBool(h, &val)
                                       : kcdxBehaviorAccess_BadHandle;
            const bool queued = g_q_raiser_queued.load();
            const bool pass = queued && got && a == kcdxBehaviorAccess_Ok &&
                              val == false;
            char reason[460];
            snprintf(reason, sizeof(reason),
                "%s — off-thread Set(lua_offthread_raiser=true) queued=%d; after the "
                "drain get()=%d (access=%d) (PASS: the queued toggle ran the impl, "
                "which RAISED — the registry cleared the record (get() reverted to "
                "the default false) and logged the raise attributed to the DECLARER "
                "(cap_102_cpp_behavior_lua), NOT the setter. The attribution is the "
                "registry's declarer-attributed raise log line. FAILS if get() is "
                "true (the raise did not clear the record) or the Set errored on "
                "thread)",
                pass ? "queued declarer-raise attributed to declarer (record cleared)"
                     : "queued declarer-raise WRONG",
                queued ? 1 : 0, val ? 1 : 0, static_cast<int>(a));
            g_api->ReportTestResult(g_self,
                                    "CAP-102-cpp-queued-declarer-raise-attribution",
                                    pass ? 1 : 0, reason);
        }

        // --- Row: off-thread table-payload → the impl receives the table ---------
        // lua_offthread_table (revert togglable table, applied at load { 0 }). The
        // off-thread Set staged a TABLE { 77 } (built off-thread via NewTable +
        // SetIndex — a plain-data description, materialized on the main thread at the
        // queued command's execution). After the drain get()[1]==77 proves the staged
        // table MATERIALIZED on the main thread + the toggle applied it. FAILS if the
        // table did not materialize (get() not a table / wrong element) or the Set
        // errored on thread.
        {
            kcdxBehaviorValue h = 0;
            bool got = g_beh->Get(kLuaOffthreadTable, &h, g_self);
            kcdxBehaviorType ty = got ? g_beh->TypeOf(h) : kcdxBehaviorType_Invalid;
            kcdxBehaviorValue child = 0;
            int64_t elem1 = -1;
            bool idxOk = got && ty == kcdxBehaviorType_Table &&
                         g_beh->Index(h, 1, &child) == kcdxBehaviorAccess_Ok &&
                         g_beh->AsInt64(child, &elem1) == kcdxBehaviorAccess_Ok;
            const bool queued = g_q_table_queued.load();
            const bool pass = queued && got && ty == kcdxBehaviorType_Table &&
                              idxOk && elem1 == 77;
            char reason[460];
            snprintf(reason, sizeof(reason),
                "%s — off-thread Set(lua_offthread_table={77}) queued=%d; after the "
                "drain get() type=%d [1]=%lld (PASS: the off-thread table built via "
                "NewTable+SetIndex STAGED as a plain-data description, materialized "
                "on the game main thread at the queued command's execution, and the "
                "toggle applied it — get() is a table with [1]==77. FAILS if the "
                "table did not materialize (wrong type/element) or the Set errored "
                "on thread)",
                pass ? "off-thread staged-table materialized + applied"
                     : "off-thread table-payload WRONG",
                queued ? 1 : 0, static_cast<int>(ty),
                static_cast<long long>(elem1));
            g_api->ReportTestResult(g_self, "CAP-102-cpp-offthread-table-payload",
                                    pass ? 1 : 0, reason);
        }
    }
    void Dispose() override { delete this; }
};

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

    // === P2 s2: Invoke (synchronous, main thread, post-boundary) =============

    // --- Row: Invoke a C++-registered (NewCallable) callable value -----------
    // Build a callable value off a C function pointer (CppCallable), Invoke it with
    // one value-handle arg (41). The engine's C-impl trampoline forwards arg 1 to
    // CppCallable as its value; the callable returns no Lua value, so Invoke's
    // result is a no-result handle (0). PASS: Invoke returns Ok, the callable FIRED
    // and received 41, and the result is the no-result handle. FAILS if the call did
    // not fire, the arg did not reach it, or Invoke errored. Reads ACTUAL state (the
    // engine-set fire flag + the int the callable coerced from the handle).
    {
        g_cpp_callable_ran = false;
        g_cpp_callable_arg = -1;
        kcdxBehaviorValue callable = g_beh->NewCallable(CppCallable, nullptr);
        kcdxBehaviorValue arg = g_beh->NewInt64(41);
        kcdxBehaviorValue argv[1] = { arg };
        kcdxBehaviorValue result = 12345;  // sentinel — must become 0 (no result)
        kcdxBehaviorAccess a = g_beh->Invoke(callable, argv, 1, &result);
        // PASS: Ok, the C callable fired with 41, the result is the no-result handle.
        const bool pass = a == kcdxBehaviorAccess_Ok && g_cpp_callable_ran &&
                          g_cpp_callable_arg == 41 && result == 0;
        char reason[440];
        snprintf(reason, sizeof(reason),
            "%s — Invoke(NewCallable, [41]) access=%d; callable ran=%d arg=%lld "
            "result=%llu (PASS: Invoke called the C++-registered callable value with "
            "the value-handle arg — the callable fired and received 41; the C-impl "
            "callable returns no Lua value so the result is the no-result handle 0. "
            "FAILS if the call did not fire, the arg did not reach it, or Invoke "
            "errored — args are value handles, uniform with the value model)",
            pass ? "Invoke on a C++ callable value ok" : "Invoke (C++ callable) WRONG",
            static_cast<int>(a), g_cpp_callable_ran ? 1 : 0,
            static_cast<long long>(g_cpp_callable_arg),
            static_cast<unsigned long long>(result));
        g_api->ReportTestResult(g_self, "CAP-102-cpp-invoke-cpp-callable",
                                pass ? 1 : 0, reason);
    }

    // --- Row: Invoke a LUA-declared callable value (returns a result) --------
    // lua_callable is a behavior whose VALUE is a Lua function(a,b) -> a+b (the Lua
    // sibling declared it; never set, so get() answers the default function). Get
    // the function value, Invoke it with two value-handle args (2, 3) → the result
    // handle must coerce to 5. PASS: Invoke Ok + AsInt64(result)==5. FAILS if the
    // call did not fire, the args did not reach the function, or the result is
    // wrong. This is the "returned result handle" half — a real Lua return value
    // pinned into a fresh handle.
    {
        kcdxBehaviorValue fn = 0;
        bool gotFn = g_beh->Get("ts.cap_102_cpp_behavior_lua.lua_callable",
                                &fn, g_self);
        kcdxBehaviorType ty = gotFn ? g_beh->TypeOf(fn) : kcdxBehaviorType_Invalid;
        kcdxBehaviorValue a2 = g_beh->NewInt64(2);
        kcdxBehaviorValue a3 = g_beh->NewInt64(3);
        kcdxBehaviorValue argv[2] = { a2, a3 };
        kcdxBehaviorValue result = 0;
        kcdxBehaviorAccess ia = gotFn && ty == kcdxBehaviorType_Function
            ? g_beh->Invoke(fn, argv, 2, &result)
            : kcdxBehaviorAccess_TypeError;
        int64_t rv = -1;
        kcdxBehaviorAccess ra = (ia == kcdxBehaviorAccess_Ok)
            ? g_beh->AsInt64(result, &rv) : kcdxBehaviorAccess_BadHandle;
        const bool pass = gotFn && ty == kcdxBehaviorType_Function &&
                          ia == kcdxBehaviorAccess_Ok && ra == kcdxBehaviorAccess_Ok &&
                          rv == 5;
        char reason[480];
        snprintf(reason, sizeof(reason),
            "%s — Get(lua_callable) type=%d; Invoke([2,3]) access=%d → result "
            "AsInt64 access=%d value=%lld (PASS: a LUA-declared callable value, "
            "called from C++ with value-handle args, returned a result handle "
            "coercing to 2+3==5 — the pcall harness reuses, the arg-marshal is the "
            "new layer; one value concept for construction AND calling. FAILS if "
            "the call did not fire, the args did not reach the function, or the "
            "result is wrong)",
            pass ? "Invoke on a Lua callable value ok"
                 : "Invoke (Lua callable) WRONG",
            static_cast<int>(ty), static_cast<int>(ia), static_cast<int>(ra),
            static_cast<long long>(rv));
        g_api->ReportTestResult(g_self, "CAP-102-cpp-invoke-lua-callable",
                                pass ? 1 : 0, reason);
    }

    // === P2 s2: the off-thread QUEUED Set fixtures ============================
    // Spawn a transient worker per off-thread Set (each builds its value OFF-THREAD
    // — the builder stages it — and issues an off-thread Set that QUEUES). JOIN each
    // (no fire-and-forget). The toggle EFFECT is observed by a DEFERRED main-thread
    // task scheduled AFTER the joins: FIFO puts the queued behavior commands (queued
    // from the workers) before the deferred check, so by the time it runs the
    // toggles have drained. We are post-boundary (PostLoad() true), so an off-thread
    // Set takes the QUEUE path (not the inline toggle).
    {
        std::thread w1(WkSetOffthread); w1.join();
        std::thread w2(WkSetMisuse);    w2.join();
        std::thread w3(WkSetRaiser);    w3.join();
        std::thread w4(WkSetTable);     w4.join();
        // Schedule the deferred check (runs on a later main-thread tick, after the
        // queued behavior commands drain). If the task interface is unavailable, the
        // four queued rows go unreported (visible as an X/Y suite gap) — but the
        // pump is the very thing under test, so this path is the failure signal.
        if (g_task) {
            g_task->AddTask(new DeferredQueuedCheck());
        }
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

    // The task interface — used to schedule the deferred queued-Set check on a
    // later main-thread tick (after the queued behavior commands drain).
    g_task = static_cast<const kcdxTaskInterface*>(
        api->QueryInterface(kcdxInterface_Task, kcdxTaskInterface_Version));

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
