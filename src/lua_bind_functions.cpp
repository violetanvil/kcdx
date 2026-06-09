// kcdx.functions.* — the function-reference value namespace + kcdx.dll.declare.
//
// A function-reference VALUE names a function (game or plugin) and carries its
// address (+ its signature, when known). The hook and statement verbs that
// consume this value as arg-1 are not yet built; this namespace stands alone
// and SELF-VERIFIES here via the `:resolve()` introspection accessor
// (mirroring the `kcdx.locator.*` value's `:resolve` and the `kcdx.op.*`
// value's `:emit_for`).
//
// TWO structurally-disjoint populations, distinguished at the call site by the
// stem:
//
//   -- GAME-DLL functions — no-dot stem, sourced from the reference DB.
//   kcdx.functions.WHGame.SaveGame      -- stem = DLL filename minus extension
//   kcdx.functions.by_id[144]           -- the stable-across-versions ID accessor
//                                       --   (GAME functions only)
//   -- PLUGIN-DLL functions — dotted <author>.<plugin> stem, bracket-indexed,
//   -- sourced from the plugin author via kcdx.dll.declare (NOT the DB).
//   kcdx.functions["redmoon.outfit_mod"].CanSwapInCombat
//
// The dotted-vs-undotted stem is structurally disjoint (game DLLs never have a
// dotted stem; plugin stems are always <author>.<plugin>), so the two
// populations never collide.
//
//   -- kcdx.dll.declare(plugin_namespace, function_map) — the author declares
//   -- their own DLL's functions with signatures COPIED FROM THEIR OWN SOURCE
//   -- (no disassembly). Populates kcdx.functions["<plugin_namespace>"].*.
//   kcdx.dll.declare("redmoon.outfit_mod", {
//       CanSwapInCombat = { signature = "bool (ptr self)" },
//       OnOutfitSwap    = { signature = "void (ptr self, i32 outfit_id)" },
//   })
//
// THE REFERENCE VALUE. A `kcdx.functions.X.Y` access returns a function-
// reference userdata carrying { stem, name, is_game, va, signature }. It is the
// value the LATER hook/statement verbs (not yet built) accept as arg-1. The
// settled-fork `:resolve()` accessor returns an introspection table the test
// asserts against (the value self-checks with NO consuming verb):
//
//   local ref = kcdx.functions.WHGame.SaveGame
//   local r = ref:resolve()
//   -- r.found       : bool   — true on a resolvable reference
//   -- r.is_game     : bool   — true for a game-DLL (DB-sourced) reference
//   -- r.stem        : string — "WHGame" / "<author>.<plugin>"
//   -- r.name        : string — the bare function name
//   -- r.address     : kcdx.memory.pointer | nil — the resolved VA (pointer
//   --                          userdata, NEVER a lua number — a pointer-magnitude
//   --                          VA rounds on a LUA_NUMBER=float build);
//   --                          nil when unresolved (a plugin DLL not yet loaded /
//   --                          a game name the DB does not carry)
//   -- r.has_address : bool   — true when the address resolved (distinguishes a
//   --                          real VA from "not resolvable yet")
//   -- r.signature   : string — the verified ABI (game: from the DB; plugin:
//   --                          from the author's declare map); "" when none
//   -- r.reason      : string — on found==false, a token (name_unknown /
//   --                          db_not_loaded / not_declared)
//
// GAME-DLL POPULATION SHAPE — LAZY __index, NOT eager-enumerate. The reference
// DB (refdb) is keyed by canonical NAME (refdb::ResolveByName) and is opened on
// the worker thread at engine startup; it exposes no per-DLL-stem bulk
// enumerate, and the curated cache carries no directly-groupable DLL stem. A
// no-dot stem access (kcdx.functions.WHGame) returns a per-stem PROXY table
// whose __index resolves a name on first access via refdb::ResolveByName — the
// equivalent of "eager at startup" against the same contract (a name resolves
// to address+ABI), without forcing a startup walk that groups a flat name cache
// into per-stem subtables (the design states eager as the approach; the lazy
// __index meets the same contract and fits the refdb surface that actually
// exists). The signature for a game function IS available from
// refdb (NameResolution.verified_signature, the ABI verified by body-wide
// stack-arg analysis against the binary), so the game-DLL reference value
// carries address AND signature.
//
// PLUGIN-DLL POPULATION — kcdx.dll.declare writes into an in-memory per-stem
// map; a dotted-stem access (kcdx.functions["a.b"]) returns a proxy table whose
// __index reads that map. A plugin function carries the AUTHOR-DECLARED
// signature regardless of whether the DLL is loaded (the signature is the one
// irreducible thing a callback hook needs; it comes from the author's source,
// not the binary). The ADDRESS for a plugin function enters via PDB auto-load /
// the C export table — a later additive path, not yet built, deferred behind a
// separate probe — so here a declared plugin function resolves with its
// signature but has_address=false (NOT a failure: a static byte op needs only
// address+range, a callback hook needs the signature; the declare path here
// establishes the declaration + signature half).
//
// DESIGN NOTES:
//   * Lua bridge: raw Lua C API only. The reference value is a raw
//     lua_newuserdata + a metatable registered with luaL_newmetatable; the
//     namespace tables are plain tables with C-function __index metamethods. NO
//     kcdx-side static-const sentinel in any GCObject, so the frealloc canary
//     stays zero.
//   * Lua precision: the resolved VA is pushed as a
//     kcdx.memory.pointer userdata via PushPointer — NEVER lua_pushinteger (a
//     pointer-magnitude VA rounds on a LUA_NUMBER=float build). The stable id on
//     by_id[N] is a small integer index, pushed/read as an integer.
//   * Fail loud: a bad declare arg raises a teaching error at the call site;
//     an unresolvable reference returns found=false + a reason token, never a
//     silent empty. kcdx.functions.<unknown> returns a reference value
//     whose :resolve() reports found=false (the value exists so the consuming
//     verb gets a uniform type; the miss surfaces at :resolve / at the verb).
//   * Disassembler test: kcdx.dll.declare is the strongest case — the author
//     declares from their OWN source (they have the types), and the consumer
//     hooks BY NAME with no disassembly. The engine carries the ABI, so no
//     hand-written signature is ever required on the common path. No author hex.

#include "lua_bind_functions.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "lua_bind_helpers.h"  // PushPointer
#include "lua_memory.h"        // kcdx::lua_memory::pointer
#include "log.h"               // LOG_*_KV, ::kcdx::log::KV
#include "refdb.h"             // refdb::ResolveByName / ResolveById

namespace kcdx::lua_bind_functions {

namespace {

// The function-reference-value userdata metatable name (LUA_REGISTRYINDEX key).
// Stable identifier, same convention as kcdx.locator.value / kcdx.op.value.
constexpr const char* kFunctionRefMetatable = "kcdx.functions.value";

// ---- the declared-plugin-function store ----------------------------------
//
// kcdx.dll.declare writes here. Keyed by "<plugin_namespace>\0<fn_name>"; the
// value is the author-declared signature. An in-memory map populated at plugin
// load (kcdx.dll.declare runs from plugin.lua), read at reference resolution.
// Both run on the same (worker) thread as every other kcdx.* Lua binding, but a
// mutex guards the map for defensiveness — declaration is a launch-time
// concern, never a hot path, so the lock cost is irrelevant (memory.md).
struct DeclaredFn {
    std::string signature;  // the author-declared ABI; never empty (declare requires it).
};

std::mutex g_declared_mutex;
// key = plugin_namespace + '\0' + fn_name (the NUL keeps the two segments
// unambiguous regardless of dots in either).
std::map<std::string, DeclaredFn> g_declared;

std::string DeclaredKey(const std::string& ns, const std::string& fn) {
    std::string k;
    k.reserve(ns.size() + 1 + fn.size());
    k = ns;
    k.push_back('\0');
    k += fn;
    return k;
}

// Look up a declared plugin function. Returns true + fills `sigOut` iff
// (plugin_namespace, fn_name) was declared.
bool LookupDeclared(const std::string& ns, const std::string& fn,
                    std::string& sigOut) {
    std::lock_guard<std::mutex> lk(g_declared_mutex);
    auto it = g_declared.find(DeclaredKey(ns, fn));
    if (it == g_declared.end()) return false;
    sigOut = it->second.signature;
    return true;
}

// ---- the plugin-function ADDRESS store -----------------------------------
//
// The address half the declare path leaves unfilled. plugin_pdb::PopulateFromPdb
// writes here at C++ plugin load (PDB-sourced internal addresses); ResolveRef
// reads it. Keyed identically to g_declared ("<ns>\0<fn>"). SEPARATE from
// g_declared so the two facts are orthogonal: a plugin function can have a
// declared signature with no PDB address (a callback hook works, a static op
// is unresolvable yet), a PDB address with no declared signature (a static op
// works, a callback hook reports "declare the signature"), or both.
//
// A second mutex (not g_declared_mutex) so the PDB-load write path and the
// declare write path never contend; declaration and PDB-load are both
// launch-time concerns (memory.md — never a hot path, lock cost irrelevant).
std::mutex g_pdb_addr_mutex;
std::map<std::string, uintptr_t> g_pdb_addr;

// Look up a PDB-sourced plugin-function address. Returns true + fills `addrOut`
// iff (ns, fn) has a recorded address.
bool LookupPluginAddress(const std::string& ns, const std::string& fn,
                         uintptr_t& addrOut) {
    std::lock_guard<std::mutex> lk(g_pdb_addr_mutex);
    auto it = g_pdb_addr.find(DeclaredKey(ns, fn));
    if (it == g_pdb_addr.end()) return false;
    addrOut = it->second;
    return true;
}

// ---- the function-reference value ----------------------------------------
//
// The userdata payload. is_game distinguishes a game-DLL (DB-sourced) reference
// from a plugin-DLL (declare-sourced) reference. `stem` is "WHGame" (game) or
// "<author>.<plugin>" (plugin); `name` the bare function name; `by_id` carries
// the stable id (has_id) for a kcdx.functions.by_id[N] reference (game-only).
// Carries std::string members → placement-new on push + explicit dtor at __gc.
struct FunctionRef {
    bool        is_game = true;
    std::string stem;          // "WHGame" or "<author>.<plugin>".
    std::string name;          // the bare function name.
    bool        has_id = false;
    uint64_t    kcdx_id = 0;    // the by_id[N] handle (game-only).
};

FunctionRef* CheckRef(lua_State* L, int idx) {
    return static_cast<FunctionRef*>(
        luaL_checkudata(L, idx, kFunctionRefMetatable));
}

int Lua_RefGc(lua_State* L) {
    auto* ref = CheckRef(L, 1);
    ref->~FunctionRef();
    return 0;
}

// Resolve a FunctionRef to (found, address, signature). Game references resolve
// against refdb (by name, or by id for a by_id reference); plugin references
// resolve against the declared-fn store (signature present, address deferred to
// the later additive PDB/C-export path). Fills the result fields; returns
// nothing (writes into the out-params).
struct RefResolution {
    bool        found = false;
    bool        has_address = false;
    uintptr_t   address = 0;
    std::string signature;
    std::string reason;  // on found==false.
};

RefResolution ResolveRef(const FunctionRef& ref) {
    RefResolution out;
    if (ref.is_game) {
        if (!refdb::IsLoaded()) {
            out.found = false;
            out.reason = "db_not_loaded";
            return out;
        }
        refdb::CallerContext ctx;
        ctx.callType = "kcdx.functions";  // attribution tag for the resolve logs.
        if (ref.has_id) {
            refdb::IdResolution r = refdb::ResolveById(ref.kcdx_id, ctx);
            if (!r.found) {
                out.found = false;
                out.reason = "name_unknown";
                return out;
            }
            out.found = true;
            // ResolveById carries only the floor signature (an honest lower
            // bound, never a verified ABI — refdb.h). The address resolves
            // against the running version's WHGame base.
            out.signature = r.floor_signature;
            uintptr_t va = refdb::ResolveAddrById(ref.kcdx_id, ctx);
            if (va != 0) { out.has_address = true; out.address = va; }
            return out;
        }
        refdb::NameResolution r = refdb::ResolveByName(ref.name, ctx);
        if (!r.found) {
            out.found = false;
            out.reason = "name_unknown";
            return out;
        }
        out.found = true;
        out.signature = r.verified_signature;  // the VERIFIED ABI (may be empty for a sig-less kind).
        uintptr_t va = refdb::ResolveAddrByName(ref.name, ctx);
        if (va != 0) { out.has_address = true; out.address = va; }
        return out;
    }

    // Plugin reference: two INDEPENDENT facts may exist for (stem, name) —
    // the declared signature (kcdx.dll.declare → g_declared) and the
    // PDB-sourced address (plugin_pdb → g_pdb_addr). Either alone resolves the
    // reference; the reference is found if EITHER is present.
    //   - signature only (declared, no PDB)  → has_address=false; a callback
    //     hook works, a static op has no address yet.
    //   - address only (PDB internal, never declared) → signature="";
    //     has_address=true; a static op works, a callback hook reports
    //     "signature needed — declare it" (it has no ABI to marshal).
    //   - both                               → the full reference.
    std::string sig;
    const bool declared = LookupDeclared(ref.stem, ref.name, sig);
    uintptr_t va = 0;
    const bool hasAddr = LookupPluginAddress(ref.stem, ref.name, va);

    if (!declared && !hasAddr) {
        out.found = false;
        out.reason = "not_declared";
        return out;
    }
    out.found = true;
    out.signature = declared ? sig : "";  // "" for a PDB-only (undeclared) internal.
    if (hasAddr) {
        out.has_address = true;
        out.address = va;
    }
    return out;
}

// :resolve() -> table — the settled-fork self-check seam (mirrors locator
// :resolve / op :emit_for). Returns an introspection table; never raises (a
// miss is found=false + reason, never an error — fail loud at the verb, not here).
int Lua_RefResolve(lua_State* L) {
    auto* ref = CheckRef(L, 1);
    RefResolution r = ResolveRef(*ref);

    lua_newtable(L);
    int t = lua_gettop(L);

    lua_pushboolean(L, r.found ? 1 : 0);
    lua_setfield(L, t, "found");
    lua_pushboolean(L, ref->is_game ? 1 : 0);
    lua_setfield(L, t, "is_game");
    lua_pushstring(L, ref->stem.c_str());
    lua_setfield(L, t, "stem");
    lua_pushstring(L, ref->name.c_str());
    lua_setfield(L, t, "name");

    if (!r.found) {
        lua_pushstring(L, r.reason.c_str());
        lua_setfield(L, t, "reason");
        lua_pushboolean(L, 0);
        lua_setfield(L, t, "has_address");
        return 1;
    }

    // signature — the verified/declared ABI; "" is legitimate (a sig-less kind).
    lua_pushstring(L, r.signature.c_str());
    lua_setfield(L, t, "signature");

    lua_pushboolean(L, r.has_address ? 1 : 0);
    lua_setfield(L, t, "has_address");
    if (r.has_address) {
        // A VA is pushed as a kcdx.memory.pointer userdata, NEVER
        // lua_pushinteger (a pointer-magnitude value rounds on a
        // LUA_NUMBER=float build).
        kcdx::lua_bind_helpers::PushPointer(
            L, kcdx::lua_memory::pointer(r.address));
        lua_setfield(L, t, "address");
    } else {
        lua_pushnil(L);
        lua_setfield(L, t, "address");
    }
    return 1;
}

// :name() / :stem() / :signature() are folded into :resolve()'s table; the
// value carries no other methods. A future hook/statement verb (not yet built)
// reads the FunctionRef payload directly via CheckRef, not through Lua.

// Install the function-reference-value metatable. Idempotent. Stack effect: 0.
void SetupMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kFunctionRefMetatable) == 0) {
        lua_pop(L, 1);  // already registered.
        return;
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");  // mt.__index = mt (methods live on mt).
    lua_pushcfunction(L, Lua_RefGc);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "kcdx.functions.value");
    lua_setfield(L, -2, "__metatable");  // hide the metatable from pak Lua.
    lua_pushcfunction(L, Lua_RefResolve);
    lua_setfield(L, -2, "resolve");
    lua_pop(L, 1);  // pop mt; restore stack.
}

// Push a fresh function-reference value carrying `ref`. Stack effect: +1.
void PushRef(lua_State* L, const FunctionRef& ref) {
    auto* mem = static_cast<FunctionRef*>(
        lua_newuserdata(L, sizeof(FunctionRef)));
    new (mem) FunctionRef(ref);
    luaL_getmetatable(L, kFunctionRefMetatable);
    lua_setmetatable(L, -2);
}

// ---- the per-stem proxy tables -------------------------------------------
//
// A stem access (kcdx.functions.WHGame, kcdx.functions["a.b"]) returns a proxy
// table whose __index mints a FunctionRef for the accessed name. The proxy
// carries two upvalue-equivalents in its own fields (set at creation): the stem
// string (field "_stem") and the is_game flag (field "_is_game"). The __index
// C function reads them off the table.

// __index for a per-stem proxy table: kcdx.functions.<stem>.<name> -> a
// function-reference value. The accessed name is the key (arg 2); the stem +
// is_game live on the proxy table (arg 1) as hidden fields.
int Lua_StemIndex(lua_State* L) {
    // arg 1 = the proxy table; arg 2 = the accessed key (the function name).
    if (lua_type(L, 2) != LUA_TSTRING) {
        // A non-string key on a stem table has no reference; return nil (not a
        // silent SUCCESS — there is genuinely no function for a numeric key on a
        // name-keyed stem).
        lua_pushnil(L);
        return 1;
    }
    const char* name = lua_tostring(L, 2);

    lua_getfield(L, 1, "_stem");
    std::string stem = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    lua_getfield(L, 1, "_is_game");
    bool is_game = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    FunctionRef ref;
    ref.is_game = is_game;
    ref.stem = stem;
    ref.name = name;
    PushRef(L, ref);
    return 1;
}

// Build a per-stem proxy table (with its __index metamethod + hidden _stem /
// _is_game fields) and leave it on the stack top. Stack effect: +1.
void PushStemProxy(lua_State* L, const std::string& stem, bool is_game) {
    lua_newtable(L);  // the proxy table.
    int proxy = lua_gettop(L);
    lua_pushstring(L, stem.c_str());
    lua_setfield(L, proxy, "_stem");
    lua_pushboolean(L, is_game ? 1 : 0);
    lua_setfield(L, proxy, "_is_game");

    lua_newtable(L);  // the proxy's metatable.
    lua_pushcfunction(L, Lua_StemIndex);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "kcdx.functions.stem");
    lua_setfield(L, -2, "__metatable");  // hide it from pak Lua.
    lua_setmetatable(L, proxy);
}

// __index for the by_id table: kcdx.functions.by_id[N] -> a game function
// reference carrying the stable id. N must be an integer (a stable id).
int Lua_ByIdIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TNUMBER) {
        lua_pushnil(L);
        return 1;
    }
    lua_Integer id = lua_tointeger(L, 2);
    if (id <= 0) {
        // A stable id is a positive integer; a non-positive key has no
        // reference. nil (genuinely no function for this key, not a silent SUCCESS).
        lua_pushnil(L);
        return 1;
    }
    FunctionRef ref;
    ref.is_game = true;     // by_id is GAME-functions only (no plugin-DLL by-id accessor).
    ref.stem = "by_id";
    ref.has_id = true;
    ref.kcdx_id = static_cast<uint64_t>(id);
    ref.name = "";          // by-id reference; the name is not the handle.
    PushRef(L, ref);
    return 1;
}

// __index for the top-level kcdx.functions table. A key is one of:
//   "by_id"             -> the by_id proxy table (game-only stable-id accessor).
//   a no-dot string     -> a game-DLL stem proxy (kcdx.functions.WHGame).
//   a dotted string     -> a plugin-DLL stem proxy (kcdx.functions["a.b"]).
// The dotted-vs-undotted distinction is structural: game stems are
// dot-free; plugin stems are <author>.<plugin>.
int Lua_FunctionsIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    std::string key = lua_tostring(L, 2);

    if (key == "by_id") {
        // Build the by_id proxy on demand (a table with a __index that mints a
        // by-id game reference). Memoize it on the functions table so repeated
        // kcdx.functions.by_id accesses return the same table.
        lua_newtable(L);  // the by_id proxy.
        lua_newtable(L);  // its metatable.
        lua_pushcfunction(L, Lua_ByIdIndex);
        lua_setfield(L, -2, "__index");
        lua_pushliteral(L, "kcdx.functions.by_id");
        lua_setfield(L, -2, "__metatable");
        lua_setmetatable(L, -2);
        // Memoize: kcdx.functions[key] = the proxy (so it is returned directly
        // next time, not re-minted). arg 1 is the functions table.
        lua_pushvalue(L, -1);
        lua_setfield(L, 1, "by_id");
        return 1;
    }

    const bool dotted = key.find('.') != std::string::npos;
    // dotted → plugin stem (declare-sourced); no-dot → game stem (DB-sourced).
    PushStemProxy(L, key, /*is_game=*/!dotted);
    return 1;
}

// ---- kcdx.dll.declare ----------------------------------------------------

// kcdx.dll.declare(plugin_namespace, function_map)
//
// Declares a plugin DLL's functions with author-supplied signatures, populating
// kcdx.functions["<plugin_namespace>"].*. function_map is
//   { FnName = { signature = "bool (ptr self)" }, ... }.
//
// plugin_namespace is the AUTHOR-OWNED <author>.<plugin> string the author
// passes explicitly — a cross-plugin export surface where the full namespace is
// stated. The engine does NOT stamp it (unlike a bare-name declaration); the
// caller supplies the qualified <author>.<plugin> form (example shape:
// kcdx.dll.declare("redmoon.outfit_mod", {...})). Returns true on accept,
// false on a rejected entry; a bad-arg shape raises a teaching Lua error at the
// call site (an author bug to surface there, the kcdx.locator/op constructor
// convention).
int Lua_DllDeclare(lua_State* L) {
    // arg 1: plugin_namespace (string, REQUIRED — the <author>.<plugin> form).
    if (lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L,
            "kcdx.dll.declare(plugin_namespace, function_map): "
            "`plugin_namespace` (string) is required — your plugin's "
            "<author>.<plugin> namespace (e.g. \"redmoon.outfit_mod\"). Call "
            "shape: kcdx.dll.declare(\"redmoon.outfit_mod\", { CanSwapInCombat "
            "= { signature = \"bool (ptr self)\" } }).");
    }
    const std::string ns = lua_tostring(L, 1);
    if (ns.empty()) {
        return luaL_error(L,
            "kcdx.dll.declare(plugin_namespace, function_map): "
            "`plugin_namespace` must be a non-empty <author>.<plugin> string.");
    }

    // arg 2: function_map (table, REQUIRED).
    if (lua_type(L, 2) != LUA_TTABLE) {
        return luaL_error(L,
            "kcdx.dll.declare(plugin_namespace, function_map): "
            "`function_map` (table) is required — a map of FnName = "
            "{ signature = \"...\" }. Call shape: "
            "kcdx.dll.declare(\"%s\", { CanSwapInCombat = { signature = "
            "\"bool (ptr self)\" } }).", ns.c_str());
    }

    // Walk the function_map. Each entry is FnName (string key) = { signature =
    // "..." } (table value). A malformed entry raises a teaching error (fail
    // loud — never a silent drop of an author-declared function).
    int declared = 0;
    lua_pushnil(L);
    while (lua_next(L, 2) != 0) {
        // key on -2, value on -1.
        if (lua_type(L, -2) != LUA_TSTRING) {
            const char* badType = lua_typename(L, lua_type(L, -2));
            lua_pop(L, 2);  // value + key.
            return luaL_error(L,
                "kcdx.dll.declare(\"%s\", ...): every function_map key must be "
                "a function NAME (string); found a %s key. Shape: "
                "{ CanSwapInCombat = { signature = \"...\" } }.",
                ns.c_str(), badType);
        }
        const std::string fnName = lua_tostring(L, -2);

        if (lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 2);
            return luaL_error(L,
                "kcdx.dll.declare(\"%s\", ...): function `%s` must map to a "
                "table { signature = \"...\" }; found a %s. Shape: "
                "{ %s = { signature = \"bool (ptr self)\" } }.",
                ns.c_str(), fnName.c_str(),
                lua_typename(L, lua_type(L, -1)), fnName.c_str());
        }

        lua_getfield(L, -1, "signature");
        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 3);  // signature + value + key.
            return luaL_error(L,
                "kcdx.dll.declare(\"%s\", ...): function `%s` is missing a "
                "`signature` string — a callback hook needs the function's ABI "
                "from your source (the engine cannot read it from a compiled "
                "DLL). Shape: { %s = { signature = \"bool (ptr self)\" } }.",
                ns.c_str(), fnName.c_str(), fnName.c_str());
        }
        const std::string sig = lua_tostring(L, -1);
        lua_pop(L, 1);  // signature.

        if (sig.empty()) {
            lua_pop(L, 2);
            return luaL_error(L,
                "kcdx.dll.declare(\"%s\", ...): function `%s` has an empty "
                "`signature` — declare the function's ABI (e.g. "
                "\"bool (ptr self)\").", ns.c_str(), fnName.c_str());
        }

        {
            std::lock_guard<std::mutex> lk(g_declared_mutex);
            g_declared[DeclaredKey(ns, fnName)] = DeclaredFn{ sig };
        }
        ++declared;
        lua_pop(L, 1);  // pop value, keep key for the next lua_next.
    }

    LOG_INFO_KV("DLL_DECLARE", "declared",
        ::kcdx::log::KV("namespace", ns),
        ::kcdx::log::KV("functions", declared));

    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable with the kcdx global table on top
// of the stack. Registers the function-reference-value metatable, the
// `functions` namespace table (a plain table with a __index metamethod), and
// the `dll` domain (carrying `declare`). Stack effect: 0.
void bind(lua_State* L) {
    SetupMetatable(L);

    const int kcdx_idx = lua_gettop(L);

    // kcdx.functions — a table whose __index dispatches stem accesses. The
    // table itself is empty; every access flows through Lua_FunctionsIndex.
    lua_newtable(L);  // the functions table.
    lua_newtable(L);  // its metatable.
    lua_pushcfunction(L, Lua_FunctionsIndex);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "kcdx.functions");
    lua_setfield(L, -2, "__metatable");  // hide it from pak Lua.
    lua_setmetatable(L, -2);
    lua_setfield(L, kcdx_idx, "functions");

    // kcdx.dll — a domain table carrying `declare`.
    lua_newtable(L);
    lua_pushcfunction(L, Lua_DllDeclare);
    lua_setfield(L, -2, "declare");
    lua_setfield(L, kcdx_idx, "dll");
}

// Record a PDB-sourced address for a plugin function (the address half a
// declared reference leaves unfilled). Called from plugin_pdb at plugin load;
// the matching read is LookupPluginAddress in ResolveRef.
void RecordPluginAddress(const std::string& ns, const std::string& fn,
                         uintptr_t address) {
    std::lock_guard<std::mutex> lk(g_pdb_addr_mutex);
    g_pdb_addr[DeclaredKey(ns, fn)] = address;
}

}  // namespace kcdx::lua_bind_functions
