#pragma once

// kcdx::hook_payload — the queued-intent payload for a kcdx.hook
// registration.
//
// Part of the manifest-only restructure. A kcdx.hook call builds
// one of these, stores it as the type-erased payload of a
// kcdx::lua_registry::Entry (Kind::Hook), and the end-of-zone apply
// pass casts it back to install the interception. This header is
// data-only — no install logic lives here. The binder fills the struct +
// queues it; the per-mode apply routines consume it.
//
// The locator fields mirror kcdx::hook_engine::HookEntry's resolution
// surface (pattern / target_symbol / address_id / callsite), since
// address resolution is shared. The signature is the already-parsed
// kcdx::hook_signature::Signature, so the apply pass never re-parses the
// DSL string.

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
// per-mode apply commits. The binder only validates that the
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

// True iff this mode value requires the called-function signature even
// though the patched bytes are at a call site rather than the function
// entry. NOTE: the callsite SCOPE is now carried by
// HookPayload::callsiteScope (the behavior mode — before/after/around/
// replace — lives in HookPayload::mode), so a callsite hook's `mode`
// value is its behavior, not Mode::Callsite. A callsite hook's signature
// requirement is enforced by the same gate as any non-Mid behavior
// (the binder requires a signature for before/after/around/replace).
// This helper is retained for diagnostics / intent.
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

    // The 2-dot namespace identity of the plugin that owns this
    // registration — `owningAuthor` (from [plugin].author) +
    // `owningPlugin` (from [plugin].name).
    // Either may be "" for an anonymous / console / pak-script call;
    // `owningAuthor` is also "" for a plugin that has not yet declared
    // [plugin].author (the in-progress namespace refactor — step 6
    // populates the manifests, after which both are real). Set by the
    // binder at parse time (OwningPluginForCurrentCall) and threaded
    // into the resolvers (ResolveLocator / ResolveCallsite →
    // address_library::ResolveByName) to drive the self > engine >
    // other precedence: a bare name resolves to this plugin's own
    // target first, then the engine seed, then any other plugin's;
    // empty identity = anonymous (engine-seed + other only, no self
    // tier).
    //
    // Engine-internal struct (not in include/kcdx/Interfaces.h) so the
    // append-only ABI discipline does not apply — appending a new
    // identity field next to `owningPlugin` is free.
    std::string owningAuthor;
    std::string owningPlugin;

    // Mode-of-interception. Validated at registration; drives which
    // apply routine the per-mode commit dispatches to.
    Mode mode = Mode::Before;

    // --- Function-entry locator (mode != Callsite) ---------------------
    // Exactly one of: addressName, pattern, addressId, targetSymbol,
    // targetLuaCfunction, address. For mode == Callsite these are EMPTY
    // and the callsite sub-locator is used instead.
    //
    // The COMMON path is `target = "<name>"` (lands in addressName below):
    // the author names the function and the engine carries its address AND
    // verified signature (the disassembler test — the engine carries both).
    // The remaining locators (pattern / addressId / targetSymbol /
    // targetLuaCfunction / address) are EXPERT/ADVANCED forms for targets
    // the library can't name yet — an escape hatch, never the default path.
    patch::Pattern                pattern;             // [advanced] AOB at the function entry
    uint64_t                      addressId = 0;       // [advanced] numeric kcdx_id in the refdb cache
    // Curated entry by human-readable NAME (e.g. "lua_pcall"). This is the
    // landing slot for the COMMON path `target = "<name>"` and for
    // `address_id = "<name>"` (the `address_id` opts key accepts a string
    // OR a number; a string lands here, a number in addressId). Resolved
    // via address_library::ResolveByName, whose engine-seed tier delegates
    // to the refdb cache. Empty = not set. The name carries address AND
    // verified signature — the author never hand-writes hex/ABI for a
    // named target (the disassembler test).
    std::string                   addressName;
    std::string                   targetSymbol;        // [advanced] cross-plugin symbol-table lookup
    std::string                   targetLuaCfunction;  // [advanced] e.g. "System.LogAlways"
    // [advanced] Raw absolute VA the author already has (a
    // kcdx.memory.pointer userdata or integer from
    // kcdx.lua.cfunction_address, kcdx.memory.scan_pattern, etc.). 0 = not
    // set. The most direct locator: no resolution needed, the VA IS the
    // target.
    uintptr_t                     address = 0;
    int                           offset = 0;          // applied after resolution
    std::optional<patch::Pattern> context;             // optional disambiguation pattern
    patch::Anchor                 anchor;               // optional string anchor
    uint32_t                      maxAnchorDistance = 4096;
    std::string                   module = "WHGame.dll";

    // --- Callsite scope (mode = "callsite") ----------------------------
    // True iff the author wrote `mode = "callsite"`. This is the explicit
    // SCOPE selector ("redirect ONE call instruction"); the BEHAVIOR
    // (Before/After/Around/Replace) lives in `mode` above, attached under
    // the normal mode key. callsiteScope + the callsite sub-locator route
    // the install to the callsite path (hook_chain::AddCallsite); the
    // behavior `mode` drives the dispatch semantics there exactly as it
    // does for a function-entry hook. Mid is not a valid callsite behavior.
    //
    // Why a scope flag rather than reusing Mode::Callsite as the `mode`
    // value: a callsite hook still needs a behavior (before/after/around/
    // replace) to know how to wrap the redirected call, so the behavior
    // enum slot must hold that behavior — not be consumed by the scope.
    // (Mode::Callsite remains in the enum for ModeToken/diagnostics +
    // ModeUsesCallsiteLocator's signature-required test.)
    bool callsiteScope = false;

    // --- Callsite sub-locator (mode = "callsite" only) -----------------
    std::optional<CallsiteLocator> callsite;

    // --- Signature (already parsed; never re-parsed at apply) ----------
    hook_signature::Signature signature;
    bool                      hasSignature = false;  // false for raw mid captures w/o sig

    // --- mode == Mid extras --------------------------------------------
    // Captures the mid callback reads/writes at `offset`. The binder
    // splits each author entry into a register/memory EXPRESSION
    // ("rax", "[rcx+0x10]") and a TYPE ("i64" default, or the `:type`
    // suffix). Parallel vectors, same length:
    //   captureExprs[i]  — the reg/mem expr (the safetyhook::MidHook adapter's
    //                      capture-source: a Context64 field or a memory deref)
    //   captureTypes[i]  — the type string  (selects the read/write width + lane)
    //   captureNames[i]  — the author's name for this capture, or "" when
    //                      the author used the positional list form. Drives
    //                      whether the callback's handle table is keyed by
    //                      name (map form) or 1..N (list form).
    std::vector<std::string> captureExprs;
    std::vector<std::string> captureTypes;
    std::vector<std::string> captureNames;

    // --- Lua callback ---------------------------------------------------
    // Reference into the Lua registry (luaL_ref) keeping the callback
    // closure alive against GC between registration and apply, and for
    // the lifetime of the installed hook. LUA_NOREF (-2) when unset.
    // The binder takes the ref; the apply pass / teardown releases it.
    int callbackRef = -2;  // LUA_NOREF

    // Off-thread routing policy. Default 0 (Marshal) routes off-thread
    // fires through warn-once-skip degradation in v1 per Outcome P (no
    // off-thread sites observed in cap-15..22 + cap-35 corpus); real
    // arg-snapshot Marshal is its own future cycle when the warn ever
    // fires. 1 = Skip with warn-once-per-hook; 2 = Error log per-fire +
    // skip. Values match kcdxHookOffThread_* in
    // include/kcdx/Interfaces.h. The engine auto-marshals off-thread hits
    // to the main thread.
    uint8_t offThread = 0;

    // C function pointer set by the kcdxHookInterface thunks; defaults
    // nullptr (Lua entries leave it unset). ApplyHookEntry branches on
    // (cFn != nullptr) to route between hook_chain::AddC and Add. See
    // step 5-main chunk 1 (b629e14) for the ChainEntry tagged union;
    // restructure-plan.md:1075-1093 for the D5 routing decision (ONE
    // Kind::Hook register; mutex enforced by which surface populated
    // the payload + assert in ApplyHookEntry).
    void* cFn = nullptr;
};

}  // namespace kcdx::hook_payload
