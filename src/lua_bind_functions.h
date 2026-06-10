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

// Declare a plugin function (name + author-supplied signature) into the SAME
// in-memory per-stem store the Lua `kcdx.dll.declare` binder writes. The single
// write-seam both surfaces share: the Lua binder and the C++ kcdxDllInterface
// both call this, so the store-insert logic lives in ONE place (no duplicate
// map write). `ns` is the <author>.<plugin> namespace; `fn` the bare function
// name; `signature` the author's ABI (REQUIRED — a callback hook needs it and
// a compiled DLL does not carry it). Returns false (and writes nothing) on an
// empty `ns` / `fn` / `signature` — the caller fails loud at its own surface
// (a Lua error / a logged C++ teaching diagnostic), never a silent drop. A
// re-declare of the same (ns, fn) overwrites with the newest signature.
// Launch-time only (never a hot path); the store's mutex guards the write.
bool DeclareFunction(const std::string& ns, const std::string& fn,
                     const std::string& signature);

// The externally-resolvable facts of a function reference — the C++ peer of the
// Lua `value:resolve()` table. Filled by ResolveFunctionRef. `address` is a raw
// resolved VA when `has_address` (no float rounding — the C++ side has no
// LUA_NUMBER precision hazard); `reason` carries the miss token on !found
// (name_unknown / db_not_loaded / not_declared). The string fields are the
// caller's to own (copied out of the engine stores).
struct ResolvedFunctionRef {
    bool        found = false;
    bool        is_game = true;
    bool        has_address = false;
    uintptr_t   address = 0;
    std::string signature;   // verified (game) / declared (plugin) ABI; "" when none.
    std::string reason;      // on !found.
};

// Resolve a function reference to its fields — the SAME refdb + declared-store
// resolution the Lua `:resolve()` path runs (reused, not reimplemented). Three
// call shapes mirror the Lua accesses one-to-one:
//   - is_game=true,  has_id=false, stem+name set → a game-DLL function by name
//     (kcdx.functions.<stem>.<name>); resolves against refdb by name.
//   - is_game=true,  has_id=true,  kcdx_id set   → a game-DLL function by stable
//     id (kcdx.functions.by_id[N]); resolves against refdb by id.
//   - is_game=false, stem+name set               → a plugin-DLL function
//     (kcdx.functions["<ns>"].<fn>); resolves against the declared-signature +
//     PDB-address stores.
// A miss returns found=false + a reason token — never raises, never a silent
// empty. Launch-/install-time safe (in-memory cache reads; no per-call SQL).
ResolvedFunctionRef ResolveFunctionRef(bool is_game, const std::string& stem,
                                       const std::string& name, bool has_id,
                                       uint64_t kcdx_id);

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
