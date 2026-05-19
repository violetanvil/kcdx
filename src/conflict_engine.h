#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "patch_engine.h"

// kcdx::conflict_engine — single source of truth for inter-entry conflict
// detection across [[patch]], [[hook]], and (future) [[mid_hook]].
//
// Before Phase 4b.3, conflict detection lived inside patch_engine and was
// patch-vs-patch only. Hook conflicts were handled ad-hoc inside
// hook_engine's apply loop. Three problems with that:
//   1. Cross-engine collisions (a [[patch]] overlapping a [[hook]] prologue)
//      had no centralized detection.
//   2. Per-engine code duplicated overlap math and conflict reporting.
//   3. Future engines ([[mid_hook]] etc.) would each have to know about
//      every other engine's footprints.
//
// This module centralizes:
//   * Resolution of every entry's target address (via the respective engine's
//     Resolve function — conflict_engine doesn't re-implement locators)
//   * Footprint collection (write ranges, verify-read ranges)
//   * Pairwise overlap detection with category classification
//   * Plain-English conflict logging
//   * Lookup APIs so the apply step can find conflicts affecting it
//
// Apply-time engines (patch::ApplyResolvedPatch, hook_engine::ApplyAll)
// READ this module's resolved data + conflict list; they no longer compute
// any of it themselves.

namespace kcdx::conflict_engine {

// What kind of entry produced this write footprint. Future engines slot in
// here. The category is used both for diagnostic log strings and for
// classification rules (e.g., HookPrologue writes are 5 bytes regardless
// of what the prologue actually is).
enum class WriteKind {
    Patch,         // [[patch]] same-length byte rewrite
    HookPrologue,  // 5-byte rel32 jmp MinHook installs at a [[hook]] target
                   // (or 14-byte abs64 jmp if rel32 can't reach — pre-flight
                   // conservatively models 5 bytes; install-time may differ)
    // Future: TrampolineRegion (for inter-trampoline collisions, currently
    // impossible by construction since the allocator never returns the same
    // address twice).
};

// One thing-that-will-be-written: who, what kind, where, how big.
struct WriteFootprint {
    std::string name;       // entry name, for log diagnostics
    WriteKind   kind;
    int         priority;   // resolved load-order position (lower applies first)
    uintptr_t   begin;
    uintptr_t   end;        // exclusive

    // Backref. Type+index identifies which g_patches[i] / g_hooks[i]
    // produced this footprint, so apply-time code can look up the
    // originating entry. -1 if unset.
    int patchIndex = -1;
    int hookIndex  = -1;
};

// One thing-that-will-be-read-and-verified-against-pristine. Currently only
// [[patch]] produces these (its `original` field). Hooks don't verify; they
// just install. Future engines may add their own.
struct ReadFootprint {
    std::string name;
    int         priority;
    uintptr_t   begin;
    uintptr_t   end;
    int         patchIndex = -1;
};

// Conflict categories. Names chosen to make log lines self-explanatory.
enum class Category {
    // Patch-vs-Patch (ported verbatim from old patch_engine::ConflictKind):

    // Earlier writer's bytes overlap later reader's verify (original) range.
    // The reader's verify will fail at apply time. Reader aborts; log line
    // names the writer as upstream culprit.
    WriteOnOriginal,

    // Both writers target the EXACT same byte range. Both apply in load
    // order; later writer's bytes win.
    WriteOnWriteFull,

    // Both writers target overlapping but non-identical byte ranges. Both
    // apply; result is a mix that may be an invalid instruction.
    WriteOnWritePartial,

    // Cross-engine (new in Phase 4b.3):

    // Two [[hook]]s target the same function entry. First-wins in v0.1
    // (chained hooks are v0.2+). Second hook aborts; log line names the
    // first hook.
    HookOnHook,

    // [[hook]]'s 5-byte rel32-jmp footprint overlaps an EARLIER [[patch]]'s
    // write range. MinHook will relocate the prologue including the patched
    // bytes — the patch effectively survives inside the hook's trampoline.
    // Informational; no abort; both apply.
    HookOverlapsEarlierPatch,

    // [[patch]] write range overlaps an EARLIER [[hook]]'s 5-byte rel32-jmp
    // footprint. The patch's verify-against-original will fail because the
    // bytes at that address are now the E9 jmp, not the function's
    // original prologue. Patch aborts; log line names the hook.
    PatchOverlapsEarlierHook,
};

struct Conflict {
    Category       category;
    WriteFootprint earlier;   // the entry that applies first under load order
    WriteFootprint later;     // the entry that applies later (or the verifier
                              // for WriteOnOriginal — its `kind` is the reader,
                              // `begin`/`end` describe the verify range)
    uintptr_t      overlapBegin;
    uintptr_t      overlapEnd;
};

// Engine state, populated by RunPreFlight():

// Resolved patches, parallel to patch::g_patches by index. (Moved here
// from patch_engine.cpp as part of the option-A refactor.)
extern std::vector<patch::ResolvedPatch> g_resolvedPatches;

// Resolved hooks, parallel to hook_engine::g_hooks by index. Each entry
// carries the function entry address; the hook's write footprint is
// (targetAddr, targetAddr + 5) for MinHook's rel32 jmp.
struct ResolvedHook {
    bool        ok = false;
    std::string reason;
    uintptr_t   targetAddr = 0;  // function entry — MinHook installs the jmp here
};
extern std::vector<ResolvedHook> g_resolvedHooks;

// Collected footprints. Sorted by (priority, begin) after RunPreFlight.
extern std::vector<WriteFootprint> g_writes;
extern std::vector<ReadFootprint>  g_reads;

// Detected conflicts. Ordered by earlier.priority then later.priority.
extern std::vector<Conflict> g_conflicts;

// Unified apply order across patches and hooks. Populated by RunPreFlight
// after resolution. Each EntryRef points back into either patch::g_patches
// (kind = Patch, index into g_resolvedPatches) or hook_engine::g_hooks
// (kind = Hook, index into g_resolvedHooks). The orchestrator in hooks.cpp
// walks this list and dispatches per-entry apply functions.
//
// Sort order: (priority asc, name asc) across all entry types. This means
// a high-priority patch and a low-priority hook are interleaved correctly,
// fixing the v0.1 bug where patches always applied before hooks regardless
// of priority.
enum class EntryKind { Patch, Hook };
struct EntryRef {
    EntryKind kind;
    size_t    index;  // into g_resolvedPatches or g_resolvedHooks
};
extern std::vector<EntryRef> g_applyOrder;

// Run the unified pre-flight pass:
//   1. Resolve every patch via patch::Resolve
//   2. Resolve every hook via hook target resolution (same locator pipeline)
//   3. Collect WriteFootprints + ReadFootprints
//   4. Detect pairwise conflicts; populate g_conflicts; log each
//
// Called from hooks.cpp's first-update-tick orchestration, AFTER
// trampoline_engine::ApplyAll (so the symbol table is populated and
// target_symbol-using patches can resolve), and BEFORE patch::ApplyAll
// and hook_engine::ApplyAll.
//
// Safe to call exactly once per session.
void RunPreFlight();

// Diagnostic-lookup APIs for the apply step:

// For a patch that's about to abort with a "bytes don't match" error,
// check if an earlier writer (patch or hook) is responsible. Returns
// the enriching conflict or nullptr.
const Conflict* FindWriteOnOriginalAffecting(const std::string& patchName);

// For a hook that's about to attempt install, check if an earlier hook
// already claimed the same target. Returns the enriching conflict or null.
const Conflict* FindHookOnHookAffecting(const std::string& hookName);

// For a write-on-write apply-time log: did this writer just clobber an
// earlier writer's bytes? Returns conflicts where this writer is the
// `later` party in a WriteOnWriteFull / WriteOnWritePartial conflict.
std::vector<const Conflict*> FindWriteOnWriteAffecting(const std::string& writerName);

}  // namespace kcdx::conflict_engine
