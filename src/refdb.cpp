#include "refdb.h"

#include <sqlite3.h>

// Compile-time proof the vendored SQLite header is the version we built
// against (3.50.x). If the include resolves to a stale/older header this
// fails the build rather than silently linking a mismatched ABI.
static_assert(SQLITE_VERSION_NUMBER >= 3050000,
              "kcdx refdb requires SQLite 3.50.0 or newer");

namespace kcdx::refdb {

const char* SqliteVersion() {
    // References a libsqlite3 symbol → proves the static lib links, not just
    // that the header is on the include path.
    return sqlite3_libversion();
}

}  // namespace kcdx::refdb
