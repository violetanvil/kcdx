// === Asset overlay — production hook on the game's pak resolver ======
//
// See asset_overlay.h for the full framing. This installs the production
// CCryPak::FOpen overlay hook through the conflict engine (hook_chain::
// AddCEngine), mirroring the engine.lua_pcall engine-direct install site in
// hooks.cpp: resolve by name, build the payload, parse the verified ABI into
// the signature, register the engine-stamped chain entry, then record the
// modification in the live inventory. The body is PASS-THROUGH this step (call
// original unchanged); the overlay-map redirect is a later step.

#include "asset_overlay.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <vector>

#include "dev.h"
#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "load_order.h"
#include "log.h"
#include "modification_inventory.h"
#include "plugin_loader.h"
#include "refdb.h"

namespace kcdx::asset_overlay {

namespace {

namespace fs = std::filesystem;

// Stable log category for the overlay-map build (greppable in kcdx-dev.log).
constexpr const char* kCat = "ASSET_OVERLAY";

// The built overlay map. Populated by BuildOverlayMap (worker thread, once at
// discovery), read by the resolver hook + the dev-log observation. Built before
// the FOpen hook first consults it; not mutated after build.
OverlayMap g_overlayMap;

// Canonical refdb name for the engine-wide open-by-path resolver. The seed row
// (kcdx_id 131, body at WHGame+0x004614A0) already exists — the common named-
// target path carries address AND verified ABI; no RVA literal, no new seed row.
constexpr const char* kNameFOpen = "CCryPak_FOpen";

// SOURCE: verified seed signature for kcdx_id 131 (CCryPak_FOpen) —
//   ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)
//   RCX=this(ICryPak*), RDX=pName, R8=szMode, R9D=nFlags.
// Win64 __fastcall. The ABI is the Address Library's, not a prologue-shape
// guess. The DSL drives the chain's JIT marshaling; OverlayFOpen below carries
// the matching Before-mode cFn ABI (the chain calls the original itself).
constexpr const char* kFOpenSig =
    "ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)";

std::atomic<bool> g_installed{false};

// === DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) =======================
// THROWAWAY — discriminator marker for the step-1 ordering probe (removed in
// step 2 with the SEAM-A probe block below; results-driven.md / working-
// artifacts.md — no residue). The step-1 AdjustFileName hook installed at the
// correct address but its first_adjustfilename_call marker NEVER fired through
// boot->menu while the menu's assets loaded. THE DISCRIMINATING QUESTION: does
// the FOpen hook fire on the same boot when AdjustFileName does not? FOpen
// fires + AdjustFileName silent → the menu reaches files via FOpen WITHOUT
// slot 1 (design's single-chokepoint claim is false for this path). Shares the
// step-1 category kProbeCtorVsReadCat so the manager greps ONE category for
// ctor_fired + first_adjustfilename_call + first_fopen_call.
//
// Category is the SAME shared probe category the SEAM-A block's step-1 markers
// use ("PROBE_CTOR_VS_READ"). Declared here (the FOpen hook at OverlayFOpen
// precedes the SEAM-A block textually) so OverlayFOpen below can reference it;
// the SEAM-A block reuses this same constant — one greppable category.
constexpr const char* kProbeCtorVsReadCat = "PROBE_CTOR_VS_READ";
std::atomic<bool> g_probeLoggedFirstFOpen{false};
// === END DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===================

}  // namespace

// === DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) =======================
// THROWAWAY — SHARED monotonic ordering counter for the step-1 ordering probe.
// The two markers (first_fopen_call here, ctor_fired in ctor_bracket.cpp) fired
// at the SAME millisecond on the SAME thread in the prior run — coincident at
// wall-clock resolution, so the ORDER was unresolved. Same-thread means the
// order is DETERMINISTIC; this counter exposes it: each marker fetch-adds once
// (on its first fire) and logs the sequence number, so N<M vs M<N gives a
// DEFINITE order regardless of the ms clock. Defined here with external
// linkage (NOT in the anonymous namespace) so ctor_bracket.cpp's marker, in a
// different translation unit, fetch-adds from the SAME counter (declared extern
// there). Relaxed ordering: a pure ordering TAG — the only consumer is the log,
// which compares the two emitted values; no happens-before edge to publish (the
// fetch_add is itself atomic, so each emitted seq is unique and monotone;
// concurrency.md — relaxed is correct for a counter nobody synchronizes against).
// Removed in step 3 (HOOK 1) with the rest of the probe residue (results-
// driven.md / working-artifacts.md — no residue in live source).
std::atomic<uint64_t> g_probeOrderSeq{0};
// === END DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===================

// The C function AddCEngine installs (the chain's `cFn`), Before mode. ABI is
// the chain's Before-mode cFn shape — `void cFn(uintptr_t args[], int* outCount,
// <typed args from cSig>)` — NOT the target's own ABI (the chain owns the
// MinHook detour and calls the original itself AFTER the Before callbacks; a
// Before entry never calls the original and returns void). Mirrors
// HookedLuaPcall_Engine in hooks.cpp exactly.
//
// PASS-THROUGH this step: observe nothing, mutate nothing, run the original
// unchanged. The overlay-map lookup + pName rewrite (writing args[1] back
// through the outCount channel) is a later step that fills this body.
//
// INVARIANT: FOpen is hot (the RE found 680 call sites; it fires thousands of
// times). NO per-call log here — that is logging.md + memory.md. The only log
// is the one-shot install line in Install(), never inside this body.
extern "C" void OverlayFOpen(uintptr_t /*args*/[], int* /*outCount*/,
                             void*       /*self*/,
                             const char* /*pName*/,
                             const char* /*szMode*/,
                             uint32_t    /*nFlags*/) {
    // === DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===================
    // FIRST-FOpen-call marker, one-shot atomic latch at the VERY TOP of the
    // body so the FIRST FOpen of the session emits exactly one marker. This is
    // the discriminator: the step-1 AdjustFileName hook installed correctly but
    // its first_adjustfilename_call NEVER fired through boot->menu. If THIS
    // fires while AdjustFileName stays silent, the menu reaches files via FOpen
    // WITHOUT slot 1. Relaxed ordering: a pure "have I logged this yet" latch,
    // no happens-before edge to publish (concurrency.md — a counter nobody
    // synchronizes against). RESPECTS the no-per-call-log invariant below: the
    // one-shot guard logs exactly ONCE, never per call (FOpen is hot — ~680
    // call sites). Removed in step 2 with the SEAM-A probe.
    {
        bool expected = false;
        if (g_probeLoggedFirstFOpen.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            // Ordering tag from the SHARED counter (defined above; ctor_fired in
            // ctor_bracket.cpp fetch-adds from the same one). Relaxed: pure
            // ordering tag, only the log compares the two values (concurrency.md
            // — no happens-before edge needed). Read ONCE on this first fire.
            const uint64_t seq =
                g_probeOrderSeq.fetch_add(1, std::memory_order_relaxed);
            LOG_DEBUG_KV(kProbeCtorVsReadCat, "first_fopen_call",
                         kcdx::log::KV("order_seq", seq),
                         kcdx::log::KV::BareStr("detail",
                             "engine's FIRST CCryPak::FOpen call this session — "
                             "compare order_seq against ctor_fired's; lower seq "
                             "fired first (definite order regardless of the ms "
                             "clock)"));
        }
    }
    // === END DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===============

    // Pass-through: the chain runs the original after this returns.
}

bool Install() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    auto sigParse = kcdx::hook_signature::Parse(kFOpenSig);
    if (!sigParse.ok) {
        log::ErrorF("engine.ccrypak_fopen: signature parse failed: %s",
                    sigParse.error.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // COMMON named-target locator: addressName resolves to the body AND the
    // verified ABI (the disassembler test) via the self > engine > other walk.
    kcdx::hook_payload::HookPayload p;
    p.mode         = kcdx::hook_payload::Mode::Before;
    p.addressName  = kNameFOpen;
    p.signature    = sigParse.sig;
    p.hasSignature = true;
    p.owningPlugin = "kcdx";
    p.owningAuthor = "kcdx";
    p.name         = "engine.ccrypak_fopen";

    // priority 0 — mirrors the engine.lua_pcall AddCEngine site (hooks.cpp);
    // priority orders engine entries among THEMSELVES only (engine-vs-plugin is
    // decided by the engine stamp, not priority).
    auto add = kcdx::hook_chain::AddCEngine(
        p, reinterpret_cast<void*>(&OverlayFOpen),
        sigParse.sig, /*pluginName=*/"kcdx",
        /*priority=*/0, /*name=*/"engine.ccrypak_fopen",
        /*handleId=*/0);
    if (!add.ok) {
        log::ErrorF("engine.ccrypak_fopen: AddCEngine failed: %s",
                    add.reason.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // Record the engine-owned hook in the live modification inventory
    // (Category::Engine), keyed by the resolved VA — same pattern as the
    // lua_pcall / update engine sites. ResolveAddrByName reads the cache built
    // in refdb::Open() (this runs after RefdbOpened); 0 = miss.
    uintptr_t fopenVA = kcdx::refdb::ResolveAddrByName(kNameFOpen);
    if (fopenVA) {
        kcdx::modification_inventory::RegisterModification(
            fopenVA, kcdx::modification_inventory::Category::Engine,
            "fopen_overlay");
    } else {
        log::Warn("engine.ccrypak_fopen: refdb name \"CCryPak_FOpen\" did not "
                  "resolve for the modification-inventory record (hook still "
                  "installed via the chain by name)");
    }

    log::InfoF("engine.ccrypak_fopen: production FOpen overlay hook installed "
               "(via hook_chain::AddCEngine; pass-through body) at %p",
               reinterpret_cast<void*>(fopenVA));
    return true;
}

// === DIAGNOSTIC (PROBE SEAM-A) =============================================
// THROWAWAY — settles the asset-resolution-OWNERSHIP seam (OQ #1-3 from
// _research/phase8.5-pak-resolver/RESOLUTION-OWNERSHIP-synthesis.md). REMOVED
// with the probe plugin (test-plugins/probe-asset-overlay/) + the staging when
// the seam is captured (results-driven.md / working-artifacts.md no-residue).
//
// THE QUESTION: does REPLACING CCryPak::AdjustFileName (kcdx_id 152, vtable
// slot 1, the resolution-decision root) in Around mode let kcdx OWN asset
// resolution for BOTH asset classes (handle-consumed .lua + memory-mapped
// .dds), independent of sys_pakPriority (default mode 2, no user.cfg this
// run), with stock resolution preserved on the miss path?
//
// THE SEAM (verified by the 5-front research): every by-name consumer + both
// asset classes route vpath -> AdjustFileName BEFORE any disk/pak touch; it
// returns the resolved concrete-path string. Owning it owns resolution above
// the per-mode existence gate. The kcdx body, on an overlay HIT, returns the
// plugin's loose-asset path directly (bypassing the engine's pak-only search);
// on a MISS, calls the original (the engine resolves stock content unchanged).
//
// Around cFn ABI for id 152 `ptr (ptr this, cstr pName, ptr outBuf, u32
// nFlags)` (read from src/dynamic_call_jit.cpp §CFnSigFor / hook_chain.cpp
// §DispatchExclusive — NOT invented): call_original is prepended as a typed
// fnptr (pointer-width in RCX), then the typed args; the cFn's returned ptr is
// written into rv by the dispatch thunk and BECOMES the resolver's result.

constexpr const char* kSeamACat = "SEAMA_PROBE";

// Canonical refdb name for the resolution-decision root. Seed row kcdx_id 152
// (CCryPak_AdjustFileName, body at WHGame+0x0006205C, vtable slot 1) exists —
// resolve by NAME, no RVA literal, no new seed row (AP1).
constexpr const char* kNameAdjustFileName = "CCryPak_AdjustFileName";

// SOURCE: verified seed signature for kcdx_id 152 (data/seeds/
// address_versions_seed.csv) — `ptr (ptr this, cstr pName, ptr outBuf, u32
// nFlags)`. Win64 __fastcall: RCX=this, RDX=pName, R8=outBuf, R9D=nFlags;
// returns the resolved concrete-path string (into outBuf, also returned).
constexpr const char* kAdjustFileNameSig =
    "ptr (ptr this, cstr pName, ptr outBuf, u32 nFlags)";

std::atomic<bool> g_seamAInstalled{false};

// Typed call_original for the Around cFn (matches the seed ABI exactly).
using AdjustFileName_t = void* (*)(void* self, const char* pName,
                                   void* outBuf, uint32_t nFlags);

// One-shot guards so the hot resolver does NOT log per call (AdjustFileName
// fires constantly — logging.md / memory.md). Distinct-class nFlags lines are
// deduped to two atomic flips (bit-28 seen / bit-28 absent), and the hit-path
// markers fire once each. Relaxed ordering is correct here: these are pure
// "have I logged this yet" latches with no happens-before edge to publish
// (concurrency.md — a counter nobody synchronizes against).
std::atomic<bool> g_seamALoggedFlagsWithBit28{false};
std::atomic<bool> g_seamALoggedFlagsNoBit28{false};
std::atomic<bool> g_seamALoggedLuaHit{false};
std::atomic<bool> g_seamALoggedDdsHit{false};

// === DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) =======================
// THROWAWAY — step-1 probe (docs/design/asset-replacement.md §8/§9 unknown 1).
// Settles whether ModManager_ctor (the ready-bracket's HookedCtor) fires
// BEFORE the engine's first overridable asset read, deciding where the
// resolution seam installs. Removed in step 2 with the rest of the SEAM-A
// probe (results-driven.md / working-artifacts.md — no residue). One shared
// probe category (kProbeCtorVsReadCat, defined earlier beside the FOpen probe)
// so the manager greps ONE category for all three markers.
//
// This marker fires on the FIRST AdjustFileName call of the session,
// regardless of overlay hit/miss — it captures "the engine's first
// overridable asset read happened" with a timestamp comparable to the
// HookedCtor "ctor_fired" marker in src/mod_absorb/ctor_bracket.cpp.
std::atomic<bool> g_probeLoggedFirstAdjust{false};
// === END DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===================

// The Around cFn. ABI per the seed signature + the §CFnSigFor Around shape:
// (call_original, this, pName, outBuf, nFlags) -> resolved-path ptr.
//
// STEP 1 (this probe) is OBSERVE-ONLY: the seam logs the first-call timestamp
// (the ordering marker), the nFlags class, and any overlay HIT it SEES — then
// ALWAYS calls the original. It never writes outBuf. So it proves the seam is
// installed + reachable + when it first fires, without overriding anything.
//
// HIT  (pName normalizes to a key in the overlay map): log that the seam saw
//       the HIT (per asset class), then fall through to the original. The
//       actual override (write the loose path via outBuf) is STEP 2's
//       production seam — deferred until the caller-side outBuf capacity is
//       verified from the binary (an unbounded write here would be an
//       out-of-bounds hazard on a long path — anti-patterns.md ABI-invention).
// MISS (every other vpath): call the original — the engine resolves stock
//       content unchanged. The fall-through MUST be clean so a non-overlaid
//       open is never perturbed (this is the hottest path).
//
// INVARIANT: null/guard everything; an empty overlay map (not yet built when
// AdjustFileName first fires in early boot) is a clean miss -> call original.
extern "C" void* SeamAAdjustFileName(AdjustFileName_t call_original,
                                     void*            self,
                                     const char*      pName,
                                     void*            outBuf,
                                     uint32_t         nFlags) {
    // === DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===================
    // FIRST-call timestamp. One-shot atomic latch at the VERY TOP of the body
    // (before any overlay-map work) so the FIRST overridable asset read of the
    // session emits exactly one timestamped marker, hit or miss. Relaxed
    // ordering: a pure "have I logged this yet" latch, no happens-before edge
    // to publish (concurrency.md — a counter nobody synchronizes against). The
    // resolver is hot; NEVER log per call (logging.md / memory.md).
    {
        bool expected = false;
        if (g_probeLoggedFirstAdjust.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kProbeCtorVsReadCat, "first_adjustfilename_call",
                         kcdx::log::KV::BareStr("detail",
                             "engine's FIRST overridable asset read (first "
                             "AdjustFileName call this session) — compare its "
                             "timestamp against ctor_fired"));
        }
    }
    // === END DIAGNOSTIC (PROBE: ctor-vs-first-read ordering) ===============

    // OQ#2 instrumentation — log the nFlags the resolver sees, deduped to ONE
    // line per bit-28 class (NEVER per-call). Cheap atomic test-and-set on the
    // hot path; no allocation, no lock.
    const bool bit28 = (nFlags & 0x10000000u) != 0;
    if (bit28) {
        bool expected = false;
        if (g_seamALoggedFlagsWithBit28.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kSeamACat, "nflags_bit28_set",
                         kcdx::log::KV("nFlags", nFlags),
                         kcdx::log::KV("bit28", 1));
        }
    } else {
        bool expected = false;
        if (g_seamALoggedFlagsNoBit28.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kSeamACat, "nflags_bit28_clear",
                         kcdx::log::KV("nFlags", nFlags),
                         kcdx::log::KV("bit28", 0));
        }
    }

    // MISS guard: null pName or empty/missing map -> straight call-original.
    if (!pName) return call_original(self, pName, outBuf, nFlags);

    const std::string key = NormalizeVPath(pName);
    const OverlayMap& m = GetOverlayMap();
    auto found = m.find(key);
    if (found == m.end()) {
        // MISS — stock resolution preserved (OQ#1). The straight fall-through.
        return call_original(self, pName, outBuf, nFlags);
    }

    // HIT — kcdx WOULD own this path. For STEP 1 (the ordering probe) the seam
    // only OBSERVES the HIT and logs it; it does NOT write outBuf. The
    // override write (return the plugin's loose-asset path via outBuf) is
    // STEP 2's production-seam work — deferred here because the caller-side
    // outBuf capacity is not yet verified from the binary, so an unbounded
    // memcpy into it would be an out-of-bounds write on a long loose path
    // (a write into an engine buffer whose size we asserted, never read —
    // anti-patterns.md ABI-invention). Step 1's question (ctor-vs-first-read
    // ordering) does not need the write; on a HIT we fall through to the
    // original so a non-overridden open is never perturbed.
    const std::string& diskPath = found->second.diskPath;

    // One-shot HIT markers per asset class (deduped — never per-call). The .dds
    // is memory-mapped; the .lua is handle-consumed. Distinct markers so the
    // log shows the seam SAW both classes — proving the seam is reachable for
    // each — without yet overriding either (the write is step 2).
    const bool isDds = key.size() >= 4 &&
                       key.compare(key.size() - 4, 4, ".dds") == 0;
    if (isDds) {
        bool expected = false;
        if (g_seamALoggedDdsHit.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kSeamACat, "overlay_hit_dds_observed",
                         kcdx::log::KV("vpath", key),
                         kcdx::log::KV("disk", diskPath));
        }
    } else {
        bool expected = false;
        if (g_seamALoggedLuaHit.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            LOG_DEBUG_KV(kSeamACat, "overlay_hit_handle_observed",
                         kcdx::log::KV("vpath", key),
                         kcdx::log::KV("disk", diskPath));
        }
    }

    // STEP 1: observe-only — no outBuf write. Fall through to the original so
    // the engine resolves the asset unchanged (the override write is step 2,
    // after the outBuf capacity is verified from the binary).
    return call_original(self, pName, outBuf, nFlags);
}

// Install the SEAM-A Around hook on CCryPak::AdjustFileName (id 152) through
// the conflict engine (hook_chain::AddCEngine, Around mode). Mirrors the FOpen
// AddCEngine install (Install() above) — resolve by name, parse the seed ABI,
// register the engine-stamped chain entry — but Around not Before. Dev-mode-
// gated (probe). Must run AFTER RefdbOpened. Idempotent.
bool InstallSeamAProbe() {
    if (!kcdx::dev::IsEnabled()) return true;  // probe is dev-mode-only

    bool expected = false;
    if (!g_seamAInstalled.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    auto sigParse = kcdx::hook_signature::Parse(kAdjustFileNameSig);
    if (!sigParse.ok) {
        log::ErrorF("SEAMA_PROBE: AdjustFileName signature parse failed: %s",
                    sigParse.error.c_str());
        g_seamAInstalled.store(false, std::memory_order_release);
        return false;
    }

    kcdx::hook_payload::HookPayload p;
    p.mode         = kcdx::hook_payload::Mode::Around;
    p.addressName  = kNameAdjustFileName;
    p.signature    = sigParse.sig;
    p.hasSignature = true;
    p.owningPlugin = "kcdx";
    p.owningAuthor = "kcdx";
    p.name         = "engine.seama_adjustfilename_probe";

    auto add = kcdx::hook_chain::AddCEngine(
        p, reinterpret_cast<void*>(&SeamAAdjustFileName),
        sigParse.sig, /*pluginName=*/"kcdx",
        /*priority=*/0, /*name=*/"engine.seama_adjustfilename_probe",
        /*handleId=*/0);
    if (!add.ok) {
        log::ErrorF("SEAMA_PROBE: AddCEngine(AdjustFileName, Around) failed: %s",
                    add.reason.c_str());
        g_seamAInstalled.store(false, std::memory_order_release);
        return false;
    }

    uintptr_t va = kcdx::refdb::ResolveAddrByName(kNameAdjustFileName);
    log::InfoF("SEAMA_PROBE: AdjustFileName Around hook installed (id 152, via "
               "hook_chain::AddCEngine; Around) at %p — overlay HIT returns the "
               "kcdx loose path, MISS calls original",
               reinterpret_cast<void*>(va));
    return true;
}
// === END DIAGNOSTIC (PROBE SEAM-A) =========================================

// === Overlay map ============================================================

std::string NormalizeVPath(const std::string& vpath) {
    // Case-insensitive + slash-insensitive: ASCII-lowercase every char,
    // '\\' -> '/'. The resolver hook normalizes the engine's open-by-path
    // argument through THIS function so its lookup hits a key inserted by
    // BuildOverlayMap.
    //
    // INVARIANT (the shared key contract): the map key and the resolver's
    // runtime lookup MUST normalize identically, or the lookup misses.
    // WHY this exact fold: the engine's open-by-path argument is OBSERVED to
    // arrive with mixed case and backslashes, so the map collapses both. That
    // the runtime argument's actual case/separator form matches this fold is a
    // checkable runtime unknown the resolver step (the redirect) owes a probe
    // to confirm BEFORE trusting the lookup — it is assumed here, not yet
    // verified against a live open of an overlaid path.
    std::string out;
    out.reserve(vpath.size());
    for (char c : vpath) {
        if (c == '\\') {
            out.push_back('/');
        } else if (c >= 'A' && c <= 'Z') {
            // Explicit ASCII fold — NOT std::tolower (locale-dependent; a vpath
            // is a byte path, not locale text). Step 3 applies this SAME fold to
            // the engine's pName, so the lookup key must be locale-invariant.
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

namespace {

// Build the load-order-sorted list of loaded plugins, reusing the engine's
// resolved order (load_order::Of) — the SAME key the rest of the engine sorts
// by (RunPostGameLoad / RunAfterEntrypoints). Does NOT re-derive load order.
// g_plugins is in topo/discovery order; this returns pointers in load order.
std::vector<const plugins::LoadedPlugin*> PluginsInLoadOrder() {
    std::vector<const plugins::LoadedPlugin*> ordered;
    ordered.reserve(plugins::g_plugins.size());
    for (const auto& p : plugins::g_plugins) ordered.push_back(&p);
    std::sort(ordered.begin(), ordered.end(),
              [](const plugins::LoadedPlugin* a, const plugins::LoadedPlugin* b) {
                  const auto& ea = load_order::Of(a->manifest.name);
                  const auto& eb = load_order::Of(b->manifest.name);
                  if (ea.priority != eb.priority) return ea.priority < eb.priority;
                  if (ea.orderIndex != eb.orderIndex)
                      return ea.orderIndex < eb.orderIndex;
                  return a->manifest.name < b->manifest.name;
              });
    return ordered;
}

}  // namespace

void BuildOverlayMap() {
    g_overlayMap.clear();

    size_t walked      = 0;  // plugins with a non-empty assets entrypoint
    size_t fileCount   = 0;  // loose files inserted as a winning slot
    size_t suppressed  = 0;  // files that lost a vpath to an earlier plugin
    size_t escaped     = 0;  // files skipped for '..' traversal

    for (const plugins::LoadedPlugin* pp : PluginsInLoadOrder()) {
        const plugins::PluginManifest& m = pp->manifest;
        if (m.assetsEntrypointRel.empty()) continue;  // no assets entrypoint

        const fs::path assetsRoot = m.folderPath / m.assetsEntrypointRel;
        std::error_code ec;

        // A declared `assets` dir that does not exist / is not a directory is
        // the author's overlay silently not happening — WARN naming the plugin
        // + the resolved path it looked for (never a silent skip; AP14).
        if (!fs::exists(assetsRoot, ec) || !fs::is_directory(assetsRoot, ec)) {
            log::WarnF("Plugin '%s' declares [entrypoints] assets = \"%s\" but "
                       "the resolved directory does not exist: %s — no assets "
                       "will be overlaid for this plugin",
                       m.name.c_str(), m.assetsEntrypointRel.c_str(),
                       assetsRoot.string().c_str());
            continue;
        }
        ++walked;

        // Canonical assets root for the traversal-escape check below.
        const fs::path assetsRootCanon = fs::weakly_canonical(assetsRoot, ec);
        const fs::path escapeBase = ec ? assetsRoot : assetsRootCanon;

        size_t pluginFiles = 0;
        for (auto it = fs::recursive_directory_iterator(
                 assetsRoot, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::directory_entry& de = *it;
            std::error_code fec;
            if (!de.is_regular_file(fec)) continue;  // dirs/symlinks-to-dirs: descend/skip

            const fs::path& disk = de.path();

            // The virtual asset path is the file's path RELATIVE to the assets
            // root, in generic ('/') form. lexically_relative gives the
            // portable relative path without touching the filesystem.
            const fs::path rel = disk.lexically_relative(assetsRoot);
            const std::string vpath = rel.generic_string();

            // Path safety (input-validation.md §Paths): a vpath that escapes
            // the assets root via '..' is a contract violation — log + skip
            // THIS file (not the whole plugin). Confirm structurally (the
            // relative path has no ".." segment) AND that the canonical file
            // stays under the canonical root (catches a symlink pointing out).
            bool escapes = false;
            for (const auto& seg : rel) {
                if (seg == "..") { escapes = true; break; }
            }
            if (!escapes) {
                const fs::path diskCanon = fs::weakly_canonical(disk, fec);
                if (!fec) {
                    // Component-wise containment, NOT a string prefix: a raw
                    // prefix test admits a sibling sharing a name prefix
                    // (root ".../assets" would "contain" ".../assets-evil/x").
                    // lexically_relative gives "" when diskCanon is not under
                    // escapeBase, and a leading ".." component when it climbs
                    // out — either means the canonical file escaped the root.
                    const fs::path under = diskCanon.lexically_relative(escapeBase);
                    if (under.empty() || under.begin() == under.end() ||
                        *under.begin() == "..") {
                        escapes = true;
                    }
                }
            }
            if (escapes) {
                log::WarnF("Plugin '%s': asset overlay path '%s' escapes the "
                           "assets root (%s) — skipping this file",
                           m.name.c_str(), vpath.c_str(),
                           assetsRoot.string().c_str());
                ++escaped;
                continue;
            }

            const std::string key = NormalizeVPath(vpath);

            // Load-order precedence: the FIRST plugin (earliest in load order)
            // to claim a vpath WINS the slot; a later plugin's same-vpath file
            // is suppressed. Mirrors the established winner/suppressed conflict
            // report (conflict_engine.cpp): name the winner, the suppressed
            // owner, why (load order), and the fix (a lower 'priority' number).
            auto found = g_overlayMap.find(key);
            if (found != g_overlayMap.end()) {
                log::WarnF("Asset overlay conflict on '%s': plugin '%s' wins "
                           "(it loads earlier); plugin '%s' is suppressed for "
                           "this file. If you wanted '%s' to win, give it a "
                           "lower 'priority' number in its kcdx.toml.",
                           vpath.c_str(), found->second.owningPlugin.c_str(),
                           m.name.c_str(), m.name.c_str());
                ++suppressed;
                continue;
            }

            g_overlayMap.emplace(key, OverlayEntry{m.name, disk.string()});
            ++fileCount;
            ++pluginFiles;
        }

        // A walk error (iterator bailed mid-tree) is a failure state — log it,
        // never swallow (logging.md). A declared-but-empty assets dir (0 files,
        // no error) is the author's overlay silently not happening — WARN.
        if (ec) {
            log::WarnF("Plugin '%s': error walking assets dir %s: %s — overlay "
                       "for this plugin may be incomplete",
                       m.name.c_str(), assetsRoot.string().c_str(),
                       ec.message().c_str());
        } else if (pluginFiles == 0) {
            log::WarnF("Plugin '%s' declares [entrypoints] assets = \"%s\" but "
                       "the directory (%s) contains no files — nothing to "
                       "overlay",
                       m.name.c_str(), m.assetsEntrypointRel.c_str(),
                       assetsRoot.string().c_str());
        }
    }

    // Discovery summary — the step's observability (test bar). One event-driven
    // line at discovery (NOT per-file in a hot loop): the walk is a one-shot
    // startup load, so logging here is unrestricted (logging.md / memory.md).
    LOG_DEBUG_KV(kCat, "overlay_map_built",
                 kcdx::log::KV("plugins_with_assets", walked),
                 kcdx::log::KV("entries", fileCount),
                 kcdx::log::KV("suppressed", suppressed),
                 kcdx::log::KV("escaped", escaped));

    // Per-entry dump (vpath -> winning plugin) so the map is fully observable
    // in kcdx-dev.log. Discovery-time, bounded by the loaded plugin set — not a
    // hot path.
    for (const auto& [vpath, entry] : g_overlayMap) {
        LOG_DEBUG_KV(kCat, "overlay_entry",
                     kcdx::log::KV("vpath", vpath),
                     kcdx::log::KV("winner", entry.owningPlugin),
                     kcdx::log::KV("disk", entry.diskPath));
    }
}

const OverlayMap& GetOverlayMap() { return g_overlayMap; }

}  // namespace kcdx::asset_overlay
