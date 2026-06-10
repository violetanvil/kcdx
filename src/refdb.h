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
#include <functional>
#include <string>
#include <string_view>
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

    // address_versions.struct_offset — the AUTHORED vtable/struct byte offset
    // (e.g. IConsole vtable +0xB8) for a kind that consumes one; NULL
    // (has_struct_offset=false) otherwise — a 0 is a real value, not a
    // sentinel for missing. Appended at the END (append-only). Plumbed but not
    // yet consumed by any caller (the hardcoded-address migration is the first
    // consumer).
    bool        has_struct_offset = false;
    int64_t     struct_offset = 0;

    // === FOLDED SURVIVAL/RE-FIND COLUMNS (D22 — the survival sibling table's
    // genuinely-survival-only facts, now nullable typed columns ON
    // address_versions). Each maps 1:1 to its av column (§11.3 comprehensiveness
    // contract: every folded column the engine consumes has a field here, every
    // field here has a backing column). Gated by `kind` like every other
    // location/survival cell. Appended at the END (append-only). Plumbed but not
    // yet consumed by any caller (the survival pass reads content_hash/length
    // today; these carry the re-find forms a future survival/re-find path uses). ===

    // address_versions.aob — the AOB pattern (bytes + folded '?' wildcard mask)
    // for an aob-form kind (callsite / instruction_anchor). Empty when NULL —
    // empty is legitimate (the kind carries no aob), like verified_signature.
    std::string aob;

    // address_versions.anchor_string — the literal anchor bytes for a
    // string_anchor kind. Empty when NULL (legitimate).
    std::string anchor_string;

    // address_versions.rule — the derivation rule for a data_slot kind
    // (e.g. disp32@<kid> / <kid>-0xA8). Empty when NULL (legitimate).
    std::string rule;

    // address_versions.slot_count — the expected slot count for a table_shape
    // kind (vtable_base); NULL (has_slot_count=false) otherwise — a 0 is a real
    // value, not a sentinel for missing.
    bool        has_slot_count = false;
    int64_t     slot_count = 0;

    // address_versions.expect_unique — the 0/1 AOB-unique / unique-xref
    // assertion for a search-locating kind; NULL (has_expect_unique=false)
    // otherwise — a 0 (assert NOT unique) is a real value.
    bool        has_expect_unique = false;
    int64_t     expect_unique = 0;

    // address_versions.derives_from — the survival-DAG self-FK
    // (→ address_versions.id) for a derivation form; NULL
    // (has_derives_from=false) otherwise — a 0 is a real value.
    bool        has_derives_from = false;
    int64_t     derives_from = 0;

    // True iff the picked address_versions row's [valid_from, valid_through]
    // interval ACTUALLY includes the running build version V (valid_from <= V
    // AND V <= valid_through, with valid_through NULL meaning open-ended). This
    // is the PRECISE coverage signal: it distinguishes the running build being
    // genuinely outside the row's recorded interval (false — the row is not for
    // this build) from the row covering V but not being freshly re-verified at V
    // (true, yet verification_state may still be Unverified — covered, only the
    // re-verification is stale). verification_state==Unverified lumps both
    // cases; this field separates them. Default false (a not-found / unresolved
    // result has no covering interval). Appended at the END (append-only).
    bool        interval_covers_version = false;
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

// =============================================================================
// Statement-resolution surface (Phase 9.3 prerequisite — the consumer the
// kcdx.locator.* / kcdx.op.* / kcdx.statement.* binders call).
//
// A curated function's statements ship in reference.sqlite (the curated subset)
// and are eager-loaded at Open() into per-function idx-ordered vectors keyed by
// the function's address_versions.id. This surface resolves a §9.3 LOCATOR
// descriptor (a "which statement" selector) to a statement INDEX within a
// resolved function, and exposes the per-statement reads the binders need.
//
// Resolution is a startup/install-time concern (a hook installs once, or the
// self-test runs at init) — NOT a per-call hot path. It reads the in-memory
// statement cache (no per-call SQL).
// =============================================================================

// The §9.3 locator catalog (00-original-plan.md "Phase 9.3" — the kcdx.locator.*
// values). Every form below resolves against the in-memory statement cache,
// EXCEPT MatchingPattern (the labeled expert raw-AOB hatch), which resolves
// against BYTES elsewhere (the byte-scan path) — NOT statement metadata. A
// StatementLocator carrying MatchingPattern returns found=false here with the
// reason `matching_pattern_not_statement_locator`; the byte hatch is resolved by
// a different path, not this one.
enum class StatementLocatorKind {
    FunctionEntry,       // function_entry()       — first statement by idx.
    FunctionExit,        // function_exit()        — last statement by idx.
    FirstCallTo,         // first_call_to(fn)      — first statement, callee == fn.
    LastCallTo,          // last_call_to(fn)       — last statement, callee == fn.
    CallTo,              // call_to(fn)            — unique statement, callee == fn (ERROR if multiple).
    FirstReturn,         // first_return()         — first statement, kind == "return".
    LastReturn,          // last_return()          — last statement, kind == "return".
    ReturnValue,         // return_value(v)        — first return statement whose pseudo_text references v.
    ReferencesString,    // references_string(s)   — first statement, string_ref == s.
    FirstReadOfCvar,     // first_read_of_cvar(n)  — first statement, string_ref == n (cvar name in string_ref).
    Matching,            // matching{...}          — first statement matching ALL provided keys (AND).
    MatchingPattern,     // matching_pattern("..") — expert raw-AOB hatch; NOT a statement-metadata locator.
};

// A §9.3 locator descriptor: the kind + the operands the kind consumes. Unused
// fields stay empty/false for kinds that don't read them. For Matching, any
// SUBSET of the has_* keys may be set; the matcher ANDs every provided key.
//
//   FirstCallTo/LastCallTo/CallTo   read `callee_or_fn`.
//   ReturnValue                     reads `return_value_operand`.
//   ReferencesString/FirstReadOfCvar read `string_arg`.
//   Matching reads the has_*-gated keys: match_kind, match_callee,
//     match_condition_contains, match_reads_cvar, match_references_string.
//   MatchingPattern                 reads `aob_pattern` (resolved elsewhere, not here).
struct StatementLocator {
    StatementLocatorKind kind = StatementLocatorKind::FunctionEntry;

    std::string callee_or_fn;          // FirstCallTo / LastCallTo / CallTo.
    std::string return_value_operand;  // ReturnValue — the operand text matched in pseudo_text.
    std::string string_arg;            // ReferencesString / FirstReadOfCvar.
    std::string aob_pattern;           // MatchingPattern (the labeled expert hatch).

    // Matching{} keys — each gated by its has_ flag (any subset; ANDed). An
    // empty has_-set Matching{} matches the FIRST statement (no constraint).
    bool        has_match_kind = false;               std::string match_kind;                // → CachedStatement.kind
    bool        has_match_callee = false;             std::string match_callee;              // → callee
    bool        has_match_condition_contains = false; std::string match_condition_contains;  // → pseudo_text (substring)
    bool        has_match_reads_cvar = false;         std::string match_reads_cvar;          // → string_ref
    bool        has_match_references_string = false;  std::string match_references_string;   // → string_ref
};

// A captured variable joined onto a resolved statement (the captures-by-name
// join, 2c). Mirrors the PUBLIC shape of refdb.cpp's CachedReferencedVar: the
// per-variable facts the kcdx.captures.* surface reads (storage_kind /
// storage_detail decode the var's location; data_type + size_bytes its type).
// has_size_bytes distinguishes "the row carries a size" (size_bytes may be any
// value, including 0) from "absent" — a 0 is a real value, not a sentinel.
struct StatementCapture {
    std::string var_name;        // referenced_vars.var_name; empty when NULL.
    std::string storage_kind;    // decoded (e.g. "stack", "register").
    std::string storage_detail;  // referenced_vars.storage_detail; empty when NULL.
    std::string data_type;       // decoded (e.g. "int", "ptr"); empty when NULL.
    int64_t     size_bytes = 0;
    bool        has_size_bytes = false;
};

// Result of a statement-locator resolution within a resolved curated function.
//
// found=true → statement_idx is the resolved statement's idx (within the
// function's idx-ordered vector) and the per-statement reads below are that
// statement's facts. found=false → a logged reason token (below), NEVER a
// silent empty (AP14). The reason tokens:
//   db_not_loaded                  — the database is not open.
//   function_no_statements         — the av_id has no statement vector (the
//                                     function is non-curated, a non-function
//                                     kind with no statements, or unknown).
//   locator_no_match               — the locator matched zero statements.
//   call_to_ambiguous              — call_to(fn) matched MULTIPLE statements
//                                     (the §9.3 "errors if multiple" form).
//   matching_pattern_not_statement_locator — a MatchingPattern locator (the
//                                     expert AOB hatch) was handed to this
//                                     statement-metadata path; it resolves
//                                     against bytes elsewhere, not here.
//   name_unknown                   — the by-name resolve (ResolveStatementByName
//                                     / ById) found no curated function of that
//                                     name/id; set by the name/id resolvers.
struct StatementResolution {
    bool        found = false;
    int64_t     statement_idx = 0;     // the resolved statement's idx.

    // Per-statement reads for the resolved statement (the kcdx.op.* fit
    // decision reads byte_range_len). callee / string_ref empty when the
    // column is NULL (legitimate). has_byte_range_* distinguishes "carries a
    // value" from "absent" — a 0 is a real value.
    std::string kind;                  // decoded statements.kind (e.g. "call", "return").
    std::string callee;                // statements.callee; empty when NULL.
    std::string string_ref;            // statements.string_ref; empty when NULL.
    std::string pseudo_text;           // statements.pseudo_text; empty when NULL.
    bool        has_byte_range_start = false;
    int64_t     byte_range_start = 0;
    bool        has_byte_range_len = false;
    int64_t     byte_range_len = 0;

    // The resolved statement's captured variables (the captures-by-name join,
    // 2c): joined from referenced_vars by (address_version_id, statement_idx)
    // at resolution. A statement with no captured vars → an EMPTY vector
    // (legitimate, not an error — AP14: absence is observable, not silent). The
    // join is an in-memory hash hit built ONCE at resolution (no per-call SQL —
    // memory.md). Appended at the END (append-only, free struct shape).
    std::vector<StatementCapture> captures;

    // The not-found reason token, populated on found=false (one of the tokens
    // documented above: db_not_loaded / function_no_statements /
    // locator_no_match / call_to_ambiguous / matching_pattern_not_statement_locator
    // / name_unknown). Empty when found=true. Carries the SAME token the
    // not-found log line records, so a consumer that surfaces resolution to the
    // user (the kcdx.locator.* :resolve accessor) can report WHICH miss happened
    // without re-deriving it from the log. Appended at the END (append-only).
    std::string reason;
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

// =============================================================================
// Cache-backed convenience helpers — added when refdb took ownership of the
// curated cache (see CachedEntity in refdb.cpp).
//
// Open() bulk-builds an in-memory cache of every curated entity resolved at
// the running game version (closest-match version row + supersession walk +
// verification state — all the algorithms ResolveByName runs, executed once at
// build instead of per-call). The helpers below are thin wrappers over the
// cache so engine-internal callers don't write
//   auto r = refdb::ResolveByName(name); return r.found ? WhgameBase() + r.rva : 0;
// boilerplate at every site.
//
// All of these are LAUNCH-TIME safe (the cache is built inside Open() and
// stays resident); they're also fine on hot paths because every lookup is an
// in-memory hash hit.
// =============================================================================

// Convenience: name → VA (WhgameBase() + rva), 0 on miss. Logs the miss
// reason at the underlying ResolveByName boundary. Used by engine-internal
// call sites that just need an address. The CallerContext default is
// engine-internal — every existing site stays correctly attributed.
uintptr_t ResolveAddrByName(const std::string& name,
                            const CallerContext& ctx = {});

// Convenience: id → VA. Same purpose as ResolveAddrByName.
uintptr_t ResolveAddrById(uint64_t kcdx_id,
                          const CallerContext& ctx = {});

// Verified ABI by name (empty string_view on miss, NEVER nullptr — the view
// always points at a stable C string in the cache row, including the empty
// terminator). Used by hook installation paths.
std::string_view SignatureByName(const std::string& name,
                                 const CallerContext& ctx = {});

// Iterate every cached entry. Stops iterating when `cb` returns false.
// Callback receives kcdx_id, the resolved (post-supersession) name, the
// resolved VA (WhgameBase() + rva, 0 if the row has no rva or WHGame.dll is
// not mapped), and the verification state.
void ForEachCached(
    const std::function<bool(uint64_t kcdx_id,
                             const std::string& name,
                             uintptr_t va,
                             NameResolution::VerificationState state)>& cb);

// Number of cache rows (== entities resolvable at the running game version).
size_t CachedRowCount();

// The picked address_versions.id for a resolved (post-supersession) entity —
// the D34 attribution handle the startup verification pass reports as the
// matched row. The input is the RESOLVED kcdx_id (the value ForEachCached
// yields), not the original. Returns true + writes avIdOut iff a cached entity
// carries that id; false (no write) on miss. Read-only cache lookup, launch-time
// safe. (NameResolution/IdResolution deliberately do not carry the
// address_versions.id; this thin accessor surfaces it for D34 attribution
// without widening either public resolve struct.)
bool CachedAddressVersionId(uint64_t resolvedKcdxId, int64_t& avIdOut);

// True iff the cache carries an entry for this name. Does NOT fire any
// SUPERSEDED/DEPRECATED/UNVERIFIED warning — pure presence check. Used by the
// address_library precedence walk to detect bare-name collisions between the
// engine seed and an author-declared target without surfacing a per-state
// warning at every collision check.
bool HasName(const std::string& name);

// =============================================================================
// Statement-locator resolution — resolve a §9.3 locator within a resolved
// curated function (identified by name or kcdx_id) to a statement index +
// per-statement reads. The function's address_versions.id (carried on the
// resolved cache entry) keys the eager-loaded statement vectors.
//
// These run the SAME supersession walk + closest-match version pick as
// ResolveByName / ResolveById (they reuse the cache entry), then resolve the
// locator against that entity's statement vector. found=false carries a logged
// reason (StatementResolution above) — name_unknown when the function name/id
// does not resolve, function_no_statements when it resolves but carries no
// statements, locator_no_match / call_to_ambiguous / etc. per the locator.
//
// Resolution is startup/install-time — an in-memory hash hit + a linear scan of
// the function's statement vector (curated functions carry tens of statements,
// not thousands). No SQL, no per-call cost.
// =============================================================================

// Resolve a locator within the curated function named `functionName`.
StatementResolution ResolveStatementByName(const std::string& functionName,
                                           const StatementLocator& locator,
                                           const CallerContext& ctx = {});

// Resolve a locator within the curated function with stable id `kcdx_id`.
StatementResolution ResolveStatementById(uint64_t kcdx_id,
                                         const StatementLocator& locator,
                                         const CallerContext& ctx = {});

// =============================================================================
// Dev-DB cross-function SEARCH layer (Phase 9.4 step 0 — the FOUNDATION the
// kcdx.find / kcdx_dev_inspect binders, steps 1/2, consume).
//
// A SECOND, separately-opened connection to reference-dev.sqlite (the full
// ~321k-function corpus), distinct from the shipped-DB connection above. It is
// a DEV-ONLY discovery tool: opened lazily on the first find/inspect call,
// gated on dev mode + the file's presence, and never opened in production (the
// 1.3 GB dev DB must not load there).
//
// Connection model (design: docs/outstanding-work/restructure/phase-09.4-
// discovery/step-0-devdb-search-layer.md §"Connection model"):
//   - g_devDb — a SECOND sqlite3*, opened lazily on the first call, NOT at
//     Open(). Distinct from g_db (the shipped connection); g_db is untouched.
//   - Gate (both required): dev mode ON (kcdx::log::IsDevModeEnabled()) AND the
//     file present at <game-bin>/kcdx-engine/data/reference-dev.sqlite.
//   - SQLITE_OPEN_READONLY + the same meta.schema_version == kExpectedSchema
//     Version gate the shipped DB uses (fail-loud on mismatch).
//
// Fail-loud contract (same as the shipped surface): every gate-off / missing-DB
// / query-error path logs a structured reason token, NEVER a silent empty. The
// dev-DB reason tokens (logged under the DEVDB category):
//   dev_mode_off        — OpenDevDb called with dev mode disabled.
//   dev_db_not_found    — reference-dev.sqlite absent / could not be opened.
//   dev_schema_mismatch — meta.schema_version != kExpectedSchemaVersion (or
//                         unreadable).
//   dev_db_unavailable  — a Find/Enumerate call could not lazy-open the dev DB
//                         (one of the three gate failures above happened); the
//                         binder maps this to the dev-tool-unavailable teaching
//                         message.
//   query_error         — a sqlite3 prepare/step returned an error code.
//   name_unknown        — EnumerateStatements found no function of that
//                         name / auto_name.
// =============================================================================

// Lazy-open the dev DB. Idempotent (a second call with the connection already
// live returns true immediately). Gated: returns false (with a logged reason
// token — dev_mode_off / dev_db_not_found / dev_schema_mismatch) when dev mode
// is off, the file is absent/unopenable, or the schema_version differs. On
// success opens a SECOND SQLITE_OPEN_READONLY connection (g_devDb); g_db is
// untouched. Must run on the worker thread (THREADSAFE=2: one connection per
// thread, same constraint as Open()).
bool OpenDevDb();

// True iff OpenDevDb() succeeded and the dev connection is live.
bool IsDevDbLoaded();

// Close the dev connection. Idempotent. Also called from Close().
void CloseDevDb();

// The six optional FindFunctions criteria. Each field is meaningful only when
// its has_ flag is set; an unset field is ignored (no constraint). The
// at-least-one-of-N validation is the BINDER's job (step 1) — this struct just
// carries the criteria. FindFunctions with NO criteria set returns an empty
// result (the binder rejects the no-criteria call earlier).
//
// Per-criterion query (design §"FindFunctions(criteria) — per-criterion query";
// each yields a set of owning address_version_ids, multi-criterion = AND):
//   string               — statements.string_ref = ?
//   cvar                 — statements.string_ref = ? (cvar names live in
//                          string_ref; same path as string, the cvar-typed lens)
//   callers_of           — statements.callee = ? (the callers of ?; full
//                          321k coverage via the TEXT callee column)
//   callee               — statements.callee = ? (owning functions)
//   name_contains        — address_names.name LIKE %?% (curated) UNION
//                          address_versions.auto_name LIKE %?%
//   callee_in_subsystem  — statements.callee LIKE <prefix>%
//
// call_edges is UNUSED (user-confirmed; it is curated-only, NULL for 320,987 of
// 321,144 functions — useless for discovery). The TEXT statements.callee is the
// full-coverage caller path. The omission is deliberate, the data shape forces
// it — do NOT "fix" it to use call_edges.
struct FindCriteria {
    bool        has_string = false;               std::string string;
    bool        has_cvar = false;                 std::string cvar;
    bool        has_callers_of = false;           std::string callers_of;
    bool        has_callee = false;               std::string callee;
    bool        has_name_contains = false;        std::string name_contains;
    bool        has_callee_in_subsystem = false;  std::string callee_in_subsystem;
};

// A captured variable on a found-function statement (the referenced_vars join).
// Mirrors StatementCapture's PUBLIC shape (storage_kind/data_type decoded from
// the dev DB's dicts); kept a distinct type so the dev-search surface is
// self-contained. has_size_bytes distinguishes a real 0 from absent.
struct FindCapture {
    std::string var_name;        // referenced_vars.var_name; empty when NULL.
    std::string storage_kind;    // decoded (e.g. "stack", "register").
    std::string storage_detail;  // referenced_vars.storage_detail; empty when NULL.
    std::string data_type;       // decoded (e.g. "int", "ptr"); empty when NULL.
    int64_t     size_bytes = 0;
    bool        has_size_bytes = false;
};

// One statement of a found function (the design's record §"statements = [{idx,
// kind, pseudo_text, captures, applicable_ops}]"). idx/kind/pseudo_text come
// straight from the statements row (kind decoded via _dict_statements_kind);
// captures are the referenced_vars join for this statement.
struct FindStatement {
    int64_t     idx = 0;             // statements.idx (position within the function).
    std::string kind;               // decoded statements.kind (store/call/return/…).
    std::string pseudo_text;        // statements.pseudo_text; empty when NULL.
    std::string callee;             // statements.callee; empty when NULL.
    std::string string_ref;         // statements.string_ref; empty when NULL.

    // The statement's captured variables (referenced_vars joined by
    // (address_version_id, statement_idx)). Empty vector = no captures
    // (legitimate, not an error).
    std::vector<FindCapture> captures;

    // applicable_ops (design names it on the record): the kcdx.op.* op NAMES
    // that fit this statement, derived from kind — the real Phase-9.3 catalog
    // names whose required-statement-kind matches, restricted to the kinds the
    // corpus emits (branch→always/never_take_branch + invert_branch_condition;
    // call→skip_call_void/skip_call_return_value/replace_call_target; return→
    // replace_with_return/replace_return_value; assign→replace_assignment_value),
    // with replace_with_noop (applies to any statement) on every kind.
    // replace_compare_constant is omitted (its kind `compare` is never emitted).
    // The author uses these names verbatim in kcdx.statement.replace_with(...).
    std::vector<std::string> applicable_ops;
};

// One found function (the design's record §"Result record": {function, module,
// rva, decompile_quality, statements}). `function` = the curated
// address_names.name when kcdx_id is non-NULL, else address_versions.auto_name.
struct FindRecord {
    std::string function;          // display name (curated name or auto_name).
    std::string module;            // module name (from modules.name via module_id).
    uint64_t    rva = 0;           // address_versions.rva.
    int         decompile_quality = 0;  // decoded _dict_address_versions_decompile_quality (0 = unknown).
    std::string decompile_quality_label;  // the decoded dict label ("clean"/"unanalyzable"/""); empty if unknown.
    std::vector<FindStatement> statements;
};

// FindFunctions result. `truncated` + `total_matches` are the LOUD over-cap
// signal (design §"Cap 500"): when the match set exceeds 500, records carries
// the first 500 (post-ranking), truncated = true, total_matches = the full
// count. A gate failure (dev DB unavailable) returns an empty result whose
// `unavailable` flag is set + a dev_db_unavailable reason logged — never a
// silent empty the binder cannot distinguish from a genuine zero-match.
struct FindResult {
    std::vector<FindRecord> records;
    bool        truncated = false;
    int64_t     total_matches = 0;

    // True when the dev DB could not be opened (gate failure) — distinct from a
    // genuine empty match set (unavailable = false, records empty, total = 0).
    // The binder maps unavailable=true to the dev-tool-unavailable teaching
    // message; an empty-but-available result is a legitimate "no matches".
    bool        unavailable = false;
};

// The 500-record cap on FindFunctions (design §"Cap 500").
constexpr int kFindResultCap = 500;

// Cross-function search over the dev DB. Lazy-opens it (OpenDevDb); on gate
// failure returns an empty FindResult with unavailable=true + a logged
// dev_db_unavailable reason. Each set criterion yields a set of owning
// address_version_ids; multi-criterion = AND (intersect). Results ranked
// `decompile_quality ASC, rva ASC` (best-decompiled first — quality 1=clean
// sorts before 2=unanalyzable; a NULL/absent quality sorts last; deterministic
// address tiebreak), capped at kFindResultCap with the loud truncation signal.
// No criteria set → an empty result (the binder rejects the no-criteria call).
FindResult FindFunctions(const FindCriteria& criteria);

// Enumerate the idx-ordered statements of a single function, resolved by curated
// name OR auto_name (for kcdx_dev_inspect, step 2). On a successful resolve
// returns the full FindRecord (function/module/rva/decompile_quality +
// statements). On not-found returns found=false with a logged name_unknown
// reason AND a name-similarity suggestion list (the candidate names step 2's
// teaching error renders). Lazy-opens the dev DB; on gate failure returns
// found=false + unavailable=true + a logged dev_db_unavailable reason.
struct EnumerateResult {
    bool        found = false;
    FindRecord  record;            // populated iff found.

    // Up to a few candidate function names closest to the requested `fn`, for
    // the not-found teaching error. The curated address_names.name set is ranked
    // by Levenshtein edit-distance (nearest-first — a 1-char typo surfaces its
    // intended name, e.g. IsInCombatt -> IsInCombat at distance 1); auto_name
    // substring matches top it up when nothing curated is close. Empty when
    // found=true or when no near-name exists.
    std::vector<std::string> suggestions;

    // Dev DB could not be opened (gate failure) — same meaning as
    // FindResult::unavailable. Distinct from a genuine name_unknown (found=false,
    // unavailable=false).
    bool        unavailable = false;
};

// Enumerate one function's statements by curated name or auto_name. See
// EnumerateResult.
EnumerateResult EnumerateStatements(const std::string& fn);

}  // namespace kcdx::refdb
