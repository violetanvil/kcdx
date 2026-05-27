// CAP-41 — GetConflictReport now folds in kcdx.bytes (kcdxBytesInterface::
// Register) patches as a FOURTH source.
//
// The regression + doc close of the "GetConflictReport reports
// kcdx.bytes" feature (steps 1-2: the C++ GetConflictReport(target, out, cap)
// now walks lua_registry Kind::Bytes entries and folds them in as a fourth
// conflict source, kind=Patch). A kcdx.bytes patch registered via
// kcdxBytesInterface::Register routes through lua_registry Kind::Bytes (NOT
// the legacy g_patches), so before this feature GetConflictReport walked only
// g_patches + g_hooks + hook_chain and was BLIND to bytes-Register patches —
// querying a kcdx.bytes-patched VA returned no entry for that bytes patch.
// This plugin proves the report now SEES it.
//
// === Test design — the SAME named-target bytes rewrite cap-39 proves =====
//
// This plugin registers ONE kcdx.bytes patch via K.bytes->Register using the
// exact verified-safe rewrite cap-39 (and cap-01) prove, located by NAME (the
// disassembler-test common path — a name, not hex):
//   target      = "outfit_swap_callsite_aob"   (Address Library id 1004)
//   offset      = 13   (the name resolves the AOB start; the rewrite is +13)
//   original    = "44 8A F0"
//   replacement = "45 31 F6"   (mov r14b,al -> xor r14d,r14d)
// The ONLY difference from cap-39 is the DISTINCT entry name "cap41_bytes_
// patch", so the report query can find THIS plugin's entry by name among the
// co-located entries.
//
// === CRITICAL — by-name, NOT by-count (the co-location subtlety) =========
//
// The site outfit_swap_callsite_aob (id 1004) is patched by MULTIPLE suite
// entries with the SAME replacement, all idempotent-coexist:
//   - cap-01   — a Lua/[[patch]] entry (legacy g_patches source)
//   - cap-39   — a kcdxBytesInterface::Register bytes patch (the new fourth
//                source, name "cap39_outfit_swap_rewrite")
//   - cap-41   — THIS plugin's kcdxBytesInterface::Register bytes patch
//                (name "cap41_bytes_patch")
// Two-plus same-replacement writers on one site is conflict_engine
// WriteOnWriteFull — NOT a rejection: every writer applies; the second+
// idempotent-skips (patch_engine.cpp VerifyOriginalAtAddr verdict==0 ->
// ApplyPatch returns true). So GetConflictReport(siteVA) returns MULTIPLE
// kind=Patch entries (cap-01's from g_patches PLUS cap-39's and cap-41's from
// the bytes-Register fold). The COUNT is therefore NOT fixed and will GROW as
// the suite grows — a "exactly N entries" assertion would flake. We assert by
// NAME: scan the returned entries for EXACTLY ONE named "cap41_bytes_patch",
// require kind == kcdxConflictEntryKind_Patch and applied != 0. The FAIL
// reason lists EVERY returned entry (name/kind/applied) so a miss is
// diagnosable.
//
// This co-location is a STRONGER proof than an isolated site: it shows the
// merge folds the bytes-Register source ALONGSIDE the legacy g_patches source
// (cap-01) AND the other bytes-Register entries (cap-39) at the SAME VA, all
// sorted and returned together — the fourth source is merged, not replacing.
//
// === Falsifiability (stated in the reason strings) =======================
//
// Pre-feature (step 2 absent): GetConflictReport(siteVA) walked only
//   g_patches + g_hooks + hook_chain — cap-41's bytes-Register entry was
//   INVISIBLE -> 0 matches for "cap41_bytes_patch" -> FAIL.
// Post-feature: the fourth source folds it in -> exactly 1 match,
//   kind=Patch, applied -> PASS.
// A match with kind != Patch -> the fold used the wrong kind.
// applied == 0 -> the accessor reported it not-applied despite IsApplied
//   true (an accessor range/flag bug).
// A precondition guard (IsApplied(handle)==true && siteVA != 0) runs FIRST so
// a report-blindness FAIL is never conflated with the patch not applying /
// the site not resolving.
//
// Test mode: boot-only. The row self-verifies at kcdxPlugin_PostGameLoad
// (fired AFTER ApplyZone(AfterGame) — the bytes patch is LIVE — and BEFORE
// kcdxMessage_InputLoaded; the cap-39 timing). An InputLoaded backstop
// (cap-39 design) reports a loud FAIL if PostGameLoad never fired, so a
// missing after-phase export never leaves a silent PENDING. NO console
// gestures, NO save/load, NO in-game action.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — must match [plugin].author + [plugin].name in
// kcdx.toml. K.Init derives self from the bare name.
const char* kAuthor = "ts";
const char* kName   = "cap_41_cpp_bytes_conflict_report";

// The single row.
const char* kRow = "CAP-41-bytes-in-conflict-report";

// The DISTINCT entry name the report query searches for (the only difference
// from cap-39's "cap39_outfit_swap_rewrite").
const char* kPatchName = "cap41_bytes_patch";

// The same verified-safe rewrite cap-39 / cap-01 prove, located by NAME (the
// disassembler-test common path). id 1004 = outfit_swap_callsite_aob.
const char* kTarget      = "outfit_swap_callsite_aob";
const char* kOriginal    = "44 8A F0";
const char* kReplacement = "45 31 F6";

// The empowered wrapper — fetches Bytes + Memory + Messaging in one Init.
Kcdx            K;
kcdxBytesHandle g_handle = 0;

// One-shot guard: the InputLoaded backstop fires only if PostGameLoad never
// reported (the cap-39 design).
bool g_post_ran = false;

void Report(bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP41", "PASS %s: %s", kRow, reason);
    else      K.log.Error("CAP41", "FAIL %s: %s", kRow, reason);
    K.api->ReportTestResult(K.self, kRow, pass ? 1 : 0, reason);
}

// Resolve the rewrite site's live VA via K.memory->ScanPattern (the SAME
// helper cap-39 uses). By PostGameLoad the site is already rewritten to
// `45 31 F6`, so we scan the post-rewrite context and return ctx+20 (the
// start of the 3-byte rewrite within the 23-byte context). On a miss we
// return 0 and the precondition guard FAILs with a site-unresolved reason.
uintptr_t ResolveSiteForReadback() {
    if (!K.memory) return 0;
    const char* postCtx =
        "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 45 31 F6";
    uintptr_t ctx = K.memory->ScanPattern("WHGame.dll", postCtx);
    if (!ctx) return 0;
    return ctx + 20;  // start of `45 31 F6` within the 23-byte context.
}

// === InputLoaded backstop — loud FAIL if PostGameLoad never fired =========

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;  // PostGameLoad already reported the row.

    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_"
        "InputLoaded — the after-phase C++ export was not dispatched; the "
        "row reported FAIL via the InputLoaded backstop";
    K.log.Error("CAP41", "FAIL backstop: %s", reason);
    K.api->ReportTestResult(K.self, kRow, 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ======================================================
//
// Register the byte rewrite now (Load wave) with the DISTINCT name. The apply
// pass runs after Load returns; PostGameLoad queries the conflict report.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        // K.Init logs why (it requires Hook; Bytes is best-effort below).
        // Report the row FAIL so it doesn't sit silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            api->ReportTestResult(self, kRow, 0,
                "Kcdx::Init returned false at Plugin_Load (engine version "
                "mismatch?)");
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    if (!K.bytes) {
        K.log.Error("INIT",
            "QueryInterface(Bytes, v%u) returned null — the row FAILs",
            kcdxBytesInterface_Version);
        Report(false,
            "K.bytes is null: QueryInterface(kcdxInterface_Bytes) returned "
            "null at Plugin_Load — cannot register the bytes patch the "
            "report is supposed to fold in");
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

    // The SAME safe rewrite cap-39 registers, with a DISTINCT name so the
    // report query can find THIS plugin's entry among the co-located ones.
    // target is the COMMON-PATH locator (a NAME the engine resolves to an
    // address — the disassembler test); owningPlugin threads our identity.
    kcdxBytesOptions opts = {};
    opts.owningPlugin = K.self;
    opts.name         = kPatchName;     // DISTINCT — the report search key
    opts.target       = kTarget;        // name resolves the address (id 1004)
    opts.offset       = 13;             // the rewrite is +13 within the AOB
    opts.original     = kOriginal;      // verify bytes (same len as replacement)
    opts.replacement  = kReplacement;   // same-length rewrite (3 bytes)
    opts.idempotent   = true;           // coexist with cap-01 / cap-39's same write

    g_handle = K.bytes->Register(&opts);
    if (g_handle == 0) {
        K.log.Error("INIT",
            "K.bytes->Register returned 0 (see BYTES_INTERFACE engine log "
            "for the teaching error) — PostGameLoad's precondition guard "
            "surfaces the FAIL");
    } else {
        K.log.Info("INIT",
            "K.bytes->Register returned handle (non-zero) for '%s'; the "
            "conflict-report query runs in PostGameLoad after the apply pass",
            kPatchName);
    }
    return true;
}

// === kcdxPlugin_PostGameLoad ==============================================
//
// Fires AFTER ApplyZone(AfterGame) (the bytes patch is LIVE) and BEFORE
// FireEngineMessage(InputLoaded). Query GetConflictReport at the patched VA
// and assert OUR entry is folded in.

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // K.api was cached in Load; same pointer.
    K.log.Info("CAP41",
               "kcdxPlugin_PostGameLoad — apply pass done; querying "
               "GetConflictReport at the kcdx.bytes-patched site");
    g_post_ran = true;

    if (!K.bytes) {
        Report(false,
            "K.bytes null in PostGameLoad (should not happen — Load checked)");
        return true;
    }

    // --- Precondition: the patch applied AND the site resolved -----------
    // If either fails, the report query is MEANINGLESS — FAIL here so a
    // report-blindness verdict is never conflated with an apply/resolve
    // failure (the falsifiability map keeps the two distinguishable).
    bool      applied = K.bytes->IsApplied(g_handle);
    uintptr_t siteVA  = ResolveSiteForReadback();
    if (g_handle == 0 || !applied || siteVA == 0) {
        char r[400];
        snprintf(r, sizeof(r),
            "PRECONDITION not met — the bytes patch did not apply / the site "
            "did not resolve, so the conflict-report query is meaningless "
            "(this is NOT a report-blindness FAIL): handle=%s, IsApplied=%d "
            "(expected 1), siteVA=%s (expected resolved). Registered via "
            "K.bytes->Register(target=\"%s\", name=\"%s\")",
            g_handle != 0 ? "non-zero" : "ZERO",
            applied ? 1 : 0,
            siteVA ? "resolved" : "UNRESOLVED",
            kTarget, kPatchName);
        Report(false, r);
        return true;
    }

    // --- The assertion: OUR bytes-Register entry is folded into the report -
    // cap=16: the site is co-located (cap-01 g_patches + cap-39 bytes-Register
    // + cap-41 bytes-Register, possibly more as the suite grows), so several
    // kind=Patch entries can come back. Find OURS by name.
    kcdxConflictEntry entries[16] = {};
    uint32_t count = K.api->GetConflictReport(
        siteVA, entries, sizeof(entries) / sizeof(entries[0]));

    int  ourMatches  = 0;     // entries named "cap41_bytes_patch"
    bool ourKindPatch = true; // our match(es) all kind == Patch
    bool ourApplied   = true; // our match(es) all applied != 0
    char names[600] = {0};    // every returned entry, for the FAIL reason
    for (uint32_t i = 0; i < count; ++i) {
        const kcdxConflictEntry& e = entries[i];
        if (e.name && std::strcmp(e.name, kPatchName) == 0) {
            ++ourMatches;
            if (e.kind != kcdxConflictEntryKind_Patch) ourKindPatch = false;
            if (!e.applied)                            ourApplied   = false;
        }
        size_t used = std::strlen(names);
        snprintf(names + used, sizeof(names) - used, "%s%s(kind=%d,applied=%d)",
                 used ? ", " : "", e.name ? e.name : "<null>",
                 e.kind, e.applied);
    }

    // PASS iff EXACTLY ONE entry is ours, and it is kind=Patch and applied.
    const bool pass = (ourMatches == 1) && ourKindPatch && ourApplied;
    char r[800];
    snprintf(r, sizeof(r),
        "%s — GetConflictReport(0x%p) at the kcdx.bytes-patched site returned "
        "%u entries: [%s]. Searched for OUR entry name=\"%s\": matches=%d "
        "(expected 1), kind=Patch=%d, applied=%d. The site is co-located "
        "(cap-01 [[patch]]/g_patches + cap-39 bytes-Register + cap-41 "
        "bytes-Register, all same-replacement idempotent-coexist), so the "
        "COUNT is not fixed — by-name is the falsifiable proof. PRE-FEATURE: "
        "GetConflictReport walked only g_patches+g_hooks+hook_chain and was "
        "BLIND to bytes-Register -> 0 matches -> FAIL. POST-FEATURE: the "
        "fourth source folds it in -> 1 match, kind=Patch, applied -> PASS. "
        "kind!=Patch -> wrong fold kind; applied==0 -> accessor flag/range bug",
        pass ? "the bytes-Register source IS folded into the conflict report"
             : "OUR bytes-Register entry was NOT folded in as expected",
        (void*)siteVA, count, names, kPatchName,
        ourMatches, ourKindPatch ? 1 : 0, ourApplied ? 1 : 0);
    Report(pass, r);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
