// CAP-90 — PDB auto-load regression fixture (the C++ DLL half).
//
// Proves the FEATURE built in src/plugin_pdb.{cpp,h}: at C++ plugin load, the
// engine parses this DLL's sidecar /DEBUG:FULL .pdb via DbgHelp and populates
// every internal (non-exported) FUNCTION's address into
// kcdx.functions["<author>.<plugin>"].* — so an undeclared internal resolves its
// address with zero author friction.
//
// This DLL contributes:
//
//   1. A KNOWN, deliberately NON-EXPORTED internal function
//      (cap90_internal_target). Plain internal-linkage free function — NOT
//      __declspec(dllexport), absent from the export table by construction
//      (verify: dumpbin /EXPORTS cap-90.dll lists ONLY kcdxPlugin_Load). It can
//      therefore appear in kcdx.functions ONLY via the PDB auto-load path, so
//      the Lua row that asserts has_address=true for it goes RED if PDB
//      auto-load did not populate the internal's address (or a FASTLINK
//      fallback silently dropped on a present /DEBUG:FULL PDB).
//
//   2. The standard kcdxPlugin_Load export so the engine loads this DLL as a
//      C++ plugin and reaches the PopulateFromPdb call site with this module's
//      HMODULE + on-disk path in hand.
//
// The internal target must survive dead-code elimination: noinline + a volatile
// sink so /OPT:REF keeps it (a stripped target has no .pdb entry to find). The
// Lua self-check (plugin.lua, same plugin → same <author>.<plugin> namespace)
// reads the address back and reports the falsifiable row.

#include <windows.h>

#include <cstdint>

#include "kcdx/Interfaces.h"

// Sink the compiler cannot reason away — forces the call below to be emitted
// and the target retained with a real address in the .pdb.
volatile int g_cap90_sink = 0;

// === The NON-EXPORTED internal under test =============================
//
// A normal file-scope free function with EXTERNAL linkage — NOT
// __declspec(dllexport), so it is absent from the DLL export table, but it has a
// unique, addressable symbol in the .pdb (this is what a real plugin author
// ships for cross-mod hooking: a plain named function, not exported, not in an
// anonymous namespace). The PDB records it under its bare name
// `cap90_internal_target`, which the Lua row indexes exactly as
// kcdx.functions["ts.cap_90_pdb_autoload"].cap90_internal_target — the author
// types the bare name, the engine owns the namespace.
//
// noinline so it keeps its own distinct entry in the .pdb (an inlined body has
// no standalone symbol/address to enumerate).
__declspec(noinline) int cap90_internal_target(int seed) {
    volatile int s = seed;
    return s * 7 + 90;
}

// === kcdxPlugin_Load — the standard C++ plugin entry ==================
//
// Calls the internal target through the volatile sink so the target is
// retained (real code, real address) and reachable for the .pdb to record. The
// engine's PopulateFromPdb runs AFTER this returns, against this module.
extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    (void)api;
    g_cap90_sink = cap90_internal_target(g_cap90_sink + 1);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
