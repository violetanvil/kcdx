#pragma once

// kcdx reference database (refdb) — read-only SQLite-backed lookup module.
//
// Owns the engine's connection to reference.sqlite (the function reference
// database that ships in the kcdx release archive). The database is CANONICAL:
// it carries, per game-binary entity, the data the engine needs to resolve a
// hook target by name or id to its address and argument shape, and to keep
// installed plugins working across game updates.
//
// This module is STANDALONE read logic — it owns the SQLite connection and the
// version-interval resolution queries. It is NOT yet wired into the engine's
// address resolution; that wire-in lands in a later step. address_library
// stays SQLite-free.
//
// Threading: the connection is THREADSAFE=2 (one connection per thread). Open()
// runs on the worker thread; all of refdb's calls must run on that same thread.
//
// Fail-loud contract (a missing/unusable database is never a silent empty):
// every load/query failure logs a structured ERROR/DEBUG line naming WHAT
// failed and WHY, with a stable reason token a debugger can grep. The reason
// tokens are documented at each return site below.
//
//   db_not_loaded        — reference.sqlite not found / could not be opened, or
//                           a query ran before Open() succeeded.
//   schema_version_mismatch — meta.schema_version != kExpectedSchemaVersion.
//   no_game_version_row  — the running build's tag is absent from game_versions.
//   name_unknown         — no kcdx_overlay row for the requested name.
//   no_entity_version    — no covering entity_versions interval (no address).
//   no_overlay_version   — no covering kcdx_overlay_versions interval.
//   not_verified         — the covering overlay-version row's status != verified.
//   query_error          — a sqlite3 step/prepare returned an error code.

#include <cstdint>
#include <string>

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
// A curated overlay name resolves through three covering-interval lookups:
// kcdx_overlay (name → kcdx_id, kind) → entity_versions (rva) →
// kcdx_overlay_versions (verified signature + offset + vtable_slot), gated on
// status == "verified". When `found` is false the lookup failed for one of the
// fail-loud reasons above (already logged); every field is then at its default
// and must not be read as a resolution.
struct NameResolution {
    bool        found = false;

    uint64_t    kcdx_id = 0;     // the stable entity id the name annotates.
    uint64_t    rva = 0;         // address in the running version (from entity_versions).

    // The VERIFIED argument signature for this version range — a real ABI, not
    // the floor. Empty for kinds that carry no signature (callsite, vtable
    // slot, data slot); empty here is legitimate, NOT a failure.
    std::string verified_signature;

    std::string kind;            // decoded kcdx_overlay.kind (e.g. "function", "callsite").

    // offset (for a callsite) / vtable_slot (for a vtable_index) are NULL in
    // the database for kinds that do not use them. has_offset / has_vtable_slot
    // distinguish "the row carries this value" from "absent" — a 0 is a real
    // value, not a sentinel for missing.
    bool        has_offset = false;
    int64_t     offset = 0;
    bool        has_vtable_slot = false;
    int64_t     vtable_slot = 0;

    // entity_versions.value — the resolved integer for a non-byte entity (a
    // vtable-slot index, a data-slot offset); NULL (has_value=false) for
    // functions.
    bool        has_value = false;
    int64_t     value = 0;

    std::string content_hash_hex;  // entity_versions.content_hash as hex; empty if NULL.
    std::string status;            // decoded status ("verified" for a found row).
};

// Result of a resolve-by-KCDX_ID lookup (the bulk / un-curated path).
//
// A bulk id has no kcdx_overlay row, so there is no verified signature — only
// the entity_versions argument-width FLOOR (an honest lower bound, never a
// verified type). `signature_is_floor_estimate` is ALWAYS true on a found
// result: the floor must never be conflated with a verified signature. A
// curated id resolves here too (its entity_versions row exists); use the
// resolve-by-name path to obtain that entity's verified signature.
struct IdResolution {
    bool        found = false;

    uint64_t    kcdx_id = 0;
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
};

// Open the reference database for the running game version.
//
// Resolves <game-bin>/kcdx-engine/reference.sqlite, opens it READ-ONLY
// (sqlite3_open_v2 + SQLITE_OPEN_READONLY — the engine never writes it),
// verifies meta.schema_version == kExpectedSchemaVersion, loads the small
// dictionary tables into memory, and finds the running build's game_versions
// row (matching kcdx::plugins::g_runtimeGameVersionString against
// game_versions.tag), caching its id + ordinal for the covering-interval
// predicate.
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

// Resolve a curated overlay NAME to its address + verified facts. See
// NameResolution. found=false (with a logged reason) when the name is unknown,
// there is no covering entity/overlay interval, or the covering overlay row is
// not verified.
NameResolution ResolveByName(const std::string& name);

// Resolve a stable KCDX_ID to its address + the entity_versions argument-width
// FLOOR. See IdResolution. found=false (with a logged reason) when there is no
// covering entity_versions interval for the id.
IdResolution ResolveById(uint64_t kcdx_id);

// Close the connection. Idempotent. (Whether dev mode keeps the connection
// open vs. non-dev closes it after launch-time resolution is the CALLER's
// decision at the later init step — resolution is identical in both; only
// connection lifetime differs. This module does not gate on dev mode.)
void Close();

}  // namespace kcdx::refdb
