#pragma once

// kcdx::hook_chain — per-target chain of kcdx.hook callbacks.
//
// Part of the manifest-only restructure. This is the NEW
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
// future work (smart-replace conflict detection)): the FIRST hook on a
// target fixes the thunk's signature.
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

// Internal-only entry point — register an engine-owned hook on the chain
// (the engine-direct migration: lua_pcall, frealloc canary, ModManager_ctor,
// BugSplat ctor, SaveGame, LoadGame). Identical to AddC in every install
// behavior — same chain-share / coexist rules, same JIT thunk wiring, same
// load-order tiebreak within the engine block — except every entry created
// here stamps ChainEntry::isEngine = true so InsertOrdered sorts engine
// entries ahead of any plugin entry regardless of priority. Reserved for
// engine internals; never called from a plugin-facing surface (the public
// AddC is the only plugin path).
//
// `pluginName` should be "kcdx" by convention; `name` is the engine-side
// site label (e.g. "engine.lua_pcall"). `priority` orders engine entries
// among themselves only (engine-vs-plugin is decided by isEngine).
AddResult AddCEngine(const kcdx::hook_payload::HookPayload& payload,
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

// One live chain's modification-inventory record: its resolved target VA plus
// the owning plugin + hook name, so the inventory DETAIL line names WHO owns a
// chain rather than the generic surface name "kcdx.hook" (the attribution gap
// in the 0xC8 save-load crash). For a
// function-entry / callsite chain the owner is the FIRST entry's
// (pluginName, name); for a mid chain it is (midPluginName, midName). An
// empty chain (all entries uninstalled — the detour stays a no-op shim) has
// no owner, so pluginName/hookName fall back to "" (the VA is still reported).
//
// `pluginName` / `hookName` point into the owning Chain's stable std::string
// storage. Chains are never destroyed (hooks live for the session), so the
// pointers are valid for the process lifetime — same lifetime contract as
// ConflictParticipant::name above. Do NOT hold them past a g_chains mutation
// of the SAME entry's strings (none happens — entries' name/pluginName are
// set at Add and never rewritten).
struct ChainTarget {
    uintptr_t   va;
    const char* pluginName;  // borrowed, process-lifetime; "" if chain empty
    const char* hookName;    // borrowed, process-lifetime; "" if chain empty
};

// Enumerate every live chain's target VA + owning plugin/hook name — the
// "what kcdx.hook has modified" set, for the modification inventory
// (modification_inventory.cpp). One record per Chain in g_chains
// (function-entry, callsite, and mid chains alike: each is ONE installed
// detour at its targetVa). Returns by value.
//
// Locking: takes g_chainsMu (the same mutex Add* takes), because Add* can run
// concurrently during the first-tick registration pass. Read-only — the map
// and its Chains are not mutated. The VAs are plain integers; the name
// pointers are process-lifetime borrows (see ChainTarget).
std::vector<ChainTarget> GetAllChainTargets();

}  // namespace kcdx::hook_chain
