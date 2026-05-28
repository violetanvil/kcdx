#pragma once

// kcdx reference database (refdb) — read-mostly SQLite-backed lookup module.
//
// This is the module stub: at present it exposes only the vendored SQLite
// version string, which doubles as the compile-time + link-time proof that
// sqlite3.h is reachable from engine code and libsqlite3 resolves. The query
// surface that consumes the database lands in a later step.

namespace kcdx::refdb {

// Returns the linked SQLite library's version string (e.g. "3.50.4").
const char* SqliteVersion();

}  // namespace kcdx::refdb
