#pragma once

// plugin_pdb — PDB auto-load worker. At C++ plugin load, parse the plugin DLL's
// sidecar .pdb via the Windows DbgHelp API and populate EVERY internal
// (non-exported) function's ADDRESS into the kcdx.functions["<author>.<plugin>"]
// namespace (the declared-store's address half, src/lua_bind_functions.cpp).
//
// This is source #2 of the three plugin-function address sources (the
// kcdx.functions.* design). It is ADDITIVE: an author who ships no PDB (or a
// FASTLINK PDB) loses nothing they had — declared functions + exports still
// resolve. The one load-bearing constraint: internal auto-load works ONLY with a
// /DEBUG:FULL (self-contained) PDB. A FASTLINK PDB (the VS2017+ default) is a build-machine-OBJ-indexing stub
// that carries NO private symbols when deployed.

#include <windows.h>

#include <string>

namespace kcdx::plugin_pdb {

// Populate kcdx.functions["<pluginNamespace>"].* internal-function ADDRESSES
// from the plugin DLL's sidecar .pdb. Called once per C++ plugin at the Load
// wave (src/plugin_loader.cpp), after the module is mapped, with the plugin's
// loaded HMODULE + its on-disk path + its <author>.<plugin> namespace in hand.
//
//   - module          : the plugin DLL's loaded HMODULE (GetModuleInformation
//                       gives its base + image size).
//   - dllPath         : the plugin DLL's on-disk path (SymLoadModuleEx reads
//                       its sidecar .pdb from beside it).
//   - pluginNamespace : the <author>.<plugin> stem the addresses register under
//                       (kcdx.functions["<ns>"].*).
//
// Graceful, additive, never fatal — one of three outcomes, each with its own
// teaching log line:
//   - no .pdb beside the DLL                -> INFO, exports + declared only.
//   - .pdb present, load/GUID-age mismatch  -> WARN naming the mismatch.
//   - .pdb loads, ZERO in-range functions   -> WARN: the FASTLINK/stub case;
//                                              "rebuild with /DEBUG:FULL".
//
// One-shot at launch (not a hot path); allocation here is fine.
void PopulateFromPdb(HMODULE module, const std::string& dllPath,
                     const std::string& pluginNamespace);

}  // namespace kcdx::plugin_pdb
