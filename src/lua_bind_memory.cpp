// Table-level Lua bindings under kcdx.memory.*:
//   pointer(addr)              — constructor function
//   scan_pattern(pat)
//   scan_pattern_from_module(mod, pat)
//   get_module_base_address([mod])
//   allocate(size)
//   free(ptr)
//
// Plus the bind() entry point that wires the metatables and this table
// onto the kcdx global. Called by lua_bind.cpp::RegisterKcdxTable.

#include <cstdint>
#include <new>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"
#include "patch_engine.h"
#include "pe_helpers.h"

// dynamic_hook / dynamic_call live in their own TUs. Forward-declare
// just the Lua C entry points so kFunctions can reference them.
namespace kcdx::lua_bind_dynamic_hook {
    int Lua_DynamicHook(lua_State* L);
}
namespace kcdx::lua_bind_dynamic_call {
    int Lua_DynamicCall(lua_State* L);
}

namespace kcdx::lua_memory {

namespace {

using kcdx::lua_bind_helpers::PushPointer;
using kcdx::lua_bind_helpers::CheckPointer;

constexpr const wchar_t* kDefaultModuleW = L"WHGame.dll";

// narrow -> wide for module names supplied from Lua. Lua strings are
// 8-bit; module names are ASCII in practice. 1:1 widen is sufficient.
std::wstring Widen(const char* s, size_t len) {
    std::wstring w;
    w.reserve(len);
    for (size_t i = 0; i < len; ++i) w.push_back((wchar_t)(unsigned char)s[i]);
    return w;
}

// --- constructors --------------------------------------------------------

// kcdx.memory.pointer(address)  -> pointer userdata
int Lua_Pointer(lua_State* L) {
    lua_Integer addr = luaL_optinteger(L, 1, 0);
    PushPointer(L, pointer((uintptr_t)addr));
    return 1;
}

// --- module helpers ------------------------------------------------------

int Lua_GetModuleBaseAddress(lua_State* L) {
    pe::ModuleView mv;
    const wchar_t* mod = kDefaultModuleW;
    std::wstring wmod;
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        size_t len = 0;
        const char* s = lua_tolstring(L, 1, &len);
        wmod = Widen(s, len);
        mod = wmod.c_str();
    }
    if (!pe::OpenModule(mod, mv)) {
        // FAIL-STATE INSTRUMENTATION (fail loud, never silently drop): a
        // null-pointer return here is indistinguishable from success — the
        // author can't tell a typo'd module name from a not-yet-loaded one.
        // Name the failed module so the WHY is visible. Warn (not Error): an
        // author-facing degraded result, not a crash-class engine fault.
        std::string modNarrow;
        for (const wchar_t* w = mod; *w; ++w) modNarrow.push_back((char)*w);
        log::WarnF("kcdx.memory.get_module_base_address: module '%s' is not "
                   "loaded (or the name is wrong) — returning a null pointer. "
                   "Using it will crash; check the module name / that it is "
                   "loaded.",
                   modNarrow.c_str());
        PushPointer(L, pointer(0));
        return 1;
    }
    PushPointer(L, pointer(reinterpret_cast<uintptr_t>(mv.baseBytes)));
    return 1;
}

int Lua_ScanPattern(lua_State* L) {
    size_t plen = 0;
    const char* p = luaL_checklstring(L, 1, &plen);
    std::string pat(p, plen);
    auto va = kcdx::patch::ScanModuleFirst(kDefaultModuleW, pat);
    if (!va.has_value()) {
        // FAIL-STATE INSTRUMENTATION (fail loud, never silently drop): a
        // no-match scan returns a null pointer that reads identical to a
        // resolved-to-0 success and to "module absent". Name the pattern +
        // module so "ran, 0 matches" is distinguishable. Warn: an
        // author-facing degraded result, not a crash-class engine fault.
        log::WarnF("kcdx.memory.scan_pattern: pattern '%s' did not match in "
                   "module 'WHGame.dll' (0 matches, or the module is not "
                   "loaded) — returning a null pointer. Using it will crash.",
                   pat.c_str());
    }
    PushPointer(L, pointer(va.value_or(0)));
    return 1;
}

int Lua_ScanPatternFromModule(lua_State* L) {
    size_t mlen = 0, plen = 0;
    const char* m = luaL_checklstring(L, 1, &mlen);
    const char* p = luaL_checklstring(L, 2, &plen);
    std::string modNarrow(m, mlen);
    std::string pat(p, plen);
    auto va = kcdx::patch::ScanModuleFirst(Widen(m, mlen), pat);
    if (!va.has_value()) {
        // FAIL-STATE INSTRUMENTATION (fail loud, never silently drop): see
        // Lua_ScanPattern — name the pattern + module so a no-match scan is
        // distinguishable from a resolved-to-0 success / an absent module.
        log::WarnF("kcdx.memory.scan_pattern_from_module: pattern '%s' did not "
                   "match in module '%s' (0 matches, or the module is not "
                   "loaded) — returning a null pointer. Using it will crash.",
                   pat.c_str(), modNarrow.c_str());
    }
    PushPointer(L, pointer(va.value_or(0)));
    return 1;
}

int Lua_Allocate(lua_State* L) {
    lua_Integer size = luaL_checkinteger(L, 1);
    if (size <= 0) {
        // FAIL-STATE INSTRUMENTATION (fail loud, never silently drop): a
        // rejected size returns a null pointer with no signal — name the
        // rejected size, mirroring the alloc-fail Error one line below (but
        // at Warn: bad author input, not a crash-class engine fault).
        log::WarnF("kcdx.memory.allocate: size %lld is not positive — nothing "
                   "allocated, returning a null pointer. Pass a positive byte "
                   "count.",
                   (long long)size);
        PushPointer(L, pointer(0));
        return 1;
    }
    void* mem = new (std::nothrow) uint8_t[size]();
    if (!mem) {
        log::ErrorF("kcdx.memory.allocate: new[%lld] returned null", (long long)size);
        PushPointer(L, pointer(0));
        return 1;
    }
    PushPointer(L, pointer((uintptr_t)mem));
    return 1;
}

int Lua_Free(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    delete[] reinterpret_cast<uint8_t*>(p->get_address());
    // The pointer userdata itself stays valid (will __gc normally);
    // we just freed the memory it pointed at. Mark it null so any
    // accidental subsequent use is caught by the per-method null check.
    *p = pointer(0);
    return 0;
}

// --- table assembly -----------------------------------------------------

const luaL_Reg kFunctions[] = {
    {"pointer",                  Lua_Pointer},
    {"get_module_base_address",  Lua_GetModuleBaseAddress},
    {"scan_pattern",             Lua_ScanPattern},
    {"scan_pattern_from_module", Lua_ScanPatternFromModule},
    {"allocate",                 Lua_Allocate},
    {"free",                     Lua_Free},
    {"dynamic_hook",             kcdx::lua_bind_dynamic_hook::Lua_DynamicHook},
    {"dynamic_call",             kcdx::lua_bind_dynamic_call::Lua_DynamicCall},
    {nullptr, nullptr},
};

}  // namespace

void bind(lua_State* L) {
    // The kcdx global table is expected at stack top.
    int kcdx_idx = lua_gettop(L);

    // Register metatables once (lua_bind_helpers handles idempotency).
    kcdx::lua_bind_helpers::RegisterMetatables(L);

    // Build kcdx.memory = { ... }
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "memory");

    log::Info("kcdx.memory.* registered");
}

}  // namespace kcdx::lua_memory
