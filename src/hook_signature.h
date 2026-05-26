#pragma once

// kcdx::hook_signature — parser for the kcdx.hook signature DSL.
//
// Authors declare the ABI of the target function as a string:
//
//   "void (ptr, wstr szApp, wstr, wstr, u32)"
//   "bool ()"
//   "f32 (f32 base, f32 multiplier)"
//   "void (rcx: ptr, wstr)"           — register-pinned override
//   "int (ptr L, int nargs)"          — named args become args.L / args.nargs
//
// The signature drives three things at hook-install time:
//
//   1. Argument marshaling: which register/stack-slot holds each
//      arg in the Win64 fastcall ABI, how to push that arg's value
//      onto the Lua stack as a properly-typed userdata/string/etc.
//   2. Named-arg lookup: 'wstr szApp' becomes args.szApp in the
//      Lua callback.
//   3. Return-value handling: void / non-void controls whether the
//      after-mode callback gets hook_retval on its args table.
//
// This module is parser-only — it produces a typed Signature struct
// that downstream code (lua_bind_hook, JIT codegen, the C++
// kcdxHookInterface in Phase 3) consumes. No marshaling logic lives
// here.
//
// Grammar (informal):
//
//   signature   := return_type WS '(' WS [arg_list] WS ')'
//   return_type := primitive
//   arg_list    := arg (',' WS arg)*
//   arg         := [pin_target ':' WS] primitive [WS name]
//   pin_target  := register     (e.g. 'rcx', 'r8', 'xmm0')
//   primitive   := i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64
//                | f32 | f64 | ptr | bool | wstr | cstr | void
//                | int            (alias for i32, common convenience)
//   name        := [a-zA-Z_][a-zA-Z0-9_]*
//
// Reserved arg names (engine-defined, parser rejects):
//   - hook_skip    (mid-mode skip-original flag; set true in callback
//                   body. Reserved so authors don't shadow it via a
//                   declared arg name.)
//   - hook_retval  (after-mode return-mutation slot; engine writes
//                   the original's return value here before the
//                   callback runs; author may mutate to replace it.
//                   Only present on after-mode hooks targeting
//                   non-void return types.)
//
// Anything else is fair game. Author param 'L', 'nargs', '_x',
// '_internal', 'errfunc', 'szApp', etc. all parse fine and become
// the args-table keys.

#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::hook_signature {

// One primitive type. The parser's only type representation; the
// downstream JIT/marshaling layer matches on this enum to decide
// how to read/write the argument at the underlying register or
// stack slot.
enum class Type : uint8_t {
    Void = 0,
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Ptr,
    Bool,
    Wstr,        // wchar_t const* — read as UTF-16, push as Lua string
    Cstr,        // char const*    — read as UTF-8, push as Lua string
};

// True iff the type can be passed in an SSE/XMM register
// (Win64 fastcall: floats use xmm0..xmm3 for arg slots 0..3, GPRs
// for everything else). Parser uses this to validate that
// `xmm0: i32` is rejected (xmm regs only hold floats) and
// `rcx: f32` is rejected (gpr only holds non-floats).
bool IsFloatType(Type t);

// One register name from the Win64 ABI. The parser accepts only
// these registers as pin targets; an unknown identifier in the
// `rcx:` position produces a parse error.
//
// Encoded as the canonical lowercase string (one of: rax, rcx, rdx,
// rbx, rsi, rdi, r8..r15, xmm0..xmm15). Stored as a small string
// to keep the type-erased ParsedArg portable across JIT backends.
struct Register {
    std::string name;   // empty = no register pin (default ABI slot)
};

// One argument's parsed shape.
struct Arg {
    Type        type = Type::Void;
    std::string name;        // empty = anonymous (positional only)
    Register    pinned;      // optional ABI-slot override
};

// Result of parsing a signature string. Returned by Parse(); the
// `ok` field reports success. On failure, `error` carries the
// diagnostic + a 1-based column index pointing at the bad token.
//
// The struct is intentionally cheap to copy (vector + a couple of
// strings); pass by value at the API boundary.
struct Signature {
    Type             returnType = Type::Void;
    std::vector<Arg> args;
};

// Error variant of Parse's return. Caller pattern:
//   auto r = hook_signature::Parse("...");
//   if (!r.ok) { return luaL_error(L, "%s", r.error.c_str()); }
struct ParseResult {
    bool        ok = false;
    Signature   sig;
    std::string error;
    int         errorColumn = 0;  // 1-based, 0 = no column info
};

ParseResult Parse(const std::string& text);

// Convert a parsed type back to its canonical token (e.g. "ptr",
// "wstr", "i32"). Useful for log messages and round-trip diagnostics.
const char* TypeToken(Type t);

// True iff the given name is reserved by the engine (rejected by
// the parser if used as an arg name). Specifically: "hook_skip" and
// "hook_retval". Exposed so other surfaces (e.g. C++ kcdxHookInterface
// in Phase 3, or future arg-builder helpers) apply the same check.
bool IsReservedArgName(const std::string& name);

// Byte-compatibility of two signatures for sharing ONE marshaling
// thunk: same arg count, and each slot + the return map to the same
// JIT type-string. Conservative — e.g. i32 vs i64 both collapse to
// "i64" so they read as compatible (the register move is identical),
// which never produces a wrong marshal. Returns false on any arg-count
// delta or per-slot/return type-string mismatch.
//
// DECLARED here so the two named-target install surfaces
// (src/hook_interface.cpp ResolveSignature + src/lua_bind_hook.cpp
// signature resolution) can cross-check an explicit author signature
// against a name-resolved verified ABI (the sig-mismatch gate). The
// body lives in src/hook_chain.cpp (it keys on that TU's file-local
// SigTypeToJitString mapping — minimal-blast, no header churn for the
// type→string table).
bool SignaturesCompatible(const Signature& a, const Signature& b);

// Conflict classification for the sig-mismatch GATE (the named-target +
// explicit-signature footgun, cap-38). DISTINCT from SignaturesCompatible:
// SignaturesCompatible answers "can two hooks share ONE marshaling thunk?"
// (the chain-coexistence question at hook_chain.cpp), and is NOT touched by
// this. ClassifyConflict answers "how SEVERE is an explicit author signature
// disagreeing with the name-resolved verified ABI?" — to drive the gate's
// log SEVERITY, never its resolution (the explicit sig always wins / install
// always proceeds — behavior-(c)).
//
//   None — the two are compatible (no diagnostic).
//   Soft — incompatible but SAME SHAPE: same arg count AND same return
//          register-WIDTH. A per-slot type nuance only (e.g. ptr vs i64,
//          i32 vs u32 — same-width register move; mis-marshals a value but
//          not the call frame). → WARN.
//   Hard — a SHAPE delta: different arg count, OR a return-WIDTH delta. The
//          call frame / return register is mis-described, the live-engine
//          crash risk (the cap-38 / 0xC8 case: 1-arg explicit vs 2-arg
//          verified). → ERROR.
//
// Return-WIDTH mapping (Type carries no width member, so this is the gate's
// definition; see ClassifyConflict's body for the table). Conservative by
// design — when in doubt a difference is classified Hard (a false-Hard
// over-warns at ERROR but NEVER under-warns a real crash risk).
//
// DECLARED here so BOTH named-target install gates (hook_interface.cpp
// ResolveSignature + lua_bind_hook.cpp signature resolution) share ONE
// definition of "hard conflict" and cannot drift. Body in hook_chain.cpp
// alongside SignaturesCompatible (so the file-local type tables resolve).
enum class SignatureConflictKind { None, Soft, Hard };

SignatureConflictKind ClassifyConflict(const Signature& explicitSig,
                                       const Signature& verifiedSig);

}  // namespace kcdx::hook_signature
