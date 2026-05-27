#include "load_order.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string_view>
#include <unordered_map>

#include "toml.hpp"

#include "log.h"
#include "plugin_loader.h"

namespace fs = std::filesystem;

namespace kcdx::load_order {

namespace {

// Parsed contents of load_order.toml. Built by Read(), consumed by Resolve().
// Keyed by plugin name (the [plugin].name from the plugin's kcdx.toml).
std::unordered_map<std::string, UserOverride> g_userOverrides;

// Output of Resolve(). Keyed by plugin name. Looked up by Of().
std::unordered_map<std::string, Effective> g_effective;

// Sentinel returned by Of() for unknown plugin names (anonymous patch
// entries, etc.). Lives forever so the returned reference is safe.
const Effective& DefaultEffective() {
    static const Effective kDefault{};  // zone=AfterGame, priority=50, userEnabled=true, engineAccepted=true
    return kDefault;
}

bool ParseZoneString(const std::string& s, Zone& out, std::string& err) {
    if (s == "before_game") { out = Zone::BeforeGame; return true; }
    if (s == "after_game")  { out = Zone::AfterGame;  return true; }
    err = "unknown zone '" + s + "' (expected 'before_game' or 'after_game')";
    return false;
}

const char* ZoneName(Zone z) {
    return z == Zone::BeforeGame ? "before_game" : "after_game";
}

}  // namespace

void Read(const fs::path& loadOrderPath) {
    g_userOverrides.clear();

    if (!fs::exists(loadOrderPath)) {
        // Quiet path: no user override file is the common case. The
        // engine just uses author defaults for every plugin.
        return;
    }

    toml::table doc;
    try {
        doc = toml::parse_file(loadOrderPath.u8string());
    } catch (const toml::parse_error& e) {
        log::WarnF("load_order.toml: parse error at %s: %s",
                   e.source().path ? e.source().path->c_str() : "<input>",
                   e.description().data());
        return;
    }

    auto* arr = doc.get("plugin");
    if (!arr || !arr->is_array()) {
        // Empty / no-op file. Author defaults apply to everything.
        log::Info("load_order.toml: present but no [[plugin]] rows; "
                  "using author defaults for every plugin");
        return;
    }

    // Recognized keys in a kcdx-engine/load_order.toml [[plugin]] row. The
    // launcher writes this file; a hand-edit with a typo'd key or wrong-typed
    // field previously dropped SILENTLY (a non-boolean `enabled` fell through
    // the `is_boolean()` guard; a wrong-typed zone/priority was ignored
    // field-wise) — the 0xC8-bug class, AP14: a user's enable/disable or
    // ordering intent vanished with no trace. STRICT posture (flipped from
    // WARN to Error): a bad row is REJECTED loudly and skipped wholesale; the
    // remaining rows still apply (this is the user's override file, not a
    // plugin manifest — one bad row must not nuke every other override).
    static constexpr std::array<std::string_view, 4> kRowKeys = {
        "name", "zone", "priority", "enabled",
    };

    size_t loaded = 0;
    for (const auto& elem : *arr->as_array()) {
        if (!elem.is_table()) {
            log::Error("load_order.toml: [[plugin]] entry is not a table; "
                       "rejecting this row");
            continue;
        }
        const auto& t = *elem.as_table();

        // Unknown-key rejection: name the offending key + reject the row.
        bool rowBad = false;
        for (const auto& [keyNode, valNode] : t) {
            std::string_view k = keyNode.str();
            bool known = false;
            for (std::string_view rk : kRowKeys) {
                if (rk == k) { known = true; break; }
            }
            if (!known) {
                log::ErrorF("load_order.toml: [[plugin]] row has unknown key "
                            "'%s' (recognized: name, zone, priority, enabled); "
                            "rejecting this row", std::string(k).c_str());
                rowBad = true;
                break;
            }
        }
        if (rowBad) continue;

        UserOverride ov;
        auto* nameNode = t.get("name");
        if (!nameNode || !nameNode->is_string()) {
            log::Error("load_order.toml: [[plugin]] row missing required "
                       "'name' (string); rejecting this row");
            continue;
        }
        ov.name = std::string(*nameNode->value<std::string>());
        if (ov.name.empty()) {
            log::Error("load_order.toml: [[plugin]] row with empty 'name'; "
                       "rejecting this row");
            continue;
        }

        // zone: present-but-wrong-type or bad value → REJECT the row (was a
        // field-level WARN-and-ignore that silently kept the row's other
        // fields, discarding the author's zone intent without saying so).
        if (auto* z = t.get("zone")) {
            if (!z->is_string()) {
                log::ErrorF("load_order.toml: plugin '%s': zone has wrong type "
                            "(expected string); rejecting this row",
                            ov.name.c_str());
                continue;
            }
            std::string s = std::string(*z->value<std::string>());
            std::string zErr;
            if (!ParseZoneString(s, ov.zone, zErr)) {
                log::ErrorF("load_order.toml: plugin '%s': zone: %s; "
                            "rejecting this row", ov.name.c_str(), zErr.c_str());
                continue;
            }
            ov.hasZone = true;
        }

        // priority: wrong-type or out-of-range → REJECT the row.
        if (auto* p = t.get("priority")) {
            if (!p->is_integer()) {
                log::ErrorF("load_order.toml: plugin '%s': priority has wrong "
                            "type (expected integer 0..100); rejecting this row",
                            ov.name.c_str());
                continue;
            }
            int prio = static_cast<int>(*p->value<int64_t>());
            if (prio < 0 || prio > 100) {
                log::ErrorF("load_order.toml: plugin '%s': priority %d out "
                            "of range (0..100); rejecting this row",
                            ov.name.c_str(), prio);
                continue;
            }
            ov.priority = prio;
            ov.hasPriority = true;
        }

        // enabled: present-but-non-boolean → REJECT the row (was a silent drop
        // — a non-boolean `enabled` fell through is_boolean() and the user's
        // disable intent vanished, the canonical 0xC8 bug shape).
        if (auto* en = t.get("enabled")) {
            if (!en->is_boolean()) {
                log::ErrorF("load_order.toml: plugin '%s': enabled has wrong "
                            "type (expected boolean true/false); rejecting "
                            "this row", ov.name.c_str());
                continue;
            }
            ov.enabled = *en->value<bool>();
            ov.hasEnabled = true;
        }

        auto inserted = g_userOverrides.emplace(ov.name, std::move(ov));
        if (!inserted.second) {
            log::WarnF("load_order.toml: plugin '%s' listed more than "
                       "once; later row wins", inserted.first->first.c_str());
            inserted.first->second = std::move(ov);
        } else {
            ++loaded;
        }
    }

    log::InfoF("load_order.toml: loaded %zu user override row(s) from %s",
               loaded, loadOrderPath.u8string().c_str());
}

void Resolve() {
    g_effective.clear();

    for (const auto& m : kcdx::plugins::g_manifests) {
        Effective eff;

        // Step 1: derive starting zone from author hint + source.
        //
        //   default_position set explicitly  → honor it
        //   empty + engine builtin            → before_game (engine fixes lead)
        //   empty + user plugin               → after_game  (safe default)
        Zone authorZone;
        if (m.defaultPosition == "before_game") {
            authorZone = Zone::BeforeGame;
        } else if (m.defaultPosition == "after_game") {
            authorZone = Zone::AfterGame;
        } else {
            // Empty → derive from source: engine builtins lead
            // (before_game), user plugins default to after_game. We
            // classify via the manifest's tomlPath; engine builtins live
            // under <game-bin>/kcdx-engine/builtin/.
            bool isEngineBuiltin = false;
            {
                auto pathStr = m.tomlPath.u8string();
                // Cheap substring check. The two discovery roots are
                // <game-bin>/kcdx-engine/builtin/ (engine fixes) and
                // <game-bin>/kcdx-plugins/ (user). "kcdx-engine" only
                // appears in the engine-root path, so that suffices.
                if (pathStr.find("kcdx-engine") != std::string::npos &&
                    pathStr.find("builtin") != std::string::npos) {
                    isEngineBuiltin = true;
                }
            }
            authorZone = isEngineBuiltin ? Zone::BeforeGame : Zone::AfterGame;
        }

        int authorPriority = m.defaultPriority;
        bool userEnabled = true;

        // Step 2: user override (load_order.toml) wins over author hint
        // when present.
        Zone requestedZone     = authorZone;
        int  requestedPriority = authorPriority;
        if (auto it = g_userOverrides.find(m.name); it != g_userOverrides.end()) {
            const auto& ov = it->second;
            if (ov.hasZone)     requestedZone     = ov.zone;
            if (ov.hasPriority) requestedPriority = ov.priority;
            if (ov.hasEnabled)  userEnabled       = ov.enabled;
        }

        // Step 3: the user's / author's declared (zone, priority) stands
        // unconditionally — no silent relocation. In the per-entry-zone
        // execution model a plugin's after-work goes in the lua_after slot
        // (which runs after_game by construction) and its before-work in the
        // lua/before slot, so "before_game zone but has after-work" is no
        // longer a contradiction to downgrade. The old full-plugin downgrade
        // (before_game → after_game at priority 50 with a warn) was a silent
        // relocation (AP13) and is deleted; the author's choice is honored.
        Zone finalZone     = requestedZone;
        int  finalPriority = requestedPriority;

        eff.zone        = finalZone;
        eff.priority    = finalPriority;
        eff.userEnabled = userEnabled;
        // eff.engineAccepted stays at its default true — zone_gate writes it
        // (false on rejection) in step 2 of the zone_gate feature; no other
        // code path touches it.

        g_effective.emplace(m.name, std::move(eff));
    }

    log::InfoF("load_order: resolved %zu plugin(s) "
               "(%zu user override row(s) applied)",
               g_effective.size(),
               g_userOverrides.size());

    // Dev-grade dump of the resolved order so authors / users can
    // verify their load_order.toml took effect. One line per plugin.
    for (const auto& m : kcdx::plugins::g_manifests) {
        const auto& eff = Of(m.name);
        // Print the FINAL gate (userEnabled AND engineAccepted) — what every
        // other reader honors and what determines whether the plugin actually
        // loads. Reading the raw user input alone would hide a zone_gate
        // rejection from the dump.
        log::InfoF("  %s: zone=%s priority=%d enabled=%s",
                   m.name.c_str(),
                   ZoneName(eff.zone),
                   eff.priority,
                   IsPluginEnabled(m.name) ? "true" : "false");
    }
}

const Effective& Of(const std::string& pluginName) {
    auto it = g_effective.find(pluginName);
    if (it == g_effective.end()) return DefaultEffective();
    return it->second;
}

bool IsPluginEnabled(const std::string& pluginName) {
    // Anonymous entries (pure-patch kcdx.toml with no [plugin] table)
    // have no row in load_order.toml and no way to toggle. Treat as
    // always enabled — they predate the launcher's toggle UI.
    if (pluginName.empty()) return true;
    // Final gate: user choice AND engine acceptance. Either being false
    // disables the plugin; a user enabled=true cannot force-load a
    // zone_gate-rejected plugin.
    const auto& e = Of(pluginName);
    return e.userEnabled && e.engineAccepted;
}

void SetEngineAccepted(const std::string& pluginName, bool accepted) {
    // zone_gate is the sole intended caller. Anonymous (empty-name)
    // entries have no Effective row to mutate; an unknown name is
    // likewise silently ignored — zone_gate only iterates manifests,
    // so a miss here would be an internal bug, not user input.
    if (pluginName.empty()) return;
    auto it = g_effective.find(pluginName);
    if (it == g_effective.end()) return;
    it->second.engineAccepted = accepted;
}

}  // namespace kcdx::load_order
