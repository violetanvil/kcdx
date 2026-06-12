#include "load_order.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

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
        "# Each [[edge]] records that a plugin SET a named behavior. kcdx\n"
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
                "' in load_order.toml (or use kcdx.behavior's auto-order method "
                "once it lands)"));
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

}  // namespace kcdx::load_order
