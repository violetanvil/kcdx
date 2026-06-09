#pragma once

// version_check_cache — the per-plugin / per-version survival-verification
// cache, persisted at <game-bin>/kcdx-engine/cache/version_check.bin.
//
// The survival pass (survival_pass.{h,cpp}) hashes the on-disk bytes of every
// game function a plugin targets to detect a changed/patched binary BEFORE the
// apply pass detours stale code. That hashing is the per-launch cost this cache
// removes: when a plugin's invalidation inputs are byte-for-byte the same as a
// previous launch, the previously-computed per-function results are reused and
// the hashing is SKIPPED for that plugin.
//
// Always-on (NOT dev-mode-gated): the mtime checks catch the rare
// end-user-edits-a-plugin-file case at near-zero cost.
//
// Invalidation — a cached record is VALID (→ reuse, skip the recheck) only when
// ALL of these match the current launch; ANY mismatch invalidates that plugin's
// record and forces a fresh recheck:
//
//   plugin_name         — the record's owning plugin.
//   game_ver            — the running build's version string.
//   sqlite_sha          — a BLAKE3 identity of reference.sqlite (a DB refresh
//                         changes the recorded hashes → must recheck).
//   toml_mtime          — the plugin's kcdx.toml last-write time.
//   entrypoints_mtime   — the newest last-write time across the plugin's
//                         declared entrypoint files (a code edit may change
//                         which functions the plugin touches).
//   cache_schema_version — a constant baked into the engine; bumped only when
//                         the CHECK LOGIC changes (the whole cache file is
//                         rejected on a mismatch, never read with stale logic).
//
// Codec discipline (mirrors the cosave serializer): a magic prefix + a wire
// format-version byte + the cache_schema_version byte; an unknown wire version
// OR a mismatched cache_schema_version OR any corrupt/truncated read is FAIL-
// LOUD-AND-TREAT-AS-EMPTY — a WARN line names what was wrong, the in-memory
// cache starts empty, and every plugin rechecks. A cache must NEVER serve a
// stale wrong result, so an unreadable file degrades to "no cache" (correct,
// slower), never to "trust whatever parsed".

#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::version_check_cache {

// The check-logic identity baked into this engine build. Bump ONLY when the
// survival-check logic changes (NOT on every kcdx release) — a bump rejects the
// entire on-disk cache so no record is reused under different check semantics.
// One byte: the on-disk slot is a single byte (see the header layout).
//
// v2: the survival-result universe grew — FuncStatus::Ambiguous (3) was added
// (the per-kind dispatch's callsite multiple-hit verdict). A new status the
// check can emit IS a check-logic change, so the bump cleanly rejects any v1
// cache (which never knew the value) rather than read it under different
// semantics.
constexpr uint8_t kCacheSchemaVersion = 2;

// The per-function survival outcome stored in a record. Mirrors
// kcdx::survival::Status; kept as its own enum so the codec's on-disk byte
// values are pinned independently of the survival header's enum order.
//
// APPEND-ONLY: the on-disk byte values are pinned (0/1/2/3) and NEVER reused —
// a new value appends at the next integer + bumps kCacheSchemaVersion (above).
enum class FuncStatus : uint8_t {
    Unchanged   = 0,  // on-disk bytes matched the recorded content_hash.
    Changed     = 1,  // on-disk bytes differ — a byte shift / patched binary.
    CannotCheck = 2,  // the check could not run (non-byte entity, missing DB row, …).
    Ambiguous   = 3,  // the locator no longer resolves UNIQUELY (a callsite AOB
                      // matching >1 site) — not Changed, not Unchanged. Produced
                      // by the per-kind dispatch's non-function checks.
};

// The plugin's on_changed posture as observed at the time the record was
// written. Mirrors PluginManifest::OnChangedFunction; pinned here so the codec
// is independent of the manifest header's enum order.
enum class Posture : uint8_t {
    WarnAndTry  = 0,
    RefuseEntry = 1,
};

// One function's recorded result inside a plugin record.
struct FuncResult {
    std::string targetKey;  // the resolved target identity (name or "#<id>").
    FuncStatus  status = FuncStatus::CannotCheck;
};

// The invalidation key set for a plugin record. ALL fields must match the
// current launch for the record to be reused.
struct InvalidationKey {
    std::string          pluginName;
    std::string          gameVer;
    std::vector<uint8_t> sqliteSha;        // 32 bytes (BLAKE3 of reference.sqlite); may be empty if the DB was unreadable.
    uint64_t             tomlMtime = 0;        // epoch seconds.
    uint64_t             entrypointsMtime = 0; // epoch seconds.
};

// One persisted plugin record: its invalidation key + posture + per-function
// results.
struct Record {
    InvalidationKey         key;
    Posture                 posture = Posture::WarnAndTry;
    std::vector<FuncResult> results;
};

// Load the cache from disk into memory. Idempotent: a second call re-reads.
//
// Returns true if a well-formed cache file was read (even if it held zero
// records). Returns false — with the in-memory cache RESET TO EMPTY — when the
// file is absent (first launch; not an error, logged at DEBUG) or corrupt /
// truncated / wrong-magic / unknown-wire-version / mismatched-schema-version
// (logged WARN; the cache degrades to empty, every plugin rechecks). NEVER
// throws; NEVER serves a partially-parsed record.
bool Load();

// Look up the cached record for `key.pluginName` and return its results IFF
// ALL invalidation inputs in `key` match the stored record. On a match, fills
// `outResults` + `outPosture` and returns true (the caller SKIPS the recheck).
// On a miss (no record, or any invalidation input differs) returns false
// (the caller rechecks); a DEBUG line names which input forced the miss.
bool Lookup(const InvalidationKey& key,
            std::vector<FuncResult>& outResults,
            Posture& outPosture);

// Insert or replace the record for `rec.key.pluginName` in the in-memory cache.
// Does NOT write to disk — call Save() once after all plugins are processed.
void Upsert(const Record& rec);

// Write the in-memory cache to <cache>/version_check.bin (creating the cache/
// directory if absent, idempotent). Atomic-ish: writes to a temp file then
// renames over the target so a crash mid-write cannot leave a half-file the
// next Load() would reject. Returns true on success; on any I/O failure logs a
// WARN naming the path/cause and returns false (the next launch simply rechecks
// — a failed write is never fatal).
bool Save();

// Reset the in-memory cache to empty. For tests / self-test isolation.
void Reset();

}  // namespace kcdx::version_check_cache
