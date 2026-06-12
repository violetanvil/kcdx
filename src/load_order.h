#pragma once
#include <climits>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace kcdx::load_order {

// ============================================================================
// Load order — zones, sentinels, priority.
//
// Mental model: one global ordered list of plugins, with one immovable
// sentinel ("game.exe"). The list naturally has two zones:
//
//     [ engine-fix plugins   ]  zone = before_game, priority 0..100
//     [ author / user plugins]  zone = before_game, priority 0..100
//     ─── game.exe ──────────────── (immovable sentinel)
//     [ author / user plugins]  zone = after_game,  priority 0..100
//
// Sort key:
//     (Zone asc, plugin_effective_priority asc, plugin_name asc,
//      Source asc, entry.priority asc, entry.name asc)
//
// Priority semantics:
//   0   = earliest in zone
//   100 = latest in zone
//   50  = middle (default)
//
// Sparse 0..100 range gives the user / author room to insert "definitely
// before X" or "definitely after Y" without renumbering siblings.
//
// Inputs to the effective values for each plugin:
//   1. Author hints from the per-plugin [load_order] table (zone / priority);
//      these populate the manifest's internal defaultPosition / defaultPriority
//      fields, which this module reads.
//   2. (If present) user override from kcdx-engine/load_order.toml.
//
// The declared (zone, priority) stands unconditionally — there is no silent
// re-zoning. In the per-entry-zone execution model a plugin's after-work runs
// from the lua_after / PostGameLoad slot (after_game by construction) and its
// before-work from the lua / Load slot, so a "before_game zone with after-work"
// declaration is no longer a contradiction to downgrade.
//
// See docs/load-order.md for the full model.
// ============================================================================

enum class Zone : uint8_t {
    BeforeGame = 0,  // applied before WHGame.dll DllMain
    AfterGame  = 1,  // applied at first-update-tick (existing path)
};

// One row from kcdx-engine/load_order.toml. The launcher writes this file;
// kcdx reads it at startup. If absent, every plugin uses author defaults.
//
// Missing fields fall back to author defaults from [plugin].default_*.
struct UserOverride {
    std::string name;                 // plugin name, must match [plugin].name exactly
    bool        hasZone     = false;  // true if user specified `zone`
    Zone        zone        = Zone::AfterGame;
    bool        hasPriority = false;  // true if user specified `priority`
    int         priority    = 50;     // 0..100
    bool        hasEnabled  = false;  // true if user specified `enabled`
    bool        enabled     = true;   // false = soft-disable (no folder rename)
};

// Read kcdx-engine/load_order.toml if present. Populates internal state for
// Effective(). Safe to call multiple times — second call replaces prior
// overrides. Idempotent if the file hasn't changed on disk.
//
// loadOrderPath is the full path to load_order.toml. Caller derives via
// kcdx::paths::EngineDataDirPath() / L"load_order.toml". If the path does
// not exist, this is a no-op (every plugin gets author defaults).
//
// Row errors (an unknown key, a missing/empty/wrong-typed `name`, a
// wrong-typed or bad-value `zone`/`priority`, a non-boolean `enabled`) are
// REJECTED at ERROR severity and the offending row is skipped wholesale —
// loud, never silently field-dropped (a silently-ignored `enabled` is the
// 0xC8-bug class: the user's disable intent vanishes with no trace — fail
// loud, never silent-drop). Recognized row keys: name, zone,
// priority, enabled. A single bad row does NOT abort the file — the remaining
// rows still apply (this is the user's override file, not a plugin manifest).
// A whole-file TOML parse error is logged at WARN and the file is skipped
// (author defaults apply to every plugin).
void Read(const std::filesystem::path& loadOrderPath);

// Resolved per-plugin load-order state. Produced by Resolve() and looked up
// at sort time by Of(name).
//
//   zone           — final zone after capability gating + user override.
//   priority       — final priority (author default, overridden by user if
//                    set).
//   userEnabled    — the user's enable choice from kcdx-engine/load_order.toml
//                    (author default true; user `enabled = false` flips it).
//   engineAccepted — engine's accept/reject verdict from zone_gate's
//                    capability/zone evaluation. Always true until
//                    zone_gate's EvaluateAllPlugins runs.
//
// The FINAL gate is `userEnabled && engineAccepted`, exposed via
// IsPluginEnabled(name). Do NOT read these two fields directly to decide
// whether to act on a plugin — go through IsPluginEnabled so a zone_gate
// rejection cannot be bypassed.
struct Effective {
    Zone        zone           = Zone::AfterGame;
    int         priority       = 50;
    bool        userEnabled    = true;
    // Set to false by zone_gate on a capability/zone rejection (see
    // src/zone_gate.h). Always true until zone_gate's EvaluateAllPlugins
    // runs in step 2.
    bool        engineAccepted = true;
    // Secondary ordering key, applied in the sort AFTER priority and BEFORE
    // name. Default INT_MAX, which is a no-op among plugins: every plugin
    // shares the same INT_MAX, so a plugin pair still breaks its priority tie
    // on name exactly as before this field existed. The only rows that carry a
    // finite orderIndex are folded vanilla pak mods ("mods.<modid>" rows), all
    // at zone=after_game priority=0 — there orderIndex preserves the
    // mod_order.txt RELATIVE order (the vanilla baseline) before the name
    // tiebreak. A pak mod absent from mod_order.txt also stays at INT_MAX, so
    // it sorts AFTER the listed ones, then alphabetically by its "mods.<modid>"
    // name. See docs/mod-loader-absorb.md "Load-order".
    int         orderIndex     = INT_MAX;
};

// Compute and cache the Effective row for every plugin AND every discovered
// vanilla pak mod. Reads:
//   - kcdx::plugins::g_manifests (for author defaults)
//   - the load_order.toml state previously populated by Read()
//   - kcdx::mod_absorb::Registry() (the discovered pak mods)
//
// Each pak mod folds into an Effective row keyed "mods.<modId>" at
// zone=after_game, priority=0 (an early after_game block), orderIndex =
// the mod's mod_order.txt line index (the secondary ordering key that keeps
// the vanilla relative order). A user load_order.toml row keyed "mods.<modid>"
// overrides priority/zone/enabled — kcdx owns the resolved order, mod_order.txt
// is the seed. After Resolve, Of("mods.<modId>") / IsPluginEnabled("mods.<modId>")
// work uniformly. The pak-mod <supports> version gate runs SEPARATELY + LATER
// (mod_absorb::ApplyVersionGate, at the point the runtime version is known) and
// flips engineAccepted on these rows.
//
// Call after LoadAllConfigs has populated the entry vectors, after pak-mod
// discovery has populated the registry, AND after load_order::Read() has been
// called (call order: discover + Read → entries parsed → Resolve).
void Resolve();

// Look up the resolved effective state for a plugin by name. If the
// plugin name isn't known (e.g. patch entry from a kcdx.toml with no
// [plugin] table), returns a default Effective(zone=AfterGame,
// priority=50, enabled=true). This is intentional — anonymous patch
// entries land at default position in the after_game zone.
const Effective& Of(const std::string& pluginName);

// True iff plugin `a` sorts BEFORE plugin `b` in the resolved load order
// — the canonical plugin sort key (zone asc, priority asc, orderIndex
// asc, name asc), the SAME key the entrypoint run-order uses
// (lua_plugin_loader::EntrypointRunsBefore, minus the Source/entry
// tiebreak that does not apply to whole plugins). Each name is looked up
// via Of(); an unknown name takes the default Effective row. Used by the
// behavior resolver to tell "the owning declarer loads LATER than you"
// (the reorder error) from "loads earlier" (a typo / failed-load error)
// — design §6's window-law branch discrimination. A strict order: equal
// keys fall through to the name compare, so RunsBefore(x, x) is false.
bool RunsBefore(const std::string& a, const std::string& b);

// True if the named plugin is enabled per the resolved load order.
// Returns the AND of the two underlying inputs on Effective:
// `userEnabled && engineAccepted`. Either input flipping to false
// disables the plugin; a user `enabled = true` cannot force-load a
// zone_gate-rejected plugin (the AND yields false).
//
// Anonymous entries (kcdx.toml with no [plugin] table — pure-patch
// files used historically by mempatch-compatible installs) have
// pluginName == "" and are always enabled. They predate the launcher
// and have no row to toggle.
//
// Every apply path that walks entries — patch, hook, mid_hook,
// trampoline, scan, command registration, Plugin_Load — calls this
// before doing work. A user setting enabled = false on their plugin
// in kcdx-engine/load_order.toml must result in zero side effects
// from that plugin, no matter which engine surface the entry
// belongs to. Likewise a zone_gate rejection flowing through
// `engineAccepted = false` produces zero side effects from that
// plugin.
bool IsPluginEnabled(const std::string& pluginName);

// Writer for Effective.engineAccepted on an existing row. The sole
// intended caller is kcdx::zone_gate::EvaluateAllPlugins(), which flips
// this to false on a capability/zone rejection. No-op if the plugin name
// has no row (anonymous patch entries, unknown names). Resolve() does
// NOT touch this field — every prior call's verdict survives the
// (currently one-call-per-session) Resolve invocation, but since
// zone_gate runs AFTER Resolve, ordering is fine for the v0.2 flow.
void SetEngineAccepted(const std::string& pluginName, bool accepted);

// A snapshot of the FULL resolved load-order state — every Effective row
// (engineAccepted verdicts included) AND the user-override layer. Captured by
// CaptureState(), restored verbatim by RestoreState(). The ONLY intended use is
// a self-test that must drive Resolve()/SetEngineAccepted() against synthetic
// rows and then put the LIVE state back EXACTLY as it was — re-running Resolve()
// alone would NOT restore it (Resolve() resets every engineAccepted to true,
// dropping the zone_gate + pak-mod-version-gate verdicts a normal boot applied).
// Not for production orchestration. The payload is a deep copy, so it survives
// any number of intervening Resolve()/Read() calls.
struct Snapshot {
    std::vector<std::pair<std::string, Effective>>    effective;
    std::vector<std::pair<std::string, UserOverride>> userOverrides;
};
Snapshot CaptureState();
void RestoreState(const Snapshot& snap);

}  // namespace kcdx::load_order
