#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kcdx::trampoline_engine {

// One [[trampoline]] entry parsed from a kcdx.toml file. Unlike [[patch]]
// and [[hook]], [[trampoline]] doesn't target an address in WHGame.dll —
// it allocates fresh executable memory from kcdx's trampoline pools and
// fills it with the supplied bytes. Optional `export` registers the
// allocated address as a named symbol other plugins can resolve via
// target_symbol.
struct TrampolineEntry {
    std::string sourceFile;
    std::string name;
    std::string description;
    int         priority = 100;

    // Raw bytes the engine copies into the allocated region.
    std::vector<uint8_t> bytes;

    // Optional override for total alloc size. Defaults to bytes.size().
    // If larger than bytes.size(), trailing bytes are NOP-padded so other
    // plugins can patch into the unused tail via target_symbol.
    std::optional<size_t> size;

    // Optional pool selector. "branch" (default) places within +/-2 GB of
    // WHGame.dll's .text — required if any plugin will rel32-jump into
    // this trampoline. "local" places anywhere via VirtualAlloc; use when
    // proximity isn't needed.
    std::string pool = "branch";

    // Optional symbol name. If non-empty, the allocated address is
    // registered in the global symbol table after the trampoline is filled.
    // Other plugins can then resolve `target_symbol = "..."` against it.
    std::string exportSymbol;
};

extern std::vector<TrampolineEntry> g_trampolines;

// Resolved allocation result for one entry, populated by ApplyAll.
struct AppliedTrampoline {
    uintptr_t allocatedAddr = 0;
    size_t    allocatedSize = 0;
    std::string name;
};

// Allocate every [[trampoline]] entry's region, copy bytes in, NOP-pad the
// tail, register exports in the symbol table. Called from HookedUpdate
// once at startup, BEFORE hook_engine::ApplyAll and patch::ApplyAll's
// symbol-import pass (those need the symbol table populated first).
//
// Returns the number of trampolines successfully allocated.
size_t ApplyAll();

}  // namespace kcdx::trampoline_engine
