#include "zone_gate.h"

#include <string>
#include <unordered_map>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "load_order.h"
#include "log.h"
#include "plugin_loader.h"  // kcdx::plugins::g_manifests

namespace kcdx::zone_gate {

// ============================================================================
// Capability table — every shipped kcdx.* API the engine exposes.
//
// Per docs/outstanding-work/restructure-plan.md §"Capability gating" (the
// authoritative table at lines 147-160), every API in this engine version
// is RequireZone::Either — they all work from both before_game and
// after_game. Deferred-registration handles the "called early but work
// must happen later" cases (e.g. kcdx.command queues until pConsole, the
// Lua-callback variant of kcdx.hook waits until the VM is up post-FIX-A).
// The author calls the API at any time; the engine ensures the work
// happens when it CAN happen. No author-visible timing knob.
//
// One synthetic test-only row — `kcdx.zone_gate_test_after_only` — is
// included so step 4's test plugin can exercise the rejection path
// (declaring a before_game plugin that "calls" this API, watching the
// gate flip engineAccepted=false). It exists purely so the gate has
// something to gate on; without it, the rejection branch in
// EvaluateAllPlugins is unreachable from any test today. Step 3 wires
// the matching Lua C function (a no-op stub on the registered surface)
// so the test plugin can call it.
//
// REMOVE the synthetic row (or convert it to RequireZone::Either) the
// day a real shipped API genuinely requires non-Either. The capability
// table is the single source of truth — drift it by either flipping a
// real row's value or removing the synthetic; never both.
// ============================================================================
const ApiCapability kCapabilities[] = {
    { "kcdx.bytes",                       RequireZone::Either },
    { "kcdx.hook",                        RequireZone::Either },
    { "kcdx.code",                        RequireZone::Either },
    { "kcdx.command",                     RequireZone::Either },
    { "kcdx.cosave",                      RequireZone::Either },
    { "kcdx.on",                          RequireZone::Either },
    { "kcdx.publish",                     RequireZone::Either },
    { "kcdx.scan",                        RequireZone::Either },
    // Synthetic test-only entry — see header comment above. The name is
    // not a real binding; the test plugin uses it as an exemplar of an
    // After-required API to confirm the gate logic rejects a
    // before_game plugin.
    { "kcdx.zone_gate_test_after_only",   RequireZone::After  },
};
const size_t kCapabilitiesCount =
    sizeof(kCapabilities) / sizeof(kCapabilities[0]);

namespace {

// Reason text per rejected plugin. Owned by zone_gate; consulted by the
// 5 init-site skip-logs via RejectReason() so each can distinguish
// engine-reject from user-disabled cause.
std::unordered_map<std::string, std::string> g_rejected;

const std::string& EmptyString() {
    static const std::string kEmpty;
    return kEmpty;
}

const char* ZoneName(load_order::Zone z) {
    return z == load_order::Zone::BeforeGame ? "before_game" : "after_game";
}

const char* RequireZoneName(RequireZone rz) {
    switch (rz) {
        case RequireZone::Before: return "before_game";
        case RequireZone::After:  return "after_game";
        default:                  return "either";
    }
}

// Decide whether `pluginZone` makes an API requiring `requireZone`
// unreachable. Either-rows never reject. Otherwise the plugin's declared
// zone must match the API's required zone exactly.
bool ZoneRejects(load_order::Zone pluginZone, RequireZone requireZone) {
    switch (requireZone) {
        case RequireZone::Before:
            return pluginZone != load_order::Zone::BeforeGame;
        case RequireZone::After:
            return pluginZone != load_order::Zone::AfterGame;
        default:
            return false;
    }
}

}  // namespace

CheckResult Check(const std::string& pluginName) {
    const auto& eff = load_order::Of(pluginName);
    const load_order::Zone pluginZone = eff.zone;

    // First mismatch wins. The teaching log line only needs ONE example
    // to surface the fix — the author edits the manifest, re-runs, and
    // sees the next mismatch (if any) on the next launch. Listing every
    // mismatch in one line buries the actionable advice in noise.
    for (size_t i = 0; i < kCapabilitiesCount; ++i) {
        const ApiCapability& cap = kCapabilities[i];
        if (cap.requireZone == RequireZone::Either) continue;
        if (!ZoneRejects(pluginZone, cap.requireZone)) continue;

        std::string reason;
        reason += "declared zone='";
        reason += ZoneName(pluginZone);
        reason += "' but calls ";
        reason += cap.name;
        reason += " (requires zone='";
        reason += RequireZoneName(cap.requireZone);
        reason += "' in kcdx 0.2.0)";
        return { false, std::move(reason) };
    }

    return { true, std::string() };
}

void EvaluateAllPlugins() {
    g_rejected.clear();

    size_t evaluated = 0;
    size_t rejected  = 0;

    for (const auto& m : kcdx::plugins::g_manifests) {
        // Skip user-disabled plugins. No point evaluating one the user
        // turned off, and emitting PLUGIN_REJECTED for it would be
        // misleading — the cause is user choice, not the gate.
        if (!load_order::Of(m.name).userEnabled) continue;

        ++evaluated;
        CheckResult r = Check(m.name);
        if (r.ok) continue;

        ++rejected;

        // kcdx.plugin.is_rejected is queried cross-plugin per
        // naming-namespaces.md (explicit `<author>.<plugin>` form);
        // zone_gate's g_rejected map keys on that same form so the
        // lookup matches. load_order's g_effective stays bare-keyed
        // (different module, internal state, not author-facing).
        const std::string fullName = m.author + "." + m.name;

        g_rejected.emplace(fullName, r.reason);
        load_order::SetEngineAccepted(m.name, false);

        // Loud (Error) and one line. The next sentence is the FIX — the
        // author should know exactly what to edit and where. Shape per
        // docs/outstanding-work/restructure-plan.md:165-168. The plugin
        // name is rendered in the 2-dot `<author>.<plugin>` form to
        // match the shape authors query with via kcdx.plugin.is_rejected
        // (consistency between the log line and the lookup key).
        LOG_ERROR("ZONE_GATE",
                  "plugin '%s' rejected: %s. Change [load_order].zone "
                  "to '%s' in this plugin's kcdx.toml to fix. Plugin "
                  "will not load until manifest is fixed.",
                  fullName.c_str(),
                  r.reason.c_str(),
                  // The fix is to move to the zone the FIRST-rejecting
                  // API requires. ZoneRejects only fires when the
                  // plugin's zone is the OPPOSITE of the API's required
                  // zone, so the actionable zone is the inverse of the
                  // plugin's current one. Computing it from the reason
                  // string is fragile; recompute from current state.
                  load_order::Of(m.name).zone ==
                      load_order::Zone::BeforeGame
                          ? "after_game"
                          : "before_game");
    }

    LOG_INFO("ZONE_GATE",
             "zone_gate: evaluated %zu plugin(s), %zu rejected",
             evaluated, rejected);
}

bool IsRejected(const std::string& name) {
    return g_rejected.find(name) != g_rejected.end();
}

const std::string& RejectReason(const std::string& name) {
    auto it = g_rejected.find(name);
    if (it == g_rejected.end()) return EmptyString();
    return it->second;
}

// ============================================================================
// Synthetic test-only Lua surface — `kcdx.zone_gate_test_after_only`
//
// A pure no-op. Registered ONLY because the synthetic capability row
// `kCapabilities[...]{ "kcdx.zone_gate_test_after_only", After }` names
// it: the capability table is the gate's source of truth, and the
// shipped Lua surface must agree with it so the name is callable rather
// than a phantom. The gate's rejection path runs BEFORE plugin.lua, so
// in the After-required / before_game case this function is never
// actually invoked — its presence is a consistency contract, not a
// runtime requirement.
//
// Top-level placement (NOT `kcdx.<domain>.<verb>`) is intentional: the
// capability table keys on the exact author-facing string, and the
// matching string here is `kcdx.zone_gate_test_after_only` (one dot —
// the engine seed form, lua-api-surface.md rule 5 / naming-namespaces.md
// "1 dot — kcdx.<seedname>"). It is the only top-level entry on the
// kcdx table outside the closed-set core verbs, and it is INTERNAL /
// test-only — gets NO author-facing doc entry; this comment + the
// header comment on the kCapabilities row are its only documentation.
//
// REMOVE this function (and the matching capability row) the day the
// synthetic capability entry goes. The two ship and retire together.
// ============================================================================
static int Lua_ZoneGateTestAfterOnly(lua_State* L) {
    (void)L;
    return 0;  // zero results — pure no-op
}

void bind(lua_State* L) {
    // The kcdx table is at the top of the Lua stack on entry (set up by
    // lua_bind.cpp::RegisterKcdxTable). Register the synthetic no-op
    // directly on it as a top-level field (NOT inside a sub-table) so
    // the registered Lua name matches the capability-table string
    // exactly: `kcdx.zone_gate_test_after_only`. Stack effect: 0.
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_ZoneGateTestAfterOnly);
    lua_setfield(L, kcdx_idx, "zone_gate_test_after_only");
}

}  // namespace kcdx::zone_gate
