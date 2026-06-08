#pragma once
// safetyhook_midhook — the mid-function detour adapter over safetyhook::MidHook.
//
// One responsibility: install a mid-function hook at a captured-instruction VA
// and route every fire to the existing kcdx::hook_chain::MidDispatch, reading +
// writing the named captures through safetyhook's Context64 (NOT a JIT'd
// per-target codegen — make_jit_midfunc is retired).
//
// WHY this is a DEDICATED unit, not an IDetourBackend / InstallRuntime route:
// safetyhook::MidHook does NOT fit IDetourBackend. It owns its own install
// (create()+enable()), takes no external detour pointer, returns no pOriginal
// trampoline for a JIT call-original slot, and its callback is a bare
// void(*)(Context&) with NO userdata channel. So the mid path installs the
// MidHook DIRECTLY (from AddMid / AddCMid), never through InstallRuntime — the
// design's §5.3 / §8 "a safetyhook::MidHook adapter that calls the existing
// MidDispatch", distinct from SafetyhookBackend.
//
// TARGET-IDENTITY RECOVERY — a fixed C-trampoline pool (ZERO runtime codegen):
// safetyhook::MidHookFn is a bare void(*)(Context&) with no userdata
// (SOURCE: vendor/safetyhook/include/safetyhook/mid_hook.hpp:22, read this
// session) and ctx.rip is the safetyhook TRAMPOLINE, not the target VA
// (SOURCE: vendor/safetyhook/include/safetyhook/context.hpp:27, this session) —
// so neither a userdata closure nor a ctx.rip key recovers the target. Identity
// is recovered by a SLOT bound at install: a fixed array of N compile-time C
// trampolines, each baking its own index, dispatches to MidDispatchFromContext
// with that index; the index looks up the slot table's bound target VA.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace safetyhook { struct Context64; }

namespace kcdx::safetyhook_midhook {

// Pool cap. Mid-targets are few (the chain keeps one mid hook per VA, and only
// a handful of game sites are mid-hooked); 128 distinct compile-time
// trampolines is generous and keeps the trampoline array compile-time-constant.
// Exhaustion (all N slots claimed) FAILS LOUD — never a silent drop.
constexpr size_t kMidTrampolinePoolSize = 128;

// Outcome of installing a mid adapter. On failure `reason` is a ready-to-surface
// diagnostic (the caller copies it into AddResult::reason). On success the
// MidHook is held for the session (kcdx never unhooks — SKSE model); the caller
// keeps no handle.
struct InstallResult {
    bool        ok = false;
    std::string reason;
};

// Install a mid-function hook at `targetVa` via safetyhook::MidHook.
//
// `captureExprs` / `captureTypes` are the parsed capture grammar (the same
// vectors make_jit_midfunc consumed) — used at FIRE time to read each capture
// out of Context64 and write a mutated value back. `targetVa` is the
// CHAIN-LOOKUP KEY: MidDispatchFromContext looks up the bound target, then calls
// hook_chain::MidDispatch(targetVa), which resolves the Chain by that key and
// runs the author callback exactly as today.
//
// The skip/resume address (ctx.rip for False/skip) is targetVa + the size of
// safetyhook's RELOCATED region (read from the hook's original_bytes() AFTER
// create, never recomputed) — the first clean byte past safetyhook's E9/FF
// patch, patch-width-correct by construction. (NOT targetVa + just the captured
// instruction's length — that would land inside the patch when the captured
// instruction is shorter than the patch; the cap-04 scar.)
//
// On ok=false the MidHook was not installed; the caller surfaces `reason`.
InstallResult Install(uintptr_t                       targetVa,
                      const std::vector<std::string>& captureExprs,
                      const std::vector<std::string>& captureTypes,
                      const std::string&              hookName);

}  // namespace kcdx::safetyhook_midhook
