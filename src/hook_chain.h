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
#include "hook_signature.h"

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
// `handleId` is the registry handle id identifying THIS specific chain
// entry (Entry::handleId); stamped onto the ChainEntry so a later
// Uninstall(handleId) finds and removes the right entry.
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
              const std::string&                   name,
              uint64_t                             handleId);

// Install or extend the chain for one kcdxHookInterface-installed hook.
// Parallel to Add() (Lua-side) but takes a raw C callback + pre-resolved
// Signature (already parsed by the kcdxHookInterface thunk via
// opts->signature parse OR address_library::ResolveSignatureByName).
//
// Mirrors Add's branching shape: routes internally to AddCMid (when
// payload.mode == Mid) or AddCCallsite (when payload.callsiteScope).
// Reuses ResolveLocator + CanCoexist + InsertOrdered. Builds ChainEntry
// {kind=C, cFn, cSig, cDispatchThunk, mode, ...}. Same g_chainsMu
// discipline as Add.
//
// On failure (ok=false + reason): the engine owns no Lua ref to release
// (the C author owns cFn's lifetime); the registry Entry's status will
// flip Failed by the existing ApplyHookEntry machinery.
AddResult AddC(const kcdx::hook_payload::HookPayload& payload,
               void*                                  cFn,
               const kcdx::hook_signature::Signature& cSig,
               const std::string&                     pluginName,
               int                                    priority,
               const std::string&                     name,
               uint64_t                               handleId);

// Uninstall a previously-Add()'d hook by registry handle id. Removes the
// entry from its chain. The chain's MinHook detour STAYS installed for
// the session (matching the documented "hooks live for the session"
// engine stance at hook_engine.cpp:39-45 and hook_chain.cpp:155-158);
// the now-empty chain is a no-op shim that dispatches straight to the
// original. The next Add on this target reuses the existing trampoline.
//
// Idempotent: uninstalling an unknown / already-removed handleId is
// safe and returns true. Safe to call from any context (g_chainsMu
// guards the entries-vector mutation; the dispatcher is robust to
// chain.entries.empty()).
//
// The caller (Lua handle:uninstall, future C++ kcdxHookInterface::
// Uninstall) is responsible for updating the registry Entry's status
// to Status::Removed via lua_registry::SetStatus after this returns true.
bool Uninstall(uint64_t handleId);

// Set the live game lua_State the chain dispatchers run callbacks
// against. Add() also captures it on first use; this is here so the
// engine can bind it explicitly at first-tick alongside the other
// lua_State consumers. Idempotent.
void SetLuaState(lua_State* L);

// One participant in a kcdx.hook conflict at a target VA — a winner
// (installed in the live chain, applied=true) OR a loser (rejected by
// CanCoexist, applied=false). Mirrors the {name, priority, applied}
// shape interfaces.cpp's GetConflictReport already builds for the
// legacy patch/hook paths, so the consumer can merge these directly
// into its existing hit list. `kind` is always hook here (this module
// only knows kcdx.hook entries); the consumer stamps the kcdxConflict
// kind enum on its side.
//
// `name` points into the owning Chain's stable std::string storage
// (the ChainEntry's `name` for winners, the RejectedEntry's `name` for
// losers). Both live for the Chain's lifetime, which is process-
// lifetime (Chains are never destroyed — hooks live for the session),
// so the pointers are valid for as long as the caller could hold them.
struct ConflictParticipant {
    const char* name;
    int         priority;
    bool        applied;  // true = winner (live chain); false = loser (rejected)
};

// All kcdx.hook participants at one RESOLVED runtime target VA — both
// the live chain winners (applied=true) and the CanCoexist-rejected
// losers (applied=false). `targetVa` is in the same address space as
// chain->targetVa (what ResolveLocator produced) and as the `target`
// GetConflictReport receives (interfaces.cpp matches it against
// rh.targetAddr). If no kcdx.hook ever touched this VA (FindChain
// null), returns empty — the caller's legacy patch/hook loops still
// run; an empty result just means "no kcdx.hook entries here".
//
// Locking: takes g_chainsMu (the same mutex Add* takes), because it is
// queried at GetConflictReport time and Add* can run concurrently
// during the first-tick registration pass. Returns by value — the
// vector is the caller's; the `name` pointers inside it remain valid
// for the process lifetime (see ConflictParticipant).
std::vector<ConflictParticipant> GetParticipantsAtTarget(uintptr_t targetVa);

// Enumerate every live chain's resolved target VA — the "what kcdx.hook has
// modified" set, for the modification inventory (modification_inventory.cpp).
// One VA per Chain in g_chains (function-entry, callsite, and mid chains
// alike: each is ONE installed detour at its targetVa). Returns by value.
//
// Locking: takes g_chainsMu (the same mutex Add* takes), because Add* can run
// concurrently during the first-tick registration pass. Read-only — the map
// and its Chains are not mutated. The returned VAs are plain integers (no
// borrowed pointers), valid indefinitely.
std::vector<uintptr_t> GetAllChainTargets();

}  // namespace kcdx::hook_chain
