#pragma once

// kcdx::hook_chain — per-target chain of kcdx.hook callbacks.
//
// Phase 2b sub-4 of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md). This is the NEW
// function-interception dispatch surface for kcdx.hook. It supersedes
// the legacy kcdx::scripting dynamic_hook_pre/post path (which stays as
// reference until this is verified, then is removed — the codebase is
// unshipped, single machine, no mods to protect).
//
// Model: one game function (target VA) gets ONE MinHook detour, built
// once when the first kcdx.hook lands on it. Subsequent kcdx.hook calls
// on the same target APPEND to that target's chain rather than failing.
// The chain is an ordered list of mode-tagged Lua callbacks. When the
// game calls the hooked function, the engine's JIT thunk invokes two C
// dispatchers (DispatchPre before the original, DispatchPost after);
// those walk the chain in unified load order and run each callback with
// a named-arg `args` table.
//
// Modes (see restructure-plan §"Hook modes"):
//   before  — runs in pre-phase; may mutate args; returning a value
//             suppresses the original (that value becomes the result).
//   after   — runs in post-phase; may mutate the return value.
//   around  — runs in pre-phase; receives a call_original() it invokes
//             0/1/N times; its return is the result. When any around
//             (or replace) is present, the thunk does NOT auto-run the
//             original — the around callback owns that.
//   replace — like around whose callback never calls call_original;
//             original never runs, callback's return is the result.
//
// Conflict policy (v1, safe-but-blunt; smart footprint coexistence is
// future work — see docs/outstanding-work/smart-replace-conflict-
// detection.md): the FIRST hook on a target fixes the thunk's signature.
// A later hook whose signature is incompatible, or that cannot coexist
// with an already-present replace, is REJECTED (its Add* returns false
// with a reason). Earlier-in-load-order wins. Compatible hooks chain.

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

#include "hook_payload.h"

namespace kcdx::hook_chain {

// Outcome of trying to add a hook to a target's chain. On failure,
// `reason` is a ready-to-surface diagnostic (goes to handle:reason()).
struct AddResult {
    bool        ok = false;
    std::string reason;
};

// Install or extend the chain for one kcdx.hook registration. Resolves
// the payload's locator to a target VA, builds the MinHook detour on
// first-touch (JIT thunk + InstallRuntime + pOriginal wiring + the
// call_original thunk over pOriginal), and appends this callback to the
// target's ordered chain.
//
// `callbackRef` is the LUA_REGISTRYINDEX ref the binder took for the
// callback closure (ownership transfers to the chain — released on the
// rare failure path; otherwise lives for the session). `pluginName` /
// `priority` / `name` drive load-order chain ordering + diagnostics.
//
// `L` is the live game lua_State (the chain dispatchers invoke callbacks
// against it). Must be non-null.
//
// Returns ok=false (with reason) on: locator resolution failure, JIT
// failure, signature-incompatible-with-existing-chain, or replace
// conflict lost by load order. On ok=false the callbackRef is released.
AddResult Add(lua_State*                          L,
              const kcdx::hook_payload::HookPayload& payload,
              int                                  callbackRef,
              const std::string&                   pluginName,
              int                                  priority,
              const std::string&                   name);

// Set the live game lua_State the chain dispatchers run callbacks
// against. Add() also captures it on first use; this is here so the
// engine can bind it explicitly at first-tick alongside the other
// lua_State consumers. Idempotent.
void SetLuaState(lua_State* L);

}  // namespace kcdx::hook_chain
