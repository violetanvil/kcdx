#pragma once
#include <climits>
#include <cstdint>
#include <filesystem>
#include <functional>
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

// ============================================================================
// Behavior dependency edges — persisted, self-invalidating store (design §6,
// Phase 9.5 s6). A behavior consumer→declarer edge is recorded in-memory each
// time a `set` resolves (or fails on ordering) at the behavior surface
// (behavior_registry::Edges()); this unit OWNS persisting that set across
// launches so a known bad order is recognized UP FRONT at the next launch,
// before any plugin executes — and (a later step's) auto-order method reads
// the same edges. The store lives in the load_order unit because §11 places
// edge persistence + the (future) auto-order + write-back here (the unit that
// owns order computation).
//
// The store is `kcdx-engine/behavior_edges.toml` (the path fixed at build,
// derived via kcdx::paths::EngineDataDirPath(), mirroring order_persist's
// load_order.toml derivation). Its I/O mirrors order_persist exactly: a parse
// error is WARN + skip + rebuild (never a hard fail), a write failure is a loud
// ERROR, an absent file is a normal first-run state.
// ============================================================================

// One persisted behavior dependency edge: a consumer plugin
// (`<author>.<plugin>`) set a behavior whose full stamped name is
// `behaviorFullName`. The DECLARER plugin is derivable from the behavior name's
// `<author>.<plugin>` prefix (the first two dot-segments); a catalog name
// (`kcdx.behavior.<bare>`) has no plugin declarer and is never recorded as an
// edge (the behavior surface records edges only on the prefixed branches).
struct BehaviorEdge {
    std::string consumerAuthor;
    std::string consumerPlugin;
    std::string behaviorFullName;  // the stamped <author>.<plugin>.<bare>
};

// A recognized stale edge at launch: a persisted edge whose consumer now loads
// BEFORE its declarer in the CURRENT resolved order (the reorder violation), so
// the consumer's set will fail again this launch. Returned by
// RecheckBehaviorEdgesAtLaunch for the up-front WARN.
struct RecognizedConflict {
    std::string consumerAuthor;
    std::string consumerPlugin;
    std::string declarerAuthor;
    std::string declarerPlugin;
    std::string behaviorFullName;
};

// ---- Pure (re)serialization — factored out so the self-test drives them from
// literals with NO file I/O (mirrors order_persist's pure serializers). ----

// Serialize the session's OBSERVED behavior edges to behavior_edges.toml body
// text: a leading "# managed by kcdx" comment block, then one [[edge]] table
// per edge (consumer = "<author>.<plugin>", behavior = "<full name>"). The
// store is REBUILT from each launch's observed set, so this is a full
// replacement, never a merge — a consumer that no longer sets a behavior simply
// is not in `edges` and so drops its row automatically.
std::string SerializeBehaviorEdgesToml(const std::vector<BehaviorEdge>& edges);

// Parse behavior_edges.toml body text back into edges. A whole-file parse error
// yields an EMPTY vector (the caller treats it as no prior store — WARN + skip +
// rebuild, never a hard fail). A malformed individual [[edge]] table (missing/
// wrong-typed field, an un-splittable consumer / behavior name) is skipped with
// a WARN; the remaining edges still load. `parseFailedOut` (optional) is set
// true ONLY on a whole-file parse error (so the caller can WARN the right
// reason); a per-row skip leaves it false.
std::vector<BehaviorEdge> ParseBehaviorEdgesToml(const std::string& text,
                                                 bool* parseFailedOut = nullptr);

// The PRUNE + RE-CHECK core (pure — drives off the resolved order already in
// this unit's caches): for each persisted edge, prune it if its consumer OR its
// declarer is absent from the discovered plugin set (IsKnownPlugin); for a
// surviving edge whose consumer now RunsBefore its declarer, emit a
// RecognizedConflict. `discovered` answers "is `<author>.<plugin>` an installed
// plugin?" (the binder's g_manifests check, injected so this stays testable);
// returns the recognized conflicts (the up-front-WARN set). A pruned edge drives
// NO conflict and NO constraint.
std::vector<RecognizedConflict> RecheckBehaviorEdges(
    const std::vector<BehaviorEdge>& edges,
    const std::function<bool(const std::string& author,
                             const std::string& plugin)>& isKnownPlugin);

// ---- Live boot/teardown entry points (the file-touching wrappers). ----

// LAUNCH re-check — called AFTER load_order::Resolve() + the pak-mod version
// gate, BEFORE any plugin script executes (the seam co-located with
// order_persist::PersistResolvedOrder in the boot sequence). Reads the prior
// behavior_edges.toml, prunes edges whose consumer/declarer is absent from the
// discovered plugin set, and logs each surviving recognized conflict (a
// consumer now loading before its declarer) UP FRONT at WARN — naming both
// plugins, the behavior, and the auto-order pointer (design §10). A parse error
// is WARN + skip (the store rebuilds at teardown). Returns the recognized
// conflicts (for the self-test); the live caller uses it only for the warn.
std::vector<RecognizedConflict> RecheckBehaviorEdgesAtLaunch();

// TEARDOWN / end-of-session WRITE — called AFTER the apply boundary
// (behavior_registry::RunApplyBoundary), once the session's observed edges are
// final. Serializes behavior_registry::Edges() to behavior_edges.toml,
// REPLACING the prior file (the store is rebuilt per launch — a dropped-consumer
// edge vanishes by not being in this launch's observed set). Write-if-changed
// (a steady-state boot writes nothing) + fail-loud on an I/O error.
void PersistBehaviorEdges();

// Did a PRIOR launch record that this consumer set this behavior? Consulted by
// the behavior binder's resolution-error branches for the §6 "second-launch
// error upgrade": a persisted edge from a prior launch CONFIRMS the
// consumer→declarer→behavior relation, so a branch-1 (reorder) or bare-name
// error may name the behavior confidently instead of using the first-launch
// (calibrated-to-what-the-engine-knows) wording. Reads the in-memory snapshot
// of the prior store that RecheckBehaviorEdgesAtLaunch loaded at boot — NOT the
// file (one launch-time read, no per-set I/O). Returns false on a first launch
// (no prior store) or a miss. The set this checks is the PRIOR launch's
// (loaded at boot, immutable for the session) — never this session's own
// in-flight edges, so it answers "has this exact edge been seen across a prior
// launch boundary?" truthfully.
bool PriorLaunchEdgeConfirms(const std::string& consumerAuthor,
                             const std::string& consumerPlugin,
                             const std::string& behaviorFullName);

// Bare-name variant for the §6 bare-name error upgrade: did a PRIOR launch
// record that this consumer set a behavior whose BARE component (the last
// dot-segment of the stamped full name) equals `bareName`? On a hit, fills
// `fullNameOut` with the recorded full `<author>.<plugin>.<bare>` name (so the
// upgraded error can name the declarer the prior launch saw) and returns true.
// This is what lets a bare-name set — which carries no prefix to discriminate
// with on a first launch — name its declarer confidently from the second
// launch. Returns false on a first launch or a miss.
bool PriorLaunchEdgeForBare(const std::string& consumerAuthor,
                            const std::string& consumerPlugin,
                            const std::string& bareName,
                            std::string& fullNameOut);

// Test-only: seed the prior-launch edge cache directly (no file), so a self-test
// can drive PriorLaunchEdgeConfirms + the binder's second-launch upgrade. The
// SOLE intended caller is the load_order edge self-test; production populates
// the cache via RecheckBehaviorEdgesAtLaunch's boot read.
void SetPriorLaunchEdgesForTest(const std::vector<BehaviorEdge>& edges);

// ============================================================================
// The auto-order method — passive, callable order correction (design §6/§11/§12,
// Phase 9.5 s7). PASSIVE: it NEVER fires on its own — no boot-sequence call site,
// no console command, no UI. The ONLY callers are the engine-internal seam (a
// future pre-launch launcher button) + the s7 self-test (which drives the pure
// core from synthetic literals per headless-testable). The rejected "active
// auto-reorder" — the engine silently mutating the user-owned order — is design
// §12's rejected option and is NOT built; the method runs only when CALLED.
//
// What it does: reads the SAME persisted behavior edges the launch re-check
// consumes (consumer must sort AFTER its declarer), computes a corrected order
// that satisfies every surviving constraint with MINIMAL DISPLACEMENT of
// unrelated rows (a stable topological sort — an unconstrained plugin keeps its
// current relative position), DETECTS a cycle (a constraint set with no valid
// order) and REPORTS it (never silently breaks it into an arbitrary order), and
// APPLIES the corrected order by writing load_order.toml priority rows that
// load_order::Read consumes at the NEXT launch (the current session's order is
// already consumed at boot — an apply takes effect next launch).
// ============================================================================

// The verdict of an auto-order computation.
enum class AutoOrderVerdict : uint8_t {
    NoChange = 0,  // the current order already satisfies every edge — nothing to do.
    Reordered = 1, // a corrected order was computed (and, for ApplyAutoOrder, applied).
    Cycle = 2,     // a constraint cycle was detected — REPORTED, no order produced.
};

// One plugin's position in a corrected order: its name + the new priority the
// apply step writes for it. Only plugins whose priority CHANGED appear in
// AutoOrderResult::moved (minimal displacement — an unmoved plugin gets no row).
struct AutoOrderMove {
    std::string pluginName;   // the [plugin].name (the load-order key + row name).
    int         newPriority;  // 0..100, the corrected position within its zone.
    int         oldPriority;  // the priority before the move (for the report).
};

// The result of ComputeAutoOrder — inspectable by the caller (the self-test
// reads it; the apply wrapper acts on it).
struct AutoOrderResult {
    AutoOrderVerdict verdict = AutoOrderVerdict::NoChange;
    // verdict == Reordered: the corrected full order (every plugin name, in the
    // satisfying sequence) + the subset that actually moved (a priority change).
    std::vector<std::string> correctedOrder;  // every input plugin, corrected.
    std::vector<AutoOrderMove> moved;         // only the rows whose priority changed.
    // verdict == Cycle: the plugins forming the constraint cycle (>= 2 members),
    // named in the report. correctedOrder/moved are empty (no order is produced).
    std::vector<std::string> cycleMembers;
};

// ---- Pure computation core (no file I/O, no global state) — the self-test
// drives it from literal edges + a literal current order, mirroring how the s6
// RecheckBehaviorEdges core is factored pure (design §14 / headless-testable). ----

// Compute a corrected order from `edges` over `currentOrder` (the plugin names
// in their CURRENT resolved sequence — typically the load_order::RunsBefore
// order). `isKnownPlugin(author, plugin)` answers "is this an installed plugin?"
// — an edge whose consumer OR declarer is absent (or whose behavior is a catalog
// name with no plugin declarer) is PRUNED (it cannot constrain), reusing the
// SAME prune rule as RecheckBehaviorEdges. `currentPriorityOf(plugin)` returns a
// plugin's current priority (0..100) so a moved plugin's new priority is derived
// and an unmoved plugin's row is omitted.
//
// Constraint: for each surviving edge consumer->declarer, the declarer must sort
// BEFORE the consumer in the corrected order (the consumer SETS a behavior the
// declarer DECLARES, so the declarer must run first). The corrected order is a
// STABLE topological sort of the constraint graph: among nodes with no remaining
// constraint between them, the one earliest in `currentOrder` is emitted first,
// so an unconstrained plugin never moves relative to another unconstrained one
// (minimal displacement). A CYCLE (no valid order) yields verdict=Cycle with the
// unresolved members; nothing is reordered.
//
// PURE: no file I/O, no global-state read — every input is a parameter, so the
// self-test exercises every branch from literals.
AutoOrderResult ComputeAutoOrder(
    const std::vector<BehaviorEdge>& edges,
    const std::vector<std::string>& currentOrder,
    const std::function<bool(const std::string& author,
                             const std::string& plugin)>& isKnownPlugin,
    const std::function<int(const std::string& plugin)>& currentPriorityOf);

// ---- The file-touching apply wrapper (the thin shell over the pure core). ----

// Compute the corrected order from the persisted edges + the live resolved order
// and APPLY it by writing load_order.toml priority rows for the moved plugins.
// Reads the prior behavior_edges.toml (the SAME store RecheckBehaviorEdgesAtLaunch
// reads), builds the current order from the live g_manifests via RunsBefore,
// computes via ComputeAutoOrder, and on verdict=Reordered upserts a `priority`
// row per moved plugin through the load_order.toml writer (UpsertPriorityRows
// below — write-if-changed + fail-loud, reusing order_persist's I/O shape). On
// verdict=Cycle it logs a WARN naming the cycle members and writes NOTHING (the
// user-owned order is left untouched). On verdict=NoChange it writes nothing.
// Returns the result so the caller (the future button / the self-test's live
// leg) can report what happened. The apply takes effect at the NEXT launch.
//
// PASSIVE: this is invoked only when CALLED (the engine-internal seam). There is
// no boot-sequence call site; the boot path only WARNS about a stale edge
// (RecheckBehaviorEdgesAtLaunch) — fixing it is this method, triggered by the
// user/UI, never automatically.
AutoOrderResult ApplyAutoOrder();

// Write/UPDATE a `priority` value for each named plugin in `moves` into the
// load_order.toml at `loadOrderPath` — an UPSERT (unlike order_persist's
// add-only MergeLoadOrderToml, which never rewrites an existing row): a plugin
// with an existing [[plugin]] row has its `priority` field rewritten in place;
// a plugin with no row gets a fresh [[plugin]] row appended. Every OTHER row
// (and every other field — zone, enabled, comments) is preserved verbatim.
// Write-if-changed (an unchanged file is not rewritten) + fail-loud on an I/O
// error, the SAME posture as order_persist::WriteLoadOrderToml. Returns true on
// success (written or unchanged), false on a read/write failure (logged ERROR).
// Exposed for the self-test (the pure upsert is driven from a literal file body
// via SerializeAutoOrderUpsert below).
bool UpsertPriorityRows(const std::filesystem::path& loadOrderPath,
                        const std::vector<AutoOrderMove>& moves);

// Pure: apply the priority upserts to load_order.toml body `existingText`,
// returning the new body. A row whose `name` matches a move has its `priority`
// line rewritten (or a `priority` line inserted if the row had none); a moved
// plugin with no row gets a `[[plugin]]` row appended (name + priority). Every
// other byte is preserved verbatim. Factored pure so the self-test drives the
// upsert from a literal body with NO file I/O (mirrors order_persist's pure
// MergeLoadOrderToml).
std::string SerializeAutoOrderUpsert(const std::string& existingText,
                                     const std::vector<AutoOrderMove>& moves);

}  // namespace kcdx::load_order
