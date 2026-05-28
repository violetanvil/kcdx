#pragma once

// survival_pass — the unified per-plugin / per-version survival-verification
// pass + the collector seam that feeds it.
//
// THE COLLECTOR SEAM (the design decision this module embodies):
// the deferred-apply registry (lua_registry) is PAYLOAD-AGNOSTIC — it never
// interprets an entry's payload, so it cannot itself enumerate "which game
// function did each binding target". Rather than add a payload accessor that
// would break that invariant, collection happens AT RESOLVE TIME: each binder,
// at the point it resolves a target through refdb, REPORTS the touched ref to
// this collector via RecordTouchedRef. The binder already paid for the refdb
// resolve (it has the rva + length + content_hash in hand), so the recorded ref
// carries those directly — the pass consumes them and does NOT re-resolve
// through refdb (that would be a redundant second DB round-trip for a fact the
// binder already holds).
//
// (The binders are wired to call RecordTouchedRef in a LATER step; this module
// builds the callable collector + the pass that consumes it. Nothing reaches
// RecordTouchedRef from production yet.)
//
// THE PASS (RunPass): for every collected (plugin, targetKey, rva, length,
// expectedHash):
//   (a) the expected content_hash is the one the binder recorded (resolved via
//       refdb at report time — by name or id, whichever the targetKey named);
//   (b) the survival check runs (kcdx::survival::SurvivalCheck) UNLESS the
//       plugin's cache record is still valid (same plugin + game_ver +
//       sqlite_sha + toml_mtime + entrypoints_mtime + cache_schema_version),
//       in which case the cached per-function results are REUSED and the
//       hashing is SKIPPED — the perf win;
//   (c) the per-(plugin, function) result is recorded;
//   (d) the whole result set is written to version_check.bin.
//
// The pass surfaces, per (plugin, function), the survival result + the plugin's
// on_changed posture. The ACTUAL apply-time enforcement (warn-and-proceed vs
// skip-the-entry) is wired in a later step — this pass only produces the
// result+posture the enforcement reads.
//
// Fail-loud: a ref whose expected hash is empty (a non-byte entity) records
// CannotCheck (never Changed); a survival check that cannot run records its
// CannotCheck reason. A cache that cannot be read degrades to "recheck
// everything" (handled in version_check_cache) — never a stale wrong result.

#include <cstdint>
#include <string>
#include <vector>

#include "version_check_cache.h"  // FuncStatus, Posture

namespace kcdx::survival_pass {

// THE COLLECTOR ENTRY POINT — called by binders (in a later step) at the moment
// they resolve a target through refdb, once per touched function.
//
//   owningPlugin  — the [plugin].name of the plugin that declared the binding
//                   (the cache-record key + the result attribution). Empty for
//                   an anonymous caller — those refs are still recorded under
//                   the empty-name bucket (the pass surfaces them; an anonymous
//                   plugin simply has no persisted cache record reused, which is
//                   correct).
//   targetKey     — the resolved target identity: the curated NAME for a
//                   resolve-by-name, or "#<id>" for a resolve-by-id. Used as the
//                   per-function result key + the cache func key. The binder
//                   formats it; the pass treats it opaquely.
//   rva           — the resolved address (from refdb) the survival check hashes.
//   length        — the byte span the content_hash covers (refdb's
//                   entity_versions.length). 0 = no span (a non-byte entity).
//   expectedHash  — the refdb content_hash (raw bytes; empty for a non-byte
//                   entity). The pass compares the on-disk bytes against this.
//
// Idempotent per (owningPlugin, targetKey): a second report for the same pair
// replaces the first (two bindings on one function record one touched ref).
void RecordTouchedRef(const std::string& owningPlugin,
                      const std::string& targetKey,
                      uint64_t rva,
                      uint64_t length,
                      const std::vector<uint8_t>& expectedHash);

// THE PASS ENTRY POINT — run the unified verify-or-cache-hit pass over every
// recorded ref, then persist the whole set to version_check.bin.
//
// `gameVer`     — the running build's version string (the cache key game_ver).
// `sqliteSha`   — a BLAKE3 identity of reference.sqlite (the cache key
//                 sqlite_sha; a DB refresh invalidates). May be empty if the DB
//                 was unreadable — an empty sha still keys consistently (it just
//                 never matches a record written under a real sha, forcing a
//                 recheck, which is the safe side).
//
// The per-plugin mtimes + postures are supplied by RecordPluginMeta (below),
// which the binders' owning-plugin context populates before the pass runs.
//
// Returns the number of (plugin, function) results produced. After RunPass the
// results are queryable via Result() / ResultsForPlugin() and the cache is
// written. Idempotent within a launch — a second call re-runs over the current
// recorded set (later refs accumulate).
size_t RunPass(const std::string& gameVer,
               const std::vector<uint8_t>& sqliteSha);

// Per-plugin metadata the pass needs that is NOT part of a touched ref: the
// invalidation mtimes + the plugin's on_changed posture. Called (in a later
// step) once per plugin that has touched refs, before RunPass. Idempotent per
// pluginName (last call wins).
//
//   tomlMtime / entrypointsMtime — epoch-second last-write times (the cache
//                                  invalidation inputs).
//   posture                      — the plugin's [plugin].on_changed_function.
void RecordPluginMeta(const std::string& pluginName,
                      uint64_t tomlMtime,
                      uint64_t entrypointsMtime,
                      kcdx::version_check_cache::Posture posture);

// One produced result: which plugin + function, the survival outcome, the
// plugin's posture (so the later apply-time enforcement reads both off one
// struct), and whether it came from a cache hit (diagnostic only).
struct PassResult {
    std::string                            pluginName;
    std::string                            targetKey;
    kcdx::version_check_cache::FuncStatus  status =
        kcdx::version_check_cache::FuncStatus::CannotCheck;
    kcdx::version_check_cache::Posture     posture =
        kcdx::version_check_cache::Posture::WarnAndTry;
    bool                                   fromCache = false;
};

// Look up the produced result for one (plugin, function). Returns nullptr if no
// ref for that pair was recorded/run. Pointer valid until the next RunPass.
const PassResult* Result(const std::string& pluginName,
                         const std::string& targetKey);

// All produced results for one plugin (the apply-time enforcement walks these).
std::vector<PassResult> ResultsForPlugin(const std::string& pluginName);

// Reset all collector + pass state to empty. For tests / self-test isolation.
void Reset();

}  // namespace kcdx::survival_pass
