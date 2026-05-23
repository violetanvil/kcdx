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
#include <cstdio>    // snprintf (callsite diagnostics)
#include <cstdlib>   // strtoull (callsite rva parse)
#include <cstring>   // memcpy (callsite E8 displacement read/write)
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
#include "hde/hde64.h"         // hde64_disasm (auto-decode mid resume offset)
#include "hook_engine.h"       // InstallRuntime
#include "log.h"
#include "lua_bind_helpers.h"  // PushPointer
#include "lua_memory.h"        // pointer, kPointerMetatable
#include "patch_engine.h"      // Resolve, ResolvedPatch (locator pipeline)
#include "pe_helpers.h"        // OpenModule (callsite rva -> module base)
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
//
// A mid Chain (isMid) is a DIFFERENT install: a mid-function detour at
// the captured-instruction VA (targetVa already includes `offset`), built
// with make_jit_midfunc + the MidDispatch C callback, carrying its own
// capture layout instead of a function signature. v1 keeps one mid hook
// per VA (the JIT bakes one capture layout); a second mid hook on the
// same VA loses by load order (CanCoexist's mid branch). The struct is
// the same shape as a signature Chain so the map + lifetime rules are
// shared; the mid-only fields sit alongside.
struct Chain {
    uintptr_t                                  targetVa = 0;
    std::unique_ptr<kcdx::rom::runtime_func_t> rf;
    // The signature the FIRST hook fixed; later hooks must be
    // SignaturesCompatible with this to share the thunk. Dispatch
    // marshals directly off this (per-slot hook_signature::Type), which
    // is why wstr/cstr round-trip correctly (the legacy type_info_t
    // can't tell them apart). Unused for mid chains.
    kcdx::hook_signature::Signature            sig;
    // call_original thunk over MinHook's pOriginal, built when the first
    // around on this target lands (a lua_CFunction; see §7). 0 = none.
    uintptr_t                                  callOriginalThunk = 0;
    std::vector<ChainEntry>                    entries;       // load-order ordered

    // --- callsite-only -------------------------------------------------
    // A callsite Chain is keyed in g_chains by the CALL-INSTRUCTION VA
    // (not the callee VA): a callsite redirect affects ONE caller, so the
    // mediated resource is the call site's 4 rewritten displacement bytes,
    // not the callee. Two plugins redirecting the SAME call site collide
    // (load-order-loses via CanCoexist's callsite branch); two plugins
    // redirecting DIFFERENT call sites to the same callee do NOT — that is
    // the whole point of callsite vs function-entry. The dispatch spine
    // (DispatchPre/Post + the entries chain + sig + callOriginalThunk) is
    // shared with a function-entry Chain; only the install differs (an E8
    // rewrite instead of a MinHook detour on the callee).
    bool                     isCallsite = false;
    uintptr_t                calleeVa = 0;  // original callee (orig() target)

    // --- mid-only ------------------------------------------------------
    bool                     isMid = false;
    int                      midCallbackRef = -2;  // LUA_NOREF
    std::string              midPluginName;
    std::string              midName;
    // Parallel capture metadata (parsed by the binder). captureNames[i]
    // == "" means positional (handle table keyed 1..N); otherwise the
    // handle table is keyed by name. captureTypes drives per-slot
    // marshaling in MidDispatch (i8..u64 / ptr / f32 / f64).
    std::vector<std::string> capExprs;
    std::vector<std::string> capTypes;
    std::vector<std::string> capNames;
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
                bool                                    incomingIsCallsite,
                std::string&                            whyNot) {
    using Mode = kcdx::hook_payload::Mode;

    // A callsite chain (keyed by the call-instruction VA) and a
    // function-entry chain (keyed by the callee VA) are distinct kinds of
    // interception; they must not share a chain even in the (vanishingly
    // unlikely) event their VAs coincide. Mediate by load order — the
    // earlier-installed kind owns the site; the later loses loudly. This
    // is the isolated callsite extension of the predicate (no cross-engine
    // knowledge; the decision stays inside hook_chain).
    if (chain.isCallsite != incomingIsCallsite) {
        whyNot = chain.isCallsite
            ? "this address is already redirected by a mode='callsite' "
              "hook; a function-entry hook cannot share it (load-order-"
              "loses; the callsite hook installed first wins)"
            : "this address already has a function-entry hook; a "
              "mode='callsite' hook cannot share it (load-order-loses; "
              "the function-entry hook installed first wins)";
        return false;
    }

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
// §5b  mid-hook capture handles + skip flag
// ===========================================================================
//
// A mid hook captures register/memory values at one instruction inside a
// function. The author's callback receives a table of capture HANDLES —
// one per capture — each a small userdata with :get() / :set(). :get()
// reads the captured value (as a Lua number/pointer); :set(v) writes it
// back into the JIT capture slot, which make_jit_midfunc's unconditional
// "apply change" loop then stores into the real register/memory after the
// callback returns (runtime_func_t.cpp:608+).
//
// Slots live in the JIT trampoline's STACK payload for the duration of
// the dispatch only. A handle is valid ONLY inside the callback; stashing
// one and using it later reads freed stack (author bug — same hazard as
// retaining any by-reference callback argument).
//
// The capture payload uses a 16-BYTE slot stride (the JIT writes
// [rsp + 16*i]) — NOT parameters_t::get_arg_ptr's 8-byte stride. We index
// the payload base directly. (cap-04 has one capture so slot0 coincides
// in both strides; the mismatch only bites 2+ captures — see
// project_kcdx_phase2b_hook_restructure memory.)

// Skip-original flag: a single byte make_jit_midfunc reads (Auto mode)
// after MidDispatch returns. Non-zero => the captured instruction is
// skipped (resume past it). hook_chain owns its OWN flag (not
// scripting::g_mid_skip_original) so it stays self-contained for the
// eventual legacy-scripting discard. Main-thread-only dispatch
// (.claude/rules/lua-callback-threading.md) means a plain byte suffices;
// MidDispatch clears it at entry and sets it from the callback's return.
uint8_t g_midSkipOriginal = 0;

// One capture handle: a pointer into the live JIT slot payload + the
// capture's type string. Bare value (not a kcdx.memory.pointer) — the
// author reads/writes plain numbers off a disassembler.
struct CaptureHandle {
    void*       slot = nullptr;  // (char*)&params->m_arguments + 16*i
    const char* type = "i64";    // capExprs[i]'s parsed type
};

const char* const kCaptureHandleMetatable = "kcdx.hook.capture";

// Push the slot value as a bare Lua value, per the capture type string.
// f32/f64 -> number; ptr -> kcdx.memory.pointer userdata (exact, per
// lua-precision.md); integer widths -> Lua integer (read at the slot's
// width so the upper bits of a 16-byte slot don't leak in).
void PushCaptureValue(lua_State* L, const CaptureHandle* h) {
    const std::string t = h->type;
    if (t == "f32")        lua_pushnumber(L, (lua_Number)*(const float*)h->slot);
    else if (t == "f64" || t == "double")
                           lua_pushnumber(L, (lua_Number)*(const double*)h->slot);
    else if (t == "float") lua_pushnumber(L, (lua_Number)*(const float*)h->slot);
    else if (t == "bool")  lua_pushboolean(L, (*(const uint64_t*)h->slot != 0) ? 1 : 0);
    else if (t == "ptr")
        kcdx::lua_bind_helpers::PushPointer(
            L, kcdx::lua_memory::pointer(*(const uintptr_t*)h->slot));
    else if (t == "i8")    lua_pushinteger(L, (lua_Integer)*(const int8_t*)h->slot);
    else if (t == "u8")    lua_pushinteger(L, (lua_Integer)*(const uint8_t*)h->slot);
    else if (t == "i16")   lua_pushinteger(L, (lua_Integer)*(const int16_t*)h->slot);
    else if (t == "u16")   lua_pushinteger(L, (lua_Integer)*(const uint16_t*)h->slot);
    else if (t == "i32")   lua_pushinteger(L, (lua_Integer)*(const int32_t*)h->slot);
    else if (t == "u32")   lua_pushinteger(L, (lua_Integer)*(const uint32_t*)h->slot);
    else if (t == "u64")   lua_pushinteger(L, (lua_Integer)*(const uint64_t*)h->slot);
    else                   lua_pushinteger(L, (lua_Integer)*(const int64_t*)h->slot);  // i64 default
}

// Write a Lua value at `idx` into the slot, per the capture type. Widens
// to the full 16-byte slot for integer types (zero/sign-extend the rest)
// so the JIT's 8-byte reload reads a clean value. Wrong-typed value is a
// no-op (mutate-by-call: not calling :set() leaves the captured value).
void WriteCaptureValue(lua_State* L, int idx, const CaptureHandle* h) {
    const std::string t = h->type;
    if (t == "f32" || t == "float") {
        if (lua_isnumber(L, idx)) *(float*)h->slot = (float)lua_tonumber(L, idx);
    } else if (t == "f64" || t == "double") {
        if (lua_isnumber(L, idx)) *(double*)h->slot = (double)lua_tonumber(L, idx);
    } else if (t == "bool") {
        if (!lua_isnil(L, idx)) *(uint64_t*)h->slot = lua_toboolean(L, idx) ? 1 : 0;
    } else if (t == "ptr") {
        // Accept a kcdx.memory.pointer userdata or an integer VA.
        if (lua_isuserdata(L, idx)) {
            if (lua_getmetatable(L, idx)) {
                luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
                const bool match = lua_rawequal(L, -1, -2) != 0;
                lua_pop(L, 2);
                if (match) {
                    auto* p = static_cast<kcdx::lua_memory::pointer*>(
                        lua_touserdata(L, idx));
                    if (p) *(uintptr_t*)h->slot = (uintptr_t)p->get_address();
                }
            }
        } else if (lua_isnumber(L, idx)) {
            *(uintptr_t*)h->slot = (uintptr_t)lua_tointeger(L, idx);
        }
    } else {
        // Integer widths: store full 64 bits so the slot is clean for the
        // JIT reload. The author's narrower intent (i8/i32) is honored on
        // the read side; on write we sign-extend through int64.
        if (lua_isnumber(L, idx))
            *(uint64_t*)h->slot = (uint64_t)(int64_t)lua_tointeger(L, idx);
    }
}

int CaptureHandle_get(lua_State* L) {
    auto* h = static_cast<CaptureHandle*>(
        luaL_checkudata(L, 1, kCaptureHandleMetatable));
    PushCaptureValue(L, h);
    return 1;
}

int CaptureHandle_set(lua_State* L) {
    auto* h = static_cast<CaptureHandle*>(
        luaL_checkudata(L, 1, kCaptureHandleMetatable));
    WriteCaptureValue(L, 2, h);
    return 0;
}

// Lazily create the capture-handle metatable on the live state. Raw Lua C
// API only (no static-const sentinel — .claude/rules/lua-bridge.md AP5).
void EnsureCaptureHandleMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kCaptureHandleMetatable)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");  // mt.__index = mt
        lua_pushcfunction(L, CaptureHandle_get);
        lua_setfield(L, -2, "get");
        lua_pushcfunction(L, CaptureHandle_set);
        lua_setfield(L, -2, "set");
    }
    lua_pop(L, 1);
}

// Push a fresh capture handle userdata for slot i of the dispatch payload.
void PushCaptureHandle(lua_State* L, void* slot, const char* type) {
    auto* h = static_cast<CaptureHandle*>(
        lua_newuserdata(L, sizeof(CaptureHandle)));
    h->slot = slot;
    h->type = type;
    luaL_getmetatable(L, kCaptureHandleMetatable);
    lua_setmetatable(L, -2);
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
// §6b  MidDispatch — the C callback make_jit_midfunc invokes
// ===========================================================================
//
// Runs once per call to the captured instruction. Builds a table of
// capture handles (keyed by name for the name-map form, or 1..N for the
// positional list form), runs the author's `mid` callback with it, and
// decides run-vs-skip from the RETURN value:
//   return "skip" or true  -> set g_midSkipOriginal (instruction skipped)
//   return nothing/false    -> leave it clear (instruction runs)
// :set() calls on handles mutate the JIT slots in place; the JIT's
// "apply change" loop stores them back into the real reg/mem afterwards.
//
// This is the FRESH dispatcher (not the legacy scripting::dynamic_hook_mid
// which carried the cap-04-c bug: it dup'd the args table with a fragile
// lua_insert/lua_pushvalue juggle to read `args._skip` post-pcall). Here
// the decision rides the return value — no global args._skip, no juggle.
//
// Returns 0: the JIT no longer reads rax (resume is decided by the skip
// flag in Auto mode).
uintptr_t MidDispatch(const kcdx::rom::runtime_func_t::parameters_t* params,
                      const size_t  param_count,
                      const uintptr_t target_func_ptr) {
    // Clear the skip flag at entry — start from a known state so a stale
    // "set" from a previous mid dispatch can't carry over.
    g_midSkipOriginal = 0;

    lua_State* L = g_L;
    if (!L) return 0;
    Chain* chain = ResolveChainForDispatch(target_func_ptr);
    if (!chain || !chain->isMid || chain->midCallbackRef == -2) return 0;

    // Capture payload base. Slots are at 16-byte stride.
    char* payload = reinterpret_cast<char*>(
        const_cast<uintptr_t*>(&params->m_arguments));
    const size_t n = (param_count < chain->capTypes.size())
                         ? param_count : chain->capTypes.size();

    lua_rawgeti(L, LUA_REGISTRYINDEX, chain->midCallbackRef);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    const int top0 = lua_gettop(L);  // [..., fn]

    // Build the capture handle table.
    lua_createtable(L, (int)n, (int)n);
    const int tbl = lua_gettop(L);
    const bool nameKeyed = !chain->capNames.empty() &&
                           !chain->capNames[0].empty();
    for (size_t i = 0; i < n; ++i) {
        void* slot = payload + 16 * i;
        if (nameKeyed && i < chain->capNames.size() &&
            !chain->capNames[i].empty()) {
            PushCaptureHandle(L, slot, chain->capTypes[i].c_str());
            lua_setfield(L, tbl, chain->capNames[i].c_str());
        } else {
            PushCaptureHandle(L, slot, chain->capTypes[i].c_str());
            lua_rawseti(L, tbl, (int)i + 1);
        }
    }

    const int status = lua_pcall(L, 1, LUA_MULTRET, 0);
    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        log::ErrorF("hook_chain: mid '%s' (plugin '%s') on 0x%p threw: %s",
                    chain->midName.c_str(), chain->midPluginName.c_str(),
                    (void*)target_func_ptr, msg ? msg : "<no message>");
        lua_settop(L, top0 - 1);
        return 0;
    }

    // Run-vs-skip from the return: "skip" (string) or true -> skip.
    const int retCount = lua_gettop(L) - (top0 - 1);
    if (retCount > 0) {
        if (lua_type(L, top0) == LUA_TSTRING) {
            const char* s = lua_tostring(L, top0);
            if (s && std::string(s) == "skip") g_midSkipOriginal = 1;
        } else if (lua_isboolean(L, top0) && lua_toboolean(L, top0)) {
            g_midSkipOriginal = 1;
        }
    }
    lua_settop(L, top0 - 1);  // balance

    LOG_DEBUG_KV("MID_HOOK", "hook_chain.mid_dispatch",
        log::KV("target",        (void*)target_func_ptr),
        log::KV("captures",      (int64_t)n),
        log::KV("skip_original", (int64_t)g_midSkipOriginal));
    return 0;
}

// ===========================================================================
// §7  Locator resolution + first-touch install + chain append
// ===========================================================================

// Resolve a HookPayload's function-entry locator to an absolute VA via
// the patch-engine locator pipeline (same path [[hook]]/kcdx.bytes use).
// Returns 0 + reason on failure.
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
        // owningPlugin threaded for the self > engine > other precedence
        // wired in the NEXT step; ResolveByName ignores it for now (engine-
        // seed-only), so this resolves exactly as before.
        uintptr_t va = kcdx::address_library::ResolveByName(
            p.addressName.c_str(), p.owningPlugin.c_str());
        if (!va) {
            reason = "address_id name '" + p.addressName +
                     "' did not resolve in the Address Library (unknown "
                     "name, or its entry doesn't match this game version / "
                     "isn't verified). Check the name against kcdx.addr.*.";
            return 0;
        }
        return va + (uintptr_t)(int64_t)p.offset;
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

// ---------------------------------------------------------------------------
// Callsite locator resolution + E8-displacement rewrite (mode="callsite")
// ---------------------------------------------------------------------------

// Resolve a CallsiteLocator to the absolute VA of the CALL instruction
// whose rel32 displacement we will rewrite. Exactly one of pattern /
// addressId / rva is set (the binder's ValidateLocator guarantees this).
//   pattern    : run it through the patch-engine locator pipeline (same
//                path function-entry hooks use), then apply the locator's
//                own `offset` (the offset to the call opcode in the match).
//   addressId  : Address Library numeric id -> VA, + offset.
//   rva        : "Module.dll @ rva 0xNNNN" -> module_base + rva (the
//                escape-hatch form; no library entry needed).
// Returns 0 + reason on failure.
uintptr_t ResolveCallsite(const kcdx::hook_payload::HookPayload& p,
                          std::string& reason) {
    const kcdx::hook_payload::CallsiteLocator& cs = *p.callsite;

    // rva form: "Module.dll @ rva 0x12345a"  (case-insensitive "rva").
    if (!cs.rva.empty()) {
        // Parse "<module> @ rva <hex>". Be forgiving about whitespace.
        std::string s = cs.rva;
        const std::string::size_type at = s.find('@');
        if (at == std::string::npos) {
            reason = "target_callsite.rva must be of the form "
                     "\"WHGame.dll @ rva 0x12345a\" (module, '@', then "
                     "'rva <hex offset>'); got: " + cs.rva;
            return 0;
        }
        std::string moduleName = s.substr(0, at);
        // trim trailing spaces from module name
        while (!moduleName.empty() &&
               (moduleName.back() == ' ' || moduleName.back() == '\t'))
            moduleName.pop_back();
        std::string rest = s.substr(at + 1);  // " rva 0x12345a"
        // find the hex token after "rva"
        const std::string::size_type rvaKw = rest.find("rva");
        if (rvaKw == std::string::npos) {
            reason = "target_callsite.rva is missing the 'rva' keyword "
                     "(expected \"<module> @ rva 0x...\"); got: " + cs.rva;
            return 0;
        }
        std::string hexPart = rest.substr(rvaKw + 3);  // after "rva"
        // strtoull handles optional leading 0x and skips leading spaces.
        char* end = nullptr;
        unsigned long long rvaVal = std::strtoull(hexPart.c_str(), &end, 0);
        if (end == hexPart.c_str() || rvaVal == 0ull) {
            reason = "target_callsite.rva offset did not parse as a "
                     "non-zero number (expected a hex RVA like 0x12345a); "
                     "got: " + cs.rva;
            return 0;
        }
        if (moduleName.empty()) moduleName = p.module;  // default WHGame.dll
        std::wstring wmod(moduleName.begin(), moduleName.end());
        kcdx::pe::ModuleView mod;
        if (!kcdx::pe::OpenModule(wmod.c_str(), mod)) {
            reason = "target_callsite.rva module '" + moduleName +
                     "' is not loaded";
            return 0;
        }
        return reinterpret_cast<uintptr_t>(mod.baseBytes) +
               static_cast<uintptr_t>(rvaVal) +
               static_cast<uintptr_t>(static_cast<int64_t>(cs.offset));
    }

    // address_id form: numeric Address Library id.
    if (cs.addressId != 0) {
        uintptr_t va = kcdx::address_library::Resolve(cs.addressId);
        if (!va) {
            reason = "target_callsite.address_id " +
                     std::to_string((unsigned long long)cs.addressId) +
                     " did not resolve in the Address Library (unknown id, "
                     "or its entry doesn't match this game version / isn't "
                     "verified)";
            return 0;
        }
        return va + static_cast<uintptr_t>(static_cast<int64_t>(cs.offset));
    }

    // pattern form: run through the patch-engine locator pipeline.
    kcdx::patch::PatchEntry pe;
    pe.sourceFile = "<lua:kcdx.hook callsite>";
    pe.name       = p.name;
    pe.module     = p.module;
    pe.pattern    = cs.pattern;
    pe.context    = p.context;       // optional disambiguation (shared field)
    pe.anchor     = p.anchor;
    pe.maxAnchorDistance = p.maxAnchorDistance;
    pe.offset     = cs.offset;       // offset to the call opcode in the match
    pe.original.clear();
    pe.replacement.clear();
    kcdx::patch::ResolvedPatch r = kcdx::patch::Resolve(pe);
    if (!r.ok) {
        reason = "target_callsite.pattern did not resolve: " + r.reason;
        return 0;
    }
    return r.patchAddr;
}

// Rewrite the 4 rel32 displacement bytes of the E8 call at
// callsiteVa+1..+4 so the call now targets `newTarget`. The opcode byte
// at callsiteVa is assumed already verified == 0xE8 by the caller.
// VirtualProtect dance + FlushInstructionCache (same shape as
// patch_engine::WriteBytesAtAddr, which is TU-local there). Returns false
// + reason on failure.
bool RewriteCallDisplacement(uintptr_t callsiteVa, uintptr_t newTarget,
                             std::string& reason) {
    // rel32 = newTarget - (callsiteVa + 5). Must fit in a signed 32-bit.
    const int64_t rel =
        static_cast<int64_t>(newTarget) -
        static_cast<int64_t>(callsiteVa + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "trampoline 0x%p is not rel32-reachable from the call site "
            "0x%p (distance %lld bytes > 2GB)",
            (void*)newTarget, (void*)callsiteVa, (long long)rel);
        reason = buf;
        return false;
    }
    const int32_t disp32 = static_cast<int32_t>(rel);
    uintptr_t writeAt = callsiteVa + 1;
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(writeAt), 4,
                        PAGE_EXECUTE_READWRITE, &oldProt)) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "VirtualProtect failed at 0x%p (err=%lu)",
            (void*)writeAt, (unsigned long)GetLastError());
        reason = buf;
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(writeAt), &disp32, 4);
    DWORD restoreOld = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(writeAt), 4, oldProt, &restoreOld);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(writeAt), 4);
    return true;
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

void SetLuaState(lua_State* L) {
    g_L = L;
    // Create the mid-capture-handle metatable once on the live state, so
    // MidDispatch can hand the callback handles with :get()/:set(). Raw
    // Lua C API only (no static-const sentinel — lua-bridge.md AP5).
    if (L) EnsureCaptureHandleMetatable(L);
}

// Install a mode=mid hook: a mid-function detour at the captured-
// instruction VA (payload's locator already resolved to it, offset
// included), built with make_jit_midfunc + MidDispatch. v1 keeps one mid
// hook per VA — the JIT bakes one capture layout, so a second mid hook on
// the same VA loses by load order (it cannot share the layout). This is
// the safe-but-blunt v1, consistent with the around/replace exclusivity;
// footprint-based mid coexistence is the same future work as the
// signature path (smart-replace-conflict-detection.md). The runtime_func_t
// holds the detour for the session.
AddResult AddMid(const kcdx::hook_payload::HookPayload& payload,
                 int callbackRef, const std::string& pluginName,
                 int priority, const std::string& name) {
    AddResult res;
    (void)priority;  // v1: one mid hook per VA, so ordering is moot

    std::string reason;
    uintptr_t targetVa = ResolveLocator(payload, reason);
    if (!targetVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    if (FindChain(targetVa)) {
        // A hook (mid or signature) already owns this VA. v1 mid can't
        // share — load-order-loses: the later one fails loud.
        res.reason =
            "target already has a hook; a 'mid' hook needs sole ownership "
            "of its capture site in v1 (the JIT bakes one capture layout). "
            "The earlier hook wins by load order. (Footprint-based mid "
            "coexistence is future work — see docs/outstanding-work/"
            "smart-replace-conflict-detection.md.)";
        return res;
    }

    // Auto-decode the resume offset (how many bytes to skip past so the
    // resume lands on an instruction boundary beyond MinHook's patched
    // region). hde64-disassemble forward from the capture site until the
    // accumulated length covers MinHook's minimum 5-byte rel32 jmp. Same
    // algorithm as hook_engine::ApplyOneMidHook — better UX than making
    // the author count instruction bytes.
    constexpr int kMinHookPatchBytes = 5;
    int stackRestoreOffset = 0;
    {
        uintptr_t scan = targetVa;
        int accumulated = 0;
        while (accumulated < kMinHookPatchBytes) {
            hde64s hs{};
            unsigned int len =
                hde64_disasm(reinterpret_cast<const void*>(scan), &hs);
            if (len == 0 || (hs.flags & F_ERROR) != 0) {
                res.reason =
                    "could not disassemble the capture site to compute the "
                    "resume point (hde64 failed at the mid offset); the "
                    "`offset` may not land on an instruction boundary";
                return res;
            }
            scan += len;
            accumulated += static_cast<int>(len);
        }
        stackRestoreOffset = accumulated;
    }
    const uintptr_t resumeAddr = targetVa + (uintptr_t)stackRestoreOffset;

    auto newChain = std::make_unique<Chain>();
    newChain->targetVa       = targetVa;
    newChain->isMid          = true;
    newChain->midCallbackRef = callbackRef;
    newChain->midPluginName  = pluginName;
    newChain->midName        = name;
    newChain->capExprs       = payload.captureExprs;
    newChain->capTypes       = payload.captureTypes;
    newChain->capNames       = payload.captureNames;
    newChain->rf             = std::make_unique<kcdx::rom::runtime_func_t>();

    // call_original_mode = 2 (Auto): the JIT pushes MinHook's trampoline
    // by default and, after MidDispatch returns, reads our skip-flag byte;
    // if set, it resumes past the captured instruction instead. This is
    // the return-value model (the callback returns "skip"/true to skip).
    uintptr_t jit = newChain->rf->make_jit_midfunc(
        newChain->capTypes,
        newChain->capExprs,
        stackRestoreOffset,
        /*call_original_mode=*/2,
        /*skip_flag_addr=*/reinterpret_cast<uintptr_t>(&g_midSkipOriginal),
        resumeAddr,
        asmjit::Arch::kX64,
        &MidDispatch,
        targetVa);
    if (!jit) {
        res.reason = "make_jit_midfunc failed (capture/codegen — check the "
                     "capture exprs + types; see kcdx.log)";
        return res;
    }

    auto install = kcdx::hook_engine::InstallRuntime(name, targetVa, (void*)jit);
    if (!install.ok) {
        res.reason = "InstallRuntime failed: " + install.reason;
        return res;
    }
    // Wire MinHook's pOriginal into the JIT trampoline's call-original
    // slot — Auto mode's default path rets into it (runs the captured
    // instruction). Without this the trampoline reads null and rets to 0.
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
    }

    g_chains.emplace(targetVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed mid '%s' (plugin '%s') at 0x%p "
               "(%zu captures, resume +%d, JIT detour 0x%p)",
               name.c_str(), pluginName.c_str(), (void*)targetVa,
               payload.captureExprs.size(), stackRestoreOffset, (void*)jit);
    return res;
}

// Install a mode="callsite" hook: redirect ONE E8 near-call so only that
// caller reaches the chain trampoline; every other caller of the same
// callee is untouched. Reuses the function-entry dispatch spine
// (DispatchPre/DispatchPost + the entries chain + the call_original
// thunk over the ORIGINAL CALLEE) — the only difference from Add()'s
// first-touch path is the install: instead of a MinHook detour on the
// callee, we point the call site's E8 rel32 at the chain trampoline and
// wire the trampoline's call-original slot to the original callee VA.
//
// The Chain is keyed in g_chains by the CALL-SITE VA (not the callee), so
// a second callsite hook on the SAME site chains/mediates, while two
// callsite hooks on DIFFERENT sites that call the same callee never
// collide — the defining property of callsite vs function-entry.
AddResult AddCallsite(const kcdx::hook_payload::HookPayload& payload,
                      int callbackRef, const std::string& pluginName,
                      int priority, const std::string& name) {
    using Mode = kcdx::hook_payload::Mode;
    AddResult res;

    if (!payload.hasSignature) {
        // Should be unreachable — the binder requires a signature for the
        // before/after/around/replace behaviors callsite uses — but guard.
        res.reason = "internal: callsite hook has no parsed signature";
        return res;
    }

    // 1. Resolve the call-site VA (the E8 instruction).
    std::string reason;
    uintptr_t callsiteVa = ResolveCallsite(payload, reason);
    if (!callsiteVa) { res.reason = std::move(reason); return res; }

    std::lock_guard<std::mutex> lock(g_chainsMu);

    // 2. Existing callsite chain on this exact site? Chain onto it (same
    //    coexistence rules as a function-entry chain — the E8 already
    //    points at that chain's trampoline; we only append a behavior).
    if (Chain* chain = FindChain(callsiteVa)) {
        std::string whyNot;
        if (!CanCoexist(*chain, payload.mode, payload.signature,
                        /*incomingIsCallsite=*/true, whyNot)) {
            res.reason = std::move(whyNot);
            return res;
        }
        ChainEntry e;
        e.mode = payload.mode; e.callbackRef = callbackRef;
        e.pluginName = pluginName; e.priority = priority; e.name = name;
        const bool needsCallOriginal = (payload.mode == Mode::Around);
        InsertOrdered(*chain, std::move(e));
        if (needsCallOriginal && !chain->callOriginalThunk &&
            chain->calleeVa) {
            std::string rt; std::vector<std::string> pts;
            SignatureToAbiStrings(chain->sig, rt, pts);
            chain->callOriginalThunk = (uintptr_t)
                kcdx::dynamic_call_jit::BuildLuaCallThunk(
                    chain->calleeVa, rt, pts);
        }
        res.ok = true;
        log::InfoF("hook_chain: appended %s '%s' (plugin '%s') to CALLSITE "
                   "0x%p (chain now %zu)",
                   kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
                   pluginName.c_str(), (void*)callsiteVa,
                   chain->entries.size());
        return res;
    }

    // 3. First touch on this call site. Read the opcode — v1 handles ONLY
    //    the E8 near-call rel32 form. FF /2 (call r/m), FF 15 (call
    //    [rip+disp]) and other indirect calls are out of scope: their
    //    displacement is not a rel32-to-callee we can recompute, so reject
    //    LOUDLY naming the actual opcode (this is an ABI fact verified at
    //    install, never assumed — .claude/rules/anti-patterns.md AP10).
    const uint8_t opcode = *reinterpret_cast<const uint8_t*>(callsiteVa);
    if (opcode != 0xE8) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "callsite at 0x%p is not an E8 near-call rel32 (opcode byte is "
            "0x%02X). mode='callsite' v1 only redirects direct E8 calls; "
            "indirect calls (FF /2 register/memory, FF 15 [rip+disp]) are "
            "out of scope. Check the target_callsite locator points at the "
            "CALL instruction (offset to the E8 byte).",
            (void*)callsiteVa, (unsigned)opcode);
        res.reason = buf;
        return res;
    }

    // 4. Original callee VA = (callsiteVa + 5) + signed disp32 at +1.
    int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(callsiteVa + 1), 4);
    const uintptr_t calleeVa =
        callsiteVa + 5 + static_cast<uintptr_t>(static_cast<int64_t>(disp));

    // 5. Build the chain trampoline over the ORIGINAL CALLEE, reusing the
    //    function-entry spine. make_jit_func bakes the address of the
    //    runtime_func_t's own detour `original_` slot into the emitted
    //    call-original path; we then write the callee VA into that slot
    //    (no MinHook detour is installed — this trampoline is a standalone
    //    function the E8 will point at). The slot is valid even without
    //    InstallRuntime (the detour_hook is default-constructed in the
    //    runtime_func_t ctor; get_jit_original_slot() == &original_).
    auto newChain = std::make_unique<Chain>();
    newChain->targetVa   = callsiteVa;
    newChain->isCallsite = true;
    newChain->calleeVa   = calleeVa;
    newChain->sig        = payload.signature;
    newChain->rf         = std::make_unique<kcdx::rom::runtime_func_t>();

    std::string rt; std::vector<std::string> pts;
    SignatureToAbiStrings(payload.signature, rt, pts);

    // The last make_jit_func arg is the value baked into the trampoline
    // and passed to DispatchPre/DispatchPost as `target_func_ptr` — it is
    // the CHAIN-LOOKUP KEY (ResolveChainForDispatch keys g_chains by it),
    // NOT the call target. For a callsite chain that key is the call-site
    // VA (how this chain is stored in g_chains), so the dispatchers find
    // THIS chain. The original callee VA is supplied separately via the
    // call-original slot (below) + the around thunk.
    uintptr_t jit = newChain->rf->make_jit_func(
        rt, pts, asmjit::Arch::kX64,
        &DispatchPre, &DispatchPost, /*target_func_ptr=*/callsiteVa);
    if (!jit) {
        res.reason = "make_jit_func failed (signature/codegen — see kcdx.log)";
        return res;
    }
    // Wire the original callee VA into the trampoline's call-original slot
    // so the spine's auto-run-original path (before/after) and the around
    // call_original thunk reach the real callee.
    if (void** slot = newChain->rf->get_jit_original_slot()) {
        *slot = reinterpret_cast<void*>(calleeVa);
    } else {
        res.reason = "callsite: runtime_func_t has no call-original slot "
                     "(internal — detour_hook missing)";
        return res;
    }

    // 6. around's call_original thunk runs over the ORIGINAL CALLEE VA
    //    directly (we have it in hand; no MinHook trampoline involved).
    if (payload.mode == Mode::Around) {
        newChain->callOriginalThunk = (uintptr_t)
            kcdx::dynamic_call_jit::BuildLuaCallThunk(calleeVa, rt, pts);
    }

    // 7. Verify rel32 reachability + rewrite the E8 displacement to the
    //    trampoline. RewriteCallDisplacement computes the distance and
    //    fails loud if the trampoline is out of rel32 range (the branch
    //    pool guarantees proximity to WHGame.dll, but the call site may be
    //    in another module — verified here, not assumed). On success the
    //    call site now reaches the chain trampoline; the conflict-engine
    //    footprint for the 4 rewritten bytes is the g_chains entry keyed
    //    by callsiteVa (CanCoexist mediates a second redirect of the same
    //    site — load-order-loses).
    if (!RewriteCallDisplacement(callsiteVa, jit, reason)) {
        res.reason = "callsite redirect failed: " + reason;
        return res;
    }

    ChainEntry e;
    e.mode = payload.mode; e.callbackRef = callbackRef;
    e.pluginName = pluginName; e.priority = priority; e.name = name;
    newChain->entries.push_back(std::move(e));

    g_chains.emplace(callsiteVa, std::move(newChain));
    res.ok = true;
    log::InfoF("hook_chain: installed CALLSITE %s '%s' (plugin '%s') at E8 "
               "site 0x%p -> callee 0x%p (trampoline 0x%p)",
               kcdx::hook_payload::ModeToken(payload.mode), name.c_str(),
               pluginName.c_str(), (void*)callsiteVa, (void*)calleeVa,
               (void*)jit);
    return res;
}

AddResult Add(lua_State*                             L,
              const kcdx::hook_payload::HookPayload& payload,
              int                                    callbackRef,
              const std::string&                     pluginName,
              int                                    priority,
              const std::string&                     name) {
    AddResult res;
    if (L) g_L = L;  // capture the dispatch state on first use

    // mode=mid is a different install (mid-function detour + captures, no
    // function signature) — branch before the signature gate.
    if (payload.mode == kcdx::hook_payload::Mode::Mid) {
        return AddMid(payload, callbackRef, pluginName, priority, name);
    }

    // mode="callsite" is a different install (rewrite ONE E8 call's rel32
    // displacement to a chain trampoline; the callee is untouched, so only
    // THIS caller is affected). The behavior (before/after/around/replace)
    // in payload.mode drives the dispatch semantics exactly as for a
    // function-entry hook — AddCallsite reuses the same DispatchPre/Post +
    // call_original spine. Branch after the Mid check (callsite uses a
    // signature, so the signature gate below would also pass; routing on
    // the scope keeps the install path explicit).
    if (payload.callsiteScope) {
        return AddCallsite(payload, callbackRef, pluginName, priority, name);
    }

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
        if (!CanCoexist(*chain, payload.mode, payload.signature,
                        /*incomingIsCallsite=*/false, whyNot)) {
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
