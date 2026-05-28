#include "refdb.h"

#include <sqlite3.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "paths.h"
#include "plugin_loader.h"  // kcdx::plugins::g_runtimeGameVersionString

// Compile-time proof the vendored SQLite header is the version we built
// against (3.50.x). If the include resolves to a stale/older header this
// fails the build rather than silently linking a mismatched ABI.
static_assert(SQLITE_VERSION_NUMBER >= 3050000,
              "kcdx refdb requires SQLite 3.50.0 or newer");

namespace kcdx::refdb {

namespace {

// ---------------------------------------------------------------------------
// Module state. THREADSAFE=2: this connection belongs to the worker thread
// that called Open(); it must not be touched from another thread.
// ---------------------------------------------------------------------------

sqlite3* g_db = nullptr;     // null = not loaded (Open failed or never ran).
bool     g_loaded = false;   // true only after schema + version both validated.

// The running build's game_versions row, cached at Open(). The ordinal is
// the monotonic sort key the verification-state derivation compares against
// (last_verified_at_version, valid_from, valid_through are FKs to
// game_versions.id; we look up their ordinals via a small in-memory id→ordinal
// map). The id is kept for diagnostic logging.
int64_t  g_gameVersionId = 0;
int64_t  g_gameVersionOrdinal = 0;
std::string g_gameVersionTag;  // copy of the running build's tag for closest-match comparison.

// The parsed components of the running build's tag (major.minor.build).
// Used by the closest-match component-distance comparison in
// PickBestVersionRow. When the running tag does not parse (mismatch mode +
// degenerate tag), every component is 0 and the closest-match still ranks
// rows consistently — the running build just sorts as 0.0.0.
int      g_runningMajor = 0;
int      g_runningMinor = 0;
int      g_runningBuild = 0;

// Version-mismatch mode flag. Set true at Open() when the running build's
// version tag is absent from the game_versions table (no_game_version_row).
// In mismatch mode resolves proceed against the closest-matching
// address_versions row; the WARN at Open() distinguishes the regime in the
// log. A plain bool (not std::atomic) — the existing module-state pattern
// in this TU is plain statics, all single-threaded per the THREADSAFE=2
// contract.
bool     g_versionMismatchMode = false;

// Dictionaries loaded once at Open() (id → val). The dicts are tiny
// (single-digit row counts); an in-memory map avoids a JOIN per resolve and
// keeps the resolution SQL flat. Only the `kind` dict is consumed by the
// engine — the schema's other dicts (caller_arg_agreement, evidence_kind,
// etc.) are author/audit-only.
std::unordered_map<int64_t, std::string> g_kindDict;    // _dict_address_versions_kind

// game_versions id → ordinal map. Populated at Open() (the table is tiny —
// one row per game release ever). Used to translate a row's FK columns
// (last_verified_at_version, valid_from, valid_through,
// superseded_at_version, deprecated_at_version) into their monotonic
// ordinals for V comparisons.
std::unordered_map<int64_t, int64_t> g_versionOrdinalById;
// game_versions id → tag map. Used for component-distance comparisons in
// PickBestVersionRow (the rule explicitly parses tag strings rather than
// ordering by ordinal; ordinal is fine for "<=/>=" derivation gates but
// not for "smallest-major-diff" component matching when the running build
// is not in the table).
std::unordered_map<int64_t, std::string> g_versionTagById;

// Once-per-session warning dedup. Keyed by (pluginHandle, callType, name) so
// the same plugin warning twice on the same name in the same call-type does
// not flood; separate plugins / call types / names each get their own dedup.
// The single-worker-thread contract (THREADSAFE=2) lets these be plain
// std::set; we mutate inline on the resolve path, no synchronization.
//
// One set per warning type — the same name across DEPRECATED/SUPERSEDED/
// UNVERIFIED legitimately fires three distinct warnings (one of each kind).
using WarnKey = std::tuple<uint32_t, std::string, std::string>;
std::set<WarnKey> g_warnedSuperseded;
std::set<WarnKey> g_warnedDeprecated;
std::set<WarnKey> g_warnedUnverified;

const char* kCategory = "REFDB";

// Encode a content_hash BLOB as lowercase hex; empty string for a NULL/empty
// blob. The database stores hashes as 32-byte blobs (not hex text).
std::string HashToHex(const void* blob, int nbytes) {
    if (!blob || nbytes <= 0) return std::string();
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<size_t>(nbytes) * 2);
    const unsigned char* p = static_cast<const unsigned char*>(blob);
    for (int i = 0; i < nbytes; ++i) {
        out.push_back(kHex[(p[i] >> 4) & 0xF]);
        out.push_back(kHex[p[i] & 0xF]);
    }
    return out;
}

// Decode a dict id against an in-memory dict map; empty string if the id is not
// present (a NULL dict column, or a value the dict does not carry).
std::string DecodeDict(const std::unordered_map<int64_t, std::string>& dict, int64_t id) {
    auto it = dict.find(id);
    return it == dict.end() ? std::string() : it->second;
}

// Translate a game_versions.id FK into its ordinal via the cached map.
// Returns -1 if the id is null / unknown. The verification-state derivation
// treats -1 as "absent" (never satisfies a >= V comparison).
int64_t OrdinalForVersionId(int64_t versionId) {
    if (versionId <= 0) return -1;
    auto it = g_versionOrdinalById.find(versionId);
    return it == g_versionOrdinalById.end() ? -1 : it->second;
}

// Translate a game_versions.id FK into its tag via the cached map.
// Returns empty string if the id is null / unknown.
std::string TagForVersionId(int64_t versionId) {
    if (versionId <= 0) return std::string();
    auto it = g_versionTagById.find(versionId);
    return it == g_versionTagById.end() ? std::string() : it->second;
}

// Parse a "<major>.<minor>.<build>" version tag into three integers. A
// missing component reads as 0. Returns true if at least one component
// parsed; false on a fully malformed string. The closest-match comparison
// tolerates 0 components — a "1.5" tag parses as (1, 5, 0).
bool ParseVersionTag(std::string_view tag, int* outMajor, int* outMinor, int* outBuild) {
    *outMajor = 0;
    *outMinor = 0;
    *outBuild = 0;
    if (tag.empty()) return false;
    int* slots[3] = { outMajor, outMinor, outBuild };
    size_t slot = 0;
    size_t i = 0;
    bool sawDigit = false;
    while (i < tag.size() && slot < 3) {
        // skip non-digit prefix (e.g. a leading 'v')
        while (i < tag.size() && (tag[i] < '0' || tag[i] > '9') && tag[i] != '.') ++i;
        long long acc = 0;
        bool anyHere = false;
        while (i < tag.size() && tag[i] >= '0' && tag[i] <= '9') {
            acc = acc * 10 + (tag[i] - '0');
            ++i;
            anyHere = true;
        }
        if (anyHere) {
            if (acc > INT_MAX) acc = INT_MAX;
            *slots[slot] = static_cast<int>(acc);
            sawDigit = true;
        }
        ++slot;
        if (i < tag.size() && tag[i] == '.') ++i;
    }
    return sawDigit;
}

// Component distance between two parsed tags: ordered triple
// (|maj diff|, |min diff|, |bld diff|). Used as the closest-match key in
// PickBestVersionRow.
struct CompDist {
    long long major = 0;
    long long minor = 0;
    long long build = 0;
    bool operator<(const CompDist& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return build < o.build;
    }
};

CompDist ComponentDistance(int a_maj, int a_min, int a_bld,
                           int b_maj, int b_min, int b_bld) {
    CompDist d;
    d.major = std::abs(static_cast<long long>(a_maj) - static_cast<long long>(b_maj));
    d.minor = std::abs(static_cast<long long>(a_min) - static_cast<long long>(b_min));
    d.build = std::abs(static_cast<long long>(a_bld) - static_cast<long long>(b_bld));
    return d;
}

// Load a `(id INTEGER, val TEXT)` dict table into `out`. Returns false (logged)
// on a prepare/step error. A dict table that is simply empty is not an error.
bool LoadDict(const char* table, std::unordered_map<int64_t, std::string>& out) {
    std::string sql = "SELECT id, val FROM ";
    sql += table;
    sql += ";";
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db, sql.c_str(), -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "dict_load_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("table", table),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        return false;
    }
    out.clear();
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(st, 0);
        const unsigned char* val = sqlite3_column_text(st, 1);
        out[id] = val ? reinterpret_cast<const char*>(val) : "";
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        LOG_ERROR_KV(kCategory, "dict_load_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("table", table),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        return false;
    }
    return true;
}

// Read meta.schema_version. Writes *out and returns true on success; on any
// query failure returns false (the caller turns that into a fail-loud Open
// rejection).
bool ReadSchemaVersion(int* out) {
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db, "SELECT schema_version FROM meta WHERE id = 1;",
                                -1, &st, nullptr);
    if (rc != SQLITE_OK) return false;
    rc = sqlite3_step(st);
    bool ok = false;
    if (rc == SQLITE_ROW) {
        *out = sqlite3_column_int(st, 0);
        ok = true;
    }
    sqlite3_finalize(st);
    return ok;
}

// Load the full game_versions table into the in-memory id→ordinal +
// id→tag maps. Tiny (one row per release ever).
bool LoadGameVersions() {
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        "SELECT id, tag, ordinal FROM game_versions;", -1, &st, nullptr);
    if (rc != SQLITE_OK) return false;
    g_versionOrdinalById.clear();
    g_versionTagById.clear();
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(st, 0);
        const unsigned char* tag = sqlite3_column_text(st, 1);
        int64_t ord = sqlite3_column_int64(st, 2);
        g_versionOrdinalById[id] = ord;
        g_versionTagById[id] = tag ? reinterpret_cast<const char*>(tag) : "";
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

// Look up the running build's game_versions row by tag. Caches id + ordinal
// in the module globals. Returns true if the tag was found.
bool ResolveRunningGameVersion(const std::string& tag) {
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        "SELECT id, ordinal FROM game_versions WHERE tag = ?;",
        -1, &st, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    bool found = false;
    if (rc == SQLITE_ROW) {
        g_gameVersionId = sqlite3_column_int64(st, 0);
        g_gameVersionOrdinal = sqlite3_column_int64(st, 1);
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

// ---------------------------------------------------------------------------
// address_names load + supersession chain walk.
//
// The new schema makes both edges version-scoped:
//   superseded_by + superseded_at_version  — the engine walks each hop iff
//                                            V >= superseded_at_version.
//   is_deprecated + deprecated_at_version  — informational; the engine
//                                            warns at resolve time but
//                                            still resolves.
//
// Cycle protection is the DB builder's job (the builder rejects any chain
// that would cycle); the engine walks without a hop cap.
// ---------------------------------------------------------------------------

struct NameRow {
    bool         found = false;
    int64_t      id = 0;
    std::string  name;
    bool         is_deprecated = false;            // raw column.
    int64_t      deprecated_at_version_id = 0;     // FK to game_versions.id; 0 = NULL.
    bool         has_superseded_by = false;
    int64_t      superseded_by = 0;
    int64_t      superseded_at_version_id = 0;     // FK to game_versions.id; 0 = NULL.
    int64_t      deprecation_replacement_id = 0;   // FK to address_names.id; 0 = NULL.
};

// Bind 7 columns out of an open statement positioned on an address_names row
// in canonical column order (id, name, is_deprecated, deprecated_at_version,
// superseded_by, superseded_at_version, deprecation_replacement).
void DecodeNameRow(sqlite3_stmt* st, NameRow* row) {
    row->found = true;
    row->id = sqlite3_column_int64(st, 0);
    const unsigned char* nm = sqlite3_column_text(st, 1);
    row->name = nm ? reinterpret_cast<const char*>(nm) : "";
    row->is_deprecated = sqlite3_column_int(st, 2) != 0;
    if (sqlite3_column_type(st, 3) != SQLITE_NULL) {
        row->deprecated_at_version_id = sqlite3_column_int64(st, 3);
    }
    if (sqlite3_column_type(st, 4) != SQLITE_NULL) {
        row->has_superseded_by = true;
        row->superseded_by = sqlite3_column_int64(st, 4);
    }
    if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
        row->superseded_at_version_id = sqlite3_column_int64(st, 5);
    }
    if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
        row->deprecation_replacement_id = sqlite3_column_int64(st, 6);
    }
}

// Load an address_names row by name. Returns found=false if the name is
// unknown; the caller turns that into a name_unknown miss.
NameRow LoadNameRowByName(const std::string& name) {
    NameRow row;
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        "SELECT id, name, is_deprecated, deprecated_at_version, "
        "       superseded_by, superseded_at_version, deprecation_replacement "
        "FROM address_names WHERE name = ? LIMIT 1;",
        -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "name_load_failed",
            kcdx::log::KV::BareStr("reason", "query_error"),
            kcdx::log::KV("name", name),
            kcdx::log::KV("sqlite_rc", (long long)rc),
            kcdx::log::KV::BareStr("sqlite_msg", sqlite3_errmsg(g_db)));
        return row;
    }
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) DecodeNameRow(st, &row);
    sqlite3_finalize(st);
    return row;
}

// Load an address_names row by id. Used by ResolveById and by the
// supersession chain walk inside ResolveByName / ResolveById.
NameRow LoadNameRowById(int64_t id) {
    NameRow row;
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        "SELECT id, name, is_deprecated, deprecated_at_version, "
        "       superseded_by, superseded_at_version, deprecation_replacement "
        "FROM address_names WHERE id = ? LIMIT 1;",
        -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "name_load_failed",
            kcdx::log::KV::BareStr("reason", "query_error"),
            kcdx::log::KV("kcdx_id", (long long)id),
            kcdx::log::KV("sqlite_rc", (long long)rc),
            kcdx::log::KV::BareStr("sqlite_msg", sqlite3_errmsg(g_db)));
        return row;
    }
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) == SQLITE_ROW) DecodeNameRow(st, &row);
    sqlite3_finalize(st);
    return row;
}

// Walk the supersession chain at the running game version V. Each hop is
// gated on V >= entity.superseded_at_version (translated through the
// ordinal map). Stops when the current entity's superseded_by is NULL OR
// V < superseded_at_version. Returns the final entity row and (via the out
// param) whether at least one hop was taken.
NameRow WalkSupersessionChain(const NameRow& start, bool* outWasSuperseded) {
    *outWasSuperseded = false;
    NameRow current = start;
    while (current.found && current.has_superseded_by) {
        // Gate the hop on V >= superseded_at_version. A NULL
        // superseded_at_version (id == 0) means the edge has no version anchor
        // — treat as "edge inactive" so we don't walk past a malformed pair.
        int64_t edgeOrd = OrdinalForVersionId(current.superseded_at_version_id);
        if (edgeOrd < 0) break;
        if (g_gameVersionOrdinal < edgeOrd) break;

        NameRow next = LoadNameRowById(current.superseded_by);
        if (!next.found) {
            // Supersession edge points at a non-existent successor — malformed
            // DB. Surface fail-loud; stop walking and return what we have.
            LOG_ERROR_KV(kCategory, "supersession_target_missing",
                log::KV::BareStr("reason", "query_error"),
                log::KV("old_name", current.name),
                log::KV("superseded_by_id", (long long)current.superseded_by),
                log::KV::BareStr("detail",
                    "address_names row's superseded_by points at an id that "
                    "does not exist — malformed reference database; reinstall "
                    "the kcdx release"));
            break;
        }
        current = next;
        *outWasSuperseded = true;
    }
    return current;
}

// ---------------------------------------------------------------------------
// address_versions row selection — closest-match by component distance.
//
// Given the set of address_versions rows for an entity, pick the one whose
// [valid_from, valid_through] interval best matches V:
//   - covering row (V in [valid_from, valid_through]) wins outright.
//   - else: minimum component-distance from V to either endpoint of each
//     row's interval (major, then minor, then build).
//
// V older than all rows → row with oldest valid_from (covered by the
// component-distance metric).
// V newer than all rows → the open row (valid_through IS NULL) covers
// everything from its valid_from onward (the "open" semantic per
// reference.md), so the cover-wins-outright branch catches it.
// ---------------------------------------------------------------------------

struct VersionRow {
    bool        valid = false;       // row decode succeeded.
    int64_t     valid_from_id = 0;
    int64_t     valid_through_id = 0;        // 0 == NULL == open.
    bool        has_valid_through = false;
    int         from_major = 0, from_minor = 0, from_build = 0;
    int         through_major = 0, through_minor = 0, through_build = 0;

    // The row's payload (the columns the resolve needs to emit).
    int64_t     kcdx_id = 0;
    int64_t     kindId = 0;
    bool        has_rva = false;
    int64_t     rva = 0;
    bool        has_signature = false;
    std::string signature;
    bool        has_value = false;
    int64_t     value = 0;
    std::vector<uint8_t> content_hash;
    bool        has_length = false;
    int64_t     length = 0;
    bool        has_offset = false;
    int64_t     offset = 0;
    bool        has_vtable_slot = false;
    int64_t     vtable_slot = 0;
    int64_t     observed_arg_slots = 0;
    int64_t     caller_reg_arg_count = 0;
    int64_t     last_verified_at_version_id = 0;  // 0 == NULL.
};

// Decode an address_versions row from a statement positioned on it. The
// SELECT shape MUST match — see kVersionSelectColumns.
//
// Column order (positional, indexed 0…):
//   0  kcdx_id
//   1  kind
//   2  rva
//   3  signature
//   4  value
//   5  content_hash
//   6  length
//   7  offset
//   8  vtable_slot
//   9  observed_arg_slots
//   10 caller_reg_arg_count
//   11 last_verified_at_version
//   12 valid_from
//   13 valid_through
void DecodeVersionRow(sqlite3_stmt* st, VersionRow* row) {
    row->valid = true;
    row->kcdx_id = sqlite3_column_int64(st, 0);
    row->kindId = sqlite3_column_int64(st, 1);
    if (sqlite3_column_type(st, 2) != SQLITE_NULL) {
        row->has_rva = true;
        row->rva = sqlite3_column_int64(st, 2);
    }
    if (sqlite3_column_type(st, 3) != SQLITE_NULL) {
        const unsigned char* sig = sqlite3_column_text(st, 3);
        row->has_signature = true;
        row->signature = sig ? reinterpret_cast<const char*>(sig) : "";
    }
    if (sqlite3_column_type(st, 4) != SQLITE_NULL) {
        row->has_value = true;
        row->value = sqlite3_column_int64(st, 4);
    }
    if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
        const void* blob = sqlite3_column_blob(st, 5);
        int nbytes = sqlite3_column_bytes(st, 5);
        if (blob && nbytes > 0) {
            const uint8_t* b = static_cast<const uint8_t*>(blob);
            row->content_hash.assign(b, b + nbytes);
        }
    }
    if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
        row->has_length = true;
        row->length = sqlite3_column_int64(st, 6);
    }
    if (sqlite3_column_type(st, 7) != SQLITE_NULL) {
        row->has_offset = true;
        row->offset = sqlite3_column_int64(st, 7);
    }
    if (sqlite3_column_type(st, 8) != SQLITE_NULL) {
        row->has_vtable_slot = true;
        row->vtable_slot = sqlite3_column_int64(st, 8);
    }
    row->observed_arg_slots = sqlite3_column_int64(st, 9);
    if (sqlite3_column_type(st, 10) != SQLITE_NULL) {
        row->caller_reg_arg_count = sqlite3_column_int64(st, 10);
    }
    if (sqlite3_column_type(st, 11) != SQLITE_NULL) {
        row->last_verified_at_version_id = sqlite3_column_int64(st, 11);
    }
    row->valid_from_id = sqlite3_column_int64(st, 12);
    if (sqlite3_column_type(st, 13) != SQLITE_NULL) {
        row->has_valid_through = true;
        row->valid_through_id = sqlite3_column_int64(st, 13);
    }

    // Parse the interval endpoint tags. valid_through NULL ("open") parses
    // as (0,0,0) but is never used for distance — the cover-wins branch
    // detects the open row by has_valid_through=false.
    ParseVersionTag(TagForVersionId(row->valid_from_id),
                    &row->from_major, &row->from_minor, &row->from_build);
    if (row->has_valid_through) {
        ParseVersionTag(TagForVersionId(row->valid_through_id),
                        &row->through_major, &row->through_minor, &row->through_build);
    }
}

constexpr const char* kVersionSelectColumns =
    "v.kcdx_id, v.kind, v.rva, v.signature, v.value, v.content_hash, "
    "v.length, v.offset, v.vtable_slot, "
    "v.observed_arg_slots, v.caller_reg_arg_count, "
    "v.last_verified_at_version, v.valid_from, v.valid_through";

// Compare two parsed tag triples lexicographically (major→minor→build).
// Returns <0 if a < b, 0 if equal, >0 if a > b.
int CompareTags(int a_maj, int a_min, int a_bld,
                int b_maj, int b_min, int b_bld) {
    if (a_maj != b_maj) return a_maj < b_maj ? -1 : 1;
    if (a_min != b_min) return a_min < b_min ? -1 : 1;
    if (a_bld != b_bld) return a_bld < b_bld ? -1 : 1;
    return 0;
}

// Pick the best matching address_versions row for the running game version
// out of a set of rows for one entity.
//
//   1. Covering row (V in [valid_from, valid_through]) wins outright. The
//      partial-unique-open index guarantees at most one row with
//      valid_through IS NULL; for closed rows the maintainer guarantees
//      non-overlap so at most one closed row covers V too.
//   2. Else: the row with minimum component-distance from V to either
//      endpoint of its interval (valid_from or valid_through). Ties are
//      broken by preferring the row with the closer valid_from.
//
// Returns index into `rows` (-1 if rows is empty).
int PickBestVersionRow(const std::vector<VersionRow>& rows) {
    if (rows.empty()) return -1;
    const int V_maj = g_runningMajor;
    const int V_min = g_runningMinor;
    const int V_bld = g_runningBuild;

    // First pass: a covering row wins outright.
    for (size_t i = 0; i < rows.size(); ++i) {
        const VersionRow& r = rows[i];
        // open row covers everything from valid_from onward.
        if (!r.has_valid_through) {
            if (CompareTags(V_maj, V_min, V_bld,
                            r.from_major, r.from_minor, r.from_build) >= 0) {
                return static_cast<int>(i);
            }
        } else {
            int fromCmp = CompareTags(V_maj, V_min, V_bld,
                                       r.from_major, r.from_minor, r.from_build);
            int throughCmp = CompareTags(V_maj, V_min, V_bld,
                                          r.through_major, r.through_minor, r.through_build);
            if (fromCmp >= 0 && throughCmp <= 0) {
                return static_cast<int>(i);
            }
        }
    }

    // Second pass: closest-by-endpoint component-distance.
    int best = 0;
    CompDist bestDist = ComponentDistance(V_maj, V_min, V_bld,
                                          rows[0].from_major, rows[0].from_minor, rows[0].from_build);
    if (rows[0].has_valid_through) {
        CompDist tDist = ComponentDistance(V_maj, V_min, V_bld,
                                            rows[0].through_major, rows[0].through_minor, rows[0].through_build);
        if (tDist < bestDist) bestDist = tDist;
    }
    for (size_t i = 1; i < rows.size(); ++i) {
        const VersionRow& r = rows[i];
        CompDist dist = ComponentDistance(V_maj, V_min, V_bld,
                                          r.from_major, r.from_minor, r.from_build);
        if (r.has_valid_through) {
            CompDist tDist = ComponentDistance(V_maj, V_min, V_bld,
                                                r.through_major, r.through_minor, r.through_build);
            if (tDist < dist) dist = tDist;
        }
        if (dist < bestDist) {
            best = static_cast<int>(i);
            bestDist = dist;
        }
    }
    return best;
}

// Load every address_versions row for an entity by kcdx_id. Returns true on
// query success (rows may be empty if the entity has no version rows — a
// data bug). The caller decides what to do with an empty result.
bool LoadVersionRowsForEntity(int64_t kcdx_id, std::vector<VersionRow>* out) {
    out->clear();
    std::string sql = "SELECT ";
    sql += kVersionSelectColumns;
    sql += " FROM address_versions v WHERE v.kcdx_id = ?;";
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db, sql.c_str(), -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "version_load_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("kcdx_id", (long long)kcdx_id),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        return false;
    }
    sqlite3_bind_int64(st, 1, kcdx_id);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        VersionRow row;
        DecodeVersionRow(st, &row);
        out->push_back(std::move(row));
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        LOG_ERROR_KV(kCategory, "version_load_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("kcdx_id", (long long)kcdx_id),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        return false;
    }
    return true;
}

// Resolve the deprecation_replacement (a FK to another address_names row)
// into the replacement entity's name. Empty string when the FK is NULL or
// the replacement row is missing.
std::string LookupDeprecationReplacementName(int64_t replacementId) {
    if (replacementId <= 0) return std::string();
    // Self-join shape: read the replacement entity's name out of the same
    // address_names table the original was loaded from.
    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        "SELECT r.name FROM address_names r "
        "WHERE r.id = ? LIMIT 1;",
        -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "deprecation_replacement_load_failed",
            kcdx::log::KV::BareStr("reason", "query_error"),
            kcdx::log::KV("replacement_id", (long long)replacementId),
            kcdx::log::KV("sqlite_rc", (long long)rc),
            kcdx::log::KV::BareStr("sqlite_msg", sqlite3_errmsg(g_db)));
        return std::string();
    }
    sqlite3_bind_int64(st, 1, replacementId);
    std::string name;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* nm = sqlite3_column_text(st, 0);
        if (nm) name = reinterpret_cast<const char*>(nm);
    }
    sqlite3_finalize(st);
    return name;
}

// ---------------------------------------------------------------------------
// Warning emission — three triggers, per (pluginHandle, callType, name)
// dedup, dual-route (engine log INFO always; plugin log WARN when handle != 0).
// ---------------------------------------------------------------------------

void EmitSupersededWarning(const CallerContext& ctx,
                           const std::string& oldName,
                           const std::string& newName) {
    WarnKey key{ ctx.pluginHandle,
                 ctx.callType ? std::string(ctx.callType) : std::string(),
                 oldName };
    if (!g_warnedSuperseded.insert(key).second) return;

    LOG_INFO_KV(kCategory, "supersession_follow",
        log::KV("old_name", oldName),
        log::KV("new_name", newName),
        log::KV("plugin_handle", (long long)ctx.pluginHandle),
        log::KV("call_type", ctx.callType ? ctx.callType : "engine_internal"));

    if (ctx.pluginHandle != 0) {
        LOG_PLUGIN_WARN(ctx.pluginHandle, kCategory,
            "Target '%s' has been renamed to '%s' \xe2\x80\x94 "
            "update your plugin to silence this warning",
            oldName.c_str(), newName.c_str());
    }
}

void EmitDeprecatedWarning(const CallerContext& ctx,
                           const std::string& name,
                           const std::string& replacementName) {
    WarnKey key{ ctx.pluginHandle,
                 ctx.callType ? std::string(ctx.callType) : std::string(),
                 name };
    if (!g_warnedDeprecated.insert(key).second) return;

    LOG_INFO_KV(kCategory, "deprecated_target",
        log::KV("name", name),
        log::KV("replacement", replacementName.empty() ? "(none)" : replacementName.c_str()),
        log::KV("plugin_handle", (long long)ctx.pluginHandle),
        log::KV("call_type", ctx.callType ? ctx.callType : "engine_internal"));

    if (ctx.pluginHandle != 0) {
        if (!replacementName.empty()) {
            LOG_PLUGIN_WARN(ctx.pluginHandle, kCategory,
                "%s is deprecated, consider using %s",
                name.c_str(), replacementName.c_str());
        } else {
            LOG_PLUGIN_WARN(ctx.pluginHandle, kCategory,
                "%s is deprecated", name.c_str());
        }
    }
}

void EmitUnverifiedWarning(const CallerContext& ctx,
                           const std::string& name,
                           const std::string& gameVersionTag) {
    WarnKey key{ ctx.pluginHandle,
                 ctx.callType ? std::string(ctx.callType) : std::string(),
                 name };
    if (!g_warnedUnverified.insert(key).second) return;

    LOG_INFO_KV(kCategory, "unverified_target",
        log::KV("name", name),
        log::KV("game_version", gameVersionTag.empty() ? "(unknown)" : gameVersionTag.c_str()),
        log::KV("plugin_handle", (long long)ctx.pluginHandle),
        log::KV("call_type", ctx.callType ? ctx.callType : "engine_internal"));

    if (ctx.pluginHandle != 0) {
        LOG_PLUGIN_WARN(ctx.pluginHandle, kCategory,
            "Target '%s' has not been re-verified for your game version (%s) "
            "\xe2\x80\x94 resolving anyway, but it may have moved",
            name.c_str(),
            gameVersionTag.empty() ? "(unknown)" : gameVersionTag.c_str());
    }
}

// ---------------------------------------------------------------------------
// Verification state derivation per reference.md's 4-state machine.
// Returns the state + (for caller convenience) booleans for the two flags
// the result struct exposes.
// ---------------------------------------------------------------------------

NameResolution::VerificationState DeriveVerificationState(
        const NameRow& finalEntity,
        bool walkedSupersession,
        const VersionRow& picked,
        bool* outIsDeprecated) {
    *outIsDeprecated = false;

    // 1. DEPRECATED: entity.is_deprecated AND V >= deprecated_at_version.
    if (finalEntity.is_deprecated) {
        int64_t depOrd = OrdinalForVersionId(finalEntity.deprecated_at_version_id);
        if (depOrd >= 0 && g_gameVersionOrdinal >= depOrd) {
            *outIsDeprecated = true;
            return NameResolution::VerificationState::Deprecated;
        }
    }

    // 2. SUPERSEDED: the chain walk took at least one hop.
    if (walkedSupersession) {
        return NameResolution::VerificationState::Superseded;
    }

    // 3. VERIFIED: last_verified_at_version >= V AND valid_from <= V.
    int64_t lvOrd = OrdinalForVersionId(picked.last_verified_at_version_id);
    int64_t vfOrd = OrdinalForVersionId(picked.valid_from_id);
    if (lvOrd >= 0 && vfOrd >= 0
            && lvOrd >= g_gameVersionOrdinal
            && vfOrd <= g_gameVersionOrdinal) {
        return NameResolution::VerificationState::Verified;
    }

    // 4. Otherwise UNVERIFIED.
    return NameResolution::VerificationState::Unverified;
}

// Translate the derived verification state into the IdResolution flavor of
// the same enum. The two are structurally identical; this keeps the
// public API additions in lockstep without forcing callers to convert.
IdResolution::VerificationState ToIdState(NameResolution::VerificationState s) {
    switch (s) {
        case NameResolution::VerificationState::Verified:   return IdResolution::VerificationState::Verified;
        case NameResolution::VerificationState::Unverified: return IdResolution::VerificationState::Unverified;
        case NameResolution::VerificationState::Deprecated: return IdResolution::VerificationState::Deprecated;
        case NameResolution::VerificationState::Superseded: return IdResolution::VerificationState::Superseded;
    }
    return IdResolution::VerificationState::Verified;
}

// Emit the db_not_loaded fail-loud line for a resolve attempt made while the
// database is not open. Never a silent empty.
void LogNotLoaded(const char* what) {
    LOG_ERROR_KV(kCategory, "resolve_not_loaded",
        log::KV::BareStr("reason", "db_not_loaded"),
        log::KV("what", what),
        log::KV::BareStr("detail",
            "reference.sqlite is not open (it was absent/unopenable at launch, "
            "or its schema/version check failed) \xe2\x80\x94 no verified "
            "resolution is available; reinstall the kcdx release to restore "
            "reference.sqlite"));
}

}  // namespace

const char* SqliteVersion() {
    // References a libsqlite3 symbol → proves the static lib links, not just
    // that the header is on the include path.
    return sqlite3_libversion();
}

bool IsLoaded() {
    return g_loaded && g_db != nullptr;
}

bool Open() {
    if (g_loaded) return true;  // idempotent.

    std::filesystem::path dbPath =
        kcdx::paths::EngineDataDirPath() / "data" / "reference.sqlite";
    std::string dbPathUtf8 = dbPath.u8string();

    // READ-ONLY: the engine never writes the database. SQLITE_OPEN_READONLY
    // also means open fails (rather than creating) if the file is absent —
    // which is exactly the fail-loud signal we want.
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPathUtf8.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "db_not_loaded"),
            log::KV("path", dbPathUtf8),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", db ? sqlite3_errmsg(db) : "out of memory"),
            log::KV::BareStr("detail",
                "could not open reference.sqlite at the path above "
                "\xe2\x80\x94 no verified resolution is available; "
                "reinstall the kcdx release to restore the database"));
        if (db) sqlite3_close(db);  // open_v2 may return a handle even on error.
        return false;
    }

    g_db = db;

    // Schema gate: a database whose shape this engine does not understand is
    // rejected loud, not read with a guessed layout.
    int schemaVersion = 0;
    if (!ReadSchemaVersion(&schemaVersion)) {
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "schema_version_mismatch"),
            log::KV("path", dbPathUtf8),
            log::KV::BareStr("detail",
                "could not read meta.schema_version from reference.sqlite "
                "\xe2\x80\x94 the file is not a recognizable kcdx reference "
                "database; reinstall the kcdx release"));
        Close();
        return false;
    }
    if (schemaVersion != kExpectedSchemaVersion) {
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "schema_version_mismatch"),
            log::KV("path", dbPathUtf8),
            log::KV("found_schema_version", (long long)schemaVersion),
            log::KV("expected_schema_version", (long long)kExpectedSchemaVersion),
            log::KV::BareStr("detail",
                "reference.sqlite schema_version does not match the schema "
                "this engine build understands \xe2\x80\x94 install the "
                "reference.sqlite that ships with this kcdx release"));
        Close();
        return false;
    }

    // Load the kind dict (the only dict the engine consumes at resolve time;
    // the other dicts are author/audit-only).
    if (!LoadDict("_dict_address_versions_kind", g_kindDict)) {
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("path", dbPathUtf8),
            log::KV::BareStr("detail",
                "failed to load reference.sqlite kind dictionary "
                "\xe2\x80\x94 the file is present but malformed; reinstall "
                "the kcdx release"));
        Close();
        return false;
    }

    // Cache the full game_versions table — both the id→ordinal mapping (the
    // verification-state derivation gates use ordinals) AND the id→tag
    // mapping (PickBestVersionRow's closest-match parses tag components).
    if (!LoadGameVersions()) {
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("path", dbPathUtf8),
            log::KV::BareStr("detail",
                "failed to load reference.sqlite game_versions table "
                "\xe2\x80\x94 the file is present but malformed; reinstall "
                "the kcdx release"));
        Close();
        return false;
    }

    // Find the running build's version row. Mismatch-mode resolves still
    // proceed — they pick the closest-matching address_versions row (the
    // running build's parsed major.minor.build is used as the comparison
    // point even when the tag is not in game_versions).
    const std::string& tag = kcdx::plugins::g_runtimeGameVersionString;
    g_gameVersionTag = tag;
    ParseVersionTag(tag, &g_runningMajor, &g_runningMinor, &g_runningBuild);

    if (tag.empty() || !ResolveRunningGameVersion(tag)) {
        g_versionMismatchMode = true;
        g_gameVersionId = 0;
        // In mismatch mode the running build has no ordinal — every
        // ordinal-keyed derivation gate (last_verified_at_version >= V,
        // valid_from <= V, deprecated_at_version, superseded_at_version)
        // becomes "absent". The verification state derivation reads -1
        // for the absent gates and falls through to UNVERIFIED.
        g_gameVersionOrdinal = -1;
        LOG_WARN_KV(kCategory, "version_mismatch_mode",
            log::KV::BareStr("reason", "no_game_version_row"),
            log::KV("path", dbPathUtf8),
            log::KV("running_game_version", tag.empty() ? "(undetected)" : tag.c_str()),
            log::KV::BareStr("detail",
                "the running game build's version tag is not present in "
                "game_versions \xe2\x80\x94 resolves pick the closest-matching "
                "address_versions row by component distance (major/minor/build) "
                "and surface every result as UNVERIFIED. Per-resolve failures "
                "remain fail-loud."));
        g_loaded = true;
        LOG_INFO_KV(kCategory, "opened",
            log::KV("path", dbPathUtf8),
            log::KV("schema_version", (long long)schemaVersion),
            log::KV("game_version", tag.empty() ? "(undetected)" : tag.c_str()),
            log::KV::BareStr("mode", "version_mismatch (closest-match)"));
        return true;
    }

    g_loaded = true;
    LOG_INFO_KV(kCategory, "opened",
        log::KV("path", dbPathUtf8),
        log::KV("schema_version", (long long)schemaVersion),
        log::KV("matched_game_version_tag", tag),
        log::KV("matched_ordinal_for_cmp",
                (long long)g_gameVersionOrdinal));
    return true;
}

// Common resolve worker shared by ResolveByName and ResolveById. Takes the
// already-loaded entity row, walks supersession, picks the best version row,
// derives verification state, fires warnings.
//
// `inputName` is the name the caller asked for (or empty for ResolveById).
// `inputId` is the id the caller asked for (or 0 for ResolveByName).
// `ctx` routes warnings.
//
// Returns a NameResolution; ResolveById converts the relevant subset into
// an IdResolution.
namespace {

NameResolution ResolveCommon(const NameRow& original,
                              const std::string& inputName,
                              uint64_t inputId,
                              const CallerContext& ctx) {
    NameResolution r;

    // Walk the supersession chain at the running game version.
    bool walkedSupersession = false;
    NameRow effective = WalkSupersessionChain(original, &walkedSupersession);

    if (walkedSupersession) {
        EmitSupersededWarning(ctx,
            inputName.empty() ? original.name : inputName,
            effective.name);
    }

    // Load every address_versions row for the effective entity and pick the
    // best match for V.
    std::vector<VersionRow> rows;
    if (!LoadVersionRowsForEntity(effective.id, &rows)) {
        // query_error already logged.
        return r;
    }
    if (rows.empty()) {
        LOG_ERROR_KV(kCategory, "resolve_miss",
            log::KV::BareStr("reason", "no_open_version"),
            log::KV("input_name", inputName.empty() ? "(by-id)" : inputName.c_str()),
            log::KV("input_id", (unsigned long long)inputId),
            log::KV("effective_name", effective.name),
            log::KV("effective_kcdx_id", (long long)effective.id),
            log::KV::BareStr("detail",
                "entity exists in address_names but no address_versions row "
                "covers it \xe2\x80\x94 a data bug in the reference database"));
        return r;
    }

    int bestIdx = PickBestVersionRow(rows);
    if (bestIdx < 0) {
        // Defensive: PickBestVersionRow only returns -1 on empty input which
        // we already handled.
        return r;
    }
    const VersionRow& picked = rows[bestIdx];

    // Derive verification state. is_deprecated flag tracks whether the
    // entity-level deprecation activated at V.
    bool entityDeprecatedAtV = false;
    NameResolution::VerificationState state = DeriveVerificationState(
        effective, walkedSupersession, picked, &entityDeprecatedAtV);

    // Fire DEPRECATED warning (in addition to the SUPERSEDED warning the
    // walk already fired, if walked — these are distinct entity-level
    // events per the new schema).
    if (state == NameResolution::VerificationState::Deprecated) {
        std::string replacementName = LookupDeprecationReplacementName(
            effective.deprecation_replacement_id);
        EmitDeprecatedWarning(ctx, effective.name, replacementName);
    }
    if (state == NameResolution::VerificationState::Unverified) {
        EmitUnverifiedWarning(ctx, effective.name, g_gameVersionTag);
    }

    // Populate the result.
    r.found = true;
    r.kcdx_id = static_cast<uint64_t>(picked.kcdx_id);
    r.rva = picked.has_rva ? static_cast<uint64_t>(picked.rva) : 0;
    r.verified_signature = picked.signature;
    r.kind = DecodeDict(g_kindDict, picked.kindId);
    r.has_offset = picked.has_offset;
    r.offset = picked.offset;
    r.has_vtable_slot = picked.has_vtable_slot;
    r.vtable_slot = picked.vtable_slot;
    r.has_value = picked.has_value;
    r.value = picked.value;
    r.content_hash_hex = HashToHex(picked.content_hash.data(),
                                    static_cast<int>(picked.content_hash.size()));
    r.content_hash = picked.content_hash;
    r.has_length = picked.has_length;
    r.length = picked.length;
    r.resolved_name = effective.name;
    r.was_superseded = walkedSupersession;
    r.is_deprecated = entityDeprecatedAtV;
    r.verification_state = state;

    LOG_DEBUG_KV(kCategory, "resolve_hit",
        log::KV("input_name", inputName.empty() ? "(by-id)" : inputName.c_str()),
        log::KV("input_id", (unsigned long long)inputId),
        log::KV("effective_name", effective.name),
        log::KV("kcdx_id", (unsigned long long)r.kcdx_id),
        log::KV("rva", (unsigned long long)r.rva),
        log::KV("kind", r.kind),
        log::KV::BareStr("verification_state",
            state == NameResolution::VerificationState::Verified ? "verified" :
            state == NameResolution::VerificationState::Unverified ? "unverified" :
            state == NameResolution::VerificationState::Deprecated ? "deprecated" :
            "superseded"));
    return r;
}

}  // namespace

NameResolution ResolveByName(const std::string& name, const CallerContext& ctx) {
    NameResolution r;
    if (!IsLoaded()) {
        LogNotLoaded("ResolveByName");
        return r;
    }

    NameRow original = LoadNameRowByName(name);
    if (!original.found) {
        LOG_DEBUG_KV(kCategory, "resolve_name_miss",
            log::KV::BareStr("reason", "name_unknown"),
            log::KV("name", name),
            log::KV("plugin_handle", (long long)ctx.pluginHandle),
            log::KV("call_type", ctx.callType ? ctx.callType : "engine_internal"),
            log::KV::BareStr("detail",
                "no address_names row carries this name \xe2\x80\x94 "
                "an un-curated or misspelled target name"));
        return r;
    }

    return ResolveCommon(original, name, 0, ctx);
}

IdResolution ResolveById(uint64_t kcdx_id, const CallerContext& ctx) {
    IdResolution r;
    if (!IsLoaded()) {
        LogNotLoaded("ResolveById");
        return r;
    }

    NameRow original = LoadNameRowById(static_cast<int64_t>(kcdx_id));
    if (!original.found) {
        LOG_DEBUG_KV(kCategory, "resolve_id_miss",
            log::KV::BareStr("reason", "name_unknown"),
            log::KV("kcdx_id", (unsigned long long)kcdx_id),
            log::KV("plugin_handle", (long long)ctx.pluginHandle),
            log::KV("call_type", ctx.callType ? ctx.callType : "engine_internal"),
            log::KV::BareStr("detail",
                "no address_names row carries this kcdx_id \xe2\x80\x94 "
                "an unknown or out-of-range id"));
        return r;
    }

    // The by-id and by-name paths share the resolution worker; ResolveById
    // then projects the NameResolution payload into an IdResolution (the
    // by-id surface exposes the floor + observed_arg_slots + caller_reg_arg_count
    // instead of the verified signature shape).
    NameResolution nameRes = ResolveCommon(original, std::string(), kcdx_id, ctx);
    if (!nameRes.found) return r;

    // Re-load the picked row from the DB to grab the floor-relevant columns
    // — ResolveCommon does not expose VersionRow outside its scope. We have
    // the entity id from the result; pick the best row again using the same
    // function. (The duplicate query is cheap and keeps ResolveCommon's
    // signature narrow.)
    std::vector<VersionRow> rows;
    if (!LoadVersionRowsForEntity(static_cast<int64_t>(nameRes.kcdx_id), &rows)
            || rows.empty()) {
        return r;
    }
    int idx = PickBestVersionRow(rows);
    if (idx < 0) return r;
    const VersionRow& picked = rows[idx];

    r.found = true;
    r.kcdx_id = nameRes.kcdx_id;
    r.rva = nameRes.rva;
    r.floor_signature = picked.signature;     // honest lower bound — never verified.
    r.signature_is_floor_estimate = true;
    r.observed_arg_slots = picked.observed_arg_slots;
    r.caller_reg_arg_count = picked.caller_reg_arg_count;
    r.has_value = nameRes.has_value;
    r.value = nameRes.value;
    r.content_hash_hex = nameRes.content_hash_hex;
    r.content_hash = nameRes.content_hash;
    r.has_length = nameRes.has_length;
    r.length = nameRes.length;
    r.was_superseded = nameRes.was_superseded;
    r.is_deprecated = nameRes.is_deprecated;
    r.verification_state = ToIdState(nameRes.verification_state);
    return r;
}

void Close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
    g_loaded = false;
    g_gameVersionId = 0;
    g_gameVersionOrdinal = 0;
    g_gameVersionTag.clear();
    g_runningMajor = 0;
    g_runningMinor = 0;
    g_runningBuild = 0;
    g_versionMismatchMode = false;
    g_kindDict.clear();
    g_versionOrdinalById.clear();
    g_versionTagById.clear();
    g_warnedSuperseded.clear();
    g_warnedDeprecated.clear();
    g_warnedUnverified.clear();
}

}  // namespace kcdx::refdb
