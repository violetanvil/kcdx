#pragma once

// kcdx.functions.* + kcdx.dll.declare — the §9.3 function-reference value
// namespace + the author-declaration verb. See lua_bind_functions.cpp for the
// surface contract.

#include <cstdint>
#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_functions {

// Register the `kcdx.functions` reference namespace (a lazy-resolving table)
// and the `kcdx.dll` domain (carrying `declare`) on the kcdx table at the top
// of the Lua stack, plus the function-reference-value userdata metatable in
// LUA_REGISTRYINDEX. Stack effect: 0.
void bind(lua_State* L);

// A function-reference value's externally-readable facts. Filled by
// ReadFunctionRef when the value at a stack index IS a kcdx.functions.value
// userdata. The hook / statement verbs (which accept a reference value as
// their `target` positional) read this to route a reference to the same
// name-resolution path a `target = "<name>"` string takes — without reaching
// into the anonymous-namespace FunctionRef type.
struct FunctionRefView {
    bool        is_game = true;   // game-DLL (DB-sourced) vs plugin-DLL (declare-sourced).
    std::string stem;             // "WHGame" (game) or "<author>.<plugin>" (plugin).
    std::string name;             // the bare function name ("" for a by_id reference).
    bool        has_id = false;   // true for a kcdx.functions.by_id[N] reference.
    uint64_t    kcdx_id = 0;      // the stable id (game-only) when has_id.
};

// If the value at `idx` is a kcdx.functions.value userdata, fill `out` and
// return true; otherwise return false (leaves `out` untouched, raises nothing).
// The arg-1-type dispatch a hook/statement verb runs: a string target →
// name resolution; a reference value → this view → the SAME name path.
bool ReadFunctionRef(lua_State* L, int idx, FunctionRefView& out);

// Record a PDB-sourced ADDRESS for a plugin function under
// kcdx.functions["<ns>"].<fn>. The address half that the kcdx.dll.declare path
// leaves unfilled (declare carries the signature; this carries the address) —
// populated at C++ plugin load by plugin_pdb::PopulateFromPdb. ResolveRef reads
// this store for a plugin reference's address (signature from g_declared,
// address from here — the two stores are independent, mirroring that a static
// op needs only the address and a callback hook needs only the signature).
//
// Same in-memory, mutex-guarded shape as the declared-signature store; both are
// written at launch (never a hot path) and read at reference resolution. A
// later write for the same (ns,fn) overwrites (a re-source is the newest fact).
void RecordPluginAddress(const std::string& ns, const std::string& fn,
                         uintptr_t address);

}  // namespace kcdx::lua_bind_functions
