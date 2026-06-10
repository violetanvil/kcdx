#pragma once

// survival_report — the verification-sweep REPORT PRODUCER.
//
// A DISTINCT responsibility from survival_verify (which VERIFIES — runs the
// per-kind rank ladder and yields a RowVerdict per curated row). This unit
// SERIALIZES that result: it owns the JSON/file I/O the verify pass deliberately
// does not (survival_verify.h: "NOT in scope here: the JSON report"). The split
// is structure-by-responsibility — verify decides verdicts, report writes them.
//
// THE INCREMENTAL-FLUSH CONTRACT (the load-bearing reason this is its own unit,
// not a post-pass over the returned vector): the sweep's live-exercise tier
// drives real game code and a row's attempt CAN fault/hang the session, so the
// report is written PER ROW as each verdict resolves — never accumulated in
// memory and bulk-written at sweep end (which loses the entire report on any
// mid-sweep death). Mechanism:
//
//   1. Begin() opens an append-per-row JSONL sink in the logs dir (the same dir
//      as kcdx-dev.log — the report lands alongside it). One row-result object
//      per line, written + FLUSHED the instant each RowVerdict finalizes.
//   2. OnRow(rv) is the per-row sink the sweep invokes inside its loop (threaded
//      into RunStartupVerification as the onRow callback): it streams the row's
//      console line AND appends+flushes the row's JSONL line —
//      printed AND durably written together — in the SAME per-row tick.
//   3. Finalize() runs AFTER the sweep returns: it wraps the accumulated JSONL
//      into the v3 JSON document (schema_version + game_version + summary +
//      rows[] + complete:true + rows_expected) alongside kcdx-dev.log, and emits
//      the canonical acceptance signal.
//
// A sweep that dies at row N still leaves a durable, complete-up-to-N JSONL the
// maintainer can ingest (or a recovery wraps into a partial v3 with
// complete:false). The v3 schema (the cross-repo frontend contract) validates
// BOTH the per-row line shape and the finalized document.
//
// FAIL LOUD: a file-open failure, a row that cannot serialize, or a finalize
// failure each LOG a structured error and surface — never a silent drop or a
// silent empty report.
//
// COLD PATH — runs once per kcdx_verify_all trigger, never during gameplay
// (survival_verify.h §"STARTUP / install-time, NOT the hot path"). File I/O +
// allocation here are acceptable; no buffer pool is warranted.

#include <cstdint>
#include <cstdio>   // std::FILE
#include <string>

#include "survival_verify.h"  // RowVerdict — the per-row result this serializes.

namespace kcdx::survival_report {

// Minimal JSON string escaper — the v3 report's name/detail fields can carry a
// quote or backslash, and a broken escape yields an invalid JSON line that fails
// v3 validation. Escapes ", \, the control range, and the JSON-required
// control-character escapes (\b \f \n \r \t); other control chars → \u00XX.
// Returns the escaped body WITHOUT surrounding quotes. Exposed for the cap-95
// self-test's round-trip assertion.
std::string JsonEscape(const std::string& s);

// The report producer. Constructed in the kcdx_verify_all handler; its OnRow is
// threaded into RunStartupVerification as the per-row sink, and Finalize runs
// after the sweep returns. One instance per sweep run.
class Reporter {
public:
    // Open the JSONL append sink in the logs dir and record the sweep's expected
    // row count + game version (for the finalized document's rows_expected +
    // game_version). `rowsExpected` is the curated-set size this run targets
    // (refdb::CachedRowCount()). A file-open failure is LOGGED loud and
    // leaves the reporter in a degraded state where OnRow/Finalize still produce
    // the console stream + the v3 document from the in-memory rows (so a logs-dir
    // I/O failure does not lose the whole report) but the incremental durability
    // guarantee is forfeited — the degraded state is reported by Finalize.
    // Returns true iff the JSONL sink opened (the incremental durability path is live).
    bool Begin(size_t rowsExpected, const std::string& gameVersion);

    // The per-row sink — invoked by the sweep as each RowVerdict finalizes
    // (BEFORE the next row's attempt). Does the three per-row actions together:
    // streams the console line, appends+flushes the JSONL line, and accumulates
    // the row for the finalize document. The console line shape is
    // `[N/total] <name> v<ver> -> <verdict> (rank <r>)`. A per-row serialize/flush
    // failure is LOGGED loud and does not abort the sweep (the remaining rows
    // still flush).
    void OnRow(const kcdx::survival_verify::RowVerdict& rv);

    // Wrap the accumulated rows into the v3 JSON document, write it alongside
    // kcdx-dev.log, close the JSONL sink, and emit the canonical acceptance
    // signal (ACCEPT-RESULT + ACCEPT-SUITE) to the dev log. `complete` is whether
    // the sweep ran to the end of the curated set (rows seen == rowsExpected). A
    // finalize write failure is LOGGED loud and returns false. Returns true iff
    // the v3 document was written.
    bool Finalize(bool complete);

    // The flush counter — the number of per-row JSONL appends that were flushed
    // to disk DURING the sweep (incremented per OnRow that successfully flushed).
    // The cap-95 incremental self-test reads this to assert the sink grew across
    // the sweep (>= 1 before Finalize ran), distinguishing the per-row flush
    // model from a forbidden bulk write-at-end.
    size_t FlushedRowCount() const { return flushedRows_; }

    // The on-disk JSONL sink path (empty until Begin opened it). The finalized v3
    // document path is the same stem with a .json extension.
    const std::string& JsonlPath() const { return jsonlPath_; }
    const std::string& ReportPath() const { return reportPath_; }

    ~Reporter();

private:
    // Serialize ONE RowVerdict into a single JSON object (one rows[] element AND
    // one JSONL line — the v3 schema's dual shape). Owns the matched-id
    // if/then/else: a verified_working/passed_not_verified row carries an integer
    // matched_address_version_id; every other verdict carries null.
    std::string SerializeRow(const kcdx::survival_verify::RowVerdict& rv) const;

    std::FILE*  jsonl_ = nullptr;     // the append-per-row JSONL sink (incremental flush).
    std::string jsonlPath_;           // <logs>/kcdx-verify_<stamp>.jsonl
    std::string reportPath_;          // <logs>/kcdx-verify_<stamp>.json (the v3 doc)
    std::string gameVersion_;         // the running build tag (report game_version).
    std::string rowsJson_;            // accumulated rows[] elements (for finalize).
    size_t      rowsExpected_ = 0;    // the curated-set size this run targets.
    size_t      rowCount_     = 0;    // rows seen via OnRow (== rows[] length).
    size_t      flushedRows_  = 0;    // per-row JSONL appends flushed to disk (incremental counter).
    size_t      passingCount_ = 0;    // verified_working + passed_not_verified (summary.passing).
    bool        sinkDegraded_ = false; // Begin could not open the JSONL sink.
};

}  // namespace kcdx::survival_report
