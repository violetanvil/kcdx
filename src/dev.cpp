// dev.cpp — intentionally empty.
//
// All kcdx::dev functionality moved into kcdx::log when the two
// subsystems were unified. dev.h is now a header-only shim providing
// backwards-compatible aliases (KV typedef, KCDX_DEV macro,
// SetEnabled / IsEnabled / IsCategoryEnabled forwards).
//
// The file is kept in the build so CMakeLists.txt's source list and
// any out-of-tree includes that referenced `dev.cpp` keep resolving.
// Delete it once the migration of all call sites to LOG_*_KV is
// complete and the shim itself can be retired.

#include "dev.h"
