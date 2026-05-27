// === FOPEN PROBE (Phase 8.5 — pak-resolver override semantics) =======
//
// See fopen_override_probe.h for the full framing. This probe resolves the two
// runtime unknowns the Phase-8.5a RE flagged before the asset-overlay design
// (8.5c) rests on them:
//   #1  CCryPak::FOpen fires for asset READS (not just WriteCachePak writes).
//   #2  a pName-rewrite in the hook OVERRIDES a pak-resident asset (vs pak
//       precedence winning).
//
// PHASE U.1 (this build, kFOpenProbeMutate == false): observe-only. Log each
// DISTINCT (mode-class, path) opened during boot→menu→load exactly once (no
// silent count cap — a repeat is suppressed, a genuine drop is WARNed), classify
// each path, always call original unmutated. Resolves #1 + yields the complete
// distinct pak-resident path list for U.2's target.
//
// PHASE U.2 (flip kFOpenProbeMutate, set kU2RedirectFrom/To from U.1's log):
// redirect ONE confirmed pak path to a loose sentinel, observe which file the
// game loaded. Resolves #2.
//
// Mirrors loc_dump_probe's MinHook install discipline, but resolves the target
// through the Address Library (ids 1206 + 1207) — the seed rows exist, so the
// AP1-clean path is used. Detours the FUNCTION BODY (process-wide), not the
// vtable slot: that matches what an overlay hook actually does and needs no
// live-instance capture. id 1207 is used only for a one-shot consistency check
// that *pCryPak's vtable[36] resolves to the same body the Library gives.

#include "fopen_override_probe.h"

#include <windows.h>
#include <intrin.h>  // _ReturnAddress()

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include "MinHook.h"

#include "../address_library.h"
#include "../dev.h"
#include "../log.h"
#include "../modification_inventory.h"  // RegisterModification (probe category)
#include "../test.h"  // engine-internal probe self-report (same pattern as
                      // loc_dump_probe / cap-23: the behavior under test is
                      // engine machinery, so the engine reports; a manifest-
                      // only plugin registers the names for PENDING tracking).

namespace kcdx::probes::fopen_override_probe {

namespace {

// === Address Library ids (landed Phase 8.5a, FINDINGS.md) ============
constexpr uint64_t kIdFOpen     = 1206;  // CCryPak_FOpen body, RVA 0x004614A0
constexpr uint64_t kIdPCryPak   = 1207;  // gEnv_pCryPak slot, RVA 0x0492B850

// vtable slot 36 (offset +0x120) — used ONLY for the one-shot consistency
// assertion (does *pCryPak's vtable[36] equal the Library's FOpen body?). The
// detour itself targets the body, not the slot.
constexpr size_t kFOpenVtableSlot = 36;  // 0x120 / 8

// === PHASE GATE ======================================================
//
// U.1/U.2a = false (observe-only). U.2b = true (hook rewrite).
// NOW U.2b (hook-rewrite override): mutate is ON. U.2a RESOLVED that pak
// precedence beats a loose sibling at the SAME vpath (kcd.log showed [3920],
// not [7]) — so CryEngine does NOT natively do loose-over-pak; the overlay
// NEEDS the hook. U.2b tests whether the HOOK's pName rewrite to a NON-pak
// loose path overrides the pak entry where native placement could not.
// Read-back = game's kcd.log "Finished loading [N] tags ... item.xml":
//   [7]    = the hook rewrite OVERRODE the pak → the overlay-hook (8.5c) works.
//   [3920] = even the rewrite lost → FOpen re-resolves internally; 8.5c needs a
//            different mechanism (return-our-own-handle / pak registration).
// === PROBE U.2c — FOpen resolution-root mapping (OBSERVE-ONLY) ========
//
// U.2 and U.2b both showed a NULL handle when redirecting item.xml to a
// Data/-relative loose path (kcdx_u2_item_sentinel.xml, kcdx_assets/...), while
// U.2a's loose file at the real vpath (Libs/Tables/item/item.xml) DID resolve.
// So FOpen does NOT open an arbitrary loose path — the real unknown is WHICH
// loose roots it resolves. U.2c maps that empirically: when the engine opens
// the item.xml database, the detour ALSO calls orig() ITSELF on a battery of
// candidate loose paths and logs which return non-null. The engine's own open
// is NOT mutated (observe-only — no crash risk). Each non-null probe handle is
// CLOSED via FClose (vtable slot 55, +0x1B8 — "int fclose(this, handle)",
// binary-confirmed at 0x1804609d0; decompile shows iVar3=fclose(param_2)) so we
// don't leak engine file handles.
constexpr bool kFOpenProbeMutate = false;  // OFF: U.4 RESOLVED YES (live 2026-05-26) —
                                           // kcd.log showed our KCDX_U4_OVERRIDE_ACTIVE
                                           // marker = the engine loaded+executed our
                                           // substitute for cheat_util.lua. The
                                           // handle-consumed FOpen-redirect override is
                                           // CONFIRMED end-to-end. Mutation off to restore
                                           // a clean baseline; detour stays observe-only.

// === PROBE U.4 — override acceptance on a HANDLE-CONSUMED class (.lua) ========
//
// Trigger: the boot-loaded scripts/cheat/cheat_util.lua (in mods/cheat/data/
// cheat.pak; logged opening rb during boot in U.1). PathMatchesTarget normalizes
// case/slash. This .lua is HANDLE-CONSUMED (read through the FOpen handle via the
// shared CCryFile helper, per the partition) — so it tests BOTH flagged unknowns
// in one launch:
//   U.4.1 (override acceptance): does the engine LOAD+EXECUTE our substitute
//     when we redirect the open to it? The substitute is the BYTE-EXACT real
//     cheat_util.lua + one appended line Cheat:logDebug("KCDX_U4_OVERRIDE_ACTIVE")
//     (uses the script's OWN logger, which demonstrably reaches kcd.log — the
//     real file's final Cheat:logDebug("cheat_util.lua loaded") is the proof the
//     channel is live, unlike the prior CombatTest target whose log was gated
//     behind a method never called at boot). Read-back = kcd.log:
//       "KCDX_U4_OVERRIDE_ACTIVE" present -> override ACCEPTED end-to-end
//         (handle-consumed FOpen override works; asset-file overlay confirmed).
//       only "cheat_util.lua loaded", no KCDX line -> NOT accepted (a finding).
//   U.4.2 (.lua route): the redirect firing at all on this .lua open confirms
//     .lua routes through FOpen (the partition's inferred edge), since the
//     detour only sees opens that go through FOpen slot 36.
constexpr const char* kU2TriggerPath = "scripts/cheat/cheat_util.lua";

// U.4 redirect target: a VALID loose Lua substitute at a Data/-prefixed path
// (the U.2c-confirmed resolvable shape) that logs System.Log("KCDX_U4_OVERRIDE_
// ACTIVE") at load instead of the real file's "CombatTest Startup". Forced flag
// 0x10006 = the OS-search flag U.2c proved makes a Data/ loose path resolve;
// .lua is handle-consumed so the engine reads our substitute THROUGH the handle.
// One-shot; null-fallback to the original pName so a null can't break boot.
//   kcd.log "KCDX_U4_OVERRIDE_ACTIVE" = override ACCEPTED (handle-consumed FOpen
//     override works end-to-end) -> overlay mechanism for asset-file classes
//     CONFIRMED.
//   kcd.log "CombatTest Startup" (real line) or neither = NOT accepted ->
//     re-examine (the partition predicted accept; a miss is a finding).
//   u4_result resolved=0 = the substitute path didn't resolve (re-check path/flag,
//     NOT a verdict — fallback serves the real file, boot safe).
constexpr const char* kU2dRedirectTo = "Data/kcdx_assets/cheat_util_override.lua";
constexpr uint32_t    kU2eForceFlags = 0x10006u;  // OS-search flag, U.2c-confirmed resolvable

// One-shot latch: run the U.4 override on the FIRST trigger open only.
std::atomic<bool> g_u2Fired{false};

// === Function-pointer typing (Win64 fastcall, verified ABI id 1206) ==
//   ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)
//   RCX=this(ICryPak*), RDX=pName, R8=szMode, R9D=nFlags.
using FOpen_t = void* (__fastcall*)(void*       self,
                                    const char* pName,
                                    const char* szMode,
                                    uint32_t    nFlags);

// === Probe state =====================================================
std::atomic<FOpen_t> g_orig{nullptr};
std::atomic<bool>    g_installed{false};

// One-shot self-report latch — report unknown #1 PASS from the FIRST read-mode
// fire (hook-fire-self-report convention; never poll a count).
std::atomic<bool>    g_reported_read{false};

constexpr const char* kRowReadFires = "cap-44-fopen-read-fires";

// === Logging discipline: DEDUP-BY-DISTINCT-PATH, never a silent count cap =====
//
// FOpen is hot (the RE found 680 call sites; it fires thousands of times), but
// on a BOUNDED set of distinct virtual paths. So we log each distinct (mode-
// class, path) exactly ONCE — on first sight — and suppress only exact repeats.
// This captures EVERY unique asset the game opens with zero flood and, crucially,
// zero SILENT loss: the old first-N-then-go-quiet throttle hid exactly the
// later pak-resident opens this probe exists to find, and a diagnostic that
// goes quiet is indistinguishable from one that broke (AP14 / fail-state-
// logging.md). A repeat is not a drop (the path was already logged); a genuine
// drop (distinct-set safety bound hit) logs a LOUD WARN naming the cap — never
// silent.
//
// Thread-safe: FOpen is called from multiple threads, so the seen-set is
// guarded by a mutex. This is a dev-only probe; the lock cost is irrelevant.
std::mutex                         g_seenMu;
std::unordered_set<std::string>    g_seenPaths;        // distinct (class-prefixed) paths logged

// Safety bound on the distinct-set size, so a pathological run can't grow the
// set unbounded. Far above the real distinct-path count (a few hundred). On
// hit, we log a single WARN and stop ADDING (still call original) — the drop is
// announced, not silent.
constexpr size_t kMaxDistinctPaths = 20000;
std::atomic<bool> g_distinctCapWarned{false};

// A read-open mode begins with 'r' (rb, r, r+, rt, ...). Per the RE's call-site
// survey the engine uses real fopen mode strings.
bool IsReadMode(const char* mode) {
    return mode && (mode[0] == 'r');
}

// Case-insensitive, slash-insensitive ('\\' == '/') path equality. The engine
// passes the U.2 target as both "Libs\Tables/item/Item.xml" and
// "libs/tables/item/item.xml" — a plain strcmp would miss the first (earliest)
// open, so the redirect must normalize case + separators.
bool PathMatchesTarget(const char* p, const char* target) {
    if (!p || !target) return false;
    for (;; ++p, ++target) {
        char a = *p, b = *target;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a != b) return false;
        if (a == '\0') return true;  // both ended together
    }
}

// Classify a virtual path so the log distinguishes pak-RESIDENT asset candidates
// (the class U.2 redirects) from boot config / mod manifests / profile / engine
// data, which resolve to LOOSE files and are not the precedence question. This
// is a heuristic for log triage only — it does NOT gate the detour or U.2.
const char* ClassifyPath(const char* p) {
    if (!p || !p[0]) return "empty";
    // Absolute Workshop / OS paths — loose, never pak-resident.
    if ((p[0] >= 'A' && p[0] <= 'Z' && p[1] == ':') || p[0] == '/') return "absolute";
    // CryEngine alias-prefixed virtual roots — profile / user / engine data.
    if (p[0] == '%') return "alias_root";
    // Helper: case-insensitive prefix match.
    auto starts = [&](const char* pre) {
        size_t i = 0;
        for (; pre[i]; ++i) {
            char a = p[i], b = pre[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };
    if (starts("./config") || starts("config/") || starts("data/config")) return "boot_config";
    if (starts("mods/"))            return "mod_manifest";
    if (starts("engine\\") || starts("engine/")) return "engine_data";
    // Everything else under data/ (textures, scripts, libs, .gfx, .dds, …) or a
    // bare virtual path is a pak-resident asset CANDIDATE — the class U.2 wants.
    return "asset_candidate";
}

// === The detour ======================================================
//
// U.1: observe-only — log read-mode opens, call original unmutated.
// U.2: for the one confirmed pak path, swap pName to the sentinel before
//      calling original; log both so the post-launch read sees the redirect.
void* __fastcall HookedFOpen(void*       self,
                             const char* pName,
                             const char* szMode,
                             uint32_t    nFlags) {
    const bool isRead = IsReadMode(szMode);
    const char* pathForLog = pName ? pName : "(null)";
    const char* modeForLog = szMode ? szMode : "(null)";
    const char* cls = ClassifyPath(pName);

    // Log each distinct (mode-class, path) exactly ONCE. Repeats are suppressed
    // (already logged), NOT silently dropped. The dedup key prefixes the mode
    // class so a path opened both read and write logs once per class.
    {
        std::string key;
        key.reserve(8 + (pName ? std::strlen(pName) : 6));
        key += (isRead ? "r|" : "o|");
        key += pathForLog;

        std::lock_guard<std::mutex> lk(g_seenMu);
        if (g_seenPaths.size() >= kMaxDistinctPaths) {
            // Distinct-set safety bound hit — announce the drop LOUDLY, once.
            if (!g_distinctCapWarned.exchange(true, std::memory_order_acq_rel)) {
                log::WarnF("FOPEN_PROBE: distinct-path log set hit the %zu cap — "
                           "further NEW paths will NOT be logged this session "
                           "(detour still runs). Raise kMaxDistinctPaths if a "
                           "real run legitimately opens this many distinct paths.",
                           kMaxDistinctPaths);
            }
        } else if (g_seenPaths.insert(key).second) {
            // First time we've seen this exact (class, path) — log it.
            LOG_DEBUG_KV("FOPEN_PROBE", isRead ? "open_read" : "open_other",
                         log::KV("class",  cls),
                         log::KV("pName",  pathForLog),
                         log::KV("szMode", modeForLog),
                         log::KV("nFlags", (uint64_t)nFlags),
                         log::KV("caller", _ReturnAddress()));
        }
    }

    if (isRead) {
        // First read-mode fire → unknown #1 confirmed (slot is on the live
        // asset-read path). Report PASS once (one-shot guarded).
        bool expected = false;
        if (g_reported_read.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
            kcdx::test::ReportResult(
                kRowReadFires, true,
                "CCryPak::FOpen (id 1206) fired with a read mode — the engine-"
                "wide open-by-path resolver is on the live asset-read path "
                "(unknown #1 resolved)");
        }
    }

    FOpen_t orig = g_orig.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("FOPEN_PROBE: orig FOpen pointer null at dispatch");
        return nullptr;
    }

    // === DIAGNOSTIC (PROBE U.4): override acceptance on a HANDLE-CONSUMED class ===
    // One-shot: on the FIRST read open of the boot .lua (CombatTest_startup.lua),
    // redirect pName to our VALID loose Lua substitute AND OR-in 0x10006 (the
    // U.2c-confirmed resolvable flag). Unlike the registry-read item DB (U.2e),
    // .lua is HANDLE-CONSUMED — the engine reads our substitute THROUGH the
    // returned handle. Read-back = the game's kcd.log:
    //   "KCDX_U4_OVERRIDE_ACTIVE"  -> the engine LOADED+EXECUTED our substitute =
    //      handle-consumed FOpen override ACCEPTED end-to-end (asset-file overlay
    //      mechanism CONFIRMED).
    //   "CombatTest Startup" (real) / neither -> NOT accepted (a finding; the
    //      partition predicted accept).
    //   u4_result resolved=0 -> substitute path didn't resolve (re-check, NOT a
    //      verdict). FRead NOT called (unverified ABI, AP2). Null-fallback so a
    //      null can't break boot. The redirect FIRING also confirms .lua routes
    //      through FOpen (U.4.2 — the partition's inferred edge).
    if (kFOpenProbeMutate && isRead && pName &&
        PathMatchesTarget(pName, kU2TriggerPath)) {
        bool already = false;
        if (g_u2Fired.compare_exchange_strong(already, true,
                                              std::memory_order_acq_rel)) {
            const uint32_t forcedFlags = nFlags | kU2eForceFlags;
            void* h = orig(self, kU2dRedirectTo, szMode, forcedFlags);
            const bool resolved = (h != nullptr);
            LOG_DEBUG_KV("FOPEN_PROBE", "u4_result",
                         log::KV("from",          pName),
                         log::KV("to",            kU2dRedirectTo),
                         log::KV("szMode",        modeForLog),
                         log::KV("orig_nFlags",   (uint64_t)nFlags),
                         log::KV("forced_nFlags", (uint64_t)forcedFlags),
                         log::KV("handle",        h),
                         log::KV("resolved",      (uint64_t)(resolved ? 1 : 0)),
                         log::KV("verdict",
                                 resolved
                                     ? "redirect RESOLVED; watch kcd.log — "
                                       "'KCDX_U4_OVERRIDE_ACTIVE' = override "
                                       "ACCEPTED end-to-end (asset-file overlay "
                                       "works); 'CombatTest Startup' = not accepted"
                                     : "redirect NULL with forced 0x10006 — re-check "
                                       "path/flag (combo resolved in U.2c), NOT a "
                                       "verdict; fallback serves the real .lua"));
            if (resolved) {
                return h;  // serve our override handle to the engine
            }
            // Fallback — never break boot.
            return orig(self, pName, szMode, nFlags);
        }
    }

    // All other opens (and post-one-shot trigger opens): unmodified.
    return orig(self, pName, szMode, nFlags);
}

}  // namespace

bool Install() {
    if (!kcdx::dev::IsEnabled()) return false;

    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    // Resolve the FOpen body via the Address Library (AP1-clean: id 1206 landed
    // 8.5a). Resolve returns 0 on unknown id / version mismatch / unverified.
    uintptr_t fopenVA = kcdx::address_library::Resolve(kIdFOpen);
    if (!fopenVA) {
        log::Warn("FOPEN_PROBE: Resolve(1206 CCryPak_FOpen) returned 0 — "
                  "wrong game version or unverified row; cannot install");
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // MinHook idempotent init (the worker-thread caller already initialized it;
    // ALREADY_INITIALIZED is the no-op case).
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        log::WarnF("FOPEN_PROBE: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // One-shot consistency check: does *pCryPak's vtable[36] resolve to the
    // same body the Library gave us? Confirms the gEnv+0x50 → vtable+0x120 reach
    // matches id 1206 on this live build. Non-fatal — logged either way.
    uintptr_t pCryPakSlotVA = kcdx::address_library::Resolve(kIdPCryPak);
    if (pCryPakSlotVA) {
        void* pCryPak = *reinterpret_cast<void**>(pCryPakSlotVA);
        if (pCryPak) {
            void** vtable = *reinterpret_cast<void***>(pCryPak);
            void* slotFn = vtable ? vtable[kFOpenVtableSlot] : nullptr;
            LOG_DEBUG_KV("FOPEN_PROBE", "reach_check",
                         log::KV("pCryPak",     pCryPak),
                         log::KV("vtable",      (void*)vtable),
                         log::KV("vtable[36]",  slotFn),
                         log::KV("body_id1206", (void*)fopenVA),
                         log::KV("match",
                                 (uint64_t)(slotFn == (void*)fopenVA ? 1 : 0)));
            // (PROBE U.5 raw-OpenPack mount removed — the design re-scoped to
            // ABSORBING the engine's mod-loader orchestrator, NOT mounting a
            // bare pak directly. The wiki KM-A-3 + the U.5 ok=0 showed a raw
            // OpenPack on a non-mods/<modid> pak fights the loader structure;
            // the orchestrator-hook RE is the next step. FINDINGS.md §WARHORSE
            // WIKI + §re-scope.)
        } else {
            log::Warn("FOPEN_PROBE: *pCryPak (gEnv+0x50) null at install — "
                      "instance not constructed yet; body detour still valid");
        }
    }

    void* target = reinterpret_cast<void*>(fopenVA);
    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&HookedFOpen),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("FOPEN_PROBE: MH_CreateHook(FOpen @ %p) failed: %d",
                   target, (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig.store(reinterpret_cast<FOpen_t>(origPtr), std::memory_order_release);

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("FOPEN_PROBE: MH_EnableHook(FOpen @ %p) failed: %d",
                   target, (int)s);
        return false;
    }

    log::InfoF("FOPEN_PROBE: CCryPak::FOpen detour installed at %p "
               "(id 1206; mutate=%d)", target, (int)kFOpenProbeMutate);
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(target),
        kcdx::modification_inventory::Category::Probe, "fopen_override");
    return true;
}

}  // namespace kcdx::probes::fopen_override_probe
