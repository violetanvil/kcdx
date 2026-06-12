#include "load_order_edge_selftest.h"

#include <cstdio>   // snprintf
#include <string>
#include <vector>

#include "load_order.h"
#include "plugin_loader.h"   // g_manifests — the discovered-plugin set for prune
#include "test.h"

// cap-101 self-test — behavior dependency edge persistence + launch re-check +
// prune. Each assertion is FALSIFIABLE and names the broken state it catches (a
// non-falsifiable PASS proves nothing — AP15). The serializer/parser/prune
// assertions are PURE (literal in, value out) — no global state. The
// reorder-recognition assertion drives the GLOBAL load_order state in isolation
// (synthetic g_manifests + Resolve) and RESTORES it verbatim before returning,
// so the live boot state is untouched (the cap-54/55 pattern). The prior-edge
// confirm assertions seed the prior-launch cache via the test seam and read it
// back; they restore the cache to empty.
//
// What this CANNOT cover in one boot run — surfaced as TWO-LAUNCH matrix rows
// (see test-plugins/README.md cap-101): the live on-disk write at session end
// (launch N) → the up-front recognized-conflict WARN at the next launch
// (launch N+1), and the second-launch error UPGRADE (a persisted edge from a
// prior launch sharpens a reorder/bare error). A single boot cannot span two
// launches; the cross-launch rows are marked for the phase's launch pass.

namespace kcdx::load_order {

namespace {

constexpr const char* kRow = "cap-101-behavior-edge-persist";

void Fail(char* reason) {
    kcdx::test::ReportResult(kRow, false, reason);
    kcdx::test::EmitSummaryIfChanged("cap-101 behavior-edge-persist");
}

BehaviorEdge Edge(const std::string& author, const std::string& plugin,
                  const std::string& behaviorFullName) {
    BehaviorEdge e;
    e.consumerAuthor   = author;
    e.consumerPlugin   = plugin;
    e.behaviorFullName = behaviorFullName;
    return e;
}

kcdx::plugins::PluginManifest MakePlugin(const std::string& author,
                                         const std::string& name,
                                         int priority) {
    kcdx::plugins::PluginManifest m;
    m.author          = author;
    m.name            = name;
    m.defaultPosition = "after_game";
    m.defaultPriority = priority;  // lower = earlier in zone (RunsBefore key)
    return m;
}

}  // namespace

void RunEdgePersistSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) return;
    s_reported = true;

    char reason[1400];

    // ========================================================================
    // Assertion 1 (UNIT, pure): serialize → parse round-trip. A two-edge set
    // serializes to [[edge]] tables and parses back to the SAME two edges
    // (consumer author/plugin split + behavior full name preserved).
    // [broken: an edge lost -> count wrong -> FAIL; a field mangled (consumer
    //  not split on the first dot, behavior dropped) -> FAIL]
    // ========================================================================
    {
        const std::vector<BehaviorEdge> edges = {
            Edge("redmoon", "tweak", "acme.realism.hardcore_combat"),
            Edge("ts", "cap_101", "ts.cap_101.local_behavior"),
        };
        const std::string text = SerializeBehaviorEdgesToml(edges);
        bool parseFailed = true;  // must be set false by a clean parse
        const std::vector<BehaviorEdge> back =
            ParseBehaviorEdgesToml(text, &parseFailed);
        const bool ok =
            !parseFailed &&
            back.size() == 2 &&
            back[0].consumerAuthor == "redmoon" &&
            back[0].consumerPlugin == "tweak" &&
            back[0].behaviorFullName == "acme.realism.hardcore_combat" &&
            back[1].consumerAuthor == "ts" &&
            back[1].consumerPlugin == "cap_101" &&
            back[1].behaviorFullName == "ts.cap_101.local_behavior";
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: behavior_edges.toml serialize->parse round-trip — "
                "parseFailed=%d, parsed %zu edge(s) (expected 2: "
                "redmoon.tweak->acme.realism.hardcore_combat, "
                "ts.cap_101->ts.cap_101.local_behavior). The serializer must "
                "emit one [[edge]] per edge (consumer=\"<author>.<plugin>\", "
                "behavior=\"<full>\") and the parser must split the consumer on "
                "the first dot and preserve the behavior name.\nTEXT:\n%s",
                parseFailed ? 1 : 0, back.size(), text.c_str());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 2 (UNIT, pure): a WHOLE-FILE parse error yields an EMPTY set
    // and sets parseFailedOut — the WARN+skip+rebuild contract (never a hard
    // fail). A per-ROW malformed [[edge]] (good edge + a malformed one) loads
    // ONLY the good edge and does NOT flag a whole-file failure.
    // [broken: a malformed file throws / loads garbage -> not empty / not
    //  flagged -> FAIL; a malformed row aborts the whole file or is loaded
    //  -> wrong count -> FAIL]
    // ========================================================================
    {
        // Whole-file garbage (unterminated string) — a parse error.
        bool wholeFailed = false;
        const std::vector<BehaviorEdge> empty =
            ParseBehaviorEdgesToml("this is = \"not valid toml", &wholeFailed);
        // A good edge + a malformed [[edge]] (consumer has no dot — skipped per
        // row) + an [[edge]] missing 'behavior' (skipped per row).
        const std::string mixed =
            "[[edge]]\n"
            "consumer = \"good.consumer\"\n"
            "behavior = \"acme.realism.hardcore_combat\"\n"
            "\n[[edge]]\n"
            "consumer = \"nodotconsumer\"\n"   // malformed consumer -> skip
            "behavior = \"x.y.z\"\n"
            "\n[[edge]]\n"
            "consumer = \"has.dot\"\n"          // missing behavior -> skip
            "\n";
        bool rowFailed = false;
        const std::vector<BehaviorEdge> kept =
            ParseBehaviorEdgesToml(mixed, &rowFailed);
        const bool ok =
            wholeFailed && empty.empty() &&
            !rowFailed && kept.size() == 1 &&
            kept[0].consumerAuthor == "good" &&
            kept[0].consumerPlugin == "consumer" &&
            kept[0].behaviorFullName == "acme.realism.hardcore_combat";
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: parse fail-loud contract — whole-file garbage: "
                "parseFailed=%d empty=%d (expected flagged + empty, WARN+skip "
                "never a hard fail); per-row skip: rowFailed=%d kept=%zu "
                "(expected NOT flagged + exactly 1 good edge — a malformed "
                "[[edge]] is skipped, the file is NOT aborted, the good edge "
                "survives).",
                wholeFailed ? 1 : 0, empty.empty() ? 1 : 0,
                rowFailed ? 1 : 0, kept.size());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 3 (UNIT, pure): the store is REBUILT per launch — an empty set
    // serializes to a header-only document (no [[edge]] tables), so a consumer
    // that no longer sets a behavior leaves NO row (its edge drops). And a
    // round-trip of the empty document parses back to zero edges.
    // [broken: an empty set emits a phantom [[edge]] -> back not empty -> FAIL;
    //  the header alone parses as an edge -> FAIL]
    // ========================================================================
    {
        const std::string emptyText = SerializeBehaviorEdgesToml({});
        const bool noEdgeTable =
            emptyText.find("[[edge]]") == std::string::npos;
        bool pf = false;
        const std::vector<BehaviorEdge> back =
            ParseBehaviorEdgesToml(emptyText, &pf);
        if (!noEdgeTable || pf || !back.empty()) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: empty-set rebuild — an empty observed set must serialize "
                "to a header-only document with NO [[edge]] table (noEdgeTable="
                "%d) and parse back to zero edges (parseFailed=%d, parsed=%zu). "
                "This is the self-invalidation by rebuild: a dropped-consumer "
                "edge simply is not in the set.\nTEXT:\n%s",
                noEdgeTable ? 1 : 0, pf ? 1 : 0, back.size(), emptyText.c_str());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 4 (UNIT, pure): PRUNE — RecheckBehaviorEdges drops an edge whose
    // consumer OR declarer is absent from the discovered set, AND drops a
    // catalog-name edge (kcdx.behavior.<bare> — no plugin declarer). A pruned
    // edge yields NO RecognizedConflict (no warn, no constraint). Drives the
    // injected isKnownPlugin (NO global state) — only a synthetic "known" set.
    // The surviving in-order edge ALSO yields no conflict (declarer not before
    // consumer); this assertion isolates PRUNE from reorder (Assertion 5).
    // [broken: a pruned edge produces a conflict -> count>0 -> FAIL; a catalog
    //  name is treated as a plugin -> FAIL]
    // ========================================================================
    {
        // "known" = {acme.realism, redmoon.tweak} only.
        auto isKnown = [](const std::string& author,
                          const std::string& plugin) -> bool {
            return (author == "acme" && plugin == "realism") ||
                   (author == "redmoon" && plugin == "tweak");
        };
        const std::vector<BehaviorEdge> edges = {
            // declarer acme.realism ABSENT-from-known? no, it IS known; but the
            // CONSUMER ghost.gone is absent -> prune.
            Edge("ghost", "gone", "acme.realism.hardcore_combat"),
            // consumer redmoon.tweak known, declarer missing.absent ABSENT -> prune.
            Edge("redmoon", "tweak", "missing.absent.some_behavior"),
            // a catalog name -> no plugin declarer -> prune (never an edge).
            Edge("redmoon", "tweak", "kcdx.behavior.outfit_swap"),
        };
        const std::vector<RecognizedConflict> conflicts =
            RecheckBehaviorEdges(edges, isKnown);
        if (!conflicts.empty()) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: prune — RecheckBehaviorEdges returned %zu conflict(s) on "
                "edges whose consumer (ghost.gone) or declarer (missing.absent) "
                "is ABSENT from the known set, plus a catalog-name edge "
                "(kcdx.behavior.outfit_swap, no plugin declarer). EVERY one must "
                "be pruned -> zero conflicts (a pruned edge drives no warn).",
                conflicts.size());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 5 (GLOBAL state, snapshot/restore): reorder RECOGNITION. With a
    // synthetic resolved order where the CONSUMER (priority 10, earlier) loads
    // BEFORE its DECLARER (priority 90, later), a persisted edge consumer->
    // declarer.behavior is a RecognizedConflict (the reorder violation). The
    // RIGHT order (consumer AFTER declarer) yields NO conflict. RunsBefore reads
    // the global g_effective, so this drives g_manifests + Resolve in isolation
    // and RESTORES verbatim.
    // [broken: a mis-ordered edge is NOT recognized -> 0 conflicts -> FAIL; an
    //  in-order edge IS recognized -> a false positive -> FAIL]
    // ========================================================================
    {
        const Snapshot saved = CaptureState();
        std::vector<kcdx::plugins::PluginManifest> savedManifests =
            kcdx::plugins::g_manifests;
        auto restore = [&]() {
            kcdx::plugins::g_manifests = savedManifests;
            RestoreState(saved);
        };

        // The discovered set for RecheckBehaviorEdges' prune test: both plugins
        // are installed (so prune passes and we exercise the reorder branch).
        auto isKnown = [](const std::string& author,
                          const std::string& plugin) -> bool {
            return (author == "acme" && plugin == "consumerp") ||
                   (author == "acme" && plugin == "declarerp");
        };

        // --- Mis-ordered: consumer (prio 10) BEFORE declarer (prio 90). ---
        kcdx::plugins::g_manifests.clear();
        kcdx::plugins::g_manifests.push_back(
            MakePlugin("acme", "consumerp", 10));
        kcdx::plugins::g_manifests.push_back(
            MakePlugin("acme", "declarerp", 90));
        Read(std::filesystem::path());  // no override file -> author defaults
        Resolve();

        const std::vector<BehaviorEdge> misEdges = {
            Edge("acme", "consumerp", "acme.declarerp.some_behavior"),
        };
        const std::vector<RecognizedConflict> misConflicts =
            RecheckBehaviorEdges(misEdges, isKnown);
        const bool misOk =
            misConflicts.size() == 1 &&
            misConflicts[0].consumerPlugin == "consumerp" &&
            misConflicts[0].declarerPlugin == "declarerp" &&
            misConflicts[0].behaviorFullName == "acme.declarerp.some_behavior";

        // --- Right order: declarer (prio 10) BEFORE consumer (prio 90). ---
        kcdx::plugins::g_manifests.clear();
        kcdx::plugins::g_manifests.push_back(
            MakePlugin("acme", "declarerp", 10));
        kcdx::plugins::g_manifests.push_back(
            MakePlugin("acme", "consumerp", 90));
        Read(std::filesystem::path());
        Resolve();
        const std::vector<RecognizedConflict> rightConflicts =
            RecheckBehaviorEdges(misEdges, isKnown);  // SAME edge, right order
        const bool rightOk = rightConflicts.empty();

        restore();

        if (!misOk || !rightOk) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: reorder recognition — mis-ordered (consumer loads BEFORE "
                "declarer): got %zu conflict(s) (expected exactly 1 naming "
                "consumerp/declarerp/acme.declarerp.some_behavior); right order "
                "(declarer BEFORE consumer): got %zu conflict(s) (expected 0). "
                "A persisted edge whose consumer now RunsBefore its declarer is "
                "the reorder violation recognized up front; the in-order edge "
                "must NOT be flagged (no false positive).",
                misConflicts.size(), rightConflicts.size());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 6 (prior-edge cache, seam-seeded): the second-launch error
    // upgrade backing — PriorLaunchEdgeConfirms answers true ONLY for an
    // exact consumer+behavior match seeded into the prior-launch cache, and
    // PriorLaunchEdgeForBare resolves a bare name to the prior full name it was
    // recorded under (a miss returns false). Seeds via the test seam, restores
    // the cache to empty.
    // [broken: a confirm on a non-seeded edge returns true -> a false upgrade
    //  -> FAIL; a seeded bare lookup does not return the full name -> FAIL;
    //  the cache is not restored -> a later read sees stale edges -> FAIL]
    // ========================================================================
    {
        const std::vector<BehaviorEdge> prior = {
            Edge("redmoon", "tweak", "acme.realism.hardcore_combat"),
        };
        SetPriorLaunchEdgesForTest(prior);

        const bool hitExact = PriorLaunchEdgeConfirms(
            "redmoon", "tweak", "acme.realism.hardcore_combat");
        const bool missConsumer = PriorLaunchEdgeConfirms(
            "other", "plugin", "acme.realism.hardcore_combat");
        const bool missBehavior = PriorLaunchEdgeConfirms(
            "redmoon", "tweak", "acme.realism.different_behavior");

        std::string fullOut;
        const bool bareHit = PriorLaunchEdgeForBare(
            "redmoon", "tweak", "hardcore_combat", fullOut);
        std::string fullOut2;
        const bool bareMiss = PriorLaunchEdgeForBare(
            "redmoon", "tweak", "no_such_bare", fullOut2);

        // Restore the cache to empty (no stale prior edges for the live session).
        SetPriorLaunchEdgesForTest({});
        const bool restored =
            !PriorLaunchEdgeConfirms("redmoon", "tweak",
                                     "acme.realism.hardcore_combat");

        const bool ok =
            hitExact && !missConsumer && !missBehavior &&
            bareHit && fullOut == "acme.realism.hardcore_combat" &&
            !bareMiss && restored;
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: prior-launch edge cache — exact confirm: hit=%d "
                "missConsumer=%d missBehavior=%d (expected 1/0/0 — confirm only "
                "an exact consumer+behavior match); bare lookup: bareHit=%d "
                "full=\"%s\" (expected 1 + acme.realism.hardcore_combat) "
                "bareMiss=%d (expected 0); cache restored to empty: %d. This "
                "backs the second-launch error upgrade — a confirm must not "
                "fire on a non-seeded edge (a false upgrade), and the bare "
                "lookup must resolve to the prior full name.",
                hitExact ? 1 : 0, missConsumer ? 1 : 0, missBehavior ? 1 : 0,
                bareHit ? 1 : 0, fullOut.c_str(), bareMiss ? 1 : 0,
                restored ? 1 : 0);
            Fail(reason);
            return;
        }
    }

    std::snprintf(reason, sizeof(reason),
        "behavior_edges.toml serialize<->parse round-trips (consumer split on "
        "the first dot, behavior preserved); a whole-file parse error is "
        "WARN+skip+rebuild (flagged + empty, never a hard fail) and a malformed "
        "[[edge]] is per-row skipped (the file is not aborted, good edges "
        "survive); an empty observed set rebuilds to a header-only document "
        "(a dropped-consumer edge leaves no row); RecheckBehaviorEdges PRUNES an "
        "edge whose consumer or declarer is absent (and a catalog name) -> no "
        "conflict; a persisted edge whose consumer now loads BEFORE its declarer "
        "is RECOGNIZED as a reorder conflict (and an in-order edge is not — no "
        "false positive); and the prior-launch cache backs the second-launch "
        "upgrade (an exact confirm + a bare->full resolution, no false confirm). "
        "TWO-LAUNCH rows (live write@N -> up-front warn@N+1, second-launch error "
        "upgrade) are the launch-pass matrix rows.");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-101 behavior-edge-persist");
}

}  // namespace kcdx::load_order
