// kcdx.addr.* — Address Library names exposed as Lua-visible pointer
// userdata.
//
// At kcdx-global init we walk the Address Library and create one
// entry per resolvable row (game_version matches AND status =
// verified AND rva != 0). Each entry is a kcdx.memory.pointer
// userdata wrapping the resolved VA. Authors use it as:
//
//   kcdx.hook(kcdx.addr.lua_pcall, { mode = "before", ... })
//
// Discoverable via the Lua `pairs()` iterator since kcdx.addr is a
// plain table:
//
//   for name, ptr in pairs(kcdx.addr) do
//       kcdx.log.info("ADDR", "%s -> %s", name, tostring(ptr))
//   end
//
// Names that don't resolve on the running KCD2 build (wrong
// game_version, unverified status, or rva == 0) are SKIPPED — they
// don't appear in kcdx.addr at all. Authors who attempt to use a
// missing name get the normal Lua "attempt to index nil" error,
// which surfaces the unmet dependency immediately rather than
// deferring to a hook-install error later.
//
// The kcdx.addr table itself is plain (no metatable). It's a one-
// shot snapshot built at kcdx-global init; if the Address Library
// ever supports runtime updates, this would need to listen for the
// update and rebuild — but that's not on the roadmap.

#include "lua_bind_addr.h"

#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "address_library.h"
#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"

namespace kcdx::lua_bind_addr {

namespace {

struct Ctx {
    lua_State* L;
    int        addrTableIdx;   // absolute stack index of kcdx.addr
    size_t     count;
};

bool ForEachCb(uint64_t /*id*/, const char* name, uintptr_t va, void* userdata) {
    auto* ctx = static_cast<Ctx*>(userdata);
    if (!name || !name[0] || va == 0) return true;  // skip empty names
    // PushPointer leaves a kcdx.memory.pointer userdata at the top
    // of the stack. lua_setfield stores it at kcdx.addr[name] and
    // pops it.
    kcdx::lua_bind_helpers::PushPointer(
        ctx->L, kcdx::lua_memory::pointer(va));
    lua_setfield(ctx->L, ctx->addrTableIdx, name);
    ctx->count++;
    return true;
}

}  // namespace

void bind(lua_State* L) {
    // Caller has the kcdx table on top of the Lua stack. We create
    // kcdx.addr as a plain sub-table, populate from the Address
    // Library, then leave the kcdx table on top for the next
    // sub-binder.
    int kcdx_idx = lua_gettop(L);

    lua_newtable(L);                // [..., kcdx, addr]
    int addr_idx = lua_gettop(L);

    Ctx ctx{L, addr_idx, 0};
    kcdx::address_library::ForEachResolvable(&ForEachCb, &ctx);

    lua_setfield(L, kcdx_idx, "addr");  // kcdx.addr = addr; pops addr

    log::InfoF("kcdx.addr: populated %zu name(s) from Address Library "
               "(matching running KCD2 build)", ctx.count);
}

}  // namespace kcdx::lua_bind_addr
