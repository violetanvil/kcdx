#include "load_order.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "toml.hpp"

#include "behavior_registry.h"  // Edges() — the in-memory set-edges the store persists
#include "log.h"
#include "paths.h"   // ToUtf8 (path -> std::string under C++20+ char8_t)
#include "plugin_loader.h"
#include "mod_absorb/pak_mod_registry.h"

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
        doc = toml::parse_file(kcdx::paths::ToUtf8(loadOrderPath));
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
    // field-wise) — the 0xC8-bug class: a user's enable/disable or
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
               loaded, kcdx::paths::ToUtf8(loadOrderPath).c_str());
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
                auto pathStr = kcdx::paths::ToUtf8(m.tomlPath);
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
        // relocation and is deleted; the author's choice is honored.
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

    // Fold the discovered vanilla pak mods into the SAME Effective map, keyed
    // "mods.<modId>". kcdx owns the resolved order; mod_order.txt is the seed.
    // Each pak mod defaults to zone=after_game, priority=0 (an early after_game
    // block, all pak mods leading the author plugins within after_game), and
    // orderIndex = its mod_order.txt line index (-1 -> INT_MAX -> sorts after
    // the listed mods, then alphabetically by "mods.<modid>"). A user
    // load_order.toml row keyed "mods.<modid>" overrides zone/priority/enabled,
    // exactly as it does for a plugin row.
    size_t pakModRows = 0;
    for (const auto& mod : kcdx::mod_absorb::Registry()) {
        Effective eff;
        eff.zone       = Zone::AfterGame;
        eff.priority   = 0;
        eff.orderIndex = (mod.modOrderIndex >= 0) ? mod.modOrderIndex : INT_MAX;
        bool userEnabled = true;

        const std::string key = kcdx::mod_absorb::LoadOrderNameFor(mod.modId);
        if (auto it = g_userOverrides.find(key); it != g_userOverrides.end()) {
            const auto& ov = it->second;
            if (ov.hasZone)     eff.zone     = ov.zone;
            if (ov.hasPriority) eff.priority = ov.priority;
            if (ov.hasEnabled)  userEnabled  = ov.enabled;
        }
        eff.userEnabled = userEnabled;
        // eff.engineAccepted stays default true — mod_absorb::ApplyVersionGate
        // flips it to false on an Incompatible pak mod, later, once the runtime
        // game version is known (the SAME mechanism zone_gate uses for plugins).

        // A "mods.<modid>" key never collides with a plugin name: [plugin].name
        // is charset [a-z0-9_] (no '.'), so it can never begin "mods.". emplace
        // would no-op a (impossible) collision; use it to be defensive + loud.
        auto inserted = g_effective.emplace(key, std::move(eff));
        if (!inserted.second) {
            log::WarnF("load_order: pak-mod key '%s' already present; the "
                       "earlier row wins (unexpected — a plugin should never "
                       "carry a 'mods.' name)", key.c_str());
        } else {
            ++pakModRows;
        }
    }

    log::InfoF("load_order: resolved %zu plugin(s) + %zu pak mod(s) "
               "(%zu user override row(s) applied)",
               g_effective.size() - pakModRows, pakModRows,
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

bool RunsBefore(const std::string& a, const std::string& b) {
    // The canonical plugin sort key: (zone asc, priority asc, orderIndex
    // asc, name asc) — the same key EntrypointRunsBefore applies (minus the
    // Source/entry tiebreak, which is per-entry, not per-plugin). An unknown
    // name takes the default Effective row via Of(). Name is the final,
    // total tiebreak, so this is a strict order: RunsBefore(x, x) == false.
    const Effective& ea = Of(a);
    const Effective& eb = Of(b);
    if (ea.zone != eb.zone) return ea.zone < eb.zone;
    if (ea.priority != eb.priority) return ea.priority < eb.priority;
    if (ea.orderIndex != eb.orderIndex) return ea.orderIndex < eb.orderIndex;
    return a < b;
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

Snapshot CaptureState() {
    Snapshot snap;
    snap.effective.reserve(g_effective.size());
    for (const auto& kv : g_effective) snap.effective.push_back(kv);
    snap.userOverrides.reserve(g_userOverrides.size());
    for (const auto& kv : g_userOverrides) snap.userOverrides.push_back(kv);
    return snap;
}

void RestoreState(const Snapshot& snap) {
    g_effective.clear();
    for (const auto& kv : snap.effective) g_effective.emplace(kv.first, kv.second);
    g_userOverrides.clear();
    for (const auto& kv : snap.userOverrides) g_userOverrides.emplace(kv.first, kv.second);
}

// ============================================================================
// Behavior dependency edges — persisted store + launch-time re-check + prune.
// I/O mirrors src/mod_absorb/order_persist.cpp EXACTLY (the existing
// launch-time persisted-TOML store): the same file read/write helpers + binary
// trunc write, the same WRITE-IF-CHANGED, the same fail-loud shape (a parse
// error → WARN + skip + rebuild, never a hard fail), the same path derivation
// via kcdx::paths::EngineDataDirPath(). Category tag "BEHAVIOR" (the behavior
// surface's tag — these edges ARE the behavior story; the load_order unit
// merely hosts the store per §11).
// ============================================================================

namespace {

constexpr const char* kEdgeCat = "BEHAVIOR";

// Read a whole file into `out`. false (out empty) iff the file is present but
// unreadable; `existed` distinguishes "absent" (a normal first-run state, true
// return, empty out) from "present-but-unreadable" (false return). Mirrors
// order_persist::ReadFileText.
bool ReadEdgeFileText(const fs::path& path, std::string& out, bool& existed) {
    out.clear();
    std::error_code ec;
    existed = fs::exists(path, ec);
    if (!existed) return true;  // absent -> empty document, not a failure.
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;       // present but unreadable -> failure.
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Write `text` (binary, truncate). false on failure. Mirrors
// order_persist::WriteFileText.
bool WriteEdgeFileText(const fs::path& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return f.good();
}

// The behavior-edge store path: <EngineDataDir>/behavior_edges.toml — derived
// the same way order_persist derives load_order.toml.
fs::path BehaviorEdgesPath() {
    return kcdx::paths::EngineDataDirPath() / L"behavior_edges.toml";
}

// The PRIOR launch's edges, loaded ONCE at boot by
// RecheckBehaviorEdgesAtLaunch and read by PriorLaunchEdgeConfirms (the binder's
// second-launch error upgrade). Immutable for the session after the boot load —
// never this session's own in-flight edges. Function-local static (no
// init-order dependence on other TUs).
std::vector<BehaviorEdge>& PriorLaunchEdges() {
    static std::vector<BehaviorEdge> s;
    return s;
}

// Split a stamped behavior full name `<author>.<plugin>.<bare>` into its
// declarer (author, plugin). Returns false if the name does not have at least
// three dot-segments with a non-empty author + plugin (a catalog
// `kcdx.behavior.<bare>` name splits to author="kcdx" plugin="behavior", which
// is NOT a plugin in the discovered set, so it prunes — see RecheckBehaviorEdges).
bool DeclarerFromBehaviorName(const std::string& fullName,
                              std::string& authorOut, std::string& pluginOut) {
    const size_t d1 = fullName.find('.');
    if (d1 == std::string::npos || d1 == 0) return false;
    const size_t d2 = fullName.find('.', d1 + 1);
    if (d2 == std::string::npos || d2 == d1 + 1) return false;  // empty plugin
    authorOut = fullName.substr(0, d1);
    pluginOut = fullName.substr(d1 + 1, d2 - d1 - 1);
    return true;
}

}  // namespace

std::string SerializeBehaviorEdgesToml(const std::vector<BehaviorEdge>& edges) {
    std::string out =
        "# managed by kcdx — behavior dependency edges (consumer -> behavior).\n"
        "# Each edge entry records that a plugin SET a named behavior. kcdx\n"
        "# re-checks these at the next launch BEFORE plugins run: a consumer\n"
        "# that now loads before its declarer is reported up front. The store\n"
        "# is rebuilt from each launch's observed sets — do not hand-edit; an\n"
        "# edge whose consumer or declarer is no longer installed is dropped.\n";
    for (const BehaviorEdge& e : edges) {
        if (e.consumerPlugin.empty() || e.behaviorFullName.empty()) continue;
        out += "\n[[edge]]\n";
        out += "consumer = \"";
        out += e.consumerAuthor;
        out += ".";
        out += e.consumerPlugin;
        out += "\"\n";
        out += "behavior = \"";
        out += e.behaviorFullName;
        out += "\"\n";
    }
    return out;
}

std::vector<BehaviorEdge> ParseBehaviorEdgesToml(const std::string& text,
                                                 bool* parseFailedOut) {
    if (parseFailedOut) *parseFailedOut = false;
    std::vector<BehaviorEdge> edges;
    if (text.empty()) return edges;

    toml::table doc;
    try {
        doc = toml::parse(text);
    } catch (const toml::parse_error& e) {
        // Whole-file parse error → empty store, the caller WARNs + rebuilds
        // (never a hard fail). Mirrors order_persist's read-fail posture.
        if (parseFailedOut) *parseFailedOut = true;
        log::WarnF("behavior_edges.toml: parse error: %s — the prior edge store "
                   "is ignored this launch and rebuilt at session end (no stale "
                   "edge drives a warn)", e.description().data());
        return edges;
    }

    auto* arr = doc.get("edge");
    if (!arr || !arr->is_array()) {
        // No [[edge]] rows — an empty store (a fresh write, or a hand-emptied
        // file). Not an error.
        return edges;
    }

    for (const auto& elem : *arr->as_array()) {
        if (!elem.is_table()) {
            log::Warn("behavior_edges.toml: [[edge]] entry is not a table; "
                      "skipping this edge (the store rebuilds at session end)");
            continue;
        }
        const auto& t = *elem.as_table();
        auto* consumerNode = t.get("consumer");
        auto* behaviorNode  = t.get("behavior");
        if (!consumerNode || !consumerNode->is_string() ||
            !behaviorNode || !behaviorNode->is_string()) {
            log::Warn("behavior_edges.toml: an [[edge]] is missing a string "
                      "'consumer' or 'behavior'; skipping it (rebuilt at "
                      "session end)");
            continue;
        }
        const std::string consumer = std::string(*consumerNode->value<std::string>());
        const std::string behavior = std::string(*behaviorNode->value<std::string>());

        // consumer = "<author>.<plugin>" — split on the FIRST dot (an author is
        // dot-free per the namespace charset, so the first dot separates author
        // from plugin). A malformed consumer (no dot, empty half) is skipped.
        const size_t cd = consumer.find('.');
        if (cd == std::string::npos || cd == 0 || cd == consumer.size() - 1) {
            log::WarnF("behavior_edges.toml: an [[edge]] has a malformed "
                       "consumer '%s' (expected <author>.<plugin>); skipping it",
                       consumer.c_str());
            continue;
        }
        if (behavior.empty()) continue;

        BehaviorEdge e;
        e.consumerAuthor   = consumer.substr(0, cd);
        e.consumerPlugin   = consumer.substr(cd + 1);
        e.behaviorFullName = behavior;
        edges.push_back(std::move(e));
    }
    return edges;
}

std::vector<RecognizedConflict> RecheckBehaviorEdges(
    const std::vector<BehaviorEdge>& edges,
    const std::function<bool(const std::string&, const std::string&)>&
        isKnownPlugin) {
    std::vector<RecognizedConflict> conflicts;
    for (const BehaviorEdge& e : edges) {
        // Derive the declarer plugin from the behavior name's prefix.
        std::string declAuthor, declPlugin;
        if (!DeclarerFromBehaviorName(e.behaviorFullName, declAuthor, declPlugin)) {
            // Not a 3-segment plugin name (a catalog name, or malformed) — no
            // plugin declarer to order against; prune (no warn, no constraint).
            continue;
        }
        // PRUNE: an edge whose consumer OR declarer is absent from the
        // discovered plugin set is ignored (not loaded, not re-checked). A
        // pruned edge drives no warn and no constraint (design §6).
        if (!isKnownPlugin(e.consumerAuthor, e.consumerPlugin)) continue;
        if (!isKnownPlugin(declAuthor, declPlugin)) continue;

        // RE-CHECK against the CURRENT resolved order. The consumer set the
        // behavior; if the consumer now RunsBefore its declarer, the declarer's
        // declares run AFTER the consumer's set — the reorder violation. (The
        // declarer plugin's load-order key is its [plugin].name, which is the
        // <plugin> component.)
        if (RunsBefore(e.consumerPlugin, declPlugin)) {
            RecognizedConflict c;
            c.consumerAuthor   = e.consumerAuthor;
            c.consumerPlugin   = e.consumerPlugin;
            c.declarerAuthor   = declAuthor;
            c.declarerPlugin   = declPlugin;
            c.behaviorFullName = e.behaviorFullName;
            conflicts.push_back(std::move(c));
        }
    }
    return conflicts;
}

std::vector<RecognizedConflict> RecheckBehaviorEdgesAtLaunch() {
    const fs::path path = BehaviorEdgesPath();
    std::string text;
    bool existed = false;
    if (!ReadEdgeFileText(path, text, existed)) {
        // Present but unreadable — WARN + skip (the store rebuilds at teardown).
        // Never a hard fail (mirrors order_persist's read-fail handling, minus
        // the merge-base concern: this store is rebuilt wholesale, so an
        // unreadable prior file just means "no recognition this launch").
        LOG_WARN_KV(kEdgeCat, "behavior_edges_read_fail",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
            kcdx::log::KV("consequence",
                "behavior_edges.toml exists but could not be read — no "
                "up-front conflict recognition this launch; the store is "
                "rebuilt from this session's observed sets at the apply "
                "boundary"));
        return {};
    }
    if (!existed) return {};  // first run — nothing to recognize yet.

    // A whole-file parse error already WARNed inside ParseBehaviorEdgesToml and
    // yielded an empty set; that path returns no conflicts and rebuilds — so the
    // caller needs no parse-failed flag here (nullptr out-param).
    const std::vector<BehaviorEdge> edges =
        ParseBehaviorEdgesToml(text, nullptr);

    // Cache the prior-launch edges for the binder's second-launch error upgrade
    // (PriorLaunchEdgeConfirms reads this). These are the PRIOR launch's edges,
    // immutable for the session — never this session's in-flight edges.
    PriorLaunchEdges() = edges;

    // The discovered-plugin test: a "<author>.<plugin>" names an INSTALLED
    // plugin iff g_manifests carries it (every discovered plugin — enabled,
    // disabled, or engine-rejected — appears here; the SAME check the behavior
    // binder's FindOwningPlugin uses). An absent consumer/declarer prunes.
    auto isKnownPlugin = [](const std::string& author,
                            const std::string& plugin) -> bool {
        for (const auto& m : kcdx::plugins::g_manifests) {
            if (m.author == author && m.name == plugin) return true;
        }
        return false;
    };

    const std::vector<RecognizedConflict> conflicts =
        RecheckBehaviorEdges(edges, isKnownPlugin);

    for (const RecognizedConflict& c : conflicts) {
        // Up-front recognized-conflict WARN (design §10): name both plugins,
        // the behavior, and the auto-order fix. This fires BEFORE the consumer
        // plugin runs again, so the user learns about the bad order even before
        // the failing set re-raises. The auto-order method is a later step, so
        // the pointer is prose (no call yet).
        LOG_WARN_KV(kEdgeCat, "recognized_stale_edge",
            kcdx::log::KV("behavior", c.behaviorFullName),
            kcdx::log::KV("consumer", c.consumerAuthor + "." + c.consumerPlugin),
            kcdx::log::KV("declarer", c.declarerAuthor + "." + c.declarerPlugin),
            kcdx::log::KV("detail",
                "a prior launch recorded that '" + c.consumerAuthor + "." +
                c.consumerPlugin + "' sets '" + c.behaviorFullName +
                "', but '" + c.consumerPlugin + "' loads BEFORE its declarer '" +
                c.declarerPlugin + "' — the set will fail again this launch. "
                "Move '" + c.consumerAuthor + "." + c.consumerPlugin +
                "' below '" + c.declarerAuthor + "." + c.declarerPlugin +
                "' in load_order.toml (or run the behavior auto-order method, "
                "which computes and writes the corrected order for the next "
                "launch)"));
    }

    if (!conflicts.empty()) {
        LOG_INFO_KV(kEdgeCat, "behavior_edges_rechecked",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
            kcdx::log::KV("recognized", (uint64_t)conflicts.size()),
            kcdx::log::KV("loaded_edges", (uint64_t)edges.size()));
    }
    return conflicts;
}

void PersistBehaviorEdges() {
    // Build the edge set from this session's OBSERVED edges (the in-memory set
    // the behavior surface recorded on every resolved/ordering-failed set). The
    // store is REPLACED wholesale — a consumer that no longer sets a behavior is
    // simply not in this set, so its edge drops automatically (self-invalidation
    // by rebuild, design §6).
    std::vector<BehaviorEdge> edges;
    const auto& observed = kcdx::behavior_registry::Edges();
    edges.reserve(observed.size());
    for (const auto& se : observed) {
        BehaviorEdge e;
        e.consumerAuthor   = se.consumerAuthor;
        e.consumerPlugin   = se.consumerPlugin;
        e.behaviorFullName = se.behaviorFullName;
        edges.push_back(std::move(e));
    }

    const fs::path path = BehaviorEdgesPath();
    const std::string serialized = SerializeBehaviorEdgesToml(edges);

    // No observed edges AND no prior file: nothing to persist (an absent file
    // stays absent — don't write a header-only file on a clean boot with no
    // behavior sets). If a prior file exists, fall through so a now-empty set
    // REPLACES it (a consumer that stopped setting must clear the stale store).
    std::string existing;
    bool existed = false;
    if (!ReadEdgeFileText(path, existing, existed)) {
        // Present but unreadable — we can still WRITE the rebuilt store (the
        // write does not depend on the old content). WARN that we could not diff
        // (the rewrite is unconditional). Mirrors order_persist::WriteModOrderTxt.
        LOG_WARN_KV(kEdgeCat, "behavior_edges_read_fail_prewrite",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
            kcdx::log::KV("consequence",
                "behavior_edges.toml exists but could not be read — rewriting "
                "with this session's observed edges unconditionally (could not "
                "compare to detect an actual change)"));
        existing.clear();
    }
    if (edges.empty() && !existed) {
        LOG_DEBUG_KV(kEdgeCat, "behavior_edges_persist_skip",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
            kcdx::log::KV("reason",
                "no behavior edges observed this session and no prior store — "
                "nothing to persist (file left absent)"));
        return;
    }

    if (serialized == existing) {
        LOG_DEBUG_KV(kEdgeCat, "behavior_edges_persist_skip",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
            kcdx::log::KV("reason",
                "unchanged — this session's observed edges match the on-disk "
                "store"));
        return;
    }

    if (!WriteEdgeFileText(path, serialized)) {
        LOG_ERROR_KV(kEdgeCat, "behavior_edges_persist_write_fail",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
            kcdx::log::KV("edge_count", (uint64_t)edges.size()),
            kcdx::log::KV("consequence",
                "could not write behavior_edges.toml — this session's behavior "
                "dependency edges were NOT persisted; the next launch cannot "
                "recognize a known bad order up front (the set still fails loud "
                "at its call site, so nothing is silently wrong)"));
        return;
    }

    LOG_INFO_KV(kEdgeCat, "behavior_edges_persist_write",
        kcdx::log::KV("file", kcdx::paths::ToUtf8(path)),
        kcdx::log::KV("edge_count", (uint64_t)edges.size()));
}

bool PriorLaunchEdgeConfirms(const std::string& consumerAuthor,
                             const std::string& consumerPlugin,
                             const std::string& behaviorFullName) {
    for (const BehaviorEdge& e : PriorLaunchEdges()) {
        if (e.consumerAuthor == consumerAuthor &&
            e.consumerPlugin == consumerPlugin &&
            e.behaviorFullName == behaviorFullName) {
            return true;
        }
    }
    return false;
}

bool PriorLaunchEdgeForBare(const std::string& consumerAuthor,
                            const std::string& consumerPlugin,
                            const std::string& bareName,
                            std::string& fullNameOut) {
    for (const BehaviorEdge& e : PriorLaunchEdges()) {
        if (e.consumerAuthor != consumerAuthor ||
            e.consumerPlugin != consumerPlugin) {
            continue;
        }
        // The bare component is the last dot-segment of the stamped full name.
        const size_t lastDot = e.behaviorFullName.rfind('.');
        const std::string bare = (lastDot == std::string::npos)
            ? e.behaviorFullName
            : e.behaviorFullName.substr(lastDot + 1);
        if (bare == bareName) {
            fullNameOut = e.behaviorFullName;
            return true;
        }
    }
    return false;
}

// Test-only seam (declared in load_order.h) — set the prior-launch edge cache
// directly so a self-test can exercise PriorLaunchEdgeConfirms (and the binder's
// upgrade) without a file. Lives here so the cache static stays private to this
// TU; production populates the cache via RecheckBehaviorEdgesAtLaunch's boot read.
void SetPriorLaunchEdgesForTest(const std::vector<BehaviorEdge>& edges) {
    PriorLaunchEdges() = edges;
}

// ============================================================================
// The auto-order method — passive, callable order correction (design §6/§11/§12,
// Phase 9.5 s7). The pure core (ComputeAutoOrder) + the pure upsert
// (SerializeAutoOrderUpsert) take every input as a parameter (no global state,
// no file I/O), so the s7 self-test drives them from literals (headless-testable
// / design §14). The file-touching wrappers (UpsertPriorityRows, ApplyAutoOrder)
// reuse the load_order.toml read/write/write-if-changed/fail-loud shape from
// src/mod_absorb/order_persist.cpp (the existing TOML write precedent) — they do
// NOT invent a parallel writer.
// ============================================================================

AutoOrderResult ComputeAutoOrder(
    const std::vector<BehaviorEdge>& edges,
    const std::vector<std::string>& currentOrder,
    const std::function<bool(const std::string&, const std::string&)>&
        isKnownPlugin,
    const std::function<int(const std::string&)>& currentPriorityOf) {
    AutoOrderResult result;

    // Index each plugin by its current position so the topo sort can break ties
    // by current order (the stable / minimal-displacement property: among nodes
    // with no remaining constraint, the earliest-in-currentOrder is emitted
    // first, so an unconstrained plugin never jumps an unconstrained sibling).
    std::unordered_map<std::string, size_t> indexOf;
    indexOf.reserve(currentOrder.size());
    for (size_t i = 0; i < currentOrder.size(); ++i) {
        indexOf.emplace(currentOrder[i], i);
    }

    // Build the constraint graph: a directed edge declarer -> consumer means the
    // declarer must be emitted BEFORE the consumer. Derive the declarer plugin
    // from each behavior's <author>.<plugin> prefix and PRUNE exactly as
    // RecheckBehaviorEdges does — an edge whose consumer or declarer is absent
    // from the discovered set, or whose behavior is a catalog name (no plugin
    // declarer), cannot constrain an order. Both endpoints must also be present
    // in currentOrder (an edge naming a plugin not in the order to be sorted is
    // not a constraint over that order).
    std::unordered_map<std::string, std::vector<std::string>> successors;  // declarer -> [consumers]
    std::unordered_map<std::string, int> inDegree;
    for (const std::string& name : currentOrder) inDegree[name] = 0;

    // Dedup identical (declarer, consumer) constraints so a repeated edge does
    // not inflate the in-degree (which would falsely look like a cycle).
    std::unordered_set<std::string> seenConstraint;
    for (const BehaviorEdge& e : edges) {
        std::string declAuthor, declPlugin;
        if (!DeclarerFromBehaviorName(e.behaviorFullName, declAuthor, declPlugin)) {
            continue;  // catalog name / malformed — no plugin declarer to order.
        }
        if (!isKnownPlugin(e.consumerAuthor, e.consumerPlugin)) continue;
        if (!isKnownPlugin(declAuthor, declPlugin)) continue;

        const std::string& consumer = e.consumerPlugin;  // the load-order key.
        const std::string& declarer = declPlugin;
        if (consumer == declarer) continue;  // self-edge — never a constraint.
        if (!indexOf.count(consumer) || !indexOf.count(declarer)) continue;

        const std::string key = declarer + "\x1f" + consumer;
        if (!seenConstraint.insert(key).second) continue;  // already counted.

        successors[declarer].push_back(consumer);
        inDegree[consumer] += 1;
    }

    // Stable Kahn's algorithm: repeatedly emit the AVAILABLE node (in-degree 0)
    // that is EARLIEST in currentOrder. A min over the current-order index keeps
    // unconstrained nodes in their current relative position (minimal
    // displacement) while still pulling a declarer ahead of its consumer.
    std::vector<std::string> corrected;
    corrected.reserve(currentOrder.size());
    std::unordered_set<std::string> emitted;
    while (corrected.size() < currentOrder.size()) {
        // Find the earliest-in-current-order node with in-degree 0, not yet
        // emitted. (currentOrder is the canonical iteration order, so this scan
        // is itself stable — first match wins.)
        const std::string* pick = nullptr;
        for (const std::string& name : currentOrder) {
            if (emitted.count(name)) continue;
            if (inDegree[name] != 0) continue;
            pick = &name;
            break;
        }
        if (!pick) {
            // No available node but nodes remain → a CYCLE. The unresolved
            // members (still in-degree > 0, or only reachable through one) are
            // the cycle (and its tail). Report them; produce no order.
            result.verdict = AutoOrderVerdict::Cycle;
            for (const std::string& name : currentOrder) {
                if (!emitted.count(name)) result.cycleMembers.push_back(name);
            }
            return result;
        }
        const std::string chosen = *pick;  // copy — currentOrder outlives, but be safe.
        corrected.push_back(chosen);
        emitted.insert(chosen);
        auto it = successors.find(chosen);
        if (it != successors.end()) {
            for (const std::string& succ : it->second) {
                inDegree[succ] -= 1;
            }
        }
    }

    // corrected[] now satisfies every constraint. If it equals currentOrder, the
    // order was already correct — no change.
    if (corrected == currentOrder) {
        result.verdict = AutoOrderVerdict::NoChange;
        return result;
    }

    // Translate the corrected SEQUENCE into priority rows with MINIMAL
    // displacement: keep each plugin's CURRENT priority wherever doing so already
    // preserves the corrected order under RunsBefore's (priority, name) tiebreak,
    // and bump ONLY a plugin that would otherwise sort out of place. A row is
    // emitted ONLY for a plugin whose priority actually changed — an unmoved row
    // keeps its current priority and gets no row.
    //
    // Walk corrected[] in order, tracking a `floor`: the priority the next plugin
    // must NOT sort before. For plugin P at corrected[i], with prev = corrected
    // [i-1]:
    //   - If P's current priority > floor, P already sorts strictly after prev on
    //     priority — keep it; advance floor to P's current priority.
    //   - If P's current priority == floor AND P's name sorts after prev's name,
    //     P sorts after prev on the name tiebreak — keep it; floor unchanged.
    //   - Otherwise P would sort at/before prev — BUMP P to floor + 1 (the
    //     smallest value that orders it strictly after, preserving sparse room)
    //     and record the move; advance floor to the bumped value.
    // The first plugin keeps its current priority (nothing precedes it). The
    // bump is clamped to 100; if floor reaches 100, ties past that still break on
    // name in corrected order (the sort is total via the name key).
    result.verdict = AutoOrderVerdict::Reordered;
    result.correctedOrder = corrected;
    int floor = INT_MIN;            // priority the current plugin must beat (or tie+name).
    std::string prevName;          // the previously-emitted plugin's name.
    bool havePrev = false;
    for (const std::string& name : corrected) {
        const int cur = currentPriorityOf(name);
        int assigned = cur;
        if (!havePrev) {
            assigned = cur;                       // first plugin — nothing precedes it.
        } else if (cur > floor) {
            assigned = cur;                       // already strictly after on priority.
        } else if (cur == floor && prevName < name) {
            assigned = cur;                       // ties prev, but name orders it after.
        } else {
            assigned = (floor < 100) ? floor + 1 : 100;  // bump to order after prev.
        }
        if (assigned != cur) {
            AutoOrderMove mv;
            mv.pluginName  = name;
            mv.newPriority = assigned;
            mv.oldPriority = cur;
            result.moved.push_back(std::move(mv));
        }
        floor    = assigned;
        prevName = name;
        havePrev = true;
    }

    // If the corrected SEQUENCE differs from currentOrder yet no priority row
    // changed (every needed move was already expressible at the same priority,
    // resolved by the name tiebreak), there is nothing to write — treat as
    // NoChange so the apply does not churn the file. (The corrected order still
    // holds via the name tiebreak.)
    if (result.moved.empty()) {
        result.verdict = AutoOrderVerdict::NoChange;
    }
    return result;
}

std::string SerializeAutoOrderUpsert(const std::string& existingText,
                                     const std::vector<AutoOrderMove>& moves) {
    // Map name -> new priority for O(1) lookup while scanning rows.
    std::unordered_map<std::string, int> wantPriority;
    for (const AutoOrderMove& mv : moves) wantPriority[mv.pluginName] = mv.newPriority;

    // Scan the existing body line by line, tracking the current [[plugin]] row's
    // name. When we leave a row whose name is a move target, ensure its priority
    // line carries the new value (rewrite an existing one, or insert one). Names
    // we successfully upsert in place are removed from `wantPriority`; whatever
    // remains is appended as a fresh row at the end.
    //
    // The line scan mirrors order_persist::ExistingRowNames' bare-scan posture
    // (NOT a toml++ round-trip — that would drop the file's comments + the
    // per-row human-name comments). We only touch the `priority` field of a
    // named row; every other byte is preserved verbatim.
    std::istringstream in(existingText);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) lines.push_back(line);

    auto trim = [](const std::string& s) -> std::string {
        size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
        return s.substr(b, e - b);
    };
    // Extract the double-quoted value after `name =`. Returns "" if not a name line.
    auto nameOfLine = [&](const std::string& raw) -> std::string {
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#') return "";
        if (t.rfind("name", 0) != 0) return "";
        size_t eq = t.find('=', 4);
        if (eq == std::string::npos) return "";
        if (!trim(t.substr(4, eq - 4)).empty()) return "";  // "named"/"namespace"
        size_t q1 = t.find('"', eq + 1);
        if (q1 == std::string::npos) return "";
        size_t q2 = t.find('"', q1 + 1);
        if (q2 == std::string::npos) return "";
        return t.substr(q1 + 1, q2 - q1 - 1);
    };
    auto isTableHeader = [&](const std::string& raw) -> bool {
        const std::string t = trim(raw);
        return !t.empty() && t[0] == '[';
    };
    auto isPriorityLine = [&](const std::string& raw) -> bool {
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#') return false;
        if (t.rfind("priority", 0) != 0) return false;
        size_t eq = t.find('=', 8);
        if (eq == std::string::npos) return false;
        return trim(t.substr(8, eq - 8)).empty();
    };

    // First pass: find each move-target row's line span [rowStart, rowEnd) and
    // whether it already has a priority line. We rewrite in a copy.
    std::vector<std::string> out;
    out.reserve(lines.size() + moves.size() * 2);

    size_t i = 0;
    while (i < lines.size()) {
        const std::string& cur = lines[i];
        const std::string rowName = nameOfLine(cur);
        auto want = (rowName.empty()) ? wantPriority.end()
                                      : wantPriority.find(rowName);
        if (want == wantPriority.end()) {
            out.push_back(cur);
            ++i;
            continue;
        }

        // This is a move-target row's name line. Emit the name line, then walk
        // the rest of the row (until the next table header / EOF), rewriting an
        // existing priority line or inserting one before the row ends.
        out.push_back(cur);
        const int newPrio = want->second;
        wantPriority.erase(want);  // consumed in place.
        ++i;

        bool wrotePriority = false;
        // Collect the row's body lines (up to the next table header), so we can
        // insert a priority line at the row's end if it had none.
        std::vector<std::string> body;
        while (i < lines.size() && !isTableHeader(lines[i])) {
            if (isPriorityLine(lines[i])) {
                body.push_back("priority  = " + std::to_string(newPrio) +
                               "   # set by kcdx auto-order");
                wrotePriority = true;
            } else {
                body.push_back(lines[i]);
            }
            ++i;
        }
        if (!wrotePriority) {
            // Insert a priority line at the END of the row body (after the last
            // non-blank body line, before any trailing blank that separates rows).
            size_t insertAt = body.size();
            while (insertAt > 0 && trim(body[insertAt - 1]).empty()) --insertAt;
            body.insert(body.begin() + insertAt,
                        "priority  = " + std::to_string(newPrio) +
                        "   # set by kcdx auto-order");
        }
        for (const std::string& bl : body) out.push_back(bl);
    }

    // Reassemble, preserving the original trailing-newline shape (join with '\n',
    // and add a final newline iff the input had one — or if the input was empty
    // and we are about to append a fresh row).
    std::string result;
    for (size_t k = 0; k < out.size(); ++k) {
        result += out[k];
        result += '\n';
    }
    // If the original had NO trailing newline, the loop added one extra; that is
    // harmless (a TOML body is newline-tolerant) and matches order_persist's
    // append posture (which also normalizes to a trailing newline before
    // appending). Now append a fresh [[plugin]] row for any move target that had
    // no existing row.
    if (!wantPriority.empty()) {
        // Deterministic append order: follow the `moves` input order.
        if (!result.empty() && result.back() != '\n') result += '\n';
        for (const AutoOrderMove& mv : moves) {
            auto it = wantPriority.find(mv.pluginName);
            if (it == wantPriority.end()) continue;
            result += "\n[[plugin]]\n";
            result += "name      = \"";
            result += mv.pluginName;
            result += "\"\n";
            result += "priority  = " + std::to_string(mv.newPriority) +
                      "   # set by kcdx auto-order\n";
            wantPriority.erase(it);
        }
    }
    return result;
}

bool UpsertPriorityRows(const fs::path& loadOrderPath,
                        const std::vector<AutoOrderMove>& moves) {
    if (moves.empty()) return true;  // nothing to upsert.

    std::string existing;
    bool existed = false;
    if (!ReadEdgeFileText(loadOrderPath, existing, existed)) {
        // Present but unreadable — fail LOUD, do NOT blind-overwrite (the same
        // posture order_persist::WriteLoadOrderToml takes: never clobber a file
        // we could not read, the user's hand-edits would be lost).
        LOG_ERROR_KV(kEdgeCat, "auto_order_write_read_fail",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(loadOrderPath)),
            kcdx::log::KV("consequence",
                "load_order.toml exists but could not be read — the auto-order "
                "priority rows were NOT written; the corrected order was not "
                "persisted (the user's order is unchanged)"));
        return false;
    }

    const std::string merged = SerializeAutoOrderUpsert(existing, moves);
    if (merged == existing) {
        LOG_DEBUG_KV(kEdgeCat, "auto_order_write_skip",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(loadOrderPath)),
            kcdx::log::KV("reason",
                "unchanged — the corrected priority rows already match disk"));
        return true;
    }

    if (!WriteEdgeFileText(loadOrderPath, merged)) {
        LOG_ERROR_KV(kEdgeCat, "auto_order_write_fail",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(loadOrderPath)),
            kcdx::log::KV("rows", (uint64_t)moves.size()),
            kcdx::log::KV("consequence",
                "could not write load_order.toml — the auto-order corrected "
                "priorities were NOT persisted; the bad order remains and the "
                "next launch's set will fail again at its call site"));
        return false;
    }

    LOG_INFO_KV(kEdgeCat, "auto_order_write",
        kcdx::log::KV("file", kcdx::paths::ToUtf8(loadOrderPath)),
        kcdx::log::KV("rows", (uint64_t)moves.size()));
    return true;
}

AutoOrderResult ApplyAutoOrder() {
    AutoOrderResult result;

    // 1. Read the SAME persisted edge store the launch re-check consumes.
    const fs::path edgePath = BehaviorEdgesPath();
    std::string text;
    bool existed = false;
    if (!ReadEdgeFileText(edgePath, text, existed)) {
        LOG_WARN_KV(kEdgeCat, "auto_order_edges_read_fail",
            kcdx::log::KV("file", kcdx::paths::ToUtf8(edgePath)),
            kcdx::log::KV("consequence",
                "behavior_edges.toml exists but could not be read — auto-order "
                "has no constraints to satisfy; nothing reordered"));
        result.verdict = AutoOrderVerdict::NoChange;
        return result;
    }
    if (!existed) {
        LOG_INFO_KV(kEdgeCat, "auto_order_no_edges",
            kcdx::log::KV("reason",
                "no persisted behavior_edges.toml — no ordering constraints to "
                "satisfy (nothing to reorder)"));
        result.verdict = AutoOrderVerdict::NoChange;
        return result;
    }
    const std::vector<BehaviorEdge> edges = ParseBehaviorEdgesToml(text, nullptr);

    // 2. Build the current order from the live resolved state: every discovered
    //    plugin, sorted by the canonical RunsBefore key. (Pak-mod "mods.<modid>"
    //    rows carry no behaviors, so they are never edge endpoints; restricting
    //    to plugin manifests keeps the constraint domain to behavior-declaring
    //    plugins.)
    std::vector<std::string> currentOrder;
    currentOrder.reserve(kcdx::plugins::g_manifests.size());
    for (const auto& m : kcdx::plugins::g_manifests) {
        if (!m.name.empty()) currentOrder.push_back(m.name);
    }
    std::sort(currentOrder.begin(), currentOrder.end(),
              [](const std::string& a, const std::string& b) {
                  return RunsBefore(a, b);
              });

    auto isKnownPlugin = [](const std::string& author,
                            const std::string& plugin) -> bool {
        for (const auto& m : kcdx::plugins::g_manifests) {
            if (m.author == author && m.name == plugin) return true;
        }
        return false;
    };
    auto currentPriorityOf = [](const std::string& plugin) -> int {
        return Of(plugin).priority;
    };

    // 3. Compute (pure).
    result = ComputeAutoOrder(edges, currentOrder, isKnownPlugin, currentPriorityOf);

    // 4. Act on the verdict.
    if (result.verdict == AutoOrderVerdict::Cycle) {
        // A cycle is REPORTED, never silently broken. Name the members; write
        // nothing (the user-owned order is untouched). This is a teaching report.
        std::string members;
        for (size_t i = 0; i < result.cycleMembers.size(); ++i) {
            if (i) members += " <-> ";
            members += result.cycleMembers[i];
        }
        LOG_WARN_KV(kEdgeCat, "auto_order_cycle",
            kcdx::log::KV("members", members),
            kcdx::log::KV("detail",
                "the recorded behavior dependencies form a CYCLE (each of these "
                "plugins must load both before and after another in the set) — "
                "no single order satisfies them all. The load order was left "
                "UNCHANGED (never silently broken). Resolve the circular "
                "dependency between these plugins, then re-run auto-order"));
        return result;
    }
    if (result.verdict == AutoOrderVerdict::NoChange) {
        LOG_INFO_KV(kEdgeCat, "auto_order_no_change",
            kcdx::log::KV("reason",
                "the current load order already satisfies every recorded "
                "behavior dependency — nothing to reorder"));
        return result;
    }

    // verdict == Reordered: apply through the load_order.toml write-back.
    const fs::path loadOrderPath =
        kcdx::paths::EngineDataDirPath() / L"load_order.toml";
    const bool wrote = UpsertPriorityRows(loadOrderPath, result.moved);
    if (!wrote) {
        // The write failed (logged ERROR inside). The computed order stands in
        // the result for the caller, but it did NOT persist. Surface via the
        // verdict's report; the caller decides (the live button shows an error).
        LOG_WARN_KV(kEdgeCat, "auto_order_apply_not_persisted",
            kcdx::log::KV("moved", (uint64_t)result.moved.size()),
            kcdx::log::KV("consequence",
                "the corrected order was computed but could NOT be written to "
                "load_order.toml (see the prior ERROR) — it will not take effect "
                "next launch"));
        return result;
    }
    LOG_INFO_KV(kEdgeCat, "auto_order_applied",
        kcdx::log::KV("moved", (uint64_t)result.moved.size()),
        kcdx::log::KV("detail",
            "computed a corrected load order satisfying the recorded behavior "
            "dependencies and wrote the priority rows to load_order.toml — the "
            "corrected order takes effect at the NEXT launch (this session's "
            "order was already consumed at boot)"));
    return result;
}

}  // namespace kcdx::load_order
