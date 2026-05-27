// kcdx.code{...} — Lua-side code/trampoline-region allocation.
//
// A core authoring verb: top-level
// (like kcdx.hook / kcdx.bytes / kcdx.command), configuring -> {named
// table}. A thin Lua binder over the EXISTING, proven trampoline pool +
// symbol table (the legacy [[trampoline]] TOML schema used these same
// engine paths — kcdx::trampoline::Allocate{Branch,Local} + symbols::
// Register). The engine is NOT touched; this brings the Lua surface to
// parity with the already-shipped C++ kcdxTrampolineInterface, so no C++
// trampoline work is owed.
//
//   local region = kcdx.code{
//       name   = "outfit_gate_logic",         -- required (logs + diagnostics)
//       bytes  = "48 83 EC 28 ...",           -- optional initial machine code
//       size   = 256,                         -- optional total alloc size
//       pool   = "branch",                    -- optional "branch"|"local"
//       export = "violetanvil.outfit_gate_logic", -- optional published symbol
//   }
//
// Returns a LIVE kcdx.memory.pointer userdata to the allocated region on
// success (so the author can :set_byte/:get_byte into it, or pass it as a
// kcdx.hook target). On failure returns (nil, teaching error) — the
// standard kcdx-binder error idiom.
//
// DESIGN LOCKS:
//   * ALLOCATE IMMEDIATELY at the call; return a live pointer. Allocation
//     has no conflict-resolution semantics (fresh memory can't clash like
//     two hooks on one address), and the author needs the address NOW.
//     So kcdx.code does NOT go through lua_registry's deferred apply pass
//     (no Kind::Code, no queue) — it allocates RIGHT THEN and returns the
//     pointer. Mirrors kcdx.command's register-immediately + the C++
//     trampoline interface (which allocates immediately).
//   * `export` REGISTERS THE SYMBOL IMMEDIATELY (right after allocation)
//     via symbols::Register(bareName, addr, owner). The author writes a
//     BARE export name; the engine derives the <owner> prefix from the
//     calling plugin and publishes <owner>.<export> — the SAME
//     <pluginname>.<name> model as author-targets.
//     A dotted `export` is an author error (the engine supplies the
//     prefix). This is EARLIER than the apply-pass target_symbol
//     resolution — that ordering is correct (the symbol exists before any
//     apply-pass Lookup resolves it). A later kcdx.hook{target_symbol=...}
//     / kcdx.bytes resolves this export at the apply pass (symbols::Lookup,
//     self > other precedence), finding what kcdx.code registered. On
//     COLLISION (the same plugin re-exporting the same bare name — names
//     are now per-namespace, so cross-plugin clashes can't happen) the
//     allocation still stands (fresh memory — fine) but the EXPORT failed:
//     log a loud error naming the prior owner (symbols::OwnerOf on the full
//     name) and return (nil, teaching error).
//   * OWNER IDENTITY via lua_registry::OwningPluginForCurrentCall (the same
//     mechanism kcdx.command/publish/on/hook use), mapped to a
//     kcdxPluginHandle via plugins::HandleOf. An anonymous caller
//     (resolves to "") -> HandleOf("") misses -> kcdxInvalidPluginHandle,
//     which the trampoline allocators accept (the handle is recorded for
//     attribution only). We warn so the anonymous allocation is observable.
//
// Lua precision (LUA_NUMBER is float): the allocated address is a POINTER —
// it is returned as a kcdx.memory.pointer userdata via PushPointer, NEVER
// lua_pushinteger (a VA must not round-trip through lua_Number=float).
//
// Lua bridge (one shared lua_State): raw Lua C API only; no kcdx-side
// static-const sentinel (the frealloc canary stays zero) — the pointer userdata
// is a raw lua_newuserdata via PushPointer.

#include "lua_bind_code.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"
#include "lua_registry.h"
#include "patch_engine.h"   // kcdx::patch::ParseBytes (the legacy [[trampoline]] hex parser)
#include "plugin_loader.h"  // kcdx::plugins::HandleOf
#include "symbols.h"        // kcdx::symbols::Register / OwnerOf
#include "trampoline.h"     // kcdx::trampoline::AllocateBranch / AllocateLocal

namespace kcdx::lua_bind_code {

namespace {

// --- Unknown-key rejection (fail loud, never silent-drop) ---------------
//
// The recognized option-key set for kcdx.code. A typo'd `byts=` / `export=`
// would otherwise be silently ignored, the author's intent lost. The
// iteration is the shared kcdx::lua_bind_helpers::FindUnknownKey; this list
// stays local because the key set belongs to this binder.
static const char* kKnown[] = {
    "name", "bytes", "size", "pool", "export",
};

// kcdx.code{ name=, bytes=, size=, pool=, export= }
//
//   name   (string, required)  : name for logs + export diagnostics.
//   bytes  (string, optional)  : initial machine-code bytes (hex string,
//                                same parse as [[trampoline]]).
//   size   (integer, optional) : total alloc size; defaults to #bytes. If
//                                > #bytes the tail is NOP-padded (0x90) so
//                                other plugins can patch into the unused
//                                space. Must be >= #bytes.
//   pool   (string, optional)  : "branch" (default; within +/-2GB of
//                                WHGame.dll .text) | "local" (anywhere).
//   export (string, optional)  : publish the allocated address as a named
//                                symbol resolvable by target_symbol.
//
// Must declare `bytes` OR `size` (or both). Returns a kcdx.memory.pointer
// userdata to the region on success; (nil, teaching error) on any failure.
int Lua_Code(lua_State* L) {
    // --- Validate arg 1 is a table ---
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.code{...}: expects a single table argument with field "
            "`name` (string) plus at least one of `bytes` (hex string) or "
            "`size` (integer), and optional `pool` (\"branch\"|\"local\") "
            "and `export` (string). Call shape: kcdx.code{ name = "
            "\"my_region\", bytes = \"90 90 90\", size = 64 }");
        return 2;
    }

    // Reject an unrecognized option key before reading anything — a typo'd
    // key would otherwise vanish silently (fail loud, never silent-drop).
    {
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, 1, kKnown, sizeof(kKnown) / sizeof(kKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.code: unrecognized option key '%s' — not a recognized "
                "kcdx.code option (check for a typo).",
                bad.c_str());
            return 2;
        }
    }

    // --- name (string, required) ---
    lua_getfield(L, 1, "name");
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.code{...}: `name` (string) is required — the name used in "
            "logs and export diagnostics (e.g. name = \"outfit_gate_logic\").");
        return 2;
    }
    std::string name = lua_tostring(L, -1);
    lua_pop(L, 1);

    // --- bytes (string, optional) ---
    std::vector<uint8_t> bytes;
    bool haveBytes = false;
    lua_getfield(L, 1, "bytes");
    if (lua_type(L, -1) == LUA_TSTRING) {
        std::string bytesStr = lua_tostring(L, -1);
        lua_pop(L, 1);
        try {
            // Same hex parse the legacy [[trampoline]] parser uses
            // (config.cpp ParseOneTrampoline). Reuse — don't reinvent.
            bytes = kcdx::patch::ParseBytes(bytesStr);
            haveBytes = true;
        } catch (const std::exception& e) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.code{ name = \"%s\" }: parse error in `bytes`: %s — "
                "`bytes` is a hex string of machine code (e.g. "
                "\"48 83 EC 28\").",
                name.c_str(), e.what());
            return 2;
        }
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: `bytes`, if present, must be a hex "
            "string of machine code (e.g. \"48 83 EC 28\").",
            name.c_str());
        return 2;
    } else {
        lua_pop(L, 1);
    }

    // --- size (integer, optional) ---
    bool haveSize = false;
    size_t sizeOverride = 0;
    lua_getfield(L, 1, "size");
    if (lua_type(L, -1) == LUA_TNUMBER) {
        lua_Integer s = lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (s <= 0) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.code{ name = \"%s\" }: `size` must be a positive "
                "integer — the total bytes to allocate.",
                name.c_str());
            return 2;
        }
        sizeOverride = static_cast<size_t>(s);
        haveSize = true;
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: `size`, if present, must be a "
            "positive integer — the total bytes to allocate (NOP-padded "
            "beyond #bytes).",
            name.c_str());
        return 2;
    } else {
        lua_pop(L, 1);
    }

    // --- Must declare bytes OR size (mirror config.cpp:785) ---
    if (!haveBytes && !haveSize) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: must declare either `bytes` (initial "
            "machine code) or `size` (a NOP region to fill in later), or both.",
            name.c_str());
        return 2;
    }

    // --- pool (string, optional; default "branch") ---
    std::string pool = "branch";
    lua_getfield(L, 1, "pool");
    if (lua_type(L, -1) == LUA_TSTRING) {
        pool = lua_tostring(L, -1);
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: `pool`, if present, must be a string "
            "— either \"branch\" (default; within +/-2GB of WHGame.dll .text) "
            "or \"local\" (anywhere).",
            name.c_str());
        return 2;
    }
    lua_pop(L, 1);
    if (pool != "branch" && pool != "local") {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: unknown pool \"%s\" — valid pools are "
            "\"branch\" (default) and \"local\".",
            name.c_str(), pool.c_str());
        return 2;
    }

    // --- export (string, optional; defaults to "") ---
    std::string exportSymbol;
    lua_getfield(L, 1, "export");
    if (lua_type(L, -1) == LUA_TSTRING) {
        exportSymbol = lua_tostring(L, -1);
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: `export`, if present, must be a "
            "string — the symbol name to publish the allocated address "
            "under (resolvable by a later kcdx.hook{ target_symbol = ... }).",
            name.c_str());
        return 2;
    }
    lua_pop(L, 1);

    // --- Compute the final allocation size (mirror trampoline_engine) ---
    // size defaults to #bytes; if a size override is given it must be >=
    // #bytes (the bytes go at the head, the tail is NOP-padded).
    size_t totalSize = haveSize ? sizeOverride : bytes.size();
    if (totalSize < bytes.size()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: declared size (%d) is smaller than "
            "the %d byte(s) of `bytes`.",
            name.c_str(), static_cast<int>(totalSize),
            static_cast<int>(bytes.size()));
        return 2;
    }

    // --- Resolve owner identity (same mechanism as kcdx.command/publish) ---
    // Map the owner NAME to a kcdxPluginHandle via plugins::HandleOf. An
    // anonymous caller (console / pak Lua) resolves to "" -> HandleOf("")
    // misses -> kcdxInvalidPluginHandle, which the trampoline allocators
    // accept (the handle is recorded for attribution only). We warn so the
    // anonymous allocation stays observable.
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);
    kcdxPluginHandle ownerHandle =
        kcdx::plugins::HandleOf(owner.plugin.empty() ? "" : owner.plugin.c_str());
    if (owner.plugin.empty()) {
        log::WarnF("kcdx.code: anonymous caller (no attributed plugin) for "
                   "region '%s' at site=%s:%d — allocating under an invalid "
                   "plugin handle (the region still works).",
                   name.c_str(),
                   callSiteFile.empty() ? "?" : callSiteFile.c_str(),
                   callSiteLine);
    }

    // --- Allocate (branch is default; local is anywhere) ---
    // Mirrors trampoline_engine::ApplyAll: pick the pool, allocate, then
    // copy bytes into the head + NOP-pad the tail.
    void* region = nullptr;
    if (pool == "local") {
        region = kcdx::trampoline::AllocateLocal(ownerHandle, totalSize);
    } else {
        region = kcdx::trampoline::AllocateBranch(ownerHandle, totalSize);
    }
    if (!region) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.code{ name = \"%s\" }: the '%s' trampoline pool could not "
            "allocate %d bytes (out of pool space, or no rel32-reachable "
            "region for \"branch\"). See kcdx.log.",
            name.c_str(), pool.c_str(), static_cast<int>(totalSize));
        return 2;
    }

    // --- Copy bytes into the head, NOP-pad the tail (mirror ApplyAll) ---
    auto* dst = reinterpret_cast<uint8_t*>(region);
    if (!bytes.empty()) {
        std::memcpy(dst, bytes.data(), bytes.size());
    }
    if (totalSize > bytes.size()) {
        // NOP-pad so a plugin patching into the unused tail doesn't trip
        // over zero-init bytes (which decode as `add [rax], al`).
        std::memset(dst + bytes.size(), 0x90 /* x86 NOP */,
                    totalSize - bytes.size());
    }

    uintptr_t addr = reinterpret_cast<uintptr_t>(region);
    LOG_DEBUG("CODE", "[%s] allocated %d bytes at 0x%p (pool=%s, plugin=%s)",
              name.c_str(), static_cast<int>(totalSize), region, pool.c_str(),
              owner.plugin.empty() ? "<anon>" : owner.plugin.c_str());

    // --- Register export IMMEDIATELY if requested (mirror ApplyAll's
    // export branch, but EARLIER — at the call, not the apply pass; that
    // ordering is correct, the symbol exists before any apply-pass
    // Lookup). ---
    //
    // NAMESPACE MODEL: `export` is a BARE name; the
    // engine derives the <owner> prefix from the calling plugin and stores the
    // symbol as <owner>.<export>. The author NEVER types their own prefix — a
    // dotted `export` is an author error. A bare collision is now per-namespace
    // (each plugin gets its own prefix), so a clash only happens when the SAME
    // plugin re-exports the SAME name. On collision the allocation stands
    // (fresh memory — fine) but the export FAILED: loud error + (nil, teaching
    // error).
    if (!exportSymbol.empty()) {
        // Reject a dotted / prefixed export — the engine supplies the prefix.
        if (exportSymbol.find('.') != std::string::npos) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.code{ name = \"%s\" }: `export` must be a BARE name — do "
                "NOT type your own \"<plugin>.\" prefix. The engine derives it "
                "from your [plugin].name and publishes the symbol as "
                "\"<yourplugin>.%s\". You wrote "
                "\"%s\".",
                name.c_str(), exportSymbol.c_str(), exportSymbol.c_str());
            return 2;
        }
        // Fully-qualified name the engine will publish (for diagnostics).
        std::string fullName =
            owner.plugin.empty() ? exportSymbol : (owner.plugin + "." + exportSymbol);
        // The binder now threads the real (author, plugin) pair from the
        // OwningPluginForCurrentCall struct (step 4 of the 2-dot namespace
        // refactor). When the manifest's [plugin].author is still empty
        // (the corpus state before step 6) the symbol table treats the
        // row as legacy 1-dot under <plugin>.<bare>, identical to today's
        // observable behavior; when populated, the export lives under the
        // full <author>.<plugin>.<bare> key.
        if (kcdx::symbols::Register(exportSymbol, addr,
                                    owner.author, owner.plugin)) {
            LOG_DEBUG("CODE", "[%s] exported symbol '%s' -> 0x%p",
                      name.c_str(), fullName.c_str(),
                      reinterpret_cast<void*>(addr));
        } else {
            std::string priorOwner = kcdx::symbols::OwnerOf(fullName);
            log::ErrorF("[kcdx.code '%s'] symbol export collision: '%s' is "
                        "already registered by '%s' — the region is allocated "
                        "but unreachable by symbol.",
                        name.c_str(), fullName.c_str(),
                        priorOwner.empty() ? "?" : priorOwner.c_str());
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.code{ name = \"%s\" }: export '%s' is already registered "
                "by '%s' — your plugin already exported this bare name. Choose "
                "a different `export` name (the region was allocated, but is "
                "unreachable by symbol).",
                name.c_str(), fullName.c_str(),
                priorOwner.empty() ? "?" : priorOwner.c_str());
            return 2;
        }
    }

    // --- Return a LIVE kcdx.memory.pointer userdata to the region. The
    // address is a POINTER — push it via PushPointer (LUA_NUMBER is float),
    // NEVER lua_pushinteger (a VA must not round-trip through
    // lua_Number=float). ---
    kcdx::lua_bind_helpers::PushPointer(L, kcdx::lua_memory::pointer(addr));
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.code is a TOP-LEVEL verb (one of the 6 core authoring verbs),
    // NOT a sub-table — the kcdx table is at
    // the top of the stack; register the function directly on it (like
    // kcdx.hook / kcdx.bytes / kcdx.command).
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Code);
    lua_setfield(L, kcdx_idx, "code");
}

}  // namespace kcdx::lua_bind_code
