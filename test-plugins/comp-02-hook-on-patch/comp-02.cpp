// COMP-02 — kcdx.bytes patch + kcdx.hook detour overlapping at ONE site.
//
// Phase 4b Batch 3 migration off the legacy [[patch]] + [[hook]] surface.
// ONE plugin installs BOTH, on the SAME function-entry VA:
//   - a kcdx.bytes IDENTITY patch (48 -> 48, a genuine no-op) via
//     kcdxBytesInterface::Register — routes through the PATCH engine
//     (g_patches / conflict_engine);
//   - a kcdx.hook REPLACE detour (callback returns false, the migration of
//     the legacy `31 C0 C3` xor-eax,ret) via kcdxHookInterface::Replace —
//     routes through the SEPARATE hook_chain engine.
//
// Both target "IsInCombat_callsite_26b" (Address Library id 1006, pattern-hit
// RVA 0x5605BC) with offset=-4. The seed prose for id 1006 states: "RVA
// stored is the pattern-hit position; function entry is at RVA-4 — consumers
// using this as a function-entry anchor apply offset = -4, as in
// comp-02-hook-on-patch." So name + offset=-4 -> the function entry; BOTH
// surfaces land on the IDENTICAL VA = genuinely the SAME site.
//
// Signature for the hook: "bool (ptr self)". The AOB `48 8B 41 08`
// (mov rax,[rcx+8]) reads through rcx as a this/object pointer -> 1 ptr arg;
// the trailing `3C 02` (cmp al,2) tests the result as a byte -> bool return.
// (comp-03 uses the same sig for the sister 1007 site.)
//
// === THE PROBE: a cross-engine coexist question, UNVERIFIED ===============
//
// Does a kcdx.bytes patch (patch engine) + a kcdx.hook detour (hook_chain)
// on ONE site BOTH apply (coexist) in the NEW engines — the way the legacy
// [[patch]] + [[hook]] did (conflict_engine logged "Both apply, no action
// needed"; MinHook relocated the patched prologue into its trampoline)? Or
// does one engine reject/clobber the other? UNKNOWN. The verifier's
// GetConflictReport assertion at the function-entry VA IS the readout.
//
// Why the function-entry VA catches BOTH sources (the query-VA derivation):
//   - The bytes patch is reported if queryVA is in
//     [appliedPatchAddr, appliedPatchAddr + replacement.size()).
//     appliedPatchAddr = resolved-base + offset(-4) = the function entry;
//     replacement "48" is 1 byte, so the patch's range is [entry, entry+1) =
//     exactly the function-entry VA.
//   - The hook (hook_chain) is reported if queryVA == the chain's targetVa =
//     resolved hook target = resolved-base + offset(-4) = the function entry.
//   - So BOTH are keyed on the function-entry VA. ResolveAddressByName(
//     "IsInCombat_callsite_26b") returns the id-1006 BASE (the pattern-hit
//     RVA 0x5605BC mapped to a runtime VA); we subtract 4 ourselves to get
//     the entry. (Contrast comp-03-B, which queries
//     ResolveAddressByName("IsInCombat_callsite_with_stack_frame") DIRECTLY
//     because for id 1007 the pattern-hit IS the entry — id 1007's seed has
//     no -4 note; id 1006's does.)
//
// OUTCOME MAP (mirrored in the verifier reason strings + the README):
//   PASS  comp02_patch kind=Patch applied + comp02_hook kind=Hook applied,
//         both present -> cross-engine coexist HOLDS in the new engines
//         (patch engine + hook_chain coexist on one site, like the legacy).
//         Migration done, COMP-02 preserved.
//   FAIL  one of ours present-but-applied=0 -> coexist does NOT hold: the
//         new hook_chain may not relocate/survive a kcdx.bytes patch the way
//         legacy MinHook did, OR the two engines don't see each other's
//         overlap. A REAL FINDING (engine gap or intentional reframe) — the
//         verifier surfaces WHICH one didn't apply, NOT papered over.
//   FAIL  GetConflictReport returns < both (only one entry) -> the report
//         doesn't catch both at the query VA — a GetConflictReport
//         VA-matching issue, DISTINCT from the coexist question. The reason
//         distinguishes "entry absent" from "entry present but applied=0".
//
// Test mode: boot-only. Both installs run at kcdxPlugin_Load; the assertion
// runs at kcdxPlugin_PostGameLoad (after ApplyZone — both engines' apply
// passes done; the cap-39 / comp-03-B pattern). An InputLoaded backstop
// reports a loud FAIL on the row if PostGameLoad never fired. NO console
// gestures, NO save/load, NO in-game action.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — must match [plugin].author + [plugin].name in kcdx.toml.
const char* kAuthor = "ts";
const char* kName   = "comp_02_hook_on_patch";
const char* kRow    = "COMP-02";

// The shared site: id 1006 pattern-hit RVA, offset -4 reaches the function
// entry (the seed prose for 1006 prescribes -4 for function-entry anchoring).
const char* kTarget      = "IsInCombat_callsite_26b";
const int   kEntryOffset = -4;

// The two entry names the conflict report must show (both applied).
const char* kPatchName = "comp02_patch";
const char* kHookName  = "comp02_hook";

// The empowered wrapper — K.bytes + K.hook + K.api + K.self + K.log +
// K.messaging in one Init (Kcdx.h exposes K.hook as the raw floor-4
// kcdxHookInterface, so the wrapper covers BOTH surfaces here).
Kcdx            K;
kcdxBytesHandle g_patch_h = 0;
kcdxHookHandle  g_hook_h  = 0;

// One-shot guard: the InputLoaded backstop fires only if PostGameLoad never
// reported (the cap-39 / comp-03-B design).
bool g_post_ran = false;

void Report(bool pass, const char* reason) {
    if (pass) K.log.Info ("COMP02", "PASS %s: %s", kRow, reason);
    else      K.log.Error("COMP02", "FAIL %s: %s", kRow, reason);
    K.api->ReportTestResult(K.self, kRow, pass ? 1 : 0, reason);
}

// === The replace callback ============================================
//
// REPLACE shape: <typed_return> cFn(/* typed args... */). Signature is
// `bool (ptr self)` (see header). Returns false (0) — the migration of the
// legacy `31 C0 C3` (xor eax,eax; ret). IsInCombat is a combat-state
// predicate; returning false at boot is harmless (player not in combat in
// the title flow). Must exist with the right shape so the install is
// well-formed.
extern "C" bool Comp02_Replace_Cb(void* self) {
    (void)self;
    return false;
}

// Install the identity bytes patch (no-op 48 -> 48) at the function entry.
bool InstallPatch() {
    kcdxBytesOptions opts = {};
    opts.owningPlugin = K.self;
    opts.name         = kPatchName;
    opts.target       = kTarget;        // id 1006 base (pattern-hit RVA)
    opts.offset       = kEntryOffset;   // -4 -> the function entry
    opts.original     = "48";           // verify byte (same len as replacement)
    opts.replacement  = "48";           // genuine identity no-op (PRESERVED)
    opts.idempotent   = true;
    g_patch_h = K.bytes->Register(&opts);
    K.log.Info("COMP02",
               "installed identity bytes patch '%s' on '%s' offset %d "
               "(h=%llu); the patch-engine half of the coexist probe",
               kPatchName, kTarget, kEntryOffset,
               (unsigned long long)g_patch_h);
    return g_patch_h != 0;
}

// Install the replace detour at the SAME function entry. opts.offset=-4 so
// the named target reaches the function entry (the legacy [[hook]] used
// offset=-4); the field doc says offset is "applied after resolution", so
// Replace applies it to the resolved named target. opts.signature carries
// the ABI (the seed row has no signature for 1006).
bool InstallHook() {
    kcdxHookOptions opts = {};
    opts.owningPlugin = K.self;
    opts.name         = kHookName;
    opts.signature    = "bool (ptr self)";
    opts.offset       = kEntryOffset;   // -4 -> the function entry (same VA)
    g_hook_h = K.hook->Replace(kTarget, (void*)&Comp02_Replace_Cb, &opts);
    K.log.Info("COMP02",
               "installed replace detour '%s' on '%s' offset %d (h=%llu); "
               "the hook_chain half of the coexist probe",
               kHookName, kTarget, kEntryOffset,
               (unsigned long long)g_hook_h);
    return g_hook_h != 0;
}

// === InputLoaded backstop — loud FAIL if PostGameLoad never fired =========

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;  // PostGameLoad already reported the row.
    Report(false,
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_InputLoaded "
        "— the after-phase C++ export was not dispatched; the row reported "
        "FAIL via the InputLoaded backstop");
}

// === The coexist assertion (PostGameLoad — after both apply passes) =======

void RunAssertion() {
    // Resolve the id-1006 BASE (pattern-hit RVA mapped to a runtime VA), then
    // subtract 4 to get the function-entry VA where BOTH the patch and the
    // hook landed (see the query-VA derivation in the header). For id 1006
    // the pattern-hit is NOT the entry (unlike id 1007 in comp-03-B), so the
    // -4 is mandatory.
    uintptr_t base = K.api->ResolveAddressByName(kTarget);
    if (base == 0) {
        Report(false,
            "ResolveAddressByName('IsInCombat_callsite_26b') returned 0 — the "
            "engine-seed name did not resolve on this build, so the conflict "
            "report cannot be queried at the function-entry VA");
        return;
    }
    uintptr_t queryVA = base + (uintptr_t)(intptr_t)kEntryOffset;

    kcdxConflictEntry entries[8] = {};
    uint32_t count = K.api->GetConflictReport(
        queryVA, entries, sizeof(entries) / sizeof(entries[0]));

    // Build the human-readable rundown of EVERY returned entry up front so
    // every FAIL reason is legible (the probe readout).
    std::string rundown;
    bool patchPresent = false, patchApplied = false;
    bool hookPresent  = false, hookApplied  = false;
    for (uint32_t i = 0; i < count; ++i) {
        const kcdxConflictEntry& e = entries[i];
        if (!rundown.empty()) rundown += ", ";
        rundown += e.name ? e.name : "<null>";
        rundown += "(";
        rundown += (e.kind == kcdxConflictEntryKind_Patch) ? "Patch" : "Hook";
        rundown += "=";
        rundown += e.applied ? "applied" : "ABORTED";
        rundown += ")";
        if (e.name && std::strcmp(e.name, kPatchName) == 0 &&
            e.kind == kcdxConflictEntryKind_Patch) {
            patchPresent = true;
            if (e.applied) patchApplied = true;
        }
        if (e.name && std::strcmp(e.name, kHookName) == 0 &&
            e.kind == kcdxConflictEntryKind_Hook) {
            hookPresent = true;
            if (e.applied) hookApplied = true;
        }
    }

    // id 1006 / IsInCombat_callsite_26b is comp-02-EXCLUSIVE in the suite (no
    // other plugin targets it — verified by grep), so an exactly-2 report is
    // the strong, expected shape. We still classify by NAME + kind so a FAIL
    // names precisely which source is missing or not-applied (robust against
    // an unexpected co-located entry rather than blindly trusting the count).
    const bool pass = patchPresent && patchApplied &&
                      hookPresent  && hookApplied  && count == 2;

    char r[512];
    if (pass) {
        snprintf(r, sizeof(r),
            "cross-engine coexist HOLDS at function-entry 0x%p (id-1006 base "
            "0x%p offset %d): GetConflictReport=[%s] — comp02_patch (kind=Patch, "
            "applied) + comp02_hook (kind=Hook, applied) BOTH present. The "
            "kcdx.bytes patch engine + the kcdx.hook hook_chain coexist on one "
            "site, like the legacy [[patch]]+[[hook]]. Migration done",
            (void*)queryVA, (void*)base, kEntryOffset, rundown.c_str());
        Report(true, r);
        return;
    }

    // Disambiguate the FAIL per the outcome map: absent vs present-but-aborted
    // vs report-blindness.
    const char* diag;
    if (!patchPresent && !hookPresent) {
        diag = "REPORT-BLIND: GetConflictReport returned NEITHER of ours at the "
               "function-entry VA — the patch's [begin,end) range and the "
               "hook's exact-VA match may not both contain the query VA (a "
               "GetConflictReport VA-matching issue, DISTINCT from the coexist "
               "question)";
    } else if (!patchPresent || !hookPresent) {
        diag = "ONE SOURCE ABSENT from the report at the function-entry VA "
               "(present-vs-absent, NOT applied=0) — a GetConflictReport "
               "VA-matching issue for the missing source, DISTINCT from the "
               "coexist question";
    } else {
        diag = "COEXIST DOES NOT HOLD: both ours are PRESENT but one shows "
               "applied=0 — the new hook_chain may not relocate/survive a "
               "kcdx.bytes patch the way legacy MinHook did, OR the two engines "
               "don't see each other's overlap. A REAL FINDING (engine gap or "
               "intentional reframe)";
    }
    snprintf(r, sizeof(r),
        "%s. At function-entry 0x%p (id-1006 base 0x%p offset %d): "
        "GetConflictReport returned %u entr%s [%s]. Expected exactly 2: "
        "comp02_patch(kind=Patch,applied) + comp02_hook(kind=Hook,applied). "
        "patchPresent=%d patchApplied=%d hookPresent=%d hookApplied=%d "
        "(install handles: patch=%llu hook=%llu)",
        diag, (void*)queryVA, (void*)base, kEntryOffset,
        count, count == 1 ? "y" : "ies", rundown.c_str(),
        patchPresent ? 1 : 0, patchApplied ? 1 : 0,
        hookPresent ? 1 : 0, hookApplied ? 1 : 0,
        (unsigned long long)g_patch_h, (unsigned long long)g_hook_h);
    Report(false, r);
}

}  // namespace

// === kcdxPlugin_Load ======================================================
//
// Install BOTH the bytes patch and the replace detour now (Load wave). The
// apply passes (patch engine + hook_chain) run after Load returns;
// PostGameLoad observes the final coexist state via GetConflictReport.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            api->ReportTestResult(self, kRow, 0,
                "Kcdx::Init returned false at Plugin_Load (engine version "
                "mismatch? — the wrapper requires the Hook interface)");
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    // BOTH surfaces are required for this probe — a null on either is a hard
    // FAIL (distinct from a valid-but-not-applied install).
    if (!K.bytes) {
        Report(false,
            "K.bytes is null: QueryInterface(kcdxInterface_Bytes) returned "
            "null at Plugin_Load — cannot install the patch half of the "
            "coexist probe (engine version mismatch?)");
        return true;
    }
    if (!K.hook) {
        Report(false,
            "K.hook is null: QueryInterface(kcdxInterface_Hook) returned null "
            "at Plugin_Load — cannot install the detour half of the coexist "
            "probe (engine version mismatch?)");
        return true;
    }

    // Messaging is best-effort — only the InputLoaded backstop needs it.
    if (K.messaging) {
        K.messaging->RegisterListener(K.self, /*sender=*/nullptr, OnMessage);
    } else {
        K.log.Warn("INIT",
            "QueryInterface(Messaging) returned null — InputLoaded backstop "
            "disabled (if PostGameLoad never fires the row sits "
            "silent-PENDING rather than loud-FAIL)");
    }

    // Install both halves. If EITHER install CALL returns a 0 handle (a
    // REGISTRATION error — distinct from a valid-but-Failed handle whose
    // applied=0 the conflict report would show), report COMP-02 FAIL now with
    // the teaching reason; the probe cannot run without both registered.
    const bool patchOk = InstallPatch();
    const bool hookOk  = InstallHook();
    if (!patchOk || !hookOk) {
        char r[400];
        snprintf(r, sizeof(r),
            "an install CALL returned a 0 handle (a REGISTRATION error, NOT "
            "the conflict-report's applied=0): patch handle=%llu (%s), hook "
            "handle=%llu (%s). The coexist probe needs BOTH registered — see "
            "the COMP02 engine log for the teaching error",
            (unsigned long long)g_patch_h, patchOk ? "ok" : "ZERO",
            (unsigned long long)g_hook_h, hookOk ? "ok" : "ZERO");
        Report(false, r);
        // Mark reported so the backstop / PostGameLoad don't double-report.
        g_post_ran = true;
        return true;
    }
    return true;
}

// === kcdxPlugin_PostGameLoad ==============================================
//
// Fires AFTER ApplyZone(AfterGame) — both the patch engine and the hook_chain
// apply passes are done — and BEFORE FireEngineMessage(InputLoaded). Query
// the conflict report at the shared function-entry VA.

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // K.api was cached in Load; same pointer.
    if (g_post_ran) return true;  // a Load-time registration FAIL already ran.
    g_post_ran = true;
    K.log.Info("COMP02",
               "kcdxPlugin_PostGameLoad — apply passes done; querying "
               "GetConflictReport at the shared function-entry VA");
    RunAssertion();
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
