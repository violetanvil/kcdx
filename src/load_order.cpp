#include "load_order.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "toml.hpp"

#include "hook_engine.h"
#include "log.h"
#include "patch_engine.h"
#include "plugin_loader.h"
#include "trampoline_engine.h"

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
    static const Effective kDefault{};  // zone=AfterGame, priority=50, enabled=true
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

    size_t loaded = 0;
    for (const auto& elem : *arr->as_array()) {
        if (!elem.is_table()) continue;
        const auto& t = *elem.as_table();

        UserOverride ov;
        auto* nameNode = t.get("name");
        if (!nameNode || !nameNode->is_string()) {
            log::Warn("load_order.toml: [[plugin]] row missing required "
                      "'name' (string); skipping row");
            continue;
        }
        ov.name = std::string(*nameNode->value<std::string>());
        if (ov.name.empty()) {
            log::Warn("load_order.toml: [[plugin]] row with empty 'name'; "
                      "skipping row");
            continue;
        }

        if (auto* z = t.get("zone"); z && z->is_string()) {
            std::string s = std::string(*z->value<std::string>());
            std::string zErr;
            if (!ParseZoneString(s, ov.zone, zErr)) {
                log::WarnF("load_order.toml: plugin '%s': zone: %s; "
                           "ignoring this field for this row",
                           ov.name.c_str(), zErr.c_str());
            } else {
                ov.hasZone = true;
            }
        }

        if (auto* p = t.get("priority"); p && p->is_integer()) {
            int prio = static_cast<int>(*p->value<int64_t>());
            if (prio < 0 || prio > 100) {
                log::WarnF("load_order.toml: plugin '%s': priority %d out "
                           "of range (0..100); ignoring this field for "
                           "this row", ov.name.c_str(), prio);
            } else {
                ov.priority = prio;
                ov.hasPriority = true;
            }
        }

        if (auto* en = t.get("enabled"); en && en->is_boolean()) {
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

MinZone DeriveMinZone(const std::string& pluginName) {
    if (pluginName.empty()) {
        // Anonymous entries (kcdx.toml with no [plugin] table) are
        // pure-patch by construction — hooks / mid-hooks / trampolines
        // can only attach to a plugin name via [plugin].name. Allow
        // them in either zone.
        return MinZone::BeforeGame;
    }

    for (const auto& h : kcdx::hook_engine::g_hooks) {
        if (h.pluginName == pluginName) return MinZone::AfterGame;
    }
    for (const auto& m : kcdx::hook_engine::g_mid_hooks) {
        if (m.pluginName == pluginName) return MinZone::AfterGame;
    }
    for (const auto& t : kcdx::trampoline_engine::g_trampolines) {
        if (t.pluginName == pluginName) return MinZone::AfterGame;
    }
    // Patches are zone-flexible; their presence alone doesn't gate.
    return MinZone::BeforeGame;
}

void Resolve() {
    g_effective.clear();

    for (const auto& m : kcdx::plugins::g_manifests) {
        Effective eff;

        // Step 1: capability minimum. This is the floor; the resolved
        // zone can never be below this.
        eff.minZone = DeriveMinZone(m.name);

        // Step 2: derive starting zone from author hint + source.
        //
        //   default_position set explicitly  → honor it (subject to capability gate)
        //   empty + Source::Engine            → before_game (engine fixes lead)
        //   empty + capability flexible       → after_game  (safe default for users)
        //   empty + capability requires after → after_game  (forced by capability)
        Zone authorZone;
        if (m.defaultPosition == "before_game") {
            authorZone = Zone::BeforeGame;
        } else if (m.defaultPosition == "after_game") {
            authorZone = Zone::AfterGame;
        } else {
            // Empty → derive. Find this plugin's source via the
            // matching kcdx.toml in g_patches/g_hooks/etc. — simpler
            // path: engine builtins are stamped Source::Engine on
            // their entries. We approximate via the manifest's
            // tomlPath; engine builtins live under
            // <game-bin>/kcdx-engine/builtin/.
            //
            // We don't need a perfect classifier here — capability
            // gating below will still bump after_game-required
            // plugins back into the right zone.
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
        bool enabled = true;

        // Step 3: user override (load_order.toml) wins over author hint
        // when present.
        Zone requestedZone     = authorZone;
        int  requestedPriority = authorPriority;
        if (auto it = g_userOverrides.find(m.name); it != g_userOverrides.end()) {
            const auto& ov = it->second;
            if (ov.hasZone)     requestedZone     = ov.zone;
            if (ov.hasPriority) requestedPriority = ov.priority;
            if (ov.hasEnabled)  enabled           = ov.enabled;
        }

        // Step 4: the user's / author's declared (zone, priority) stands
        // unconditionally — no silent relocation. In the per-entry-zone
        // execution model a plugin's after-work goes in the lua_after slot
        // (which runs after_game by construction) and its before-work in the
        // lua/before slot, so "before_game zone but has after-work" is no
        // longer a contradiction to downgrade. The old full-plugin downgrade
        // (before_game → after_game at priority 50 with a warn) was a silent
        // relocation (AP13) and is deleted; the author's choice is honored.
        Zone finalZone     = requestedZone;
        int  finalPriority = requestedPriority;

        eff.zone     = finalZone;
        eff.priority = finalPriority;
        eff.enabled  = enabled;

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
        log::InfoF("  %s: zone=%s priority=%d enabled=%s%s",
                   m.name.c_str(),
                   ZoneName(eff.zone),
                   eff.priority,
                   eff.enabled ? "true" : "false",
                   eff.reason.empty() ? "" :
                       (" (" + eff.reason + ")").c_str());
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
    return Of(pluginName).enabled;
}

}  // namespace kcdx::load_order
