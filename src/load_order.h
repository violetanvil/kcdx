#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
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
//   1. Author hints from [plugin].default_position / default_priority.
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
// Parser errors (malformed TOML, unknown fields, out-of-range priority,
// invalid zone string) are logged at WARN. A bad row is dropped; remaining
// rows still apply.
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
};

// Compute and cache the Effective row for every plugin. Reads:
//   - kcdx::plugins::g_manifests (for author defaults)
//   - the load_order.toml state previously populated by Read()
//
// Call after LoadAllConfigs has populated the entry vectors AND
// load_order::Read() has been called (call order: Read → entries
// parsed → Resolve).
void Resolve();

// Look up the resolved effective state for a plugin by name. If the
// plugin name isn't known (e.g. patch entry from a kcdx.toml with no
// [plugin] table), returns a default Effective(zone=AfterGame,
// priority=50, enabled=true). This is intentional — anonymous patch
// entries land at default position in the after_game zone.
const Effective& Of(const std::string& pluginName);

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

}  // namespace kcdx::load_order
