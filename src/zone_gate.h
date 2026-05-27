#pragma once

#include <cstddef>
#include <string>

extern "C" {
#include "lua.h"
}

// ============================================================================
// zone_gate — capability/zone evaluation
//
// Each kcdx.* API the engine ships has a `requireZone` annotation declaring
// which load-order zone (before_game vs after_game) the API can legally be
// called from. zone_gate cross-references EVERY plugin's resolved zone
// (already populated by load_order::Resolve()) against the engine's static
// capability table and decides whether the plugin's declared zone makes any
// `requireZone` API unreachable.
//
// On rejection, the plugin's Effective.engineAccepted is flipped to false.
// IsPluginEnabled(name) returns `userEnabled && engineAccepted`, so every
// gate site (the 5 plugin-init paths + the 2 runtime readers in ldr_notify
// and lua_registry) naturally skips a rejected plugin without code change.
//
// Wired into src/config.cpp::LoadAllConfigs immediately AFTER
// load_order::Resolve() and BEFORE any plugin-init path (C++ Preload/Load,
// Lua plugin.lua) runs. By that ordering, a rejection prevents any
// registration from happening — no half-loaded plugin state.
//
// The check is STATIC — it does NOT inspect what each plugin actually calls
// (we don't have that info pre-init). It asks: "given this engine version's
// capability table, would any zone-requiring API be illegal to call from
// this plugin's declared zone?" In a world where every shipped API is
// `Either`, only a `Before`- or `After`-required entry can trip the gate.
//
// See the capability-gating design for details.
// ============================================================================

namespace kcdx::zone_gate {

// Which zone(s) an API is legal to call from.
//
//   Either = the API works from before_game AND after_game.
//   Before = the API requires the calling plugin's zone be before_game;
//            an after_game plugin is rejected.
//   After  = the API requires the calling plugin's zone be after_game;
//            a before_game plugin is rejected.
enum class RequireZone {
    Either = 0,
    Before = 1,
    After  = 2,
};

// One row in the static capability table. `name` is the author-facing
// kcdx.* spelling (used in the rejection log line, so it teaches the
// author which API forced the verdict).
struct ApiCapability {
    const char* name;
    RequireZone requireZone;
};

// The engine's static capability table. EVERY shipped API has a row;
// today they are all Either. A synthetic test-only entry
// (`kcdx.zone_gate_test_after_only`) is included so the test plugin can
// exercise the rejection path. See the .cpp for the comment on removing
// the synthetic the day a real API genuinely requires non-Either.
extern const ApiCapability kCapabilities[];
extern const size_t        kCapabilitiesCount;

// Outcome of Check() for one plugin.
//   ok     = true  → the plugin's declared zone is legal against every row.
//   ok     = false → at least one row is unreachable; reason is the
//                    teaching string emitted in the PLUGIN_REJECTED log
//                    line, and the plugin's engineAccepted flips false.
struct CheckResult {
    bool        ok;
    std::string reason;
};

// Run the static capability check against ONE plugin. Reads the plugin's
// resolved zone via load_order::Of(name).zone. Iterates kCapabilities;
// for each row whose requireZone != Either, decides whether the plugin's
// zone would make that API unreachable; the FIRST mismatch wins (the
// teaching error only needs ONE example to surface the fix). Returns
// {ok=true, reason=""} on no mismatch.
CheckResult Check(const std::string& pluginName);

// Evaluate every plugin known to kcdx::plugins::g_manifests. Skips
// user-disabled plugins (no point gating a plugin the user turned off,
// and we don't want to emit PLUGIN_REJECTED for one). For every other
// plugin, runs Check(); on a non-ok result, records the reason in the
// internal rejected map, calls load_order::SetEngineAccepted(name, false),
// and emits the PLUGIN_REJECTED log line.
//
// Called once from src/config.cpp::LoadAllConfigs immediately after
// load_order::Resolve(). Safe-to-call-once contract — no idempotence
// guarantee (the load path runs this exactly once per session).
void EvaluateAllPlugins();

// Record a PARSE-TIME manifest rejection (config.cpp's ParsePluginManifest
// false-return path + the LoadOneFile [kcdx]-table early rejects). A
// parse-rejected plugin returns from LoadOneFile BEFORE it ever registers
// into g_manifests, so EvaluateAllPlugins never sees it and the zone-gate
// g_rejected map cannot record it — yet kcdx.plugin.is_rejected must still
// report it as rejected (otherwise a validation reject is indistinguishable
// from a clean load — a silent accept). This is the ADDITIONAL source the
// is_rejected accessor folds in alongside g_rejected; zone_gate's own
// EvaluateAllPlugins path is untouched.
//
//   `folderPath`     — the plugin's folder path (parent of its kcdx.toml).
//                      ALWAYS present + stable; the internal key for an
//                      identity-malformed reject that has no usable name.
//   `authorPluginKey`— the "<author>.<plugin>" 2-dot key when a VALID author
//                      + name were already parsed at reject time (the reject
//                      is on a DIFFERENT key/table/type), so the reject is
//                      ALSO queryable by name via is_rejected("author.plugin").
//                      Empty when no valid identity was parsable (the reject
//                      hit before/at the identity reads) — folder key only.
//   `reason`         — the teaching `err` string config.cpp already produced.
//
// Idempotent-ish: a later call for the same key overwrites (a folder can only
// reject once per load, so this never collides in practice).
void RecordParseReject(const std::string& folderPath,
                       const std::string& authorPluginKey,
                       const std::string& reason);

// True iff this name was rejected this session — by zone_gate's evaluation
// OR by a parse-time manifest reject (RecordParseReject). Used by the 5
// init-site skip-logs to distinguish engine-reject from user-disabled cause,
// and by kcdx.plugin.is_rejected. `name` is the 2-dot "<author>.<plugin>"
// form (or, for the parse-reject internal lookup, a folder path).
bool IsRejected(const std::string& name);

// The teaching reason string recorded for a rejected plugin (zone_gate's
// EvaluateAllPlugins OR a parse-time RecordParseReject), or a static empty
// string if the plugin wasn't rejected. Safe to call on any name — returns
// the empty string for unknown / accepted plugins so callers can branch on
// `.empty()`.
const std::string& RejectReason(const std::string& name);

// Register the zone_gate-owned Lua surface on the kcdx global table at
// the top of the Lua stack. Stack effect: 0. Called from
// lua_bind.cpp::RegisterKcdxTable alongside the other domain binders.
//
// Today this registers exactly ONE function — the synthetic test-only
// no-op `kcdx.zone_gate_test_after_only`, present so step 4's test
// plugin has a real Lua function paired with the synthetic capability
// row in kCapabilities. The capability table is the gate's source of
// truth; keeping the registered surface in sync with that table is
// what this hook is for. INTERNAL / test-only — NOT documented in
// docs/lua/ and NOT in the closed-set core verbs.
//
// REMOVE this bind() (or repurpose it for a real shipped API) the day
// the synthetic capability row goes — the table and the binder ship
// and retire together.
void bind(lua_State* L);

}  // namespace kcdx::zone_gate
