// kcdx::hook_chain — per-target chain of kcdx.hook callbacks. See
// hook_chain.h for the model + conflict policy, and
// docs/outstanding-work/smart-replace-conflict-detection.md for the
// footprint-coexistence upgrade this architecture is built to accept.
//
// This file is the NEW kcdx.hook dispatch path. The legacy
// kcdx::scripting dynamic_hook_pre/post path is reference-only and will
// be removed once this is verified.
//
// Structure of this TU:
//   §1  signature -> make_jit_func type-string conversion
//   §2  ChainEntry + Chain data model (footprint-ready per the spec)
//   §3  CanCoexist — the isolated conflict predicate (v1 blunt body)
//   §4  named-arg `args` table construction (the UX surface)
//   §5  call_original bridge over MinHook's pOriginal (true around)
//   §6  DispatchPre / DispatchPost — the C callbacks the JIT thunk calls
//   §7  Add — locator resolve, first-touch install, append to chain

#include "hook_chain.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>  // WideCharToMultiByte / MultiByteToWideChar

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <asmjit/asmjit.h>

#include "address_library.h"   // ResolveByName (address_id = "name")
#include "dynamic_call_jit.h"  // BuildLuaCallThunk (call_original over pOriginal)
#include "hook_engine.h"       // InstallRuntime
#include "log.h"
#include "lua_bind_helpers.h"  // PushPointer
#include "lua_memory.h"        // pointer, kPointerMetatable
#include "patch_engine.h"      // Resolve, ResolvedPatch (locator pipeline)
#include "rom_borrowed/runtime_func_t.h"
#include "rom_borrowed/type_info_t.h"

namespace kcdx::hook_chain {

namespace {

// ===========================================================================
// §1  signature -> make_jit_func type strings
// ===========================================================================
//
// make_jit_func consumes return/param type STRINGS, parsed by
// kcdx::rom::get_type_info_from_string into type_info_t. Map each parsed
// hook_signature::Type to the canonical string that resolves to the
// right type_info_t (see rom_borrowed/type_info_t.cpp's matcher):
//   string_  <- "string" / "const char*"
//   boolean_ <- "bool"
//   ptr_     <- "ptr"
//   float_   <- "float"
//   double_  <- "double"
//   integer_ <- anything else (default)

const char* SigTypeToJitString(kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Void: return "void";
        case T::Bool: return "bool";
        case T::F32:  return "float";
        case T::F64:  return "double";
        case T::Ptr:  return "ptr";
        // Wide/narrow C strings both marshal as a char* the wrapper
        // pushes as a Lua string. (wstr write-back nuance is handled by
        // the value_wrapper at :set() time; see lua_memory.)
        case T::Wstr: return "string";
        case T::Cstr: return "string";
        // Integer widths must be PRESERVED, not collapsed to i64. The
        // call_original thunk (BuildLuaCallThunk) emits an asmjit call to
        // the original function with this exact ABI signature — calling
        // an int32-returning function as int64 reads the return from the
        // full RAX when the callee only wrote EAX (upper bits undefined),
        // corrupting the result. (Root cause of CAP-20-around returning 0;
        // see docs/known-issues/cap-20-around-wraps-original-wrong-result.md.)
        // get_type_id maps each of these to the right asmjit width.
        case T::I8:  return "i8";
        case T::I16: return "i16";
        case T::I32: return "i32";
        case T::I64: return "i64";
        case T::U8:  return "u8";
        case T::U16: return "u16";
        case T::U32: return "u32";
        case T::U64: return "u64";
        default:     return "i64";
    }
}

// Build the (return_type, param_types) string pair make_jit_func wants
// from a parsed signature.
void SignatureToJitStrings(const kcdx::hook_signature::Signature& sig,
                           std::string&              returnTypeOut,
                           std::vector<std::string>& paramTypesOut) {
    returnTypeOut = SigTypeToJitString(sig.returnType);
    paramTypesOut.clear();
    paramTypesOut.reserve(sig.args.size());
    for (const auto& a : sig.args) {
        paramTypesOut.emplace_back(SigTypeToJitString(a.type));
    }
}

// Byte-compatibility of two signatures for sharing ONE marshaling thunk
// (§1.1 of the spec). v1 rule: same arg count and each slot maps to the
// same JIT type-string + same return type-string. This is conservative
// (e.g. i32 vs i64 both -> "i64" so they're treated compatible, which is
// fine — the register move is identical) and never produces a wrong
// marshal.
bool SignaturesCompatible(const kcdx::hook_signature::Signature& a,
                          const kcdx::hook_signature::Signature& b) {
    if (a.args.size() != b.args.size()) return false;
    if (SigTypeToJitString(a.returnType) !=
        std::string(SigTypeToJitString(b.returnType))) return false;
    for (size_t i = 0; i < a.args.size(); ++i) {
        if (std::string(SigTypeToJitString(a.args[i].type)) !=
            SigTypeToJitString(b.args[i].type)) return false;
    }
    return true;
}

// ===========================================================================
// §2  ChainEntry + Chain data model
// ===========================================================================
//
// Footprint-readiness (see docs/outstanding-work/
// smart-replace-conflict-detection.md): ChainEntry is a value in a
// std::vector<ChainEntry> — N entries, any mode mix. There is NO
// distinguished "the replace" slot. The smart-coexistence upgrade adds
// a `Footprint footprint;` member here and swaps the body of CanCoexist
// (§3); nothing else changes. DO NOT collapse replace into a single
// field — that turns the upgrade into a rewrite.

struct ChainEntry {
    kcdx::hook_payload::Mode mode = kcdx::hook_payload::Mode::Before;
    int          callbackRef = -2;  // LUA_NOREF; the Lua callback closure
    std::string  pluginName;        // owning plugin (load-order attribution)
    int          priority   = 50;   // effective load-order priority
    std::string  name;              // author name (diagnostics)
};

// One hooked target. Owns the JIT trampoline (runtime_func_t) for its
// lifetime + the ordered callback chain. The runtime_func_t holds the
// MinHook detour; destroying it would uninstall the hook, which we never
// do (hooks live for the session, matching SKSE).
struct Chain {
    uintptr_t                                  targetVa = 0;
    std::unique_ptr<kcdx::rom::runtime_func_t> rf;
    // The signature the FIRST hook fixed; later hooks must be
    // SignaturesCompatible with this to share the thunk. Dispatch
    // marshals directly off this (per-slot hook_signature::Type), which
    // is why wstr/cstr round-trip correctly (the legacy type_info_t
    // can't tell them apart).
    kcdx::hook_signature::Signature            sig;
    // call_original thunk over MinHook's pOriginal, built when the first
    // around on this target lands (a lua_CFunction; see §7). 0 = none.
    uintptr_t                                  callOriginalThunk = 0;
    std::vector<ChainEntry>                    entries;       // load-order ordered
};

// target VA -> Chain. Process-lifetime; node-stable via unique_ptr so
// the dispatchers can hold a raw Chain* across the hot path without the
// map rehashing it away.
std::unordered_map<uintptr_t, std::unique_ptr<Chain>> g_chains;

// Guards g_chains structure (Add appends; dispatch reads). The Lua-VM
// single-thread contract (see .claude/rules/lua-callback-threading.md)
// means dispatch is main-thread-only, but Add can run during the
// first-tick registration pass; a coarse mutex keeps the map consistent.
// Dispatch takes it only to resolve target->Chain*, then releases before
// the lua_pcall (which can run arbitrary Lua).
std::mutex g_chainsMu;

// The live game lua_State, set by Install on first use (the chain
// dispatchers run callbacks against it). Mirrors scripting::lua_state();
// we capture our own copy so this module doesn't depend on the legacy
// scripting TU.
lua_State* g_L = nullptr;

Chain* FindChain(uintptr_t va) {
    auto it = g_chains.find(va);
    return it == g_chains.end() ? nullptr : it->second.get();
}

// ===========================================================================
// §3  CanCoexist — the isolated conflict predicate (v1 blunt body)
// ===========================================================================
//
// THE ONE function the smart-coexistence upgrade replaces. v1 body:
// two hooks on a target can coexist iff their signatures are compatible
// (share the thunk) AND neither is replace/around (those assume
// worst-case full footprint). When the smart work lands, this becomes
// footprint-overlap analysis per the spec §5; Add() and the chain
// container do not change.
bool CanCoexist(const Chain&                            chain,
                kcdx::hook_payload::Mode                incomingMode,
                const kcdx::hook_signature::Signature&  incomingSig,
                std::string&                            whyNot) {
    using Mode = kcdx::hook_payload::Mode;

    if (!SignaturesCompatible(chain.sig, incomingSig)) {
        whyNot = "target already hooked with an incompatible signature; "
                 "all hooks sharing a target must declare the same "
                 "argument + return types (v1 shares one marshaling "
                 "thunk per target)";
        return false;
    }

    // v1 worst-case: replace/around are assumed to touch everything, so
    // they cannot coexist with any other hook on the same target.
    const bool incomingExclusive =
        (incomingMode == Mode::Replace || incomingMode == Mode::Around);
    for (const auto& e : chain.entries) {
        const bool existingExclusive =
            (e.mode == Mode::Replace || e.mode == Mode::Around);
        if (incomingExclusive || existingExclusive) {
            whyNot =
                std::string("target already has a '") +
                kcdx::hook_payload::ModeToken(e.mode) +
                "' hook; a '" + kcdx::hook_payload::ModeToken(incomingMode) +
                "' hook cannot coexist with replace/around on the same "
                "target in v1 (load-order-loses; smart footprint "
                "coexistence is future work — see docs/outstanding-work/"
                "smart-replace-conflict-detection.md)";
            return false;
        }
    }
    return true;
}

// ===========================================================================
// §4  Type-keyed slot <-> Lua marshaling (the wstr-correct path)
// ===========================================================================
//
// hook_chain owns its own marshaling, keyed on the parsed
// hook_signature::Type per slot — NOT the legacy type_info_t /
// value_wrapper_t (which collapses wstr and cstr into one "string" and
// reads wstr as a narrow char*, truncating UTF-16 at the first null).
// The author gets a BARE Lua value per param (no wrapper userdata), so
// they write `szApp:find(":")`, not `szApp:get():...`.
//
// Strings need a lifetime arena: when a callback returns a changed
// cstr/wstr, the native side reads the pointer AFTER the callback (and
// the lua_pcall) returns, so the bytes must outlive the Lua string the
// author produced. We pin converted strings in a per-dispatch arena
// cleared at the end of each top-level dispatch. Dispatch is
// main-thread-only (.claude/rules/lua-callback-threading.md), so a
// single thread-local arena is safe.

// Per-dispatch string-pin arena. PinUtf8 keeps a UTF-8 (cstr) buffer;
// PinWide keeps a UTF-16 (wstr) buffer. Pointers returned stay valid
// until the OUTERMOST dispatch finishes.
//
// Re-entrancy: a hook is re-entered when an `around` callback's
// orig() (or any callback) calls into another hooked function — or the
// same one — before the first dispatch returns. That is ALLOWED (the
// author is trusted; a genuinely infinite hook->original->hook loop is
// the author's bug and surfaces as a natural stack overflow, like any
// runaway recursion). The engine's only job is to not corrupt itself
// while it happens: the pin arena is shared thread-local state, so an
// inner dispatch must NOT free the outer dispatch's pinned strings. We
// track nesting depth and clear the arena only when the outermost
// dispatch (depth 0) exits. The depth is NOT a limiter — it exists
// solely to time the arena clear correctly. Dispatch is main-thread-
// only (.claude/rules/lua-callback-threading.md), so a thread-local
// counter is sufficient.
thread_local std::vector<std::unique_ptr<std::string>>  g_pinUtf8;
thread_local std::vector<std::unique_ptr<std::wstring>> g_pinWide;
thread_local int g_dispatchDepth = 0;

void ClearPinArena() { g_pinUtf8.clear(); g_pinWide.clear(); }

const char* PinUtf8(std::string s) {
    g_pinUtf8.emplace_back(std::make_unique<std::string>(std::move(s)));
    return g_pinUtf8.back()->c_str();
}
const wchar_t* PinWide(std::wstring s) {
    g_pinWide.emplace_back(std::make_unique<std::wstring>(std::move(s)));
    return g_pinWide.back()->c_str();
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}
std::wstring Utf8ToWide(const char* s) {
    if (!s) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

// Push the value at `slot` (a uintptr_t-sized cell) onto the Lua stack
// as a bare value, per its hook_signature::Type. Stack effect: +1.
void PushSlot(lua_State* L, const uintptr_t* slot,
              kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Bool: lua_pushboolean(L, (*slot != 0) ? 1 : 0); break;
        case T::F32:  lua_pushnumber(L, (lua_Number)*(const float*)slot);  break;
        case T::F64:  lua_pushnumber(L, (lua_Number)*(const double*)slot); break;
        case T::Ptr:
            // Pointers go through a kcdx.memory.pointer userdata, never
            // lua_pushinteger (LUA_NUMBER=float loses pointer magnitude;
            // see .claude/rules/lua-precision.md).
            kcdx::lua_bind_helpers::PushPointer(
                L, kcdx::lua_memory::pointer((uintptr_t)*slot));
            break;
        case T::Cstr: {
            const char* p = *(const char* const*)slot;
            if (p) lua_pushstring(L, p); else lua_pushnil(L);
            break;
        }
        case T::Wstr: {
            const wchar_t* p = *(const wchar_t* const*)slot;
            if (p) {
                std::string u8 = WideToUtf8(p);
                lua_pushlstring(L, u8.data(), u8.size());
            } else {
                lua_pushnil(L);
            }
            break;
        }
        case T::Void: lua_pushnil(L); break;
        // All integer widths/signs: push as Lua integer. 64-bit values
        // above 2^53 lose precision through lua_Number — documented
        // limit; pointer-magnitude integers should use ptr instead.
        default:      lua_pushinteger(L, (lua_Integer)(int64_t)*slot); break;
    }
}

// Read the Lua value at `idx` and write it into `slot` per its type.
// For strings, the converted bytes are pinned in the per-dispatch arena
// so they outlive the call. Leaves slot unchanged if the Lua value is
// the wrong type (a callback that returns nil for a slot keeps the
// original — see the mutate-by-return contract).
void WriteSlot(lua_State* L, int idx, uintptr_t* slot,
               kcdx::hook_signature::Type t) {
    using T = kcdx::hook_signature::Type;
    switch (t) {
        case T::Bool:
            if (!lua_isnil(L, idx)) *slot = lua_toboolean(L, idx) ? 1 : 0;
            break;
        case T::F32:
            if (lua_isnumber(L, idx)) *(float*)slot = (float)lua_tonumber(L, idx);
            break;
        case T::F64:
            if (lua_isnumber(L, idx)) *(double*)slot = (double)lua_tonumber(L, idx);
            break;
        case T::Ptr:
            // Accept a kcdx.memory.pointer userdata or an integer VA.
            // Non-throwing: a wrong-typed return for a ptr slot leaves the
            // slot unchanged (mutate-by-return: unspecified return = keep).
            if (lua_isuserdata(L, idx)) {
                if (lua_getmetatable(L, idx)) {
                    luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
                    const bool match = lua_rawequal(L, -1, -2) != 0;
                    lua_pop(L, 2);
                    if (match) {
                        auto* p = static_cast<kcdx::lua_memory::pointer*>(
                            lua_touserdata(L, idx));
                        if (p) *slot = (uintptr_t)p->get_address();
                    }
                }
            } else if (lua_isnumber(L, idx)) {
                *slot = (uintptr_t)lua_tointeger(L, idx);
            }
            break;
        case T::Cstr:
            if (lua_isstring(L, idx)) {
                *(const char**)slot = PinUtf8(std::string(lua_tostring(L, idx)));
            }
            break;
        case T::Wstr:
            if (lua_isstring(L, idx)) {
                *(const wchar_t**)slot = PinWide(Utf8ToWide(lua_tostring(L, idx)));
            }
            break;
        case T::Void: break;
        default:
            if (lua_isnumber(L, idx)) *slot = (uintptr_t)(int64_t)lua_tointeger(L, idx);
            break;
    }
}

// Push the target's params as positional Lua call arguments. Returns the
// count pushed (== param count).
int PushParamsPositional(lua_State* L, const Chain& chain,
                         const kcdx::rom::runtime_func_t::parameters_t* params) {
    const int n = static_cast<int>(chain.sig.args.size());
    for (int i = 0; i < n; ++i) {
        const uintptr_t* slot = reinterpret_cast<const uintptr_t*>(
            params->get_arg_ptr(static_cast<uint8_t>(i)));
        PushSlot(L, slot, chain.sig.args[i].type);
    }
    return n;
}

// Write Lua return values (stack indices firstRet..) back into the param
// slots — the mutate-by-return contract for `before`. retCount==0 leaves
// args untouched; returned values replace args 0..retCount-1.
void WriteBackParams(lua_State* L, const Chain& chain,
                     kcdx::rom::runtime_func_t::parameters_t* params,
                     int firstRet, int retCount) {
    const int n = static_cast<int>(chain.sig.args.size());
    for (int i = 0; i < n && i < retCount; ++i) {
        uintptr_t* slot = reinterpret_cast<uintptr_t*>(
            params->get_arg_ptr(static_cast<uint8_t>(i)));
        WriteSlot(L, firstRet + i, slot, chain.sig.args[i].type);
    }
}

// Push the current return value onto the Lua stack (for `after`). +1.
void PushReturn(lua_State* L, const Chain& chain,
                kcdx::rom::runtime_func_t::return_value_t* rv) {
    PushSlot(L, reinterpret_cast<const uintptr_t*>(rv->get()),
             chain.sig.returnType);
}

// Write a Lua value at `idx` into the return slot.
void WriteReturn(lua_State* L, int idx, const Chain& chain,
                 kcdx::rom::runtime_func_t::return_value_t* rv) {
    WriteSlot(L, idx, reinterpret_cast<uintptr_t*>(rv->get()),
              chain.sig.returnType);
}

// ===========================================================================
// §6  DispatchPre / DispatchPost — the C callbacks the JIT thunk calls
// ===========================================================================

// Resolve the Chain for a target under the lock, then release it before
// running Lua (lua_pcall can run arbitrary code; we don't hold the mutex
// across it). Returns nullptr if no chain (shouldn't happen for an
// installed target).
Chain* ResolveChainForDispatch(uintptr_t target) {
    std::lock_guard<std::mutex> lock(g_chainsMu);
    return FindChain(target);
}

// Handle a replace or around entry (both own the original-call
// decision; both fire in the pre-phase). For replace: run the callback
// with positional params, write its return into return_value, original
// never runs. For around: run the callback with (call_original, params...)
// — the callback decides whether/when to invoke the original — and write
// its return into return_value.
void DispatchExclusive(lua_State* L, Chain& chain, const ChainEntry& e,
                       const kcdx::rom::runtime_func_t::parameters_t* params,
                       kcdx::rom::runtime_func_t::return_value_t* return_value) {
    using Mode = kcdx::hook_payload::Mode;
    const bool hasReturn =
        (chain.sig.returnType != kcdx::hook_signature::Type::Void);

    lua_rawgeti(L, LUA_REGISTRYINDEX, e.callbackRef);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    const int top0 = lua_gettop(L);  // [..., fn]

    int nargs = 0;
    if (e.mode == Mode::Around) {
        // First param is `orig` — the call_original thunk, itself a
        // lua_CFunction (a typed call over MinHook's pOriginal trampoline,
        // JIT'd at install). Pushing it directly makes orig(args...) a
        // normal Lua call: it marshals, runs the original, returns the
        // typed result. If the thunk is missing (build failed), push a
        // closure that errors clearly rather than a nil the author would
        // call and crash on.
        if (chain.callOriginalThunk) {
            lua_pushcfunction(L,
                reinterpret_cast<lua_CFunction>(chain.callOriginalThunk));
        } else {
            lua_pushcfunction(L, [](lua_State* Ls) -> int {
                return luaL_error(Ls, "call_original: unavailable (the "
                    "engine failed to build the call-through thunk for "
                    "this target; see kcdx.log)");
            });
        }
        ++nargs;
    }
    nargs += PushParamsPositional(L, chain,
        const_cast<kcdx::rom::runtime_func_t::parameters_t*>(params));

    const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        log::ErrorF("hook_chain: %s '%s' (plugin '%s') threw: %s",
                    kcdx::hook_payload::ModeToken(e.mode), e.name.c_str(),
                    e.pluginName.c_str(), msg ? msg : "<no message>");
        lua_settop(L, top0 - 1);
        return;
    }
    const int retCount = lua_gettop(L) - (top0 - 1);
    if (hasReturn && retCount > 0) {
        WriteReturn(L, top0, chain, return_value);
    }
    lua_settop(L, top0 - 1);  // balance
}

// pre callback: runs before the original. Returns false to suppress the
// original (replace/around that didn't call it). before/around/replace
// all fire here in load order.
bool DispatchPre(const kcdx::rom::runtime_func_t::parameters_t* params,
                 const uint8_t  /*param_count*/,
                 kcdx::rom::runtime_func_t::return_value_t* return_value,
                 const uintptr_t target_func_ptr) {
    // Enter a dispatch level FIRST — unconditionally, before any early
    // return — so it pairs exactly with DispatchPost's unconditional
    // decrement (the JIT always invokes post after pre). Spans
    // pre -> original -> post. A re-entrant dispatch (an around's orig()
    // calling another hooked fn) nests fully inside this one; that is
    // allowed. The breadcrumb makes a runaway hook->original->hook loop
    // traceable in the dev log before the stack overflows (depth climbs
    // each line) — it is NOT a limiter; a non-terminating loop is the
    // hook author's bug, same as any runaway recursion.
    ++g_dispatchDepth;
    if (g_dispatchDepth > 1) {
        log::DebugF("hook_chain: re-entrant dispatch depth=%d at 0x%p "
                    "(allowed; a non-terminating loop here is the hook "
                    "author's bug)",
                    g_dispatchDepth, (void*)target_func_ptr);
    }

    lua_State* L = g_L;
    if (!L) return true;
    Chain* chain = ResolveChainForDispatch(target_func_ptr);
    if (!chain || chain->entries.empty()) return true;

    bool runOriginal = true;

    for (const ChainEntry& e : chain->entries) {
        using Mode = kcdx::hook_payload::Mode;
        if (e.mode == Mode::After) continue;  // after fires in DispatchPost

        // Push the callback closure.
        lua_rawgeti(L, LUA_REGISTRYINDEX, e.callbackRef);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); continue; }

        if (e.mode == Mode::Before) {
            // before(self, szApp, ...) -> [changed args...] | nothing
            const int top0 = lua_gettop(L);  // stack: [..., fn]
            const int nargs = PushParamsPositional(L, *chain,
                const_cast<kcdx::rom::runtime_func_t::parameters_t*>(params));
            const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
            if (status != 0) {
                const char* msg = lua_tostring(L, -1);
                log::ErrorF("hook_chain: before '%s' (plugin '%s') on "
                            "0x%p threw: %s", e.name.c_str(),
                            e.pluginName.c_str(), (void*)target_func_ptr,
                            msg ? msg : "<no message>");
                lua_pop(L, 1);
                continue;
            }
            // Returns are at top0 .. lua_gettop(L). top0 was the fn slot,
            // which pcall popped; results start where fn was (top0).
            const int retCount = lua_gettop(L) - (top0 - 1);
            if (retCount > 0) {
                WriteBackParams(L, *chain,
                    const_cast<kcdx::rom::runtime_func_t::parameters_t*>(params),
                    top0, retCount);
            }
            lua_settop(L, top0 - 1);  // pop all results; balance stack
            // before NEVER suppresses the original.
        } else if (e.mode == Mode::Replace || e.mode == Mode::Around) {
            // replace/around own the original-call decision; the thunk
            // must NOT auto-run the original after we return.
            runOriginal = false;
            DispatchExclusive(L, *chain, e, params, return_value);
        }
    }
    return runOriginal;
}

// post callback: runs after the original. Only `after` entries fire
// here; each receives the current return value and may return a
// replacement.
void DispatchPost(const kcdx::rom::runtime_func_t::parameters_t* /*params*/,
                  const uint8_t /*param_count*/,
                  kcdx::rom::runtime_func_t::return_value_t* return_value,
                  const uintptr_t target_func_ptr) {
    lua_State* L = g_L;
    Chain* chain = (L) ? ResolveChainForDispatch(target_func_ptr) : nullptr;

    const bool hasReturn = chain &&
        (chain->sig.returnType != kcdx::hook_signature::Type::Void);

    if (chain && !chain->entries.empty()) {

    for (const ChainEntry& e : chain->entries) {
        if (e.mode != kcdx::hook_payload::Mode::After) continue;

        lua_rawgeti(L, LUA_REGISTRYINDEX, e.callbackRef);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); continue; }

        const int top0 = lua_gettop(L);  // [..., fn]
        int nargs = 0;
        if (hasReturn) {
            PushReturn(L, *chain, return_value);
            nargs = 1;
        }
        const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
        if (status != 0) {
            const char* msg = lua_tostring(L, -1);
            log::ErrorF("hook_chain: after '%s' (plugin '%s') on 0x%p "
                        "threw: %s", e.name.c_str(), e.pluginName.c_str(),
                        (void*)target_func_ptr, msg ? msg : "<no message>");
            lua_pop(L, 1);
            continue;
        }
        const int retCount = lua_gettop(L) - (top0 - 1);
        if (hasReturn && retCount > 0) {
            // after returns the (possibly changed) return value.
            WriteReturn(L, top0, *chain, return_value);
        }
        lua_settop(L, top0 - 1);  // balance
    }
    }  // if (chain && !entries.empty())

    // Leave this dispatch level. DispatchPost is the matching exit for
    // the DispatchPre increment (the JIT always invokes post, even when
    // pre returned false to skip the original). The decrement runs
    // UNCONDITIONALLY — outside the chain guard above — so depth stays
    // balanced even on the no-chain / no-after path. Only the OUTERMOST
    // dispatch (depth back to 0) frees the pinned strings, so a nested
    // re-entrant dispatch can't pull the arena out from under an outer
    // dispatch whose original is still reading a pinned arg.
    if (g_dispatchDepth > 0) --g_dispatchDepth;
    if (g_dispatchDepth == 0) {
        ClearPinArena();
    }
}

// ===========================================================================
// §7  Locator resolution + first-touch install + chain append
// ===========================================================================

// Resolve a HookPayload's function-entry locator to an absolute VA via
// the patch-engine locator pipeline (same path [[hook]]/kcdx.bytes use).
// function_name is NOT handled here — that's sub-4b (export-table +
// mangled-name resolution); reject it loudly. Returns 0 + reason on
// failure.
uintptr_t ResolveLocator(const kcdx::hook_payload::HookPayload& p,
                         std::string& reason) {
    // Direct address — the VA is already in hand (pointer userdata or
    // integer the author got from kcdx.lua.cfunction_address /
    // scan_pattern / etc.). No resolution needed.
    if (p.address != 0) {
        return p.address;
    }
    // Address Library by human-readable NAME (address_id = "lua_pcall").
    // Resolves against the compiled-in library — the readable surface that
    // spares authors the opaque numeric id. Loud fail on miss (typo or
    // unknown name): a dead hook is worse UX than a clear error.
    if (!p.addressName.empty()) {
        uintptr_t va = kcdx::address_library::ResolveByName(p.addressName.c_str());
        if (!va) {
            reason = "address_id name '" + p.addressName +
                     "' did not resolve in the Address Library (unknown "
                     "name, or its entry doesn't match this game version / "
                     "isn't verified). Check the name against kcdx.addr.*.";
            return 0;
        }
        return va + (uintptr_t)(int64_t)p.offset;
    }
    if (!p.functionName.empty()) {
        // function_name (raw Module.dll!Export, incl. mangled C++ symbols)
        // is intentionally NOT supported — mangled names are unusable UX.
        // Game/DLL targets resolve by readable Address Library name
        // (address_id = "..."), numeric id, target_symbol, or pattern.
        reason = "function_name (\"Module.dll!Export\") is not a kcdx locator "
                 "— raw/mangled export names are poor UX. Use address_id "
                 "with a readable Address Library name (e.g. "
                 "address_id = \"lua_pcall\") or a numeric id, or "
                 "target_symbol / pattern.";
        return 0;
    }
    // Build a PatchEntry carrying just the locator fields Resolve reads.
    kcdx::patch::PatchEntry pe;
    pe.sourceFile   = "<lua:kcdx.hook>";
    pe.name         = p.name;
    pe.module       = p.module;
    pe.pattern      = p.pattern;
    pe.context      = p.context;
    pe.anchor       = p.anchor;
    pe.maxAnchorDistance = p.maxAnchorDistance;
    pe.offset       = p.offset;
    pe.targetSymbol = p.targetSymbol;
    pe.addressId    = p.addressId;
    // Resolve checks original.size()==replacement.size(); give it equal
    // empties so the locator path runs.
    pe.original.clear();
    pe.replacement.clear();

    kcdx::patch::ResolvedPatch r = kcdx::patch::Resolve(pe);
    if (!r.ok) {
        reason = "locator did not resolve: " + r.reason;
        return 0;
    }
    return r.patchAddr;
}

// Build the JIT type-strings (return + params) from a parsed signature
// for make_jit_func / the call_original thunk. ABI-level: wstr/cstr are
// pointer-width (passed in a register as a pointer); the string<->Lua
// conversion is a marshaling concern handled in §4, not here.
void SignatureToAbiStrings(const kcdx::hook_signature::Signature& sig,
                           std::string&              returnTypeOut,
                           std::vector<std::string>& paramTypesOut) {
    returnTypeOut = SigTypeToJitString(sig.returnType);
    paramTypesOut.clear();
    for (const auto& a : sig.args) {
        // wstr/cstr -> "ptr" at the ABI (they're pointers in registers).
        kcdx::hook_signature::Type t = a.type;
        if (t == kcdx::hook_signature::Type::Wstr ||
            t == kcdx::hook_signature::Type::Cstr) {
            paramTypesOut.emplace_back("ptr");
        } else {
            paramTypesOut.emplace_back(SigTypeToJitString(t));
        }
    }
}

// Same, but for the return type's ABI string (wstr/cstr -> ptr).
std::string ReturnAbiString(const kcdx::hook_signature::Signature& sig) {
    if (sig.returnType == kcdx::hook_signature::Type::Wstr ||
        sig.returnType == kcdx::hook_signature::Type::Cstr) {
        return "ptr";
    }
    return SigTypeToJitString(sig.returnType);
}

// Insert an entry into the chain in load order (priority asc, name asc).
void InsertOrdered(Chain& chain, ChainEntry&& e) {
    auto pos = chain.entries.begin();
    for (; pos != chain.entries.end(); ++pos) {
        if (e.priority < pos->priority) break;
        if (e.priority == pos->priority && e.name < pos->name) break;
    }
    chain.entries.insert(pos, std::move(e));
}

}  // namespace

void SetLuaState(lua_State* L) { g_L = L; }

AddResult Add(lua_State*                             L,
              const kcdx::hook_payload::HookPayload& payload,
              int                                    callbackRef,
              const std::string&                     pluginName,
              int                                    priority,
              const std::string&                     name) {
    AddResult res;
    if (L) g_L = L;  // capture the dispatch state on first use

    if (!payload.hasSignature) {
        res.reason = "internal: hook has no parsed signature";
        return res;
    }

    // Resolve the target VA.
    std::string reason;
    uintptr_t targetVa = ResolveLocator(payload, reason);
    if (!targetVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    Chain* chain = FindChain(targetVa);

    if (chain) {
        // Existing target — check coexistence, then append.
        std::string whyNot;
        if (!CanCoexist(*chain, payload.mode, payload.signature, whyNot)) {
            res.reason = std::move(whyNot);
            return res;
        }
        ChainEntry e;
        e.mode = payload.mode; e.callbackRef = callbackRef;
        e.pluginName = pluginName; e.priority = priority; e.name = name;
        const bool needsCallOriginal =
            (payload.mode == kcdx::hook_payload::Mode::Around);
        InsertOrdered(*chain, std::move(e));
        // Build the call_original thunk if this is the first around and
        // we haven't built one yet.
        if (needsCallOriginal && !chain->callOriginalThunk) {
            void** origSlot = chain->rf->get_jit_original_slot();
            if (origSlot && *origSlot) {
                std::string rt; std::vector<std::string> pts;
                SignatureToAbiStrings(chain->sig, rt, pts);
                lua_CFunction th = kcdx::dynamic_call_jit::BuildLuaCallThunk(
                    (uintptr_t)*origSlot, rt, pts);
                chain->callOriginalThunk = (uintptr_t)th;
            }
        }
        res.ok = true;
        log::InfoF("hook_chain: appended %s '%s' (plugin '%s') to target "
                   "0x%p (chain now %zu)",
                   kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
                   pluginName.c_str(), (void*)targetVa,
                   chain->entries.size());
        return res;
    }

    // First touch — build the detour for this target.
    auto newChain = std::make_unique<Chain>();
    newChain->targetVa = targetVa;
    newChain->sig      = payload.signature;
    newChain->rf       = std::make_unique<kcdx::rom::runtime_func_t>();

    std::string rt; std::vector<std::string> pts;
    SignatureToAbiStrings(payload.signature, rt, pts);

    uintptr_t jit = newChain->rf->make_jit_func(
        rt, pts, asmjit::Arch::kX64,
        &DispatchPre, &DispatchPost, targetVa);
    if (!jit) {
        res.reason = "make_jit_func failed (signature/codegen — see kcdx.log)";
        return res;
    }

    auto install = kcdx::hook_engine::InstallRuntime(name, targetVa, (void*)jit);
    if (!install.ok) {
        res.reason = "InstallRuntime failed: " + install.reason;
        return res;
    }
    // Wire MinHook's pOriginal into the JIT trampoline's call-original
    // slot (same as the dynamic_hook path; without this the thunk's
    // call-through reads null).
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
    }

    // Build the call_original thunk now if this first hook is an around.
    if (payload.mode == kcdx::hook_payload::Mode::Around) {
        if (install.pOriginal) {
            newChain->callOriginalThunk = (uintptr_t)
                kcdx::dynamic_call_jit::BuildLuaCallThunk(
                    (uintptr_t)install.pOriginal, rt, pts);
        }
    }

    ChainEntry e;
    e.mode = payload.mode; e.callbackRef = callbackRef;
    e.pluginName = pluginName; e.priority = priority; e.name = name;
    newChain->entries.push_back(std::move(e));

    g_chains.emplace(targetVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed %s '%s' (plugin '%s') at target 0x%p "
               "(JIT detour 0x%p)",
               kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
               pluginName.c_str(), (void*)targetVa, (void*)jit);
    return res;
}

}  // namespace kcdx::hook_chain
