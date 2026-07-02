// CAP-89 — PDB auto-load probe plugin (DbgHelp internal-symbol enumeration).
//
// This DLL is the PROBE FIXTURE, not a feature. It exists to answer ONE
// checkable runtime unknown: can the Windows DbgHelp API enumerate a foreign
// plugin DLL's NON-EXPORTED internal functions — name AND address — from a
// Release-build sidecar .pdb, after SymInitialize + SymLoadModuleEx?
//
// The DLL contributes exactly two things the engine-side probe needs:
//
//   1. A KNOWN, deliberately NON-EXPORTED internal function with a
//      recognizable name (cap89_internal_probe_target). It is a plain free
//      function with internal linkage — NOT marked dllexport, so it does NOT
//      appear in the DLL's export table. It can therefore appear in the
//      enumeration ONLY if DbgHelp read it from the sidecar .pdb. That is the
//      whole point: an exports-only enumeration would NOT find it (the
//      falsifying outcome), a PDB-backed enumeration would.
//
//   2. The standard kcdxPlugin_Load export so the engine loads this DLL like
//      any C++ plugin and reaches the probe site with this module's HMODULE +
//      on-disk path in hand.
//
// The internal target must survive dead-code elimination: a function with no
// observable caller and no exported address would be stripped by the linker,
// and then there is nothing in the .pdb to find. It is kept alive by being
// CALLED from kcdxPlugin_Load with a volatile sink, so the compiler cannot
// prove it dead.

#include <windows.h>

#include <cstdint>

#include "kcdx/Interfaces.h"

namespace {

// Sink that the compiler cannot reason away — forces the call below to be
// emitted and the target to be retained with a real address in the .pdb.
volatile int g_cap89_sink = 0;

// === The NON-EXPORTED internal under test =============================
//
// Plain internal-linkage free function. NOT __declspec(dllexport), NOT
// extern "C" with an export — it is absent from the DLL export table by
// construction. A recognizable, unambiguous name so the engine-side probe
// can search the enumeration for it exactly.
//
// noinline so it keeps its own distinct entry in the .pdb (an inlined body
// would have no standalone symbol/address to enumerate).
__declspec(noinline) int cap89_internal_probe_target(int seed) {
    // A non-trivial body the optimizer keeps; the volatile read/write defeats
    // both folding and elimination.
    volatile int s = seed;
    return s * 3 + 89;
}

}  // namespace

// === kcdxPlugin_Load — the standard C++ plugin entry ==================
//
// Calls the internal target through the volatile sink so the target is
// retained (real code, real address) and reachable for the .pdb to record.
// The engine-side probe runs AFTER this returns, against this module.
extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    (void)api;
    g_cap89_sink = cap89_internal_probe_target(g_cap89_sink + 1);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
