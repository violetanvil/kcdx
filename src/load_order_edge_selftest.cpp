#include "load_order_edge_selftest.h"

#include <cstdio>   // snprintf
#include <string>
#include <vector>

#include "load_order.h"
#include "mod_absorb/order_persist.h"  // ExistingRowNames — read the upsert body back
#include "plugin_loader.h"   // g_manifests — the discovered-plugin set for prune
#include "test.h"

// cap-101 self-test — behavior dependency edge persistence + launch re-check +
// prune (Phase 9.5 s6) AND the passive callable auto-order method (s7). Each
// assertion is FALSIFIABLE and names the broken state it catches (a
// non-falsifiable PASS proves nothing — AP15). The serializer/parser/prune/
// auto-order assertions are PURE (literal in, value out) — no global state. The
// reorder-recognition assertion drives the GLOBAL load_order state in isolation
// (synthetic g_manifests + Resolve) and RESTORES it verbatim before returning,
// so the live boot state is untouched (the cap-54/55 pattern). The prior-edge
// confirm assertions seed the prior-launch cache via the test seam and read it
// back; they restore the cache to empty.
//
// The s7 auto-order assertions (7-10) drive the PURE core (ComputeAutoOrder +
// SerializeAutoOrderUpsert) from literal edges + a literal current order — no
// file I/O, no global state — exactly the headless-testable seam design §14
// names. Each reads the ACTUAL computed order / verdict / upsert body (never a
// tautology).
//
// What this CANNOT cover in one boot run — surfaced as TWO-LAUNCH matrix rows
// (see test-plugins/README.md cap-101): the live on-disk write at session end
// (launch N) → the up-front recognized-conflict WARN at the next launch
// (launch N+1), and the second-launch error UPGRADE (a persisted edge from a
// prior launch sharpens a reorder/bare error). A single boot cannot span two
// launches; the cross-launch rows are marked for the phase's launch pass. The
// s7 auto-order LIVE file-apply (ApplyAutoOrder writing the live
// load_order.toml) is NOT exercised here — the pure core + the pure upsert ARE
// (assertions 7-10), so the file-touching wrapper is the thin shell; its live
// two-launch confirmation (write@N → corrected order honored@N+1) is a
// TWO-LAUNCH matrix row, not faked into a one-run PASS.

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

    char reason[1800];

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
        // Check for the EMITTED table form ("\n[[edge]]\n", the exact string the
        // serializer writes per edge) — NOT a bare "[[edge]]" substring, which
        // the managed-file header comment legitimately contains.
        const bool noEdgeTable =
            emptyText.find("\n[[edge]]\n") == std::string::npos;
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

    // ========================================================================
    // Assertion 7 (s7 auto-order, UNIT pure): CORRECTED ORDER + MINIMAL
    // DISPLACEMENT. A synthetic mis-ordered set — consumer "cons" loads BEFORE
    // its declarer "decl" (edge cons->decl.behavior) and an UNRELATED plugin
    // "free" sits between them — ComputeAutoOrder yields a corrected order that
    // puts "decl" BEFORE "cons" (the consumer-after-declarer constraint), AND
    // preserves "free"'s position relative to the others as much as the
    // constraint allows (it is in no edge, so it never jumps a sibling it did
    // not need to). Reads the ACTUAL computed correctedOrder.
    // [broken: the consumer still precedes the declarer -> constraint unmet ->
    //  FAIL; the unconstrained "free" was reshuffled past a sibling it did not
    //  need to move past -> FAIL]
    // ========================================================================
    {
        // current order: cons, free, decl  (cons BEFORE decl — the violation).
        const std::vector<std::string> current = {"cons", "free", "decl"};
        const std::vector<BehaviorEdge> edges = {
            Edge("acme", "cons", "acme.decl.some_behavior"),  // cons sets decl's behavior.
        };
        auto isKnown = [](const std::string& a, const std::string& p) -> bool {
            return a == "acme" && (p == "cons" || p == "decl" || p == "free");
        };
        // current priorities: all 50 (so the corrected order must come from the
        // topo sort, not a pre-existing priority spread).
        auto prioOf = [](const std::string&) -> int { return 50; };

        const AutoOrderResult r =
            ComputeAutoOrder(edges, current, isKnown, prioOf);

        // Find positions in the corrected order.
        auto posOf = [&](const std::string& n) -> int {
            for (size_t i = 0; i < r.correctedOrder.size(); ++i)
                if (r.correctedOrder[i] == n) return (int)i;
            return -1;
        };
        const int pCons = posOf("cons"), pDecl = posOf("decl"), pFree = posOf("free");
        const bool constraintMet = (r.verdict == AutoOrderVerdict::Reordered) &&
                                   pDecl >= 0 && pCons >= 0 && pDecl < pCons;
        // Minimal displacement: "free" (unconstrained) keeps its current
        // relative order with whichever neighbors did not have to move. The
        // stable topo sort emits decl (pulled ahead), then the earliest
        // remaining in current order. current = [cons, free, decl]; decl emits
        // first (it is the only in-degree-0 node initially? no — free is also
        // in-degree 0). The stable rule emits the EARLIEST in current order
        // among in-degree-0: cons has in-degree 1 (blocked), free=0, decl=0;
        // free precedes decl in current order -> free emits, then decl, then
        // cons. Expected corrected: free, decl, cons.
        const bool minimalDisp =
            r.correctedOrder == std::vector<std::string>{"free", "decl", "cons"};
        if (!constraintMet || !minimalDisp) {
            std::string got;
            for (size_t k = 0; k < r.correctedOrder.size(); ++k) {
                if (k) got += ",";
                got += r.correctedOrder[k];
            }
            std::snprintf(reason, sizeof(reason),
                "FAIL: auto-order corrected order / minimal displacement — "
                "verdict=%d corrected=[%s] (positions cons=%d decl=%d free=%d). "
                "Expected verdict=Reordered(1) with the declarer BEFORE the "
                "consumer (decl<cons) and the stable topo order [free, decl, "
                "cons] (free is unconstrained -> emits first as the earliest "
                "in-degree-0 node in current order; the consumer-after-declarer "
                "constraint pulls decl ahead of cons). constraintMet=%d "
                "minimalDisp=%d.",
                (int)r.verdict, got.c_str(),
                pCons, pDecl, pFree, constraintMet ? 1 : 0, minimalDisp ? 1 : 0);
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 8 (s7 auto-order, UNIT pure): CYCLE reported, NOT applied. A
    // cyclic edge set — A sets B's behavior AND B sets A's behavior (A->B and
    // B->A) — has no valid order. ComputeAutoOrder returns verdict=Cycle, names
    // both members, and produces NO corrected order / NO moves (the order is
    // left for the caller to leave unchanged — never silently broken).
    // [broken: a cycle silently yields a (necessarily wrong) reordered output
    //  -> verdict != Cycle / correctedOrder non-empty -> FAIL; the cycle is not
    //  detected -> FAIL; the members are not named -> FAIL]
    // ========================================================================
    {
        const std::vector<std::string> current = {"plug_a", "plug_b"};
        const std::vector<BehaviorEdge> edges = {
            Edge("acme", "plug_a", "acme.plug_b.beh_b"),  // a sets b's behavior -> b before a.
            Edge("acme", "plug_b", "acme.plug_a.beh_a"),  // b sets a's behavior -> a before b.
        };
        auto isKnown = [](const std::string& a, const std::string& p) -> bool {
            return a == "acme" && (p == "plug_a" || p == "plug_b");
        };
        auto prioOf = [](const std::string&) -> int { return 50; };

        const AutoOrderResult r =
            ComputeAutoOrder(edges, current, isKnown, prioOf);

        // Members named (both, in some order); no order produced.
        bool hasA = false, hasB = false;
        for (const std::string& m : r.cycleMembers) {
            if (m == "plug_a") hasA = true;
            if (m == "plug_b") hasB = true;
        }
        const bool ok = (r.verdict == AutoOrderVerdict::Cycle) &&
                        r.correctedOrder.empty() && r.moved.empty() &&
                        hasA && hasB;
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: auto-order cycle — verdict=%d corrected.size=%zu "
                "moved.size=%zu cycleMembers.size=%zu (hasA=%d hasB=%d). A "
                "cyclic edge set (plug_a<->plug_b, each sets the other's "
                "behavior) has NO valid order: expected verdict=Cycle(2), an "
                "EMPTY corrected order + EMPTY moves (nothing reordered — never "
                "silently broken), and BOTH plug_a + plug_b named as members.",
                (int)r.verdict, r.correctedOrder.size(), r.moved.size(),
                r.cycleMembers.size(), hasA ? 1 : 0, hasB ? 1 : 0);
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 9 (s7 auto-order, UNIT pure): an already-correct order yields
    // NoChange (no churn) — declarer ALREADY before consumer -> nothing to do;
    // and a pruned-only edge set (consumer absent) likewise yields NoChange (no
    // surviving constraint). Reads the ACTUAL verdict + empty moves.
    // [broken: an in-order set is "reordered" (a needless write) -> FAIL; a
    //  pruned edge produces a move -> FAIL]
    // ========================================================================
    {
        // Right order already: decl BEFORE cons.
        const std::vector<std::string> current = {"decl", "cons"};
        auto isKnown = [](const std::string& a, const std::string& p) -> bool {
            return a == "acme" && (p == "cons" || p == "decl");
        };
        auto prioOf = [](const std::string&) -> int { return 50; };

        const std::vector<BehaviorEdge> inOrder = {
            Edge("acme", "cons", "acme.decl.some_behavior"),
        };
        const AutoOrderResult rOk =
            ComputeAutoOrder(inOrder, current, isKnown, prioOf);

        // A pruned edge set (consumer "ghost" absent) — no surviving constraint.
        const std::vector<BehaviorEdge> pruned = {
            Edge("acme", "ghost", "acme.decl.some_behavior"),
        };
        const AutoOrderResult rPruned =
            ComputeAutoOrder(pruned, current, isKnown, prioOf);

        const bool ok =
            rOk.verdict == AutoOrderVerdict::NoChange && rOk.moved.empty() &&
            rPruned.verdict == AutoOrderVerdict::NoChange && rPruned.moved.empty();
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: auto-order no-change — in-order set: verdict=%d "
                "moved=%zu (expected NoChange(0) + 0 moves — declarer already "
                "before consumer, nothing to do); pruned-only set (absent "
                "consumer 'ghost'): verdict=%d moved=%zu (expected NoChange(0) + "
                "0 moves — the only edge prunes, no surviving constraint). An "
                "already-correct or fully-pruned order must NOT churn the file.",
                (int)rOk.verdict, rOk.moved.size(),
                (int)rPruned.verdict, rPruned.moved.size());
            Fail(reason);
            return;
        }
    }

    // ========================================================================
    // Assertion 10 (s7 auto-order write-back, UNIT pure): the priority UPSERT
    // expresses the corrected order as load_order.toml rows. SerializeAutoOrderUpsert
    // (a) REWRITES the `priority` field of an EXISTING [[plugin]] row in place
    // (preserving its other fields + comments), and (b) APPENDS a fresh
    // [[plugin]] row for a moved plugin that had NO row — reusing the TOML write
    // mechanics, NOT a parallel writer. Reads the ACTUAL written body back via
    // ExistingRowNames + a priority-line check.
    // [broken: the existing row's priority is not updated (still old) -> FAIL;
    //  a no-row plugin gets no appended row -> FAIL; another row is mangled ->
    //  FAIL]
    // ========================================================================
    {
        // An existing body with one [[plugin]] row (has-row case) carrying a
        // priority + a comment, plus an unrelated row that must survive verbatim.
        const std::string existing =
            "# kcdx load order\n"
            "\n"
            "[[plugin]]\n"
            "name      = \"acme.has_row\"\n"
            "zone      = \"after_game\"   # author note kept\n"
            "priority  = 50\n"
            "enabled   = true\n"
            "\n"
            "[[plugin]]\n"
            "name      = \"other.untouched\"\n"
            "priority  = 20\n";

        std::vector<AutoOrderMove> moves;
        { AutoOrderMove m; m.pluginName = "acme.has_row"; m.newPriority = 90; m.oldPriority = 50; moves.push_back(m); }
        { AutoOrderMove m; m.pluginName = "acme.no_row";  m.newPriority = 10; m.oldPriority = 50; moves.push_back(m); }

        const std::string body = SerializeAutoOrderUpsert(existing, moves);

        // (a) the EXISTING row's priority was rewritten to 90 (the old "50" line
        //     for has_row is gone; a "priority  = 90" line is present).
        const bool hasRowUpdated =
            body.find("priority  = 90") != std::string::npos &&
            body.find("priority  = 50") == std::string::npos;  // old value gone.
        // (b) the no-row plugin was appended as a fresh row with its priority.
        const std::vector<std::string> names =
            kcdx::mod_absorb::order_persist::ExistingRowNames(body);
        bool hasNoRowAppended = false, hasRowPresent = false, untouchedPresent = false;
        for (const std::string& n : names) {
            if (n == "acme.no_row")     hasNoRowAppended = true;
            if (n == "acme.has_row")    hasRowPresent    = true;
            if (n == "other.untouched") untouchedPresent = true;
        }
        // the no_row's appended priority value (10) is present; the untouched
        // row's own priority (20) survives verbatim.
        const bool noRowPrio   = body.find("priority  = 10") != std::string::npos;
        const bool untouchedOk = body.find("priority  = 20") != std::string::npos &&
                                 body.find("author note kept") != std::string::npos;

        const bool ok = hasRowUpdated && hasNoRowAppended && hasRowPresent &&
                        untouchedPresent && noRowPrio && untouchedOk;
        if (!ok) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: auto-order priority upsert — has_row prio rewritten to 90 "
                "(old 50 gone)=%d; no_row appended as a row=%d; has_row still "
                "present=%d; other.untouched survives=%d; no_row prio=10 written="
                "%d; untouched prio=20 + comment survive verbatim=%d. The upsert "
                "must REWRITE an existing row's priority in place AND append a "
                "fresh row for a no-row plugin, preserving every other row + "
                "field + comment (reuse the TOML write mechanics, not a parallel "
                "writer).\nBODY:\n%s",
                hasRowUpdated ? 1 : 0, hasNoRowAppended ? 1 : 0,
                hasRowPresent ? 1 : 0, untouchedPresent ? 1 : 0,
                noRowPrio ? 1 : 0, untouchedOk ? 1 : 0, body.c_str());
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
        "AUTO-ORDER (s7): ComputeAutoOrder corrects a mis-ordered set (declarer "
        "before consumer) with minimal displacement (an unconstrained row keeps "
        "its position), REPORTS a cycle (verdict=Cycle, members named, NO order "
        "produced — never silently broken), yields NoChange on an already-correct "
        "or fully-pruned set, and SerializeAutoOrderUpsert rewrites an existing "
        "row's priority in place + appends a fresh row for a no-row plugin "
        "(reusing the TOML write mechanics). TWO-LAUNCH rows (live edge "
        "write@N -> up-front warn@N+1, second-launch error upgrade, auto-order "
        "apply@N -> corrected order honored@N+1) are the launch-pass matrix rows.");
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-101 behavior-edge-persist");
}

}  // namespace kcdx::load_order
