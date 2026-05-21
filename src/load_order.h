#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kcdx::load_order {

// ============================================================================
// Load order — zones, sentinels, priority, capability gating.
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
//   3. Capability gating: if the resulting zone is incompatible with the
//      plugin's declared entries (mid-hooks, lua-callback hooks, etc. that
//      can't run before WHGame.dll's DllMain), the engine downgrades to
//      the capability minimum and logs a one-line reason. The plugin
//      still loads; it just gets re-zoned.
//
// See docs/load-order.md for the full model.
// ============================================================================

enum class Zone : uint8_t {
    BeforeGame = 0,  // applied before WHGame.dll DllMain
    AfterGame  = 1,  // applied at first-update-tick (existing path)
};

// Capability-derived minimum zone for a plugin. Computed once at config-load
// time from the plugin's declared [[patch]] / [[hook]] / [[mid_hook]] /
// [[trampoline]] entries.
//
//   BeforeGame — all entries are zone-flexible (pure [[patch]] entries).
//                Plugin may sit in either zone; author hint / user override
//                decides.
//   AfterGame  — at least one entry requires WHGame.dll, MinHook, the
//                JIT branch-pool, or the Lua VM. Plugin MUST sit in the
//                after_game zone; engine refuses requests to move it
//                before WHGame.dll.
enum class MinZone : uint8_t {
    BeforeGame = 0,
    AfterGame  = 1,
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
//   zone     — final zone after capability gating + user override.
//   priority — final priority (author default, overridden by user if set).
//   enabled  — final enabled flag (author default true, overridden by user).
//
// reasonsLogged is for diagnostics: the human-readable lines emitted when
// resolution mutated the user's request (e.g. "downgraded to after_game
// because plugin declares mid-hook entries"). Populated by Resolve();
// already logged at WARN — the field exists so tests / future UI surfaces
// can re-read without re-emitting.
struct Effective {
    Zone        zone        = Zone::AfterGame;
    int         priority    = 50;
    bool        enabled     = true;
    MinZone     minZone     = MinZone::BeforeGame;
    std::string reason;  // populated if the request was mutated
};

// Compute and cache the Effective row for every plugin. Reads:
//   - kcdx::plugins::g_manifests (for author defaults)
//   - kcdx::patch::g_patches, kcdx::hook_engine::g_hooks +
//     g_mid_hooks, kcdx::trampoline_engine::g_trampolines (to derive
//     each plugin's MinZone)
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
// belongs to.
bool IsPluginEnabled(const std::string& pluginName);

// Capability derivation. Walks the entry vectors for `pluginName` and
// returns the strictest zone requirement. Pure read; no side effects.
//
// Capability matrix:
//   [[patch]]     zone-flexible
//   [[hook]]      after_game  (MinHook + JIT branch-pool)
//   [[mid_hook]]  after_game  (MinHook + JIT + Lua VM)
//   [[trampoline]] after_game (JIT branch-pool needs WHGame.dll proximity)
//
// Empty plugin (no entries) → BeforeGame (allowed anywhere).
MinZone DeriveMinZone(const std::string& pluginName);

}  // namespace kcdx::load_order
