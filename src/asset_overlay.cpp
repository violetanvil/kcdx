// === Asset overlay — production HOOK 1: the AdjustFileName resolution decision
//
// See asset_overlay.h for the full framing. This installs the production
// CCryPak::AdjustFileName resolver hook through the conflict engine (hook_chain::
// AddCEngine, Around mode), mirroring the engine.lua_pcall engine-direct install
// site in hooks.cpp: resolve by name, parse the verified ABI into the signature,
// register the engine-stamped chain entry, then record the modification in the
// live inventory. On an overlay HIT the body writes the overlay's disk path into
// the caller's outBuf (bounded to 2048) and returns a char* to it; on a MISS it
// calls the original unchanged (stock resolution byte-identical).

#include "asset_overlay.h"

#include <windows.h>  // MultiByteToWideChar / CP_UTF8 — widen the disk path for
                      // _wfopen_s on a HOOK-2 loose-overlay open

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>  // std::size (wmode bound)
#include <system_error>
#include <vector>

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

// Stable log category for the overlay-map build + the resolver hits (greppable
// in kcdx-dev.log).
constexpr const char* kCat = "ASSET_OVERLAY";

// The built overlay map. Populated by BuildOverlayMap (worker thread, once at
// discovery), read by the resolver hook + the dev-log observation. Built before
// the resolver hook first consults it; not mutated after build.
OverlayMap g_overlayMap;

// Canonical refdb name for the resolution-decision root. Seed row kcdx_id 152
// (CCryPak_AdjustFileName, vtable slot 1) exists — resolve by NAME, no RVA
// literal, no new seed row (AP1, no-hardcoded-addresses.md).
constexpr const char* kNameAdjustFileName = "CCryPak_AdjustFileName";

// Canonical refdb name for the open-by-path handle minter (HOOK 2). Seed row
// kcdx_id 131 (CCryPak_FOpen, vtable slot 36) exists — resolve by NAME, no RVA
// literal, no new seed row (AP1, no-hardcoded-addresses.md).
constexpr const char* kNameFOpen = "CCryPak_FOpen";

// SOURCE: verified seed signature for kcdx_id 152 (data/seeds/
// address_versions_seed.csv) — `ptr (ptr this, cstr pName, ptr outBuf, u32
// nFlags)`. Win64 __fastcall: RCX=this, RDX=pName, R8=outBuf, R9D=nFlags;
// returns the resolved concrete-path string (written into outBuf, also returned).
// The DSL drives the chain's Around-mode JIT marshaling; AdjustFileNameResolver
// below carries the matching Around-mode cFn ABI.
constexpr const char* kAdjustFileNameSig =
    "ptr (ptr this, cstr pName, ptr outBuf, u32 nFlags)";

// SOURCE: verified seed signature for kcdx_id 131 (data/seeds/
// address_versions_seed.csv) — `ptr (ptr this, cstr pName, cstr szMode, u32
// nFlags)`. Win64 __fastcall: RCX=this, RDX=pName, R8=szMode, R9D=nFlags;
// returns a FILE*-like open handle (null on miss). On a loose open the engine's
// FOpen mints a CRT FILE* (_wfopen via the loose producer); the unmodified read
// family routes any real FILE* to its OS arm (FRead's handle−1-vs-pak-count
// dispatch — a heap FILE* always exceeds the small pak count). HOOK 2 returns
// kcdx's OWN _wfopen FILE* from the Around cFn, which the read family then
// serves directly. The ptr return threads through the chain's Around-mode JIT
// exactly as HOOK 1's ptr return does (dynamic_call_jit.cpp StoreReturn — the
// 8-byte qword store for a Ptr/Cstr/Wstr return).
constexpr const char* kFOpenSig =
    "ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)";

// INVARIANT (the HIT write contract — outBuf bound): the engine's universal path
// cap, CryEngine ICryPak::g_nMaxPath. Every AdjustFileName caller read passes a
// 2048-byte outBuf (FOpen / FOpenRaw / GetFileSize-by-name; verified in 3 caller
// bodies — _research/adjustfilename-outbuf-recon/FINDINGS.md, gated PROCEED). The
// HIT write bounds to THIS as the cap-as-invariant, NOT "because those 3 callers
// allocate it" — robust to unread callers. A real disk path is always well under
// 2048; an over-cap path is truncated LOUD, never an OOB write (the KI-0004
// stack-overflow class is structurally excluded).
constexpr size_t kMaxPath = 2048;

std::atomic<bool> g_installed{false};

// Typed call_original for the Around cFn — matches the seed ABI exactly. The
// chain prepends this as a pointer-width fnptr in RCX (dynamic_call_jit.cpp
// §CFnSigFor, Around shape), then the typed args; the body calls it on a MISS.
using AdjustFileName_t = void* (*)(void* self, const char* pName,
                                   void* outBuf, uint32_t nFlags);

// Typed call_original for HOOK 2's Around cFn — matches the FOpen seed ABI
// exactly. Same chain mechanism as AdjustFileName_t: the chain prepends this as
// a pointer-width fnptr in RCX, then the typed args; the body calls it on a
// MISS (and on a HIT whose open failed). Returns the FOpen handle (a CRT FILE*
// on a loose open, an index+1 on a pak open) — kcdx returns its own FILE* on a
// HIT instead.
using FOpen_t = void* (*)(void* self, const char* pName, const char* szMode,
                          uint32_t nFlags);

// One-shot HIT latch so the HOT resolver does NOT log per call (AdjustFileName
// fires constantly — logging.md / memory.md). The first overlay HIT of the
// session emits exactly one debug line naming the winning vpath -> diskPath;
// subsequent HITs are silent. Relaxed ordering is correct: a pure "have I logged
// this yet" latch with no happens-before edge to publish (concurrency.md — a
// counter nobody synchronizes against).
std::atomic<bool> g_loggedFirstHit{false};

// One-shot HIT latch for HOOK 2's FOpen open (same rationale as
// g_loggedFirstHit above — FOpen is hot; the first served-loose-overlay open of
// the session logs once, the rest are silent). Relaxed: a pure logged-yet latch
// with no happens-before edge (concurrency.md).
std::atomic<bool> g_loggedFirstFOpen{false};

}  // namespace

// The Around cFn the chain installs (ChainEntry::cFn). ABI per the seed
// signature + dynamic_call_jit.cpp §CFnSigFor Around shape: call_original
// arrives pointer-width in RCX, then the typed args (this, pName, outBuf,
// nFlags); the cFn's returned ptr is written into the dispatch thunk's rv slot
// (§"Write the cFn return register into rv") and BECOMES the resolver's result.
// Mirrors the now-removed SEAM-A probe wiring, with the bounded outBuf write it
// deferred now added.
//
// HIT  (pName normalizes to a key in the overlay map): write the overlay's disk
//       path into outBuf bounded to kMaxPath (g_nMaxPath), return a char* to
//       outBuf. kcdx owns this resolution above the engine's per-mode gate.
// MISS (every other vpath): call the original — the engine resolves stock
//       content unchanged (it itself falls to the leaves). The fall-through is
//       the hottest path; keep it allocation-light (the NormalizeVPath + map
//       lookup is the per-FS-query cost, acceptable per the plan-spec).
//
// INVARIANT: null/guard everything; an empty overlay map (not yet built when
// AdjustFileName first fires in early boot) is a clean MISS -> call original.
// A HIT whose write would fail must fail LOUD, never silently miss (AP14).
extern "C" void* AdjustFileNameResolver(AdjustFileName_t call_original,
                                        void*            self,
                                        const char*      pName,
                                        void*            outBuf,
                                        uint32_t         nFlags) {
    // MISS guard: a null pName or null outBuf -> straight call-original. A null
    // outBuf means there is nowhere to write the overlay path, so kcdx cannot
    // own this resolution even on a HIT — defer to the engine (it handles its
    // own null-arg contract). A real HIT into a null outBuf would be a silent
    // wrong-serve (AP14); falling through to the original is the correct,
    // observable behavior.
    if (!pName || !outBuf) return call_original(self, pName, outBuf, nFlags);

    const std::string key = NormalizeVPath(pName);
    const OverlayMap& m = GetOverlayMap();
    auto found = m.find(key);
    if (found == m.end()) {
        // MISS — stock resolution preserved. The hottest path: straight
        // fall-through, no allocation beyond the lookup key above.
        return call_original(self, pName, outBuf, nFlags);
    }

    // HIT — kcdx owns this resolution. Write the overlay's concrete disk path
    // into the caller's outBuf, BOUNDED to kMaxPath (g_nMaxPath), then return a
    // char* to it (the engine's return==outBuf convention so return-consuming
    // callers like FOpen get the overlay path too).
    const std::string& diskPath = found->second.diskPath;
    char* out = static_cast<char*>(outBuf);

    // Bounded copy: snprintf caps at kMaxPath and always NUL-terminates. Its
    // return is the length it WOULD have written (excluding NUL); >= kMaxPath
    // means the path was truncated. A real filesystem path is always well under
    // 2048 (g_nMaxPath), so this branch is effectively unreachable — but if it
    // is ever hit, do NOT serve the clipped path (a half-written path that "may
    // fail to open" is a degraded serve). Fail LOUD and fall through to the
    // ORIGINAL so the engine serves stock resolution — kcdx declines the
    // resolution it cannot represent, rather than mis-serving a truncated one.
    // (The write is bounded, so outBuf was never overrun — the KI-0004
    // stack-overflow class is structurally excluded regardless of this branch.)
    const int written = std::snprintf(out, kMaxPath, "%s", diskPath.c_str());
    if (written < 0 || static_cast<size_t>(written) >= kMaxPath) {
        LOG_WARN_KV(kCat, "overlay_path_over_cap",
                    kcdx::log::KV("vpath", key),
                    kcdx::log::KV("plugin", found->second.owningPlugin),
                    kcdx::log::KV("cap", static_cast<unsigned long long>(kMaxPath)),
                    kcdx::log::KV("disk", diskPath));
        // outBuf may hold a truncated string; the engine's original resolver
        // ignores our partial write and resolves from pName itself.
        return call_original(self, pName, outBuf, nFlags);
    }

    // One-shot HIT marker (deduped — never per-call; the resolver is hot). Names
    // the winning vpath -> diskPath so the DECISION is observable in
    // kcdx-dev.log (the step's test bar: a declared overlay for a vanilla path
    // is CHOSEN and logged).
    bool expected = false;
    if (g_loggedFirstHit.compare_exchange_strong(expected, true,
                                                  std::memory_order_relaxed)) {
        LOG_DEBUG_KV(kCat, "overlay_resolved",
                     kcdx::log::KV("vpath", key),
                     kcdx::log::KV("winner", found->second.owningPlugin),
                     kcdx::log::KV("disk", diskPath));
    }

    return out;
}

// === HOOK 2 — the loose OPEN: return kcdx's own CRT FILE* ===================
//
// The Around cFn the chain installs on CCryPak::FOpen (id 131, slot 36). ABI
// per the seed signature + dynamic_call_jit.cpp CFnSigFor Around shape:
// call_original arrives pointer-width in RCX, then the typed args (this, pName,
// szMode, nFlags); the cFn's returned ptr is written into the dispatch thunk's
// rv slot (StoreReturn — the 8-byte qword store for a Ptr return) and BECOMES
// FOpen's result. This mirrors HOOK 1's AdjustFileNameResolver return mechanism
// EXACTLY (both return a void* ptr from an Around cFn) — the proven pointer-
// return precedent.
//
// HIT  (pName normalizes to a key in the overlay map): open the overlay's disk
//       file ourselves (_wfopen) and RETURN that CRT FILE* WITHOUT calling the
//       original. The engine's unmodified read family serves it via its OS arm
//       (FRead routes any real heap FILE* there — handle−1 ≫ pak-count;
//       gate-verified, _research/asset-fopen-handle-recon/FINDINGS.md). This
//       serves add-new assets + the loose side of replace, for every class,
//       without depending on the engine's loose-search (the layer the v1 path-
//       redirect failed at). Handle lifecycle (close) follows the engine's
//       normal FClose on the returned handle (touch nothing in the read family
//       / FClose).
// HIT-OPEN-FAILURE (the loose file is declared but unopenable): FAIL LOUD
//       (LOG_WARN_KV naming vpath + disk + errno) and FALL THROUGH to the
//       original — let the engine try stock content. Do NOT return a null
//       handle as if it were a valid open (AP14 silent-success); a degraded-
//       but-loud miss beats a silent broken handle.
// MISS (every other vpath): call the original — the engine opens stock content
//       normally (the hottest path; FOpen fires constantly — keep it clean, no
//       per-call log).
//
// INVARIANT: null/guard everything; an empty overlay map (early boot, before
// the map is built) is a clean MISS -> call original.
extern "C" void* FOpenLooseOverlay(FOpen_t     call_original,
                                   void*       self,
                                   const char* pName,
                                   const char* szMode,
                                   uint32_t    nFlags) {
    // MISS guard: a null pName -> straight call-original (the engine handles its
    // own null-arg contract). The hottest path.
    if (!pName) return call_original(self, pName, szMode, nFlags);

    const std::string key = NormalizeVPath(pName);
    const OverlayMap& m = GetOverlayMap();
    auto found = m.find(key);
    if (found == m.end()) {
        // MISS — stock open preserved. The hottest path: straight fall-through.
        return call_original(self, pName, szMode, nFlags);
    }

    // HIT — kcdx owns this open. Open the overlay's loose disk file ourselves
    // and return that CRT FILE* (the engine's read family serves it via the OS
    // arm). Preserve the caller's mode string verbatim so a write-mode open
    // ("wb"/"ab"/"w+b") is honored exactly as the engine would; default to a
    // binary read when the engine passed no mode.
    const std::string& diskPath = found->second.diskPath;
    const char* mode = (szMode && szMode[0]) ? szMode : "rb";

    // _wfopen on the wide disk path: diskPath is a UTF-8/ASCII std::string built
    // from std::filesystem (BuildOverlayMap's disk.string()); widen it to the
    // engine's path cap so a non-ASCII path on disk opens correctly. fopen
    // (narrow) would mojibake a non-ASCII path; _wfopen is the correct CRT entry
    // for a wide path on Windows.
    wchar_t wpath[kMaxPath];
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, diskPath.c_str(), -1,
                                         wpath, static_cast<int>(kMaxPath));
    wchar_t wmode[16];
    const int wmlen = MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode,
                                          static_cast<int>(std::size(wmode)));

    FILE* fp = nullptr;
    errno_t oerr = 0;
    if (wlen > 0 && wmlen > 0) {
        oerr = _wfopen_s(&fp, wpath, wmode);
    } else {
        oerr = -1;  // path/mode widening overflowed the cap or failed
    }

    if (oerr != 0 || !fp) {
        // HIT-OPEN-FAILURE — fail LOUD, then fall through to the original (AP14:
        // never return a silent null/broken handle as if it were a valid open).
        // The engine then tries stock content: a degraded-but-LOUD miss, not a
        // silently broken serve. Logged once-per-call here is acceptable — a HIT
        // open-failure is a rare error path (a declared overlay that won't open),
        // not the hot MISS path (logging.md: every failure state is logged).
        LOG_WARN_KV(kCat, "overlay_open_failed",
                    kcdx::log::KV("vpath", key),
                    kcdx::log::KV("plugin", found->second.owningPlugin),
                    kcdx::log::KV("disk", diskPath),
                    kcdx::log::KV("mode", mode),
                    kcdx::log::KV("errno", static_cast<long long>(oerr)));
        return call_original(self, pName, szMode, nFlags);
    }

    // One-shot served-open marker (deduped — never per-call; FOpen is hot).
    // Names the vpath -> diskPath whose loose open kcdx served, so the SERVE is
    // observable in kcdx-dev.log (the step's test bar: a declared loose overlay
    // is opened by kcdx and served by the engine's read family).
    bool expected = false;
    if (g_loggedFirstFOpen.compare_exchange_strong(expected, true,
                                                    std::memory_order_relaxed)) {
        LOG_DEBUG_KV(kCat, "overlay_opened",
                     kcdx::log::KV("vpath", key),
                     kcdx::log::KV("winner", found->second.owningPlugin),
                     kcdx::log::KV("disk", diskPath),
                     kcdx::log::KV("mode", mode));
    }

    // Return our own CRT FILE* as FOpen's result. The chain's Around-mode JIT
    // writes this pointer-width value into FOpen's rv slot (the same path HOOK
    // 1's char* return takes); the engine's read family then serves kcdx's bytes
    // off this handle without ever consulting the engine's loose-search.
    return static_cast<void*>(fp);
}

bool Install() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    auto sigParse = kcdx::hook_signature::Parse(kAdjustFileNameSig);
    if (!sigParse.ok) {
        log::ErrorF("engine.ccrypak_adjustfilename: signature parse failed: %s",
                    sigParse.error.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // COMMON named-target locator: addressName resolves to the body AND the
    // verified ABI (the disassembler test) via the self > engine > other walk.
    // Around mode: the chain does NOT auto-run the original — the cFn owns the
    // call_original it receives (calls it on a MISS, suppresses it on a HIT).
    kcdx::hook_payload::HookPayload p;
    p.mode         = kcdx::hook_payload::Mode::Around;
    p.addressName  = kNameAdjustFileName;
    p.signature    = sigParse.sig;
    p.hasSignature = true;
    p.owningPlugin = "kcdx";
    p.owningAuthor = "kcdx";
    p.name         = "engine.ccrypak_adjustfilename";

    // priority 0 — mirrors the engine.lua_pcall AddCEngine site (hooks.cpp);
    // priority orders engine entries among THEMSELVES only (engine-vs-plugin is
    // decided by the engine stamp, not priority).
    auto add = kcdx::hook_chain::AddCEngine(
        p, reinterpret_cast<void*>(&AdjustFileNameResolver),
        sigParse.sig, /*pluginName=*/"kcdx",
        /*priority=*/0, /*name=*/"engine.ccrypak_adjustfilename",
        /*handleId=*/0);
    if (!add.ok) {
        log::ErrorF("engine.ccrypak_adjustfilename: AddCEngine failed: %s",
                    add.reason.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // Record the engine-owned hook in the live modification inventory
    // (Category::Engine), keyed by the resolved VA — same pattern as the
    // lua_pcall / update engine sites. ResolveAddrByName reads the cache built
    // in refdb::Open() (this runs after RefdbOpened); 0 = miss.
    uintptr_t adjustVA = kcdx::refdb::ResolveAddrByName(kNameAdjustFileName);
    if (adjustVA) {
        kcdx::modification_inventory::RegisterModification(
            adjustVA, kcdx::modification_inventory::Category::Engine,
            "adjustfilename_resolver");
    } else {
        log::Warn("engine.ccrypak_adjustfilename: refdb name "
                  "\"CCryPak_AdjustFileName\" did not resolve for the "
                  "modification-inventory record (hook still installed via the "
                  "chain by name)");
    }

    log::InfoF("engine.ccrypak_adjustfilename: production AdjustFileName "
               "resolver hook live (via hook_chain::AddCEngine, Around) at %p "
               "— overlay HIT writes the kcdx path into outBuf (bounded %zu) and "
               "returns it; MISS calls original",
               reinterpret_cast<void*>(adjustVA), kMaxPath);

    // === HOOK 2 — the loose OPEN, alongside HOOK 1 in the SAME ready-bracket
    // install window (design §8 — both hooks live before the first asset read).
    // A second independent AddCEngine on a DISTINCT target (FOpen, id 131, vs
    // HOOK 1's AdjustFileName, id 152) — each builds its own signature, payload,
    // chain entry, and MinHook detour; registering two engine hooks from one
    // Install() is the same pattern hooks.cpp uses for lua_pcall + update. If
    // HOOK 2 fails to install, the whole Install() fails loud (g_installed reset)
    // — the seam is incomplete without both hooks; never ship a half-seam silently.
    auto fopenSigParse = kcdx::hook_signature::Parse(kFOpenSig);
    if (!fopenSigParse.ok) {
        log::ErrorF("engine.ccrypak_fopen: signature parse failed: %s",
                    fopenSigParse.error.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    kcdx::hook_payload::HookPayload pf;
    pf.mode         = kcdx::hook_payload::Mode::Around;
    pf.addressName  = kNameFOpen;
    pf.signature    = fopenSigParse.sig;
    pf.hasSignature = true;
    pf.owningPlugin = "kcdx";
    pf.owningAuthor = "kcdx";
    pf.name         = "engine.ccrypak_fopen";

    auto addF = kcdx::hook_chain::AddCEngine(
        pf, reinterpret_cast<void*>(&FOpenLooseOverlay),
        fopenSigParse.sig, /*pluginName=*/"kcdx",
        /*priority=*/0, /*name=*/"engine.ccrypak_fopen",
        /*handleId=*/0);
    if (!addF.ok) {
        log::ErrorF("engine.ccrypak_fopen: AddCEngine failed: %s",
                    addF.reason.c_str());
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    uintptr_t fopenVA = kcdx::refdb::ResolveAddrByName(kNameFOpen);
    if (fopenVA) {
        kcdx::modification_inventory::RegisterModification(
            fopenVA, kcdx::modification_inventory::Category::Engine,
            "fopen_loose_overlay");
    } else {
        log::Warn("engine.ccrypak_fopen: refdb name \"CCryPak_FOpen\" did not "
                  "resolve for the modification-inventory record (hook still "
                  "installed via the chain by name)");
    }

    log::InfoF("engine.ccrypak_fopen: production FOpen loose-overlay hook live "
               "(via hook_chain::AddCEngine, Around) at %p — overlay HIT opens "
               "the kcdx loose file and returns its own CRT FILE*; MISS calls "
               "original",
               reinterpret_cast<void*>(fopenVA));
    return true;
}

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
    // checkable runtime unknown HOOK 1's live acceptance confirms (the
    // overlay-HIT log line firing on an overlaid path is the confirmation).
    std::string out;
    out.reserve(vpath.size());
    for (char c : vpath) {
        if (c == '\\') {
            out.push_back('/');
        } else if (c >= 'A' && c <= 'Z') {
            // Explicit ASCII fold — NOT std::tolower (locale-dependent; a vpath
            // is a byte path, not locale text). The resolver applies this SAME
            // fold to the engine's pName, so the lookup key must be
            // locale-invariant.
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

    // Discovery summary — the build's observability. One event-driven line at
    // discovery (NOT per-file in a hot loop): the walk is a one-shot startup
    // load, so logging here is unrestricted (logging.md / memory.md).
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
