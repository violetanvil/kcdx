// === DIAGNOSTIC (PROBE S / KI-0028 HOP 3) — Draw* caller attribution ===
#pragma once

// WHY (KI-0028 differential trace, HOP 3): HOP 2 proved the indexed-capable engine
// submit fn (0x501cb0) NEVER fires swap-ON, yet draw_instanced=21011 — a DIFFERENT
// engine path issues them. _ReturnAddress at the D3D12 Draw* hooks names that path
// mechanically: a bounded unique-caller table per draw kind, module-relative RVAs,
// comparable across arms. Consumed by drawcall_probe.cpp; the offline arm diff
// compares the caller sets. NO-RESIDUE on retire (with drawcall_probe).

#include <atomic>
#include <cstdint>

namespace kcdx::fs_takeover {

constexpr int kMaxDrawCallers = 8;

// Bounded unique-caller tally. Lock-free CAS slot claim; relaxed (diagnostic
// tallies, no happens-before needed — the log line is the record).
struct DrawCallerTable {
    std::atomic<uint64_t> rva[kMaxDrawCallers] = {};
    std::atomic<uint64_t> cnt[kMaxDrawCallers] = {};
    std::atomic<uint64_t> overflow{0};  // distinct callers past the table bound
};

// Capture the WHGame base once at arm so tallied return addresses log as
// module-relative RVAs (0 / unresolved => raw addresses, still diffable per-run).
void DrawCallerTallySetBase(uintptr_t whgameBase);

// Tally one return-address. Returns the caller RVA when this call claimed a NEW
// slot (first sighting — the one discrete event worth a log line), 0 otherwise.
uint64_t TallyDrawCaller(DrawCallerTable& t, void* retAddr);

// Dump a table — event-driven (watcher end), one line per occupied slot.
void DumpDrawCallers(const char* which, DrawCallerTable& t);

}  // namespace kcdx::fs_takeover
