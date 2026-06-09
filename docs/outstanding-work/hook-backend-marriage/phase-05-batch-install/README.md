# Phase 5 — batch install (CLOSED as a design correction — no batch)

**Outcome.** Phase 5 closed not by building a batch path but by DROPPING it on
measured evidence. The phase's whole premise — that `safetyhook::InlineHook::enable()`
suspends all threads per hook, so N installs should be collapsed into one
stop-the-world window — was FALSE for this safetyhook build. Opening the phase,
the U7 probe was resolved by a static read of the vendored source:
`trap_threads` (`vendor/safetyhook/src/os.windows.cpp:268-318`) is **VEH +
`VirtualProtect`, not a thread-suspend** (zero `SuspendThread` in the entire
vendored tree). There is no stop-the-world cost to amortize; the only safe
multi-target window-collapse saves ~0 (scattered `VirtualProtect`s on different
prologue pages can't coalesce) and widens the mid-prologue safety window; and
MinHook's real `MH_ApplyQueued` batch is immaterial at its tiny N≈1-4
(loader-lock/bootstrap paths only). Per-hook `enable()` is the measured-correct
path. The finding is the design's institutional-memory record so the question is
not re-investigated.

This was the user-approved Phase-5 close (a clean drop + a `/design` correction,
over keeping a non-delivering batch API for "future-proofing"). The batch
mechanism (former step 9) and its N-hook fixture (former step 10) are RETIRED —
not built, not deferred. `IDetourBackend` has no batch entry point.

**The marriage is functionally complete after this phase.** Phases 1–4 (the
InstallRuntime backend seam, function-entry on safetyhook, the mid-hook
retirement, foreign-hook coexistence — all live-verified) deliver the marriage's
value; Phase 5 was a Performance add-on that the measurement showed delivers
nothing. Only the backend reference doc (Phase 6) remains.

Design authority: [`hook-backend-marriage.md §4.5 "No batch install — why"`](../../../design/hook-backend-marriage.md)
+ the measured cost record `_research/batch-install-cost/FINDINGS.md`.

Shared spec: [`../context.md`](../context.md) (design elements E23–E26 → DROPPED;
U7 → RESOLVED).

## Status ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| Step 9 — batch-install mechanism | RETIRED (premise falsified — no batch built) | `03c934c` (design drop) |
| Step 10 — comp-NN N-hook batch fixture | RETIRED (no batch path to exercise) | `03c934c` (design drop) |

## Verification gate

No build gate — Phase 5 produced no code. The phase is verified by the design
correction: the U7 unknown is resolved (static source read, architect-verified),
the false premise is corrected in design §4.5/§7/§9.7, and the full cap-NN suite
is unaffected (no Phase-5 mechanism touches it). The measured cost evidence is
captured in `_research/batch-install-cost/FINDINGS.md`.
