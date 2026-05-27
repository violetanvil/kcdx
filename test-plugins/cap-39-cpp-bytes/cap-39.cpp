// CAP-39 — kcdxBytesInterface (C++ mirror of kcdx.bytes) end-to-end.
//
// The verification plugin that proves
// kcdxBytesInterface v1 works for a real C++ DLL author. The ABI was wired
// end-to-end (header decl, src/bytes_interface.cpp
// + QueryInterface, K.bytes on Kcdx.h); this plugin is the FIRST
// non-Lua-binder consumer of that surface, and the C++ PEER of cap-01's
// Lua kcdx.bytes coverage (both surfaces of one capability ship a regression).
//
// === Test design — why the named-target common path, not a self-host =====
//
// The deferred byte-rewrite registration locates a SITE by name/pattern/
// symbol (mirroring Lua kcdx.bytes); there is NO raw `address` locator on
// kcdxBytesOptions (unlike kcdxHookOptions). So cap-36's "opts.address =
// &MyStub" self-host trick is NOT available here.
//
// A self-host via `pattern =` over the plugin's OWN module was rejected
// after reading the engine: BOTH the pattern scanner (scan_engine.cpp
// ScanAll) and the patch locator (patch_engine.cpp Resolve ->
// ResolveUniquePatternMatch) scan `pe::ExecutableSections` ONLY. A plugin's
// own marker buffer lives in a WRITABLE, NON-executable .data section, so a
// pattern scan would never find it — the self-host marker is not supported
// by the engine as built (established by reading the scanner, not theorizing).
//
// Chosen observable (the disassembler-test COMMON PATH — a NAME, not hex):
//   target      = "outfit_swap_callsite_aob"   (Address Library id 1004)
//   offset      = 13   (the name resolves the AOB start; the rewrite is +13)
//   original    = "44 8A F0"
//   replacement = "45 31 F6"
// This is the SAME verified-safe rewrite cap-01 proves on the Lua /
// [[patch]] side (mov r14b,al -> xor r14d,r14d). Driving it through the
// C++ kcdxBytesInterface instead of Lua kcdx.bytes is exactly the parity
// proof the C++ byte-rewrite surface owes.
//
// Conflict-engine entanglement — resolved, NOT blind. cap-01's [[patch]]
// rewrites this same site with the SAME replacement and is in the suite.
// Two same-replacement writers on one site is conflict_engine
// WriteOnWriteFull — NOT a rejection (conflict_engine.cpp:59-70: "Both mods
// applied; later wins"). Whichever applies first does the write; the other
// idempotent-skips (patch_engine.cpp VerifyOriginalAtAddr verdict==0 ->
// ApplyPatch returns true -> lua_registry Status::Applied). The end state of
// the site is `45 31 F6` in EVERY interleaving, and cap-39's handle reaches
// Status::Applied either way — so the observable below has NO fragile
// dependency on cap-01's apply order. (cap-39's registration applies via
// lua_registry ApplyZone -> patch::ApplyPatch, which re-resolves against the
// current DLL state; it does not ride the conflict_engine preflight matrix.)
//
// === The two rows ========================================================
//
//   * CAP-39-cpp-bytes-register   The deferred-apply-through-C++ proof.
//       At kcdxPlugin_Load: K.bytes->Register(&opts) returns a NON-ZERO
//       handle (registration validated: exactly-one-locator + replacement
//       present + original==replacement length + the name resolved).
//       At kcdxPlugin_PostGameLoad (after ApplyZone(AfterGame), every
//       deferred write LIVE): K.bytes->IsApplied(handle)==true AND
//       K.memory->ReadBytes(site, 3) reads `45 31 F6`. A broken Register
//       (zero handle / not applied / bytes unchanged) => FAIL.
//
//   * CAP-39-cpp-bytes-uninstall-rejected   The no-revert teaching (peer of
//       cap-35's bytes-error row). K.bytes->Uninstall(handle) returns FALSE
//       — a byte rewrite has no revert path (the original bytes are not
//       retained), and IsApplied stays true (the bytes are NOT reverted).
//       A revert would silently flip status while the rewrite remains live
//       in memory — a lie about state. Falsifiable: an Uninstall that returns true
//       or a site that reverted to `44 8A F0` => FAIL.
//
// Test mode: boot-only. Both rows self-verify at kcdxPlugin_PostGameLoad
// (the after_game C++ export the engine fires AFTER ApplyZone(AfterGame) and
// BEFORE kcdxMessage_InputLoaded — by then the deferred write is LIVE; the
// cap-29 / cap-36 pattern). NO console gestures, NO save/load. An
// InputLoaded backstop (the cap-29 / cap-36 design) reports a loud FAIL on
// both rows if PostGameLoad never fired, so a missing after-phase export
// never leaves silent PENDING.

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
const char* kName   = "cap_39_cpp_bytes";

// Row IDs.
const char* kRowRegister  = "CAP-39-cpp-bytes-register";
const char* kRowUninstall = "CAP-39-cpp-bytes-uninstall-rejected";

// The same verified-safe rewrite cap-01 proves, located by NAME (the
// disassembler-test common path). id 1004 = outfit_swap_callsite_aob, the
// 'mov r14b, al' site that becomes 'xor r14d, r14d'.
const char* kTarget      = "outfit_swap_callsite_aob";
const char* kOriginal    = "44 8A F0";
const char* kReplacement = "45 31 F6";
const unsigned char kReplBytes[3] = { 0x45, 0x31, 0xF6 };

// The empowered wrapper — fetches Bytes + Memory + Messaging in one Init.
Kcdx           K;
kcdxBytesHandle g_handle = 0;

// One-shot guard: the InputLoaded backstop fires only if PostGameLoad never
// reported (the cap-29 / cap-36 design).
bool g_post_ran = false;

void Report(const char* row, bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP39", "PASS %s: %s", row, reason);
    else      K.log.Error("CAP39", "FAIL %s: %s", row, reason);
    K.api->ReportTestResult(K.self, row, pass ? 1 : 0, reason);
}

// Resolve the rewrite site's live VA via K.memory->ScanPattern so the
// PostGameLoad read-back has an address to read. We scan for the
// POST-rewrite bytes appended to the surrounding context (the same wide
// pattern cap-01 uses), because by PostGameLoad the site has already been
// rewritten to `45 31 F6` and the pristine 16-byte AOB no longer matches.
// On a miss we return 0 and the read-back is skipped (the FAIL reason says
// so) — IsApplied remains the authoritative apply check either way.
uintptr_t ResolveSiteForReadback() {
    if (!K.memory) return 0;
    // Wide context ending in the post-rewrite bytes; offset +20 lands on the
    // 3-byte rewrite site (same geometry as cap-01's kPostPatchPattern,
    // whose terminal `45 31 F6` is at context offset 20).
    const char* postCtx =
        "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 45 31 F6";
    uintptr_t ctx = K.memory->ScanPattern("WHGame.dll", postCtx);
    if (!ctx) return 0;
    return ctx + 20;  // start of `45 31 F6` within the 23-byte context.
}

// === InputLoaded backstop — loud FAIL if PostGameLoad never fired =========

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;  // PostGameLoad already reported both rows.

    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_"
        "InputLoaded — the after-phase C++ export was not dispatched; "
        "both rows reported FAIL via the InputLoaded backstop";
    K.log.Error("CAP39", "FAIL backstop: %s", reason);
    K.api->ReportTestResult(K.self, kRowRegister, 0, reason);
    K.api->ReportTestResult(K.self, kRowUninstall, 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ======================================================
//
// Register the byte rewrite now (Load wave). The apply pass runs after Load
// returns; PostGameLoad observes the final applied state.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        // K.Init logs why (it requires Hook; Bytes is best-effort below).
        // Report both rows FAIL so neither sits silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            api->ReportTestResult(self, kRowRegister, 0,
                "Kcdx::Init returned false at Plugin_Load (engine version "
                "mismatch?)");
            api->ReportTestResult(self, kRowUninstall, 0,
                "Kcdx::Init returned false at Plugin_Load");
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    if (!K.bytes) {
        K.log.Error("INIT",
            "QueryInterface(Bytes, v%u) returned null — both rows FAIL",
            kcdxBytesInterface_Version);
        Report(kRowRegister, false,
            "K.bytes is null: QueryInterface(kcdxInterface_Bytes) returned "
            "null at Plugin_Load (engine version mismatch?)");
        Report(kRowUninstall, false, "K.bytes is null at Plugin_Load");
        return true;
    }

    // Messaging is best-effort — only the InputLoaded backstop needs it.
    if (K.messaging) {
        K.messaging->RegisterListener(K.self, /*sender=*/nullptr, OnMessage);
    } else {
        K.log.Warn("INIT",
            "QueryInterface(Messaging) returned null — InputLoaded backstop "
            "disabled (if PostGameLoad never fires the rows sit "
            "silent-PENDING rather than loud-FAIL)");
    }

    // Build the options as a POD aggregate; default-zero every field, then
    // set ONLY what this rewrite uses. target is the COMMON-PATH locator
    // (a NAME the engine resolves to an address — the disassembler test);
    // owningPlugin threads our identity for self > engine > other resolution.
    kcdxBytesOptions opts = {};
    opts.owningPlugin = K.self;
    opts.name         = "cap39_outfit_swap_rewrite";
    opts.target       = kTarget;        // name resolves the address (id 1004)
    // The named target resolves to the 16-byte AOB's START (0x56174C); the
    // 'mov r14b,al' -> 'xor r14d,r14d' rewrite is at +13 within it (per the
    // seed-row description for id 1004, and matching cap-01's offset=13). With
    // offset 0 the apply pass reads 48 81 C1 at the site, not the stated
    // original 44 8A F0, and correctly REJECTS the patch (the bug the first
    // launch caught). The named target gives the WHERE; the author still
    // supplies the intra-site offset for a mid-pattern rewrite.
    opts.offset       = 13;
    opts.original     = kOriginal;      // verify bytes (same len as replacement)
    opts.replacement  = kReplacement;   // same-length rewrite (3 bytes)
    opts.idempotent   = true;           // coexist with cap-01's same write

    g_handle = K.bytes->Register(&opts);
    if (g_handle == 0) {
        // Register auto-logs the teaching reason to the engine + plugin log.
        K.log.Error("INIT",
            "K.bytes->Register returned 0 (see BYTES_INTERFACE engine log "
            "for the teaching error)");
        // PostGameLoad's per-row check surfaces the FAIL with the observed
        // handle == 0; do not report here (let the after-phase own the row).
    } else {
        K.log.Info("INIT",
            "K.bytes->Register returned handle (non-zero); final IsApplied "
            "verdict read in PostGameLoad after the apply pass");
    }
    return true;
}

// === kcdxPlugin_PostGameLoad ==============================================
//
// Fires AFTER ApplyZone(AfterGame) (the deferred write is LIVE) and BEFORE
// FireEngineMessage(InputLoaded). Read the apply state + the live bytes.

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // K.api was cached in Load; same pointer.
    K.log.Info("CAP39",
               "kcdxPlugin_PostGameLoad — apply pass done; running the "
               "register + uninstall-rejected assertions");
    g_post_ran = true;

    if (!K.bytes) {
        Report(kRowRegister, false,
            "K.bytes null in PostGameLoad (should not happen — Load checked)");
        Report(kRowUninstall, false, "K.bytes null in PostGameLoad");
        return true;
    }

    // --- Row 1: register (deferred apply through C++) ---------------------
    bool applied = K.bytes->IsApplied(g_handle);

    // Read the live site back to confirm the bytes actually changed to the
    // replacement (the observable that a broken Register can FAIL even if
    // IsApplied somehow reported true).
    uintptr_t site = ResolveSiteForReadback();
    unsigned char live[3] = { 0, 0, 0 };
    bool read_ok = false;
    bool bytes_match = false;
    if (site && K.memory) {
        read_ok = K.memory->ReadBytes(site, live, sizeof(live)) != 0;
        bytes_match = read_ok &&
            std::memcmp(live, kReplBytes, sizeof(kReplBytes)) == 0;
    }

    {
        char reason[400];
        const bool pass = (g_handle != 0) && applied && bytes_match;
        snprintf(reason, sizeof(reason),
            "%s — handle=%s, IsApplied=%d; site %s, read=%d, live bytes "
            "%02X %02X %02X (expected 45 31 F6); registered via "
            "K.bytes->Register(target=\"%s\") — the deferred-apply path "
            "through the C++ kcdxBytesInterface (C++ peer of cap-01's Lua "
            "kcdx.bytes)",
            pass ? "register + deferred apply took effect"
                 : "register / deferred apply did NOT take effect",
            g_handle != 0 ? "non-zero" : "ZERO",
            applied ? 1 : 0,
            site ? "resolved" : "UNRESOLVED (read-back skipped)",
            read_ok ? 1 : 0,
            live[0], live[1], live[2],
            kTarget);
        Report(kRowRegister, pass, reason);
    }

    // --- Row 2: Uninstall is rejected (no revert) -------------------------
    //
    // A byte rewrite has no revert path; Uninstall returns false + logs a
    // teaching line, and the bytes stay rewritten (IsApplied stays true).
    {
        bool uninstall_ret  = K.bytes->Uninstall(g_handle);
        bool applied_after  = K.bytes->IsApplied(g_handle);

        // Re-read the site: it must STILL be the replacement (no revert).
        unsigned char live2[3] = { 0, 0, 0 };
        bool still_rewritten = false;
        if (site && K.memory && K.memory->ReadBytes(site, live2, sizeof(live2))) {
            still_rewritten =
                std::memcmp(live2, kReplBytes, sizeof(kReplBytes)) == 0;
        }

        char reason[400];
        const bool pass = (g_handle != 0) && !uninstall_ret &&
                          applied_after && still_rewritten;
        snprintf(reason, sizeof(reason),
            "%s — Uninstall returned %d (expected 0/false: bytes have no "
            "revert path), IsApplied after=%d (expected 1: NOT reverted), "
            "live bytes %02X %02X %02X (expected still 45 31 F6). A revert "
            "would flip status while the rewrite stays live (a lie about state)",
            pass ? "Uninstall correctly rejected; rewrite stays live"
                 : "Uninstall behaved unexpectedly",
            uninstall_ret ? 1 : 0,
            applied_after ? 1 : 0,
            live2[0], live2[1], live2[2]);
        Report(kRowUninstall, pass, reason);
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
