// CAP-96 — C++ peer of the static-bytes statement surface.
//
// Exercises kcdxStatementInterface (ReplaceWith with a STATIC kcdxOp value;
// InsertBefore's register-and-defer contract; the kcdxFunctionRef-as-target
// opts affordance) through the Kcdx.h wrapper's K.statement accessor — the
// C++ parity mirror of the Lua kcdx.statement.* surface (cap-92). Proves ONE
// ENGINE PATH, both languages: a C++ registration lands as the same
// deferred-apply statement entry the Lua verbs queue, kind-checked and
// emitted by the same apply handler.
//
// Four falsifiable rows (the C++ mirror of cap-92's structural rows):
//
//   1. CAP-96-replace-registers
//      K.statement->ReplaceWith("SaveGame", &noopOp, &opts) (a
//      kcdxOp{kcdxOp_ReplaceWithNoop}; opts.module="WHGame.dll") returns a
//      NON-ZERO handle — a static op is accepted as the registration's
//      required input (no callback path). Pending / applied / deploy-state-
//      degraded at InputLoaded are ALL honest PASSes (IsApplied false at this
//      point is an expected deferred-apply state, not a failure — the same
//      posture cap-92's registers row takes). FALSIFIABLE: handle == 0 (the
//      static op was not accepted / the curated target did not resolve at
//      registration) -> FAIL.
//
//   2. CAP-96-kind-mismatch
//      ReplaceWith("SaveGame", &branchOp, &opts) with
//      kcdxOp{kcdxOp_AlwaysTakeBranch} (requires a "branch" statement) and
//      the default function-entry locator (SaveGame's first statement is not
//      a branch) registers (non-zero handle — the kind check fires at APPLY,
//      not registration) and does NOT silently apply: IsApplied(h) == false
//      at InputLoaded (pending or rejected — both honest). FALSIFIABLE: a
//      branch op on a non-branch statement reports IsApplied == true (a
//      silent wrong-kind apply) -> FAIL; handle == 0 (a valid static op
//      rejected at registration) -> FAIL.
//
//   3. CAP-96-insert-defers
//      K.statement->InsertBefore("SaveGame", &firstReturnLoc,
//      (void*)&DummyInsertCb, &opts) returns a NON-ZERO handle AND
//      IsApplied(h) == false AND GetReason(h) is non-null/non-empty — the
//      register-and-DEFER contract: the statement-locator capture-thunk apply
//      path is unwired (on BOTH surfaces) and the deferral is LOUD.
//      FALSIFIABLE: handle == 0 (registration broken) -> FAIL; IsApplied ==
//      true (a deferred insert silently "applied" — the over-claim) -> FAIL;
//      GetReason null/empty (a silent deferral) -> FAIL.
//
//   4. CAP-96-targetref
//      ref = K.functions->GameByName("WHGame", "SaveGame"); opts.targetRef =
//      &ref with a null positional target; ReplaceWith(nullptr, &noopOp,
//      &opts) returns a NON-ZERO handle — the resolve-once-pass-to-N-verbs
//      reference affordance works as a statement target. DEGRADED PASS when
//      the reference DB is not loaded (the mint reports reason db_not_loaded
//      — a pre-deploy state; the assert is skipped and reported as degraded).
//      FALSIFIABLE: a found reference passed as opts.targetRef is rejected at
//      registration (handle == 0) -> FAIL; the mint misses with
//      name_unknown (a real rename/renumber regression, NOT degraded) -> FAIL.
//
// SaveGame is referenced BY NAME (no hardcoded address) — the same curated
// fixture cap-92 uses. Rows 1-3 REGISTER at kcdxPlugin_Load (so the engine's
// apply pass has processed the entries before the readiness gate) and REPORT
// from the kcdxMessage_InputLoaded gate; row 4 registers at the gate (the
// reference mint needs the reference DB, open by then — the same gate cap-94
// uses). All test_suite_only — production users never see this plugin.
// snprintf for every report string (a bounded format, never wsprintfA).

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
constexpr const char* kName   = "cap_96_cpp_statement";

constexpr const char* kRowReplaceRegisters = "CAP-96-replace-registers";
constexpr const char* kRowKindMismatch     = "CAP-96-kind-mismatch";
constexpr const char* kRowInsertDefers     = "CAP-96-insert-defers";
constexpr const char* kRowTargetRef        = "CAP-96-targetref";

// The curated game-function fixture (resolved BY NAME, never an address).
constexpr const char* kGameStem   = "WHGame";
constexpr const char* kTargetName = "SaveGame";
constexpr const char* kModule     = "WHGame.dll";

Kcdx              g_K;
std::atomic<bool> g_reported{false};

// Handles minted at kcdxPlugin_Load (rows 1-3 register early so the apply
// pass has processed the entries by the InputLoaded report point).
kcdxStatementHandle g_replaceHandle  = 0;
kcdxStatementHandle g_mismatchHandle = 0;
kcdxStatementHandle g_insertHandle   = 0;
bool                g_registered     = false;

// The insert callback — registered but NEVER fired today (the insert apply
// path is deferred; the engine does not store this pointer yet). It exists so
// the registration call carries a real non-null function pointer.
void DummyInsertCb() {}

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_K.log.Info ("CAP96", "PASS %s: %s", row, reason);
    else      g_K.log.Error("CAP96", "FAIL %s: %s", row, reason);
    g_K.api->ReportTestResult(g_K.self, row, pass ? 1 : 0, reason);
}

// FAIL all four rows with one reason — used when the statement surface is
// unavailable (a version mismatch), so no row sits silent-PENDING.
void FailAll(const char* reason) {
    Report(kRowReplaceRegisters, false, reason);
    Report(kRowKindMismatch,     false, reason);
    Report(kRowInsertDefers,     false, reason);
    Report(kRowTargetRef,        false, reason);
}

// Shared options for the SaveGame registrations: module + owner attribution.
kcdxStatementOptions BaseOpts(const char* name) {
    kcdxStatementOptions opts = {};
    opts.module       = kModule;
    opts.owningPlugin = g_K.self;
    opts.name         = name;
    return opts;
}

// === Registrations (run at kcdxPlugin_Load) ==============================

void RegisterRows() {
    // Row 1 — a static no-op op (applies to ANY statement kind) at the
    // default function-entry locator.
    {
        kcdxOp noopOp = {};
        noopOp.kind = kcdxOp_ReplaceWithNoop;
        kcdxStatementOptions opts = BaseOpts("cap96_replace_registers");
        g_replaceHandle = g_K.statement->ReplaceWith(kTargetName, &noopOp, &opts);
    }

    // Row 2 — a branch-requiring op against the default function-entry
    // locator (which resolves a non-branch statement on SaveGame): registers,
    // then the APPLY-time kind check must reject it — never a silent apply.
    {
        kcdxOp branchOp = {};
        branchOp.kind = kcdxOp_AlwaysTakeBranch;
        kcdxStatementOptions opts = BaseOpts("cap96_kind_mismatch");
        g_mismatchHandle = g_K.statement->ReplaceWith(kTargetName, &branchOp, &opts);
    }

    // Row 3 — a callback insert at a REQUIRED statement locator (first
    // return). Registers; the apply pass fails it LOUD with the not-yet-wired
    // teaching reason (the register-and-defer contract).
    {
        kcdxLocator firstReturn = {};
        firstReturn.kind = kcdxLocator_FirstReturn;
        kcdxStatementOptions opts = BaseOpts("cap96_insert_defers");
        g_insertHandle = g_K.statement->InsertBefore(
            kTargetName, &firstReturn,
            reinterpret_cast<void*>(&DummyInsertCb), &opts);
    }

    g_registered = true;
}

// === Row reports (run at the InputLoaded readiness gate) =================

void ReportReplaceRegistersRow() {
    char reason[600];
    if (g_replaceHandle == 0) {
        snprintf(reason, sizeof(reason),
            "K.statement->ReplaceWith(\"%s\", { kcdxOp_ReplaceWithNoop }, "
            "opts{module=\"%s\"}) returned handle 0 — a STATIC op on a curated "
            "target must be accepted at registration (the teaching reason is "
            "in the dev log, category STATEMENT_INTERFACE)",
            kTargetName, kModule);
        Report(kRowReplaceRegisters, false, reason);
        return;
    }
    const bool applied = g_K.statement->IsApplied(g_replaceHandle);
    const char* why    = g_K.statement->GetReason(g_replaceHandle);
    snprintf(reason, sizeof(reason),
        "K.statement->ReplaceWith(\"%s\", { kcdxOp_ReplaceWithNoop }) -> "
        "handle=%llu (non-zero; a static op accepted, no callback path, "
        "queued as a deferred-apply statement entry). State at InputLoaded: "
        "applied=%s%s%s — pending/applied/deploy-state-degraded are all "
        "honest outcomes here (the C++ peer of the Lua registers row)",
        kTargetName,
        static_cast<unsigned long long>(g_replaceHandle),
        applied ? "true" : "false",
        why ? ", reason=" : "",
        why ? why : "");
    Report(kRowReplaceRegisters, true, reason);
}

void ReportKindMismatchRow() {
    char reason[600];
    if (g_mismatchHandle == 0) {
        snprintf(reason, sizeof(reason),
            "K.statement->ReplaceWith(\"%s\", { kcdxOp_AlwaysTakeBranch }) "
            "returned handle 0 — a valid static op must REGISTER (the kind "
            "check fires at apply, not registration)",
            kTargetName);
        Report(kRowKindMismatch, false, reason);
        return;
    }
    if (g_K.statement->IsApplied(g_mismatchHandle)) {
        snprintf(reason, sizeof(reason),
            "K.statement->ReplaceWith(\"%s\", { kcdxOp_AlwaysTakeBranch }) "
            "reports IsApplied=true — a branch-requiring op applied to a "
            "NON-branch statement (the function-entry default), a SILENT "
            "wrong-kind apply the kind check must reject loud",
            kTargetName);
        Report(kRowKindMismatch, false, reason);
        return;
    }
    const char* why = g_K.statement->GetReason(g_mismatchHandle);
    snprintf(reason, sizeof(reason),
        "K.statement->ReplaceWith(\"%s\", { kcdxOp_AlwaysTakeBranch }) -> "
        "handle=%llu, IsApplied=false at InputLoaded%s%s — a branch op on a "
        "non-branch statement did NOT silently apply (pending or rejected, "
        "both honest; the C++ peer of the Lua kind-mismatch row)",
        kTargetName,
        static_cast<unsigned long long>(g_mismatchHandle),
        why ? ", reason=" : "",
        why ? why : "");
    Report(kRowKindMismatch, true, reason);
}

void ReportInsertDefersRow() {
    char reason[700];
    if (g_insertHandle == 0) {
        snprintf(reason, sizeof(reason),
            "K.statement->InsertBefore(\"%s\", first_return locator, cb) "
            "returned handle 0 — a well-formed insert must REGISTER (the "
            "register half of the register-and-defer contract is broken)",
            kTargetName);
        Report(kRowInsertDefers, false, reason);
        return;
    }
    if (g_K.statement->IsApplied(g_insertHandle)) {
        snprintf(reason, sizeof(reason),
            "K.statement->InsertBefore(\"%s\", ...) reports IsApplied=true — "
            "the insert apply path is UNWIRED, so a deferred insert claiming "
            "to be applied is an over-claim (the callback can never have been "
            "installed)",
            kTargetName);
        Report(kRowInsertDefers, false, reason);
        return;
    }
    const char* why = g_K.statement->GetReason(g_insertHandle);
    if (!why || !why[0]) {
        snprintf(reason, sizeof(reason),
            "K.statement->InsertBefore(\"%s\", ...) -> handle=%llu, "
            "IsApplied=false, but GetReason is null/empty — a SILENT deferral. "
            "The register-and-defer contract requires the not-yet-wired "
            "teaching reason to be readable off the handle",
            kTargetName, static_cast<unsigned long long>(g_insertHandle));
        Report(kRowInsertDefers, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.statement->InsertBefore(\"%s\", first_return locator, cb) -> "
        "handle=%llu, IsApplied=false, GetReason=\"%.220s\" — the insert "
        "registered and deferred LOUD (the apply path is unwired on both "
        "surfaces; an honest deferral, never a faked install)",
        kTargetName, static_cast<unsigned long long>(g_insertHandle), why);
    Report(kRowInsertDefers, true, reason);
}

void RunTargetRefRow() {
    char reason[700];
    const kcdxFunctionRef ref = g_K.functions->GameByName(kGameStem, kTargetName);

    if (!ref.found) {
        const char* tok = ref.reason ? ref.reason : "";
        if (std::strcmp(tok, "db_not_loaded") == 0) {
            snprintf(reason, sizeof(reason),
                "DEGRADED PASS — the reference DB is not loaded "
                "(GameByName(\"%s\", \"%s\") reason=db_not_loaded, a "
                "pre-deploy state), so the ref-as-target registration assert "
                "is skipped. The targetRef plumbing is exercised at the next "
                "deployed-DB launch",
                kGameStem, kTargetName);
            Report(kRowTargetRef, true, reason);
            return;
        }
        snprintf(reason, sizeof(reason),
            "K.functions->GameByName(\"%s\", \"%s\") reported found=false "
            "(reason \"%s\") — a known curated game function must resolve by "
            "name (a name_unknown is a real regression, NOT a degraded "
            "deploy-state miss), so the ref-as-target row cannot run honestly",
            kGameStem, kTargetName, tok);
        Report(kRowTargetRef, false, reason);
        return;
    }

    kcdxOp noopOp = {};
    noopOp.kind = kcdxOp_ReplaceWithNoop;
    kcdxStatementOptions opts = BaseOpts("cap96_targetref");
    opts.targetRef = &ref;  // the reference WINS; the positional target is null.
    const kcdxStatementHandle h =
        g_K.statement->ReplaceWith(nullptr, &noopOp, &opts);

    if (h == 0) {
        snprintf(reason, sizeof(reason),
            "ReplaceWith(nullptr, { kcdxOp_ReplaceWithNoop }, "
            "opts{targetRef=GameByName(\"%s\",\"%s\") [found=true]}) returned "
            "handle 0 — a FOUND reference passed as opts.targetRef must be "
            "accepted as the target (the resolve-once-pass-to-N-verbs parity "
            "affordance is broken)",
            kGameStem, kTargetName);
        Report(kRowTargetRef, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "ReplaceWith with opts.targetRef = GameByName(\"%s\", \"%s\") (null "
        "positional target) -> handle=%llu — a kcdxFunctionRef resolved once "
        "is accepted as a statement target (the ref collapses to its carried "
        "name for resolution; the C++ peer of passing a kcdx.functions.* "
        "value to the Lua verb)",
        kGameStem, kTargetName, static_cast<unsigned long long>(h));
    Report(kRowTargetRef, true, reason);
}

void RunAllReports() {
    if (g_reported.exchange(true)) return;  // run once.
    if (!g_registered) {
        FailAll("the Load-time registrations never ran (kcdxPlugin_Load did "
                "not reach RegisterRows)");
        return;
    }
    ReportReplaceRegistersRow();
    ReportKindMismatchRow();
    ReportInsertDefersRow();
    RunTargetRefRow();
}

// InputLoaded readiness gate — the apply pass has processed the Load-time
// registrations by here, and the reference DB is open (the same gate cap-94's
// reference mints use).
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
                kRowReplaceRegisters, kRowKindMismatch, kRowInsertDefers,
                kRowTargetRef,
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

    if (!g_K.statement || !g_K.functions) {
        FailAll("K.statement and/or K.functions is null: "
                "QueryInterface(kcdxInterface_Statement / _Functions) returned "
                "null at Plugin_Load (engine version mismatch — rebuild "
                "against the engine that ships these interfaces)");
        return true;
    }

    // Rows 1-3 register NOW (so the engine's apply pass processes the entries
    // before the InputLoaded report point); row 4 registers at the gate (the
    // reference mint needs the reference DB, open by then).
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
