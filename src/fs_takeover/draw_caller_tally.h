// === DIAGNOSTIC (PROBE S / KI-0028 HOP 3) — Draw* caller attribution ===
#pragma once

// WHY (KI-0028 differential trace, HOP 3): HOP 2 proved the indexed-capable engine
// submit fn (0x501cb0) NEVER fires swap-ON, yet draw_instanced=21011 — a DIFFERENT
// path issues them. _ReturnAddress at the D3D12 Draw* hooks names that path
// mechanically: a bounded unique-caller table per draw kind. The swap-ON run showed
// the sole caller is OUTSIDE WHGame (raw-minus-whbase ~0xA80557A6, VA 0x7FFE...),
// so each first-seen caller ALSO resolves its owning MODULE (basename + module-
// relative offset) — attribution survives ASLR across runs. Consumed by
// drawcall_probe.cpp; the offline arm diff compares the caller sets.
// NO-RESIDUE on retire (with drawcall_probe).

#include <atomic>
#include <cstdint>

namespace kcdx::fs_takeover {

constexpr int kMaxDrawCallers = 8;
constexpr int kDrawCallerNameLen = 32;

// Bounded unique-caller tally. Lock-free CAS slot claim; relaxed (diagnostic
// tallies). The claiming thread resolves + writes the module fields, then
// publishes them via `named` (release); readers acquire before reading them.
struct DrawCallerTable {
    std::atomic<uint64_t> rva[kMaxDrawCallers] = {};   // vs WHGame base (raw if below)
    std::atomic<uint64_t> cnt[kMaxDrawCallers] = {};
    std::atomic<bool>     named[kMaxDrawCallers] = {}; // module fields published
    uint64_t              modOff[kMaxDrawCallers] = {};
    char                  modName[kMaxDrawCallers][kDrawCallerNameLen] = {};
    std::atomic<uint64_t> overflow{0};  // distinct callers past the table bound
};

// Capture the WHGame base once at arm so tallied return addresses log as
// WHGame-relative RVAs where applicable (module resolution is separate).
void DrawCallerTallySetBase(uintptr_t whgameBase);

// Tally one return-address. On the FIRST sighting of a caller, resolves its owning
// module (cold, <= kMaxDrawCallers times per run) and logs one
// `draw_caller_first_seen` line (module + module-relative offset + whgame-rva).
void TallyDrawCaller(DrawCallerTable& t, const char* which, void* retAddr);

// Dump a table — event-driven (periodic watcher iteration + watcher end), one line
// per occupied slot, carrying module attribution.
void DumpDrawCallers(const char* which, DrawCallerTable& t);

}  // namespace kcdx::fs_takeover
