#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "config.h"  // for kcdx::config::Source

namespace kcdx::patch {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;  // true = literal, false = wildcard
};

// Parse "48 ?? 89 5C 24 ..." into Pattern. Throws std::runtime_error on bad input.
Pattern ParsePattern(const std::string& s);

// Parse "44 8A F0" into raw bytes. No wildcards allowed.
std::vector<uint8_t> ParseBytes(const std::string& s);

// Convenience: parse pattern + scan an executable module by name, return
// VA of first match. Returns std::nullopt for zero matches; logs how many
// matches were seen so multi-match cases are visible. Used by the Lua
// scripting layer (lua_memory::scan_pattern_from_module) for pak-mod
// memory primitives. The full patch-resolution pipeline (with anchor +
// context + uniqueness constraints) is in Resolve(), which this does NOT
// reuse — that one expects a PatchEntry, this one just needs (module, pattern).
std::optional<uintptr_t> ScanModuleFirst(const std::wstring& module_name_wide,
                                         const std::string&  pattern);

// Anchor variant types ----------------------------------------------------
struct AnchorString          { std::string literal; };
struct AnchorFunctionByExport{ std::string name; };
struct AnchorSymbol          { std::string name; };
using Anchor = std::variant<std::monostate, AnchorString, AnchorFunctionByExport, AnchorSymbol>;

struct PatchEntry {
    std::string sourceFile;   // for error messages — path of the toml that contributed this patch
    // 2-dot namespace identity of the plugin this entry belongs to:
    // `pluginAuthor` (from the owning kcdx.toml's [plugin].author) +
    // `pluginName` (from [plugin].name). Stamped by LoadOneFile (for
    // TOML [[patch]] rows) or by lua_bind_bytes (for kcdx.bytes calls),
    // alongside `source`. `pluginAuthor` may be "" during the in-progress
    // namespace refactor (legacy 1-dot scope — the plugin has not yet
    // declared [plugin].author); the symbol-table / address-library
    // resolvers tolerate an empty author by walking the legacy 1-dot
    // tier under pluginName. Engine-internal struct (not in
    // include/kcdx/Interfaces.h), so appending the new identity field is
    // unconstrained by the plugin-ABI append-only rule.
    //
    // Used by the load-order sort to look up the plugin's effective
    // zone + priority, by logs to attribute entries to their plugin,
    // and by Resolve()'s symbols::Lookup call to thread the consuming
    // plugin's full identity into the namespace resolver.
    std::string pluginAuthor;
    std::string pluginName;
    // Which discovery root this came from. Stamped by LoadOneFile
    // (Engine for kcdx-engine/builtin/, User for plugins/). Used as
    // the primary sort key so engine fixes apply before any user
    // plugin patch, regardless of numeric priority.
    kcdx::config::Source source = kcdx::config::Source::User;
    std::string name;
    std::string description;
    int priority = 100;
    std::string module = "WHGame.dll";

    // Locator. Exactly one of `pattern` / `targetSymbol` / `addressId`
    // must be set.
    //
    //   - pattern:      AOB scan of the named module (the v1 path).
    //   - targetSymbol: lookup in the cross-plugin symbol table
    //                   (resolves to a [[trampoline]] / [[hook]] export).
    //   - addressId:    lookup in kcdx's compiled-in Address Library
    //                   (kcdx::address_library::Resolve). Stable across
    //                   game patches; recommended for authors who don't
    //                   want to maintain their own AOBs.
    //
    // In every case the resolved VA gets `offset` added to produce
    // the final write target (pattern-hit semantics).
    Pattern pattern;
    std::string targetSymbol;
    uint64_t addressId = 0;          // 0 = no address-library locator

    // Pre-resolved virtual address — the opt-in resolved-VA carrier.
    //   - 0 (unset) = resolve normally via pattern / target_symbol /
    //     address_id (the path EVERY existing PatchEntry takes; behavior
    //     unchanged for them).
    //   - nonzero   = "skip locator resolution, use this VA directly":
    //     patch::Resolve treats it as the located base and adds `offset`
    //     to produce patchAddr, exactly as the other locator paths do.
    //
    // Set ONLY by the kcdx.bytes `target = "<name>"` path when the name
    // resolves to a BARE VA — an engine-seed name OR an Rva author-target —
    // i.e. the cases address_library::ResolveByName returns a nonzero VA
    // for. Pattern / TargetSymbol named targets do NOT set this; they route
    // through pattern / targetSymbol as before. This is what gives
    // kcdx.bytes{ target = "<name>" } the same WHERE-resolution kcdx.hook
    // already has, for ALL locator kinds.
    uintptr_t resolvedVa = 0;

    int offset = 0;
    std::vector<uint8_t> original;
    std::vector<uint8_t> replacement;
    bool idempotent = true;

    std::optional<Pattern> context;
    Anchor anchor;
    uint32_t maxAnchorDistance = 4096;

    // Set true by ApplyResolvedPatch on successful apply (or verified
    // idempotent-skip / dry-run). Read by GetConflictReport so test
    // plugins can verify outcomes.
    //
    // NOTE (COMP-15): ApplyResolvedPatch now sets this itself (it takes a
    // non-const PatchEntry&). The legacy g_patches callers (hooks.cpp,
    // ldr_notify.cpp) also assign it post-call; those writes are now
    // redundant-but-harmless. For the bytes-Register path
    // (ApplyBytesEntry -> ApplyPatch -> ApplyResolvedPatch) this is the
    // ONLY thing that sets appliedOK — that path previously left it at the
    // false default even on success, relying solely on the registry Status
    // atomic. Setting it here closes that gap so a bytes-Register entry's
    // appliedOK reflects reality.
    bool appliedOK = false;

    // Final post-apply write target, cached by ApplyResolvedPatch when the
    // patch is effectively applied (successful write, verified idempotent-
    // skip, or dry-run). Equals the ResolvedPatch::patchAddr (located base
    // + offset) for WHATEVER locator kind resolved this entry — target /
    // pattern / target_symbol / address_id / resolvedVa. The write range is
    // [appliedPatchAddr, appliedPatchAddr + replacement.size()).
    //
    // Stays 0 for an entry that never applied (resolve failure, byte
    // mismatch, plugin disabled). A 0 base can therefore never CONTAIN a
    // real target VA, so an unapplied entry cannot false-match a target
    // query (GetAppliedBytesPatchesAtTarget below relies on this).
    //
    // Unlike `resolvedVa` (set only by the target=/seed/Rva path, 0 for
    // pattern/target_symbol), this is populated for every applied entry
    // regardless of locator — that is the point: it gives every applied
    // bytes-Register patch a stable post-apply write-range the conflict
    // report can fold in (COMP-15). Engine-internal struct field (not in
    // include/kcdx/), so this append is unconstrained by the plugin-ABI
    // append-only rule.
    uintptr_t appliedPatchAddr = 0;
};

// Engine state — set by config.cpp at startup, read by hooks.cpp when applying.
extern std::vector<PatchEntry> g_patches;
extern bool g_dryRun;

// A contiguous byte range [begin, end) on the loaded module.
struct ByteRange {
    uintptr_t begin = 0;
    uintptr_t end   = 0;   // exclusive
    bool empty() const { return end <= begin; }
    bool overlaps(const ByteRange& o) const {
        return begin < o.end && o.begin < end;
    }
};

// Result of resolving a patch's locators against the pristine module — produced
// by the pre-flight pass and consumed by both conflict detection and the apply
// step. If `ok == false` the locators couldn't be resolved (pattern not found,
// context disagreement, anchor failure); reason holds a one-line explanation.
//
// Read ranges are kept separate by source because they have different conflict
// semantics. A write that overlaps another plugin's `pattern` or `context`
// is incidental and SAFE (the other plugin still patches whatever it intended;
// its locators were already resolved). A write that overlaps another plugin's
// `original` field is GENUINE — that plugin's verify check will fail.
struct ResolvedPatch {
    bool ok = false;
    std::string reason;              // populated when !ok
    uintptr_t patchAddr = 0;         // pattern_match + offset
    ByteRange writeRange;            // bytes ApplyPatch will overwrite
    ByteRange patternRange;          // bytes the pattern scanned
    ByteRange originalRange;         // bytes the verify check will compare (== writeRange)
    std::optional<ByteRange> contextRange;  // bytes the context pattern scanned (if any)
};

// Resolve a patch's locators against the pristine module WITHOUT writing.
// Pure read; safe to call before any patches have been applied. Used by the
// pre-flight pass and by Lua-runtime patches (which don't participate in
// pre-flight). For TOML patches, ApplyPatch uses the cached pre-flight
// ResolvedPatch instead of calling this again.
//
// Conflict types and the conflict detection pass moved to conflict_engine
// — patch_engine no longer owns those.
ResolvedPatch Resolve(const PatchEntry& p);

// Backwards-compat shim. Today this defers to conflict_engine::RunPreFlight
// for the resolution + conflict detection pipeline, but also serves as a
// fallback resolver when ApplyAll is called outside the normal orchestration
// (e.g., tests, future runtime apply paths). The hooks.cpp first-update-tick
// path calls conflict_engine::RunPreFlight directly and this shim becomes
// a no-op.
void PreFlightAll();

// Apply one TOML patch using its pre-flight resolution. Idempotent. Returns
// true on a successful write OR a verified-idempotent skip. Returns false on
// any abort condition. If pre-flight predicted a write-on-original conflict
// affecting this patch, the abort message names the upstream culprit. If
// pre-flight predicted a write-on-write conflict where this patch is the
// writer landing on previously-written bytes, that's logged at apply time.
//
// Takes a non-const PatchEntry& because on an effective apply (write,
// verified idempotent-skip, or dry-run) it caches the final write target
// onto p.appliedPatchAddr and sets p.appliedOK (COMP-15). All callers
// already pass a non-const lvalue (g_patches[i], the for(auto& p) loops in
// ldr_notify.cpp, the *PatchEntry behind the bytes-Register shared_ptr).
bool ApplyResolvedPatch(PatchEntry& p, const ResolvedPatch& r);

// Apply a single patch by re-resolving (no pre-flight context). Used by the
// Lua KCDX.ScanAndWrite runtime path and the kcdx.bytes apply handler
// (ApplyBytesEntry). Does not benefit from pre-flight's "incidental overlap"
// tolerance. Non-const for the same appliedPatchAddr/appliedOK caching as
// ApplyResolvedPatch.
bool ApplyPatch(PatchEntry& p);

// Apply all patches in g_patches. Internally calls PreFlightAll() first.
void ApplyAll();

// --- Conflict-report participation (COMP-15) ----------------------------
//
// One bytes-Register patch participating in a conflict at a target VA.
// Mirrors hook_chain::ConflictParticipant {name, priority, applied} so
// GetConflictReport (interfaces.cpp) can fold these into its existing
// per-target hit list exactly as it folds the hook_chain participants
// (COMP-14). `applied` is the entry's PatchEntry::appliedOK (true = applied
// or idempotent-skipped; false = a resolved-but-failed apply).
//
// `name` points into the PatchEntry's `name` std::string, which lives in
// the lua_registry Entry's payload (shared_ptr<PatchEntry>) for the process
// lifetime — registry entries are append-only and never destroyed (see
// lua_registry.cpp g_entries std::deque). The pointer is valid for as long
// as the caller could hold it. Mirrors the lifetime guarantee on
// hook_chain::ConflictParticipant.
struct AppliedBytesPatch {
    const char* name;
    int         priority;
    bool        applied;
};

// All kcdx.bytes (Kind::Bytes) registrations whose APPLIED write range
// [appliedPatchAddr, appliedPatchAddr + replacement.size()) contains
// `targetVa`. Walks the lua_registry Kind::Bytes entries (via
// lua_registry::ForEachEntryOfKind), casts each type-erased payload to
// PatchEntry (the patch engine owns PatchEntry; the registry stays payload-
// agnostic; interfaces.cpp stays payload-blind — DECISION A), and yields a
// participant per containing entry.
//
// An entry that never applied has appliedPatchAddr == 0, so its range can't
// contain any real targetVa and it is naturally skipped — mirroring the
// legacy g_patches report loop's skip of resolve-failures (!r.ok). Returns
// empty when no bytes-Register patch's applied range covers `targetVa`.
//
// Locking: the registry enumerator takes the registry's own mutex for the
// walk (same discipline as hook_chain::GetParticipantsAtTarget taking
// g_chainsMu) — the report can run concurrently with the first-tick apply
// pass. Returns by value; the `name` pointers remain valid for the process
// lifetime (see AppliedBytesPatch).
std::vector<AppliedBytesPatch> GetAppliedBytesPatchesAtTarget(uintptr_t targetVa);

// Find every byte-offset within [data, data+size) where the pattern matches.
std::vector<size_t> FindAllInBuffer(const uint8_t* data, size_t size, const Pattern& pat);

}  // namespace kcdx::patch
