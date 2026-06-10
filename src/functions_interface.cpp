// kcdx::functions_interface — engine-side impl of kcdxFunctionsInterface.
//
// Mirrors the Lua kcdx.functions.* binder (src/lua_bind_functions.cpp) — the
// SAME refdb + declared-store resolution, surfaced as a by-value reference the
// C++ author reads directly. Three mint thunks:
//
//   GameByName(stem, name)  ↔ kcdx.functions.<stem>.<name>   (game, by name)
//   GameById(kcdxId)        ↔ kcdx.functions.by_id[N]        (game, by stable id)
//   PluginByName(ns, name)  ↔ kcdx.functions["<ns>"].<name>  (plugin, declared)
//
// Each resolves via lua_bind_functions::ResolveFunctionRef (the public seam over
// the Lua :resolve() path — one resolution, both surfaces) and packs the 8
// fields into a kcdxFunctionRef returned BY VALUE.
//
// String lifetime: kcdxFunctionRef carries const char* fields the contract
// promises are engine-owned and process-lifetime. The resolution yields
// std::string values (signature / reason) and the caller hands transient
// const char* inputs (stem / name); both are INTERNED into a process-lifetime
// string set so the returned pointers outlive the call. Interning is a
// launch-/install-time concern (a reference is minted at hook-install time, not
// on a hot path), so the set's mutex + dedup cost is irrelevant (memory.md).

#include "functions_interface.h"

#include <mutex>
#include <set>
#include <string>

#include "lua_bind_functions.h"  // ResolveFunctionRef, ResolvedFunctionRef

namespace kcdx::functions_interface {

namespace {

// Process-lifetime intern pool for the const char* fields a minted reference
// hands back. A std::set never invalidates element addresses on insert (unlike
// vector), so a c_str() taken after insert stays valid for the process lifetime
// — the lifetime the kcdxFunctionRef contract promises. Deduped so repeated
// mints of the same name/signature do not grow the pool unbounded.
std::mutex g_intern_mutex;
std::set<std::string> g_intern;

// Intern `s` and return a process-lifetime const char*. Empty string interns to
// a stable "" (so a "no signature" / "name is empty" field is a valid non-null
// pointer, never a dangling one).
const char* Intern(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_intern_mutex);
    return g_intern.insert(s).first->c_str();
}

// Pack a resolved reference into the by-value kcdxFunctionRef the ABI returns.
// `stem` / `name` are the caller's inputs (interned so they outlive the call);
// signature / reason come from the resolution (also interned). address is the
// raw resolved VA as a void* (no rounding — the C++ side has no LUA_NUMBER
// hazard).
kcdxFunctionRef Pack(const std::string& stem, const std::string& name,
                     const kcdx::lua_bind_functions::ResolvedFunctionRef& r) {
    kcdxFunctionRef out;
    out.found      = r.found;
    out.isGame     = r.is_game;
    out.stem       = Intern(stem);
    out.name       = Intern(name);
    out.address    = r.has_address ? reinterpret_cast<void*>(r.address) : nullptr;
    out.hasAddress = r.has_address;
    out.signature  = Intern(r.signature);  // "" interns to a stable "" (never null).
    out.reason     = Intern(r.reason);     // "" when found; the miss token otherwise.
    return out;
}

// GameByName("WHGame", "SaveGame") — a game-DLL reference by name.
kcdxFunctionRef Thunk_GameByName(const char* stem, const char* name) {
    const std::string stemStr = (stem && stem[0]) ? stem : "";
    const std::string nameStr = (name && name[0]) ? name : "";
    auto r = kcdx::lua_bind_functions::ResolveFunctionRef(
        /*is_game=*/true, stemStr, nameStr, /*has_id=*/false, /*kcdx_id=*/0);
    return Pack(stemStr, nameStr, r);
}

// GameById(N) — a game-DLL reference by stable id (game-only). The name is empty
// (the id is the handle), matching the Lua by_id[N] reference; the stem is the
// "by_id" sentinel the Lua path also stamps.
kcdxFunctionRef Thunk_GameById(unsigned long long kcdxId) {
    auto r = kcdx::lua_bind_functions::ResolveFunctionRef(
        /*is_game=*/true, /*stem=*/"by_id", /*name=*/"", /*has_id=*/true,
        static_cast<uint64_t>(kcdxId));
    return Pack(/*stem=*/"by_id", /*name=*/"", r);
}

// PluginByName("redmoon.outfit", "CanSwapInCombat") — a plugin-DLL reference,
// resolved against the declared-signature + PDB-address stores.
kcdxFunctionRef Thunk_PluginByName(const char* pluginNamespace,
                                   const char* name) {
    const std::string stemStr = (pluginNamespace && pluginNamespace[0])
                                    ? pluginNamespace : "";
    const std::string nameStr = (name && name[0]) ? name : "";
    auto r = kcdx::lua_bind_functions::ResolveFunctionRef(
        /*is_game=*/false, stemStr, nameStr, /*has_id=*/false, /*kcdx_id=*/0);
    return Pack(stemStr, nameStr, r);
}

// -----------------------------------------------------------------------------
// Vtable instance. Order MATCHES the kcdxFunctionsInterface struct field order
// in include/kcdx/Interfaces.h byte-for-byte (append-only ABI; fixed offsets).
// DO NOT reorder.
// -----------------------------------------------------------------------------

kcdxFunctionsInterface g_functionsInterface = {
    /*GameByName=*/   Thunk_GameByName,
    /*GameById=*/     Thunk_GameById,
    /*PluginByName=*/ Thunk_PluginByName,
};

}  // namespace

const kcdxFunctionsInterface* GetInterface() {
    return &g_functionsInterface;
}

}  // namespace kcdx::functions_interface
