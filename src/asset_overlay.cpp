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

}  // namespace

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
