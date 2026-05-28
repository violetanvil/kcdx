#include "refdb.h"

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

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

// The running build's game_versions row, cached at Open(). The ordinal is the
// monotonic build number used by the covering-interval predicate.
int64_t  g_gameVersionId = 0;
int64_t  g_gameVersionOrdinal = 0;

// Version-mismatch mode flag. Set true at Open() when the running build's
// version tag is absent from the game_versions table (no_game_version_row).
// In mismatch mode the connection stays OPEN: resolves use only the
// OPEN-interval rows (valid_through IS NULL) — the "latest verified" semantic —
// instead of the ordinal-bounded covering-interval predicate. A plain bool
// (not std::atomic) — the existing module-state pattern in this TU is plain
// statics (g_db / g_loaded / the ordinals / the dict maps), all single-threaded
// per the THREADSAFE=2 contract documented in refdb.h (Open and every resolve
// run on the worker thread).
bool     g_versionMismatchMode = false;

// Dictionaries loaded once at Open() (id → val). The dicts are tiny
// (single-digit row counts); an in-memory map avoids a JOIN per resolve and
// keeps the resolution SQL flat. Chosen over per-query dict JOINs for that
// reason.
std::unordered_map<int64_t, std::string> g_kindDict;    // _dict_kcdx_overlay_kind
std::unordered_map<int64_t, std::string> g_statusDict;  // _dict_kcdx_overlay_versions_status

// The "verified" status dict id, resolved from g_statusDict at Open(). Used to
// prefer a verified covering overlay-version row when (schema-permitted) both a
// verified and an unverified row cover the same interval. -1 if the dict has no
// "verified" entry (a malformed DB — caught at Open()).
int64_t  g_verifiedStatusId = -1;

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

// Look up the running build's game_versions row by tag. Caches id + ordinal in
// the module globals. Returns true if the tag was found.
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

// Forward decl: on a resolve-by-name with no covering verified row, this probes
// which gate stopped resolution (name_unknown / no_entity_version /
// no_overlay_version) and returns a logged not-found result. Defined below.
NameResolution DiagnoseNameMiss(const std::string& name);

// Emit the db_not_loaded fail-loud line for a resolve attempt made while the
// database is not open. Never a silent empty.
void LogNotLoaded(const char* what) {
    LOG_ERROR_KV(kCategory, "resolve_not_loaded",
        log::KV::BareStr("reason", "db_not_loaded"),
        log::KV("what", what),
        log::KV::BareStr("detail",
            "reference.sqlite is not open (it was absent/unopenable at launch, "
            "or its schema/version check failed) — no verified resolution is "
            "available; reinstall the kcdx release to restore reference.sqlite"));
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
        kcdx::paths::EngineDataDirPath() / "reference.sqlite";
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
                "could not open reference.sqlite — no verified resolution is "
                "available; reinstall the kcdx release to restore the database "
                "at the path above"));
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
                "could not read meta.schema_version from reference.sqlite — the "
                "file is not a recognizable kcdx reference database; reinstall "
                "the kcdx release"));
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
                "reference.sqlite schema_version does not match the schema this "
                "engine build understands — install the reference.sqlite that "
                "ships with this kcdx release"));
        Close();
        return false;
    }

    // Load the small dictionaries used by the resolution decoders.
    if (!LoadDict("_dict_kcdx_overlay_kind", g_kindDict) ||
        !LoadDict("_dict_kcdx_overlay_versions_status", g_statusDict)) {
        // LoadDict already logged the query_error cause.
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("path", dbPathUtf8),
            log::KV::BareStr("detail",
                "failed to load reference.sqlite dictionary tables — the file is "
                "present but malformed; reinstall the kcdx release"));
        Close();
        return false;
    }

    // Cache the "verified" status dict id — the resolution gate keys on it. A
    // status dict with no "verified" entry is a malformed DB: gate every resolve
    // off rather than read with a guessed id.
    g_verifiedStatusId = -1;
    for (const auto& kv : g_statusDict) {
        if (kv.second == "verified") { g_verifiedStatusId = kv.first; break; }
    }
    if (g_verifiedStatusId < 0) {
        LOG_ERROR_KV(kCategory, "open_failed",
            log::KV::BareStr("reason", "schema_version_mismatch"),
            log::KV("path", dbPathUtf8),
            log::KV::BareStr("detail",
                "the status dictionary carries no \"verified\" entry — the "
                "reference database is malformed; reinstall the kcdx release"));
        Close();
        return false;
    }

    // Find the running build's version row. The covering-interval predicate
    // compares ordinals when a row IS found; when the row is ABSENT (this
    // engine running against a build the shipped reference.sqlite has not
    // catalogued yet) the connection stays open in version-mismatch mode —
    // resolves fall back to the OPEN-interval rows (valid_through IS NULL),
    // i.e. the latest verified addresses, regardless of when they became
    // valid. WARN (not ERROR) — the boot continues; plugins try to apply
    // with those addresses; per-resolve failures remain fail-loud at the
    // install site (the existing pattern), so a stale row that does not
    // survive the on-disk hash check is rejected then with its own log line.
    //
    // The schema_version_mismatch and db_not_loaded paths above are still
    // hard fails (the DB is structurally unreadable). Only the
    // no_game_version_row case is now non-fatal.
    const std::string& tag = kcdx::plugins::g_runtimeGameVersionString;
    if (tag.empty() || !ResolveRunningGameVersion(tag)) {
        g_versionMismatchMode = true;
        g_gameVersionId = 0;
        g_gameVersionOrdinal = 0;
        LOG_WARN_KV(kCategory, "version_mismatch_mode",
            log::KV::BareStr("reason", "no_game_version_row"),
            log::KV("path", dbPathUtf8),
            log::KV("running_game_version", tag.empty() ? "(undetected)" : tag.c_str()),
            log::KV::BareStr("detail",
                "the running game build's version tag is not present in "
                "game_versions — proceeding with the OPEN interval rows "
                "(valid_through IS NULL); resolves will use the latest "
                "verified addresses regardless of version match. Plugin "
                "failures at the individual resolve site remain fail-loud."));
        g_loaded = true;
        LOG_INFO_KV(kCategory, "opened",
            log::KV("path", dbPathUtf8),
            log::KV("schema_version", (long long)schemaVersion),
            log::KV("game_version", tag.empty() ? "(undetected)" : tag.c_str()),
            log::KV::BareStr("mode", "version_mismatch (open-interval rows only)"));
        return true;
    }

    g_loaded = true;
    LOG_INFO_KV(kCategory, "opened",
        log::KV("path", dbPathUtf8),
        log::KV("schema_version", (long long)schemaVersion),
        log::KV("game_version", tag),
        log::KV("game_version_ordinal", (long long)g_gameVersionOrdinal));
    return true;
}

NameResolution ResolveByName(const std::string& name) {
    NameResolution r;
    if (!IsLoaded()) {
        LogNotLoaded("ResolveByName");
        return r;
    }

    // Covering-interval resolve-by-name (verified against the real DB):
    //   kcdx_overlay (name=?) → kcdx_id, kind, overlay id
    //   entity_versions covering interval → rva, value, content_hash
    //   kcdx_overlay_versions covering interval → verified signature/offset/vtable
    // The covering interval is selected by ORDINAL, not by valid_through IS NULL
    // alone: a row covers the running build when
    //   valid_from.ordinal <= running AND (valid_through IS NULL OR valid_through.ordinal >= running).
    // valid_from/valid_through are FKs to game_versions.id, joined to compare
    // ordinals. status is gated to "verified" via its dict id (resolved at
    // Open()); only verified rows resolve at runtime.
    //
    // VERSION-MISMATCH FALLBACK: when the running build's tag is not in
    // game_versions (Open() set g_versionMismatchMode = true), there is no
    // running ordinal to compare against. The resolver instead selects rows
    // whose valid_through IS NULL — the OPEN-interval rows — yielding the
    // "latest verified" facts the DB carries for each entity. The plugin's
    // own per-resolve survival check (on-disk content_hash comparison) still
    // rejects a stale row whose code has moved.
    //
    // Returned columns: kcdx_id, rva, ev.value, ev.content_hash, kind(dict id),
    // ovv.signature, ovv.offset, ovv.vtable_slot, status(dict id), ev.length.
    // ev.length is appended LAST (column 9) so the existing 0..8 indices below
    // are unchanged — the survival check needs the span the content_hash covers.
    static const char* kSqlOrdinal =
        "SELECT o.kcdx_id, ev.rva, ev.value, ev.content_hash, o.kind, "
        "       ovv.signature, ovv.offset, ovv.vtable_slot, ovv.status, "
        "       ev.length "
        "FROM kcdx_overlay o "
        "JOIN entity_versions ev ON ev.kcdx_id = o.kcdx_id "
        "JOIN game_versions evf ON evf.id = ev.valid_from "
        "LEFT JOIN game_versions evt ON evt.id = ev.valid_through "
        "JOIN kcdx_overlay_versions ovv ON ovv.overlay_id = o.id "
        "JOIN game_versions ovf ON ovf.id = ovv.valid_from "
        "LEFT JOIN game_versions ovt ON ovt.id = ovv.valid_through "
        "WHERE o.name = ? "
        "  AND evf.ordinal <= ? AND (ev.valid_through IS NULL OR evt.ordinal >= ?) "
        "  AND ovf.ordinal <= ? AND (ovv.valid_through IS NULL OR ovt.ordinal >= ?) "
        // Prefer a verified covering overlay-version row over an unverified one
        // if the schema ever permits both to cover the same interval; today each
        // (overlay, interval) is single-row, so this is defensive ordering, not
        // load-bearing for the current DB.
        "ORDER BY (ovv.status = ?) DESC "
        "LIMIT 1;";
    static const char* kSqlOpenOnly =
        "SELECT o.kcdx_id, ev.rva, ev.value, ev.content_hash, o.kind, "
        "       ovv.signature, ovv.offset, ovv.vtable_slot, ovv.status, "
        "       ev.length "
        "FROM kcdx_overlay o "
        "JOIN entity_versions ev ON ev.kcdx_id = o.kcdx_id "
        "JOIN kcdx_overlay_versions ovv ON ovv.overlay_id = o.id "
        "WHERE o.name = ? "
        "  AND ev.valid_through IS NULL "
        "  AND ovv.valid_through IS NULL "
        "ORDER BY (ovv.status = ?) DESC "
        "LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        g_versionMismatchMode ? kSqlOpenOnly : kSqlOrdinal,
        -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "resolve_name_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("name", name),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        return r;
    }
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (g_versionMismatchMode) {
        sqlite3_bind_int64(st, 2, g_verifiedStatusId);  // ORDER BY verified-first.
    } else {
        sqlite3_bind_int64(st, 2, g_gameVersionOrdinal);
        sqlite3_bind_int64(st, 3, g_gameVersionOrdinal);
        sqlite3_bind_int64(st, 4, g_gameVersionOrdinal);
        sqlite3_bind_int64(st, 5, g_gameVersionOrdinal);
        sqlite3_bind_int64(st, 6, g_verifiedStatusId);  // ORDER BY verified-first.
    }

    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        LOG_ERROR_KV(kCategory, "resolve_name_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("name", name),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        sqlite3_finalize(st);
        return r;
    }

    if (rc == SQLITE_DONE) {
        // No row satisfied the join. Distinguish "name not in kcdx_overlay" from
        // "name exists but no covering interval" with a cheap probe so the
        // failure names WHICH gate stopped resolution.
        sqlite3_finalize(st);
        return DiagnoseNameMiss(name);
    }

    // SQLITE_ROW — a covering, addressable row. Decode it.
    r.kcdx_id = static_cast<uint64_t>(sqlite3_column_int64(st, 0));
    r.rva     = static_cast<uint64_t>(sqlite3_column_int64(st, 1));
    if (sqlite3_column_type(st, 2) != SQLITE_NULL) {
        r.has_value = true;
        r.value = sqlite3_column_int64(st, 2);
    }
    if (sqlite3_column_type(st, 3) != SQLITE_NULL) {
        const void* blob = sqlite3_column_blob(st, 3);
        int nbytes = sqlite3_column_bytes(st, 3);
        r.content_hash_hex = HashToHex(blob, nbytes);
        if (blob && nbytes > 0) {
            const uint8_t* b = static_cast<const uint8_t*>(blob);
            r.content_hash.assign(b, b + nbytes);  // raw blob for the survival check
        }
    }
    r.kind = DecodeDict(g_kindDict, sqlite3_column_int64(st, 4));
    if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
        const unsigned char* sig = sqlite3_column_text(st, 5);
        r.verified_signature = sig ? reinterpret_cast<const char*>(sig) : "";
    }
    if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
        r.has_offset = true;
        r.offset = sqlite3_column_int64(st, 6);
    }
    if (sqlite3_column_type(st, 7) != SQLITE_NULL) {
        r.has_vtable_slot = true;
        r.vtable_slot = sqlite3_column_int64(st, 7);
    }
    int64_t statusId = sqlite3_column_int64(st, 8);
    r.status = DecodeDict(g_statusDict, statusId);
    if (sqlite3_column_type(st, 9) != SQLITE_NULL) {
        r.has_length = true;
        r.length = sqlite3_column_int64(st, 9);
    }
    sqlite3_finalize(st);

    // status gate: only "verified" rows resolve at runtime. A covering row that
    // is not verified is a fail-loud miss (its RVA is not promised correct).
    if (r.status != "verified") {
        LOG_DEBUG_KV(kCategory, "resolve_name_miss",
            log::KV::BareStr("reason", "not_verified"),
            log::KV("name", name),
            log::KV("kcdx_id", (unsigned long long)r.kcdx_id),
            log::KV("status", r.status.empty() ? "(unknown)" : r.status.c_str()),
            log::KV::BareStr("detail",
                "a covering overlay-version row exists but its status is not "
                "\"verified\" — the verified facts are not promised correct for "
                "the running build, so refdb refuses it"));
        NameResolution miss;  // found=false
        return miss;
    }

    r.found = true;
    LOG_DEBUG_KV(kCategory, "resolve_name_hit",
        log::KV("name", name),
        log::KV("kcdx_id", (unsigned long long)r.kcdx_id),
        log::KV("rva", (unsigned long long)r.rva),
        log::KV("kind", r.kind));
    return r;
}

namespace {

// On a resolve-by-name that returned no covering verified row, probe which gate
// stopped it: name absent from kcdx_overlay (name_unknown) vs. present but no
// covering entity_versions (no_entity_version) vs. present with an entity
// version but no covering overlay_versions (no_overlay_version). Each is a
// distinct fail-loud reason a caller must tell apart.
NameResolution DiagnoseNameMiss(const std::string& name) {
    NameResolution miss;  // found=false

    // Does the name exist at all?
    int64_t kcdxId = 0;
    int64_t overlayId = 0;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "SELECT id, kcdx_id FROM kcdx_overlay WHERE name = ? LIMIT 1;",
                -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) {
                overlayId = sqlite3_column_int64(st, 0);
                kcdxId = sqlite3_column_int64(st, 1);
            }
            sqlite3_finalize(st);
        }
    }
    if (overlayId == 0) {
        LOG_DEBUG_KV(kCategory, "resolve_name_miss",
            log::KV::BareStr("reason", "name_unknown"),
            log::KV("name", name),
            log::KV::BareStr("detail",
                "no kcdx_overlay row carries this name — an un-curated or "
                "misspelled target name"));
        return miss;
    }

    // Name exists. Is there a covering entity_versions interval? In
    // version-mismatch mode there is no running ordinal — check for an
    // open-interval row instead (the same "latest verified" relaxation the
    // main query uses).
    bool hasEntityVersion = false;
    {
        sqlite3_stmt* st = nullptr;
        const char* kEvSql = g_versionMismatchMode
            ? "SELECT 1 FROM entity_versions ev "
              "WHERE ev.kcdx_id = ? AND ev.valid_through IS NULL "
              "LIMIT 1;"
            : "SELECT 1 FROM entity_versions ev "
              "JOIN game_versions f ON f.id = ev.valid_from "
              "LEFT JOIN game_versions t ON t.id = ev.valid_through "
              "WHERE ev.kcdx_id = ? "
              "  AND f.ordinal <= ? AND (ev.valid_through IS NULL OR t.ordinal >= ?) "
              "LIMIT 1;";
        if (sqlite3_prepare_v2(g_db, kEvSql, -1, &st, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, kcdxId);
            if (!g_versionMismatchMode) {
                sqlite3_bind_int64(st, 2, g_gameVersionOrdinal);
                sqlite3_bind_int64(st, 3, g_gameVersionOrdinal);
            }
            hasEntityVersion = (sqlite3_step(st) == SQLITE_ROW);
            sqlite3_finalize(st);
        }
    }
    if (!hasEntityVersion) {
        LOG_DEBUG_KV(kCategory, "resolve_name_miss",
            log::KV::BareStr("reason", "no_entity_version"),
            log::KV("name", name),
            log::KV("kcdx_id", (unsigned long long)kcdxId),
            log::KV::BareStr("detail",
                "the name resolves to a kcdx_id but no entity_versions interval "
                "covers the running build — no address for this version"));
        return miss;
    }

    // Entity version covers, so the missing piece is a covering overlay_versions
    // interval (the WHERE in the main query failed on the overlay side).
    LOG_DEBUG_KV(kCategory, "resolve_name_miss",
        log::KV::BareStr("reason", "no_overlay_version"),
        log::KV("name", name),
        log::KV("kcdx_id", (unsigned long long)kcdxId),
        log::KV("overlay_id", (long long)overlayId),
        log::KV::BareStr("detail",
            "the name has an address for the running build but no "
            "kcdx_overlay_versions interval covers it — no verified facts for "
            "this version"));
    return miss;
}

}  // namespace

IdResolution ResolveById(uint64_t kcdx_id) {
    IdResolution r;
    if (!IsLoaded()) {
        LogNotLoaded("ResolveById");
        return r;
    }

    // Covering-interval entity_versions lookup (no kcdx_overlay join — a bulk id
    // has no curated name). Returns the address + the argument-width FLOOR. The
    // floor is ALWAYS marked an estimate, never conflated with a verified
    // signature (signature_is_floor_estimate stays true).
    // ev.length appended LAST (column 6) so the existing 0..5 indices below are
    // unchanged — the survival check needs the span the content_hash covers.
    //
    // VERSION-MISMATCH FALLBACK: same shape as ResolveByName — when the
    // running build's tag is not in game_versions, the resolver selects the
    // OPEN-interval row (valid_through IS NULL) instead of the
    // ordinal-bounded covering interval.
    static const char* kSqlOrdinal =
        "SELECT ev.rva, ev.value, ev.content_hash, ev.signature, "
        "       ev.observed_arg_slots, ev.caller_reg_arg_count, ev.length "
        "FROM entity_versions ev "
        "JOIN game_versions f ON f.id = ev.valid_from "
        "LEFT JOIN game_versions t ON t.id = ev.valid_through "
        "WHERE ev.kcdx_id = ? "
        "  AND f.ordinal <= ? AND (ev.valid_through IS NULL OR t.ordinal >= ?) "
        "LIMIT 1;";
    static const char* kSqlOpenOnly =
        "SELECT ev.rva, ev.value, ev.content_hash, ev.signature, "
        "       ev.observed_arg_slots, ev.caller_reg_arg_count, ev.length "
        "FROM entity_versions ev "
        "WHERE ev.kcdx_id = ? AND ev.valid_through IS NULL "
        "LIMIT 1;";

    sqlite3_stmt* st = nullptr;
    int rc = sqlite3_prepare_v2(g_db,
        g_versionMismatchMode ? kSqlOpenOnly : kSqlOrdinal,
        -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR_KV(kCategory, "resolve_id_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("kcdx_id", (unsigned long long)kcdx_id),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        return r;
    }
    sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(kcdx_id));
    if (!g_versionMismatchMode) {
        sqlite3_bind_int64(st, 2, g_gameVersionOrdinal);
        sqlite3_bind_int64(st, 3, g_gameVersionOrdinal);
    }

    rc = sqlite3_step(st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        LOG_ERROR_KV(kCategory, "resolve_id_failed",
            log::KV::BareStr("reason", "query_error"),
            log::KV("kcdx_id", (unsigned long long)kcdx_id),
            log::KV("sqlite_rc", (long long)rc),
            log::KV("sqlite_msg", sqlite3_errmsg(g_db)));
        sqlite3_finalize(st);
        return r;
    }
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(st);
        LOG_DEBUG_KV(kCategory, "resolve_id_miss",
            log::KV::BareStr("reason", "no_entity_version"),
            log::KV("kcdx_id", (unsigned long long)kcdx_id),
            log::KV::BareStr("detail",
                "no entity_versions interval covers the running build for this "
                "kcdx_id — either an unknown id or one with no form on this "
                "game version"));
        return r;
    }

    r.kcdx_id = kcdx_id;
    r.rva = static_cast<uint64_t>(sqlite3_column_int64(st, 0));
    if (sqlite3_column_type(st, 1) != SQLITE_NULL) {
        r.has_value = true;
        r.value = sqlite3_column_int64(st, 1);
    }
    if (sqlite3_column_type(st, 2) != SQLITE_NULL) {
        const void* blob = sqlite3_column_blob(st, 2);
        int nbytes = sqlite3_column_bytes(st, 2);
        r.content_hash_hex = HashToHex(blob, nbytes);
        if (blob && nbytes > 0) {
            const uint8_t* b = static_cast<const uint8_t*>(blob);
            r.content_hash.assign(b, b + nbytes);  // raw blob for the survival check
        }
    }
    if (sqlite3_column_type(st, 3) != SQLITE_NULL) {
        const unsigned char* sig = sqlite3_column_text(st, 3);
        r.floor_signature = sig ? reinterpret_cast<const char*>(sig) : "";
    }
    r.observed_arg_slots = sqlite3_column_int64(st, 4);
    if (sqlite3_column_type(st, 5) != SQLITE_NULL)
        r.caller_reg_arg_count = sqlite3_column_int64(st, 5);
    if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
        r.has_length = true;
        r.length = sqlite3_column_int64(st, 6);
    }
    sqlite3_finalize(st);

    r.found = true;
    r.signature_is_floor_estimate = true;  // never a verified signature.
    LOG_DEBUG_KV(kCategory, "resolve_id_hit",
        log::KV("kcdx_id", (unsigned long long)r.kcdx_id),
        log::KV("rva", (unsigned long long)r.rva),
        log::KV::BareStr("signature_is_floor_estimate", "true"));
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
    g_verifiedStatusId = -1;
    g_versionMismatchMode = false;
    g_kindDict.clear();
    g_statusDict.clear();
}

}  // namespace kcdx::refdb
