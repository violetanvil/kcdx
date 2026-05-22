#pragma once

// kcdx::hook_payload — the queued-intent payload for a kcdx.hook
// registration.
//
// Phase 2b of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md). A kcdx.hook call builds
// one of these, stores it as the type-erased payload of a
// kcdx::lua_registry::Entry (Kind::Hook), and the end-of-zone apply
// pass casts it back to install the interception. This header is
// data-only — no install logic lives here. Sub-3 fills the struct +
// queues it; the per-mode apply routines (sub-4..9) consume it.
//
// The locator fields mirror kcdx::hook_engine::HookEntry's resolution
// surface (pattern / target_symbol / address_id / function_name /
// callsite), since address resolution is shared. The signature is the
// already-parsed kcdx::hook_signature::Signature, so the apply pass
// never re-parses the DSL string.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hook_signature.h"
#include "patch_engine.h"  // patch::Pattern, patch::Anchor

namespace kcdx::hook_payload {

// Which interception shape the author asked for. One-to-one with the
// `mode = "..."` strings in the Lua surface. See the plan's "Hook
// modes" table for per-mode dispatch semantics — those land in the
// per-mode apply commits (sub-4..9). Sub-3 only validates that the
// requested mode is one of these and stores it.
enum class Mode : uint8_t {
    Before = 0,   // run callback before original, may mutate args
    After,        // run callback after original, may mutate return value
    Around,       // callback decides whether/when to call original
    Replace,      // original never runs; callback's return is the result
    Mid,          // mid-function capture at `offset`
    Callsite,     // redirect ONE call instruction (located by callsite)
};

// Parse a mode token ("before", "after", ...) into the enum. Returns
// false and leaves `out` untouched on an unknown token.
bool ParseMode(const std::string& token, Mode& out);

// Canonical token for a mode value (for log lines + diagnostics).
const char* ModeToken(Mode m);

// True iff the mode requires the function-being-called signature to
// be present even though the patched bytes are at a call site rather
// than the function entry. (Callsite is the only such mode today.)
inline bool ModeUsesCallsiteLocator(Mode m) { return m == Mode::Callsite; }

// The callsite sub-locator (only meaningful when mode == Callsite).
// Exactly one of pattern / addressId / rva resolves the address of
// the CALL instruction whose rel32 displacement gets rewritten.
struct CallsiteLocator {
    patch::Pattern pattern;             // the call instruction's bytes
    int            offset    = 0;       // offset to the call opcode in the match
    uint64_t       addressId = 0;       // Address Library id of the callsite
    std::string    rva;                 // "WHGame.dll @ rva 0x12345a" form
};

// One queued kcdx.hook registration. Built by the binder, owned by the
// registry Entry via shared_ptr, consumed by the per-mode apply pass.
struct HookPayload {
    // --- Identity (mirrors the bytes payload) ---
    std::string name;
    std::string description;

    // Mode-of-interception. Validated at registration; drives which
    // apply routine the per-mode commit dispatches to.
    Mode mode = Mode::Before;

    // --- Function-entry locator (mode != Callsite) ---------------------
    // Exactly one of: functionName, pattern, addressId, targetSymbol,
    // targetLuaCfunction. For mode == Callsite these are EMPTY and the
    // callsite sub-locator is used instead (functionName is still
    // allowed alongside callsite — it supplies the called function's
    // signature info, not a patch target).
    std::string                   functionName;        // "WHGame.dll!Symbol" or mangled export
    patch::Pattern                pattern;             // AOB at the function entry
    uint64_t                      addressId = 0;       // Address Library id
    std::string                   targetSymbol;        // cross-plugin symbol-table lookup
    std::string                   targetLuaCfunction;  // e.g. "System.LogAlways"
    int                           offset = 0;          // applied after resolution
    std::optional<patch::Pattern> context;             // optional disambiguation pattern
    patch::Anchor                 anchor;               // optional string anchor
    uint32_t                      maxAnchorDistance = 4096;
    std::string                   module = "WHGame.dll";

    // --- Callsite sub-locator (mode == Callsite only) ------------------
    std::optional<CallsiteLocator> callsite;

    // --- Signature (already parsed; never re-parsed at apply) ----------
    hook_signature::Signature signature;
    bool                      hasSignature = false;  // false for raw mid captures w/o sig

    // --- mode == Mid extras --------------------------------------------
    // Capture descriptors ("r14b", "[rcx+0x10]:i32") forwarded verbatim
    // to the mid-hook engine at apply; parsed there, not here.
    std::vector<std::string> captures;

    // --- Lua callback ---------------------------------------------------
    // Reference into the Lua registry (luaL_ref) keeping the callback
    // closure alive against GC between registration and apply, and for
    // the lifetime of the installed hook. LUA_NOREF (-2) when unset.
    // The binder takes the ref; the apply pass / teardown releases it.
    int callbackRef = -2;  // LUA_NOREF
};

}  // namespace kcdx::hook_payload
