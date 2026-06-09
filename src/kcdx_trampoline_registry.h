#pragma once
// kcdx_trampoline_registry — the authoritative record of every memory range
// kcdx ITSELF allocated as a detour trampoline / relocated-original / detour
// body.
//
// One responsibility: answer "does this address fall inside a kcdx-owned
// trampoline range?" for the foreign-hook classifier (foreign_hook_detect).
// The classifier reads a target's prologue jump and must distinguish a jump
// into a kcdx trampoline (already in a kcdx chain) from a jump into another
// mod's detour (foreign). The discriminator is kcdx's OWN records, because
// safetyhook's Allocator does NOT expose its ranges (m_memory is private;
// SOURCE: vendor/safetyhook/include/safetyhook/allocator.hpp, read this
// session) — kcdx cannot ask safetyhook "is this yours," so kcdx tracks what
// it allocated itself.
//
// WHY a central registry fed at install (not a classify-time walk of the three
// per-hook record stores): the safetyhook InlineHook / MidHook trampoline
// addresses are only knowable AFTER create() (they come from
// hook.trampoline().address()/.size()), and they live inside the per-backend /
// per-slot objects scattered across hook_engine + safetyhook_midhook. A single
// registry the install path feeds gives the classifier ONE O(N) range lookup
// over a flat list, instead of reaching into three subsystems' private state
// each classify. Each producer registers its range right after its trampoline
// is allocated; the classifier reads the merged list.
//
// Append-only, session-lifetime: kcdx never unhooks (SKSE "no FreeLibrary, no
// teardown"), so a range, once registered, is valid for the whole session and
// is never removed.

#include <cstddef>
#include <cstdint>

namespace kcdx::kcdx_trampoline_registry {

// What a kcdx-owned range IS — for the log line that names a foreign vs
// kcdx-trampoline classification, and for a future audit.
enum class Kind {
    SafetyhookInline,   // a SafetyhookBackend's InlineHook relocated-original trampoline
    SafetyhookMid,      // a safetyhook_midhook MidHook trampoline
    BranchPool,         // a trampoline::AllocateBranch reservation (detour bodies live here)
};

// Register a [base, base+size) range kcdx allocated. Thread-safe; called from
// the install path right after the trampoline is allocated. A zero size or
// null base is ignored (a no-op — a failed allocation registers nothing).
void Register(uintptr_t base, size_t size, Kind kind);

// Is `addr` inside any registered kcdx-owned range? Thread-safe; the classifier
// calls this for a decoded prologue-jump target. Returns true iff addr falls in
// [base, base+size) of some registered range.
bool Contains(uintptr_t addr);

// Count of registered ranges — for the selftest + diagnostics only.
size_t Count();

}  // namespace kcdx::kcdx_trampoline_registry
