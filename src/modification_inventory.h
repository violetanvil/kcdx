#pragma once
#include <cstdint>

#include "log.h"  // for log::Level (LogInventory summary severity)

// kcdx::modification_inventory — the LIVE "what kcdx has modified" registry +
// its diagnostic summary.
//
// WHY THIS MODULE EXISTS (and why it does NOT read hook_engine::g_hooks):
// The save-load crash (docs/known-issues/save-load crash 0xC8 ...) was
// invisible because nothing recorded what kcdx actually modifies on the load
// path. The first cut of this inventory read hook_engine::g_hooks /
// g_patches / g_mid_hooks — but those legacy [[hook]]/[[patch]] vectors are
// DEAD post-Phase-5 (empty, nothing populates them; the TOML parsers were
// deleted). Reading them produced an all-zero inventory that mis-served the
// crash bisect. This module reads the LIVE modification sources instead:
//
//   1. hook_chain::g_chains    — the live kcdx.hook plugin hooks (the
//                                primary signal). Folded via
//                                hook_chain::GetAllChainTargets().
//   2. The fixed engine / lifecycle / probe MinHook installs — which have
//      no central registry, so each install site calls RegisterModification
//      on success and this module folds the registered set.
//
// The summary line is per-category (not a flat count) + an order-independent
// fingerprint over ALL target VAs. The crash guard reads the cached
// pre-formatted summary string from inside its SEH handler with zero
// allocation (LastInventorySummary).

namespace kcdx::modification_inventory {

// Category of a modification, so the summary line is meaningful rather than a
// flat count. The string forms are stable + greppable (used verbatim in the
// summary line + per-target DETAIL).
enum class Category : uint8_t {
    PluginHook = 0,  // "plugin_hook" — hook_chain (live kcdx.hook installs)
    Engine     = 1,  // "engine"      — lua_pcall / update / frealloc (PROBE Q)
    Lifecycle  = 2,  // "lifecycle"   — Phase 6 save/load hooks
    Probe      = 3,  // "probe"       — dev probes (bugsplat_ctor / fopen / loc_dump)
};

// Register a fixed (non-hook_chain) MinHook install into the inventory. Called
// from each install site on SUCCESS — the lifecycle hooks (save_load_hooks),
// the engine self-instrumentation hooks (hooks.cpp: lua_pcall / update /
// frealloc), and the dev probes when armed. hook_chain entries are NOT
// registered here — they are enumerated live from g_chains at LogInventory
// time (the registry is only for installs that lack a central store).
//
// `targetVa` is the resolved runtime VA the detour sits on. `name` points to a
// string literal (process-lifetime) identifying the install (e.g. "lua_pcall",
// "SaveGame", "fopen_override"). Idempotent per (targetVa, category): a repeat
// registration of the same target+category is ignored (probes are idempotent /
// retried; double-registration must not double-count).
//
// Thread-safe: takes the module's own mutex. Safe to call from the worker-
// thread install path.
void RegisterModification(uintptr_t targetVa, Category category, const char* name);

// Emit the live engine-modification inventory. Shared by boot (hooks.cpp
// first-update-tick orchestration, right after everything is applied) and
// save-load start (save_load_hooks.cpp HookedLoadGameWrapper ENTER).
//
// Folds hook_chain::GetAllChainTargets() (the live plugin hooks) together with
// the RegisterModification'd fixed set. Emits, under the "INVENTORY" category:
//   - SUMMARY (always-on, `summaryLevel` — Info at boot/load): per-category
//     counts (plugin_hook / engine / lifecycle / probe) + total + a STABLE,
//     ORDER-INDEPENDENT FINGERPRINT (XOR-rotate fold) over ALL target VAs.
//     Two runs with the same modified set produce the same fingerprint,
//     diffable by eye between builds.
//   - per-target DETAIL (Debug, dev-only): each target VA + category + name.
//
// As a side effect it refreshes the cached pre-formatted summary string
// (LastInventorySummary) so the crash guard can dump it with zero allocation
// from inside the SEH handler. Iterating the registry + g_chains is safe HERE
// (boot / load-start are allocation-safe contexts); the SEH handler must NOT
// iterate them — it reads only the cached string.
void LogInventory(log::Level summaryLevel);

// Returns the cached, pre-formatted inventory summary string last produced by
// LogInventory(). Refreshed at boot and at each save-load start. Safe to read
// from inside an SEH handler: a fixed-size static buffer, no allocation, no
// lock, no iteration. "(inventory not yet captured)" until the first
// LogInventory() call. Process-lifetime static buffer; caller must not free it.
const char* LastInventorySummary();

// Total count of modifications the last LogInventory() folded (hook_chain
// targets + registered fixed installs). Used by the cap-45 self-test to assert
// the inventory is non-empty at boot. Refreshed by LogInventory().
size_t LastTotalModifications();

}  // namespace kcdx::modification_inventory
