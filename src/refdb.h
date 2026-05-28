#pragma once

// kcdx reference database (refdb) — read-only SQLite-backed lookup module.
//
// Owns the engine's connection to reference.sqlite (the curated reference
// database that ships in the kcdx release archive). The database is CANONICAL:
// it carries, per curated entity, the per-version data the engine needs to
// resolve a hook target by name or id to its address and argument shape, and
// to keep installed plugins working across game updates.
//
// Schema shape (two curated tables; verification state is DERIVED, not stored):
//   address_names     — one row per curated entity, ever. `id` IS the kcdx_id
//                       (the PK is the handle). Carries `name`, plus the
//                       entity-level edges `superseded_by` /
//                       `superseded_at_version` (version-scoped supersession
//                       chain — the engine walks at query time, each hop gated
//                       on V >= superseded_at_version) and `is_deprecated` /
//                       `deprecated_at_version` / `deprecation_replacement`
//                       (informational; not a chain — the engine warns but
//                       still resolves).
//   address_versions  — one row per (entity, version-interval). The
//                       [valid_from, valid_through] interval describes the
//                       range of game versions the row's facts (rva,
//                       signature, content_hash, …) hold for. `valid_through
//                       IS NULL` marks the OPEN (current) form. The engine
//                       picks the row whose interval covers V; on miss, the
//                       row whose interval endpoint is closest to V by
//                       component distance (major / minor / build) — see
//                       refdb.cpp PickBestVersionRow.
//
// There is NO `status` column. Verification state at the running game version
// V is DERIVED from the four-state machine:
//   1. entity.is_deprecated AND V >= entity.deprecated_at_version → DEPRECATED.
//   2. Else, walked through entity.superseded_by                  → SUPERSEDED.
//   3. Else if row.last_verified_at_version >= V AND
//      row.valid_from <= V                                        → VERIFIED.
//   4. Else                                                       → UNVERIFIED.
// The engine resolves in ALL four states (informational, not a gate); a
// SUPERSEDED / DEPRECATED / UNVERIFIED resolve emits a once-per-session
// warning, with a routed plugin-log line when the caller is plugin-attributed.
//
// Resolve flow (canonical):
//   1. address_names where name = ? (or id = ?) → the entity row.
//   2. Walk the supersession chain: while entity.superseded_by IS NOT NULL
//      AND V >= entity.superseded_at_version, follow the edge to the next
//      entity. No cycle protection — the DB builder guarantees no cycles.
//   3. address_versions where kcdx_id = final-entity.id, picking the row
//      whose [valid_from, valid_through] interval BEST MATCHES V by
//      component-distance (see PickBestVersionRow).
//   4. Derive verification state per the rule above; surface a warning at
//      resolve time when the state is SUPERSEDED, DEPRECATED, or UNVERIFIED.
//      Resolution succeeds in all cases.
//
// This module is STANDALONE read logic — it owns the SQLite connection and
// the resolution queries. address_library stays SQLite-free.
//
// Threading: the connection is THREADSAFE=2 (one connection per thread). Open()
// runs on the worker thread; all of refdb's calls must run on that same thread.
//
// Fail-loud contract (a missing/unusable database is never a silent empty):
// every load/query failure logs a structured ERROR/DEBUG line naming WHAT
// failed and WHY, with a stable reason token a debugger can grep. The reason
// tokens are documented at each return site below.
//
//   db_not_loaded                  — reference.sqlite not found / could not be
//                                     opened, or a query ran before Open()
//                                     succeeded.
//   schema_version_mismatch        — meta.schema_version != kExpectedSchemaVersion,
//                                     or the dictionaries are malformed.
//   no_game_version_row            — the running build's tag is absent from
//                                     game_versions (WARN at Open(); resolves
//                                     still proceed against the best-matching
//                                     row).
//   name_unknown                   — no address_names row carries this name/id.
//   no_open_version                — the entity exists in address_names but no
//                                     address_versions row covers it at all
//                                     (a data bug — every curated entity must
//                                     have at least one version row).
//   query_error                    — a sqlite3 step/prepare returned an error
//                                     code.

#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::refdb {

// The schema shape this engine build understands. A database whose
// meta.schema_version differs is rejected at Open() (fail loud) rather than
// read with a guessed-at layout.
constexpr int kExpectedSchemaVersion = 1;

// Returns the linked SQLite library's version string (e.g. "3.50.4").
// Doubles as the compile/link-time proof the vendored SQLite is reachable.
const char* SqliteVersion();

// Result of a resolve-by-NAME lookup (the curated path).
//
// The lookup walks the address_names supersession chain at the running game
// version, picks the address_versions row whose [valid_from, valid_through]
// interval best matches V (covering row if any, otherwise closest endpoint
// by major/minor/build component distance), and derives the verification
// state. Resolution succeeds in all four verification states; the state is
// surfaced on the result + as a once-per-session warning at resolve time.
struct NameResolution {
    bool        found = false;

    uint64_t    kcdx_id = 0;     // the stable entity id (post-supersession walk).
    uint64_t    rva = 0;         // address in the running version (from address_versions).

    // The VERIFIED argument signature for the picked form — a real ABI, not
    // the floor. Empty for kinds that carry no signature (callsite, vtable
    // slot, data slot); empty here is legitimate, NOT a failure.
    std::string verified_signature;

    std::string kind;            // decoded address_versions.kind (e.g. "function", "callsite").

    // offset (for a callsite) / vtable_slot (for a vtable_index) are NULL in
    // the database for kinds that do not use them. has_offset / has_vtable_slot
    // distinguish "the row carries this value" from "absent" — a 0 is a real
    // value, not a sentinel for missing.
    bool        has_offset = false;
    int64_t     offset = 0;
    bool        has_vtable_slot = false;
    int64_t     vtable_slot = 0;

    // address_versions.value — the resolved integer for a non-byte entity (a
    // vtable-slot index, a data-slot offset); NULL (has_value=false) for
    // functions.
    bool        has_value = false;
    int64_t     value = 0;

    std::string content_hash_hex;  // address_versions.content_hash as hex; empty if NULL.

    // address_versions.content_hash as the RAW 32-byte blob (empty if NULL) —
    // the survival check needs the raw bytes to compare against an on-disk
    // BLAKE3, not the hex form. Same source column as content_hash_hex; both
    // populated together. Appended at the end of the struct (append-only).
    std::vector<uint8_t> content_hash;

    // address_versions.length — the byte span the content_hash was computed
    // over ([rva, rva+length) on the on-disk binary). The survival check needs
    // this to know HOW MANY bytes to hash; content_hash alone is insufficient.
    // 0 when the column is NULL (a non-byte entity carries no span).
    // has_length distinguishes "the row carries a span" (length may
    // legitimately be any value) from "absent". Appended at the end
    // (append-only).
    bool        has_length = false;
    int64_t     length = 0;

    // === APPEND-ONLY ADDITIONS (derived from the new schema's entity-level
    // edges and the picked address_versions row at the running game version) ===

    // The name that was actually resolved (after supersession walk). Equal to
    // the input `name` when no supersession happened; the FINAL successor's
    // name when a chain was walked.
    std::string resolved_name;

    // True if the supersession chain was walked (resolved_name != input name).
    bool        was_superseded = false;

    // True if entity.is_deprecated AND V >= deprecated_at_version on the
    // FINAL (post-walk) entity row.
    bool        is_deprecated = false;

    // Verification state at running game version V. Informational; callers
    // don't gate on this (resolution succeeded regardless).
    enum class VerificationState {
        Verified,
        Unverified,
        Deprecated,
        Superseded,
    };
    VerificationState verification_state = VerificationState::Verified;
};

// Result of a resolve-by-KCDX_ID lookup (the by-id path).
//
// The user database carries only curated entities (every address_versions row
// has a non-NULL kcdx_id pointing at an address_names row). ResolveById loads
// the address_names row by id (honouring supersession at the running game
// version, same as ResolveByName) and reads the best-matching
// address_versions row. The returned signature is the floor only (an honest
// lower bound, never a verified type) — `signature_is_floor_estimate` is
// ALWAYS true on a found result; callers that need the VERIFIED signature
// must resolve by name.
struct IdResolution {
    bool        found = false;

    uint64_t    kcdx_id = 0;     // the effective id (post-supersession walk).
    uint64_t    rva = 0;

    // The auto-derived argument-width floor (e.g. "? (i64, i32)"): one
    // width-typed slot per detected argument, return unknown ('?'). A lower
    // bound, never a verified type. May be empty (no floor derived).
    std::string floor_signature;
    bool        signature_is_floor_estimate = true;  // groundwork: the floor is ALWAYS marked an estimate.

    int64_t     observed_arg_slots = 0;     // the floor's slot count (a lower bound).
    int64_t     caller_reg_arg_count = 0;   // caller-side register-arg estimate (≤4); tighter lower bound.

    bool        has_value = false;
    int64_t     value = 0;

    std::string content_hash_hex;

    // address_versions.content_hash as the RAW 32-byte blob (empty if NULL) —
    // see NameResolution::content_hash. Appended at the end (append-only).
    std::vector<uint8_t> content_hash;

    // address_versions.length — the byte span content_hash was computed over.
    // See NameResolution::length. 0 + has_length=false when the column is NULL.
    // Appended at the end (append-only).
    bool        has_length = false;
    int64_t     length = 0;

    // === APPEND-ONLY ADDITIONS (same shape as NameResolution; no
    // resolved_name field since the by-id path has no input name) ===

    bool        was_superseded = false;
    bool        is_deprecated = false;
    enum class VerificationState {
        Verified,
        Unverified,
        Deprecated,
        Superseded,
    };
    VerificationState verification_state = VerificationState::Verified;
};

// Caller context for a Resolve* call.
//
// Refdb routes its SUPERSEDED / DEPRECATED / UNVERIFIED warnings to two
// destinations: an engine-log INFO line on every call (with `plugin_handle`
// + `call_type` for grep/audit), and a plugin-log WARN line when the caller
// is plugin-attributed (so the plugin author sees the warning in their own
// log without trawling the engine log).
//
// Both routes share the same per-(pluginHandle, callType, name) once-per-
// session dedup so a tight resolve loop in a plugin does not flood either
// log. Engine-internal resolves (pluginHandle == 0, callType == nullptr)
// emit only the engine-log line.
//
// The default-constructed value is engine-internal — existing call sites
// pass nothing and stay correctly attributed as engine-internal.
struct CallerContext {
    uint32_t    pluginHandle = 0;     // 0 = engine-internal (no plugin-log routing).
    const char* callType = nullptr;   // e.g. "kcdx.hook" / "kcdx.bytes"; nullptr for engine-internal.
};

// Open the reference database for the running game version.
//
// Resolves <game-bin>/kcdx-engine/data/reference.sqlite, opens it READ-ONLY
// (sqlite3_open_v2 + SQLITE_OPEN_READONLY — the engine never writes it),
// verifies meta.schema_version == kExpectedSchemaVersion, loads the small
// dictionary tables into memory, finds the running build's game_versions
// row (matching kcdx::plugins::g_runtimeGameVersionString against
// game_versions.tag), and caches its id + ordinal for the verification-state
// derivation and the version-component closest-match.
//
// Returns true on success. On ANY failure (database absent/unopenable, schema
// mismatch, running version not in game_versions) returns false after logging
// a fail-loud ERROR naming the path/cause/consequence and the distinct reason
// token. After a failed Open(), the connection stays closed and every Resolve*
// returns a not-found result whose logged reason is db_not_loaded — never a
// silent empty.
//
// Must run on the worker thread (THREADSAFE=2: one connection per thread).
bool Open();

// True iff Open() succeeded and the connection is live (schema + version both
// validated). Resolve* calls check this internally; exposed for callers/tests.
bool IsLoaded();

// Resolve a curated NAME to its address + verified facts. See NameResolution.
// Resolution succeeds in all four verification states (Verified, Unverified,
// Deprecated, Superseded); the state is surfaced on the result + as a
// once-per-session warning at resolve time. found=false (with a logged
// reason) only when the name is unknown (name_unknown), the entity has no
// address_versions row at all (no_open_version — data bug), or a SQLite
// query errored (query_error).
//
// The supersession chain is walked at the running game version: each hop is
// gated on V >= superseded_at_version. The returned `resolved_name` /
// `was_superseded` reflect the walk; `kcdx_id` is the FINAL successor's id.
NameResolution ResolveByName(const std::string& name,
                             const CallerContext& ctx = {});

// Resolve a stable KCDX_ID (= address_names.id) to its address + the
// address_versions argument-width FLOOR. See IdResolution. Same supersession
// walk + same warning routing as ResolveByName; only the input handle and
// the absence of a `resolved_name` field differ.
IdResolution ResolveById(uint64_t kcdx_id,
                         const CallerContext& ctx = {});

// Close the connection. Idempotent. Clears the warning-dedup set so a fresh
// Open() emits its warnings again. (Whether dev mode keeps the connection
// open vs. non-dev closes it after launch-time resolution is the CALLER's
// decision at the later init step — resolution is identical in both; only
// connection lifetime differs. This module does not gate on dev mode.)
void Close();

}  // namespace kcdx::refdb
