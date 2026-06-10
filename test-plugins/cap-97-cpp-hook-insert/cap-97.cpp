// CAP-97 — C++ peer of the hook-insert sub-verbs + the targetRef hook option.
//
// Exercises the kcdxHookInterface v3 additions (InsertBefore/InsertAfter —
// the C++ parity mirror of the Lua kcdx.hook.insert_before / insert_after
// sub-verbs; the kcdxHookOptions.targetRef / .statementLocator appended
// fields) through the Kcdx.h wrapper's K.hook accessor. Proves ONE ENGINE
// PATH, both languages: a C++ insert registration lands as the same
// insert-pending deferred-apply hook entry the Lua insert verbs queue, failed
// LOUD by the same apply handler (the statement capture-thunk apply path is
// unwired on BOTH surfaces — the register-and-DEFER contract, never a faked
// install).
//
// Three falsifiable rows:
//
//   1. CAP-97-insert-defers
//      K.hook->InsertBefore("SaveGame", cb, opts{module="WHGame.dll",
//      statementLocator=&firstReturn}) returns a NON-ZERO handle AND
//      IsApplied(h) == false AND GetReason(h) is non-null/non-empty — the
//      register-and-DEFER contract. FALSIFIABLE: handle == 0 (registration
//      broken) -> FAIL; IsApplied == true (a deferred insert silently
//      "applied" — the over-claim) -> FAIL; GetReason null/empty (a silent
//      deferral) -> FAIL.
//
//   2. CAP-97-insert-requires-locator
//      InsertBefore("SaveGame", cb, opts) with NO statementLocator returns
//      handle 0 — a loud registration reject ("insert before what?" has no
//      default; the same required-locator teaching error the Lua insert
//      raises). FALSIFIABLE: a locator-less insert registers (non-zero
//      handle — the required argument was silently defaulted) -> FAIL.
//
//   3. CAP-97-targetref-hook
//      ref = K.functions->GameByName("WHGame", "SaveGame"); opts.targetRef =
//      &ref with a NULL positional target; a plain K.hook->Before(nullptr,
//      cb, &opts) returns a NON-ZERO handle — the ref-as-target affordance
//      works on the EXISTING verbs too, not just the inserts (resolve once,
//      pass to N verbs; the ref collapses to its carried name). DEGRADED
//      PASS when the reference DB is not loaded (the mint reports reason
//      db_not_loaded — a pre-deploy state; the assert is skipped and
//      reported as degraded). FALSIFIABLE: a found ref as targetRef is
//      rejected at registration (handle == 0) -> FAIL; the mint misses with
//      name_unknown (a real rename/renumber regression, NOT degraded) ->
//      FAIL.
//
// SaveGame is referenced BY NAME (no hardcoded address) — the same curated
// fixture cap-96 uses. Rows 1-2 REGISTER at kcdxPlugin_Load (so the engine's
// apply pass has processed the insert entry before the readiness gate) and
// REPORT from the kcdxMessage_InputLoaded gate; row 3 registers at the gate
// (the reference mint needs the reference DB, open by then — the same gate
// cap-96's targetRef row uses). All test_suite_only — production users never
// see this plugin. snprintf for every report string (a bounded format, never
// wsprintfA).

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — MUST match [plugin].author + [plugin].name in kcdx.toml.
constexpr const char* kAuthor = "ts";
constexpr const char* kName   = "cap_97_cpp_hook_insert";

constexpr const char* kRowInsertDefers   = "CAP-97-insert-defers";
constexpr const char* kRowRequiresLoc    = "CAP-97-insert-requires-locator";
constexpr const char* kRowTargetRefHook  = "CAP-97-targetref-hook";

// The curated game-function fixture (resolved BY NAME, never an address).
constexpr const char* kGameStem   = "WHGame";
constexpr const char* kTargetName = "SaveGame";
constexpr const char* kModule     = "WHGame.dll";

Kcdx              g_K;
std::atomic<bool> g_reported{false};

// Handles minted at kcdxPlugin_Load (rows 1-2 register early so the apply
// pass has processed the insert entry by the InputLoaded report point).
kcdxHookHandle g_insertHandle     = 0;
kcdxHookHandle g_noLocatorHandle  = 0;  // EXPECTED zero (the reject row).
bool           g_registered       = false;

// The insert callback — registered but NEVER fired today (the insert apply
// path is unwired; the apply pass fails the entry loud before any install).
// Before-callback shape (args back-channel + commit count) so the pointer is
// a well-formed callback if the contract ever changes under this test.
void DummyInsertCb(uintptr_t* args, int* outCount) {
    (void)args;
    if (outCount) *outCount = 0;  // leave args unchanged
}

// The targetRef row's Before callback. Registered after the apply pass ran
// (it stays pending), and behavior-preserving even if a later pass applies
// it: *outCount = 0 leaves every argument unchanged and the original runs.
void DummyBeforeCb(uintptr_t* args, int* outCount) {
    (void)args;
    if (outCount) *outCount = 0;
}

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_K.log.Info ("CAP97", "PASS %s: %s", row, reason);
    else      g_K.log.Error("CAP97", "FAIL %s: %s", row, reason);
    g_K.api->ReportTestResult(g_K.self, row, pass ? 1 : 0, reason);
}

// FAIL all three rows with one reason — used when the hook/functions surface
// is unavailable (a version mismatch), so no row sits silent-PENDING.
void FailAll(const char* reason) {
    Report(kRowInsertDefers,  false, reason);
    Report(kRowRequiresLoc,   false, reason);
    Report(kRowTargetRefHook, false, reason);
}

// Shared options for the SaveGame registrations: module + owner attribution.
kcdxHookOptions BaseOpts(const char* name) {
    kcdxHookOptions opts = {};
    opts.module       = kModule;
    opts.owningPlugin = g_K.self;
    opts.name         = name;
    return opts;
}

// === Registrations (run at kcdxPlugin_Load) ==============================

void RegisterRows() {
    // Row 1 — a well-formed insert at a REQUIRED statement locator (first
    // return). Registers; the apply pass fails it LOUD with the
    // not-yet-wired teaching reason (the register-and-defer contract).
    {
        static kcdxLocator firstReturn = {};
        firstReturn.kind = kcdxLocator_FirstReturn;
        kcdxHookOptions opts = BaseOpts("cap97_insert_defers");
        opts.statementLocator = &firstReturn;
        g_insertHandle = g_K.hook->InsertBefore(
            kTargetName, reinterpret_cast<void*>(&DummyInsertCb), &opts);
    }

    // Row 2 — the SAME insert with NO statementLocator: must be rejected
    // LOUD at registration (handle 0). "Insert before what?" has no default.
    {
        kcdxHookOptions opts = BaseOpts("cap97_insert_no_locator");
        // opts.statementLocator deliberately left null.
        g_noLocatorHandle = g_K.hook->InsertBefore(
            kTargetName, reinterpret_cast<void*>(&DummyInsertCb), &opts);
    }

    g_registered = true;
}

// === Row reports (run at the InputLoaded readiness gate) =================

void ReportInsertDefersRow() {
    char reason[700];
    if (g_insertHandle == 0) {
        snprintf(reason, sizeof(reason),
            "K.hook->InsertBefore(\"%s\", cb, opts{module=\"%s\", "
            "statementLocator=first_return}) returned handle 0 — a "
            "well-formed insert must REGISTER (the register half of the "
            "register-and-defer contract is broken; the teaching reason is "
            "in the dev log, category HOOK_INTERFACE)",
            kTargetName, kModule);
        Report(kRowInsertDefers, false, reason);
        return;
    }
    if (g_K.hook->IsApplied(g_insertHandle)) {
        snprintf(reason, sizeof(reason),
            "K.hook->InsertBefore(\"%s\", ...) reports IsApplied=true — the "
            "insert apply path is UNWIRED, so a deferred insert claiming to "
            "be applied is an over-claim (the callback can never have been "
            "installed)",
            kTargetName);
        Report(kRowInsertDefers, false, reason);
        return;
    }
    const char* why = g_K.hook->GetReason(g_insertHandle);
    if (!why || !why[0]) {
        snprintf(reason, sizeof(reason),
            "K.hook->InsertBefore(\"%s\", ...) -> handle=%llu, "
            "IsApplied=false, but GetReason is null/empty — a SILENT "
            "deferral. The register-and-defer contract requires the "
            "not-yet-wired teaching reason to be readable off the handle",
            kTargetName, static_cast<unsigned long long>(g_insertHandle));
        Report(kRowInsertDefers, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.hook->InsertBefore(\"%s\", first_return statementLocator, cb) -> "
        "handle=%llu, IsApplied=false, GetReason=\"%.220s\" — the insert "
        "registered and deferred LOUD (the statement capture-thunk apply "
        "path is unwired on both surfaces; an honest deferral, never a "
        "faked install)",
        kTargetName, static_cast<unsigned long long>(g_insertHandle), why);
    Report(kRowInsertDefers, true, reason);
}

void ReportRequiresLocatorRow() {
    char reason[600];
    if (g_noLocatorHandle != 0) {
        snprintf(reason, sizeof(reason),
            "K.hook->InsertBefore(\"%s\", cb, opts) with NO "
            "opts.statementLocator returned handle=%llu (non-zero) — the "
            "REQUIRED locator was silently defaulted. \"Insert before "
            "what?\" has no default; a locator-less insert must be a loud "
            "zero-handle registration reject",
            kTargetName, static_cast<unsigned long long>(g_noLocatorHandle));
        Report(kRowRequiresLoc, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.hook->InsertBefore(\"%s\", cb, opts) with NO "
        "opts.statementLocator -> handle=0 — the required statement locator "
        "was rejected LOUD at registration (the teaching reason is in the "
        "dev log, category HOOK_INTERFACE), mirroring the Lua insert's "
        "required-locator error",
        kTargetName);
    Report(kRowRequiresLoc, true, reason);
}

void RunTargetRefHookRow() {
    char reason[700];
    const kcdxFunctionRef ref = g_K.functions->GameByName(kGameStem, kTargetName);

    if (!ref.found) {
        const char* tok = ref.reason ? ref.reason : "";
        if (std::strcmp(tok, "db_not_loaded") == 0) {
            snprintf(reason, sizeof(reason),
                "DEGRADED PASS — the reference DB is not loaded "
                "(GameByName(\"%s\", \"%s\") reason=db_not_loaded, a "
                "pre-deploy state), so the ref-as-target hook registration "
                "assert is skipped. The targetRef plumbing is exercised at "
                "the next deployed-DB launch",
                kGameStem, kTargetName);
            Report(kRowTargetRefHook, true, reason);
            return;
        }
        snprintf(reason, sizeof(reason),
            "K.functions->GameByName(\"%s\", \"%s\") reported found=false "
            "(reason \"%s\") — a known curated game function must resolve by "
            "name (a name_unknown is a real regression, NOT a degraded "
            "deploy-state miss), so the ref-as-target row cannot run honestly",
            kGameStem, kTargetName, tok);
        Report(kRowTargetRefHook, false, reason);
        return;
    }

    kcdxHookOptions opts = BaseOpts("cap97_targetref_hook");
    opts.targetRef = &ref;  // the reference WINS; the positional target is null.
    const kcdxHookHandle h = g_K.hook->Before(
        nullptr, reinterpret_cast<void*>(&DummyBeforeCb), &opts);

    if (h == 0) {
        snprintf(reason, sizeof(reason),
            "K.hook->Before(nullptr, cb, opts{targetRef=GameByName(\"%s\","
            "\"%s\") [found=true]}) returned handle 0 — a FOUND reference "
            "passed as opts.targetRef must be accepted as the target on the "
            "EXISTING verbs (the resolve-once-pass-to-N-verbs parity "
            "affordance is broken on the hook surface)",
            kGameStem, kTargetName);
        Report(kRowTargetRefHook, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.hook->Before with opts.targetRef = GameByName(\"%s\", \"%s\") "
        "(null positional target) -> handle=%llu — a kcdxFunctionRef "
        "resolved once is accepted as a hook target on a plain Before (the "
        "ref collapses to its carried name; the affordance covers every "
        "hook verb, not just the inserts)",
        kGameStem, kTargetName, static_cast<unsigned long long>(h));
    Report(kRowTargetRefHook, true, reason);
}

void RunAllReports() {
    if (g_reported.exchange(true)) return;  // run once.
    if (!g_registered) {
        FailAll("the Load-time registrations never ran (kcdxPlugin_Load did "
                "not reach RegisterRows)");
        return;
    }
    ReportInsertDefersRow();
    ReportRequiresLocatorRow();
    RunTargetRefHookRow();
}

// InputLoaded readiness gate — the apply pass has processed the Load-time
// registrations by here, and the reference DB is open (the same gate
// cap-96's targetRef row uses).
void OnMessage(kcdxMessage* msg) {
    if (msg && msg->messageType == kcdxMessage_InputLoaded) {
        RunAllReports();
    }
}

}  // namespace

// === kcdxPlugin_Load ====================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!g_K.Init(api, kAuthor, kName)) {
        // Init logs why. Report every row FAIL so none sit silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            const char* rows[] = {
                kRowInsertDefers, kRowRequiresLoc, kRowTargetRefHook,
            };
            for (const char* row : rows) {
                api->ReportTestResult(self, row, 0,
                    "Kcdx::Init returned false at Plugin_Load (engine version "
                    "mismatch?)");
            }
        }
        return true;
    }
    g_K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
                 api->kcdxVersion);

    if (!g_K.hook || !g_K.functions) {
        FailAll("K.hook and/or K.functions is null: "
                "QueryInterface(kcdxInterface_Hook v3 / _Functions) returned "
                "null at Plugin_Load (engine version mismatch — rebuild "
                "against the engine that ships kcdxHookInterface_Version 3)");
        return true;
    }

    // Rows 1-2 register NOW (so the engine's apply pass processes the insert
    // entry before the InputLoaded report point); row 3 registers at the
    // gate (its reference mint needs the reference DB, open by then).
    RegisterRows();

    if (!g_K.messaging) {
        g_K.log.Warn("INIT",
            "messaging interface null — reporting rows at load (the apply "
            "pass has not run yet, so the deferred states read as pending)");
        RunAllReports();
        return true;
    }
    g_K.messaging->RegisterListener(g_K.self, nullptr, OnMessage);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
