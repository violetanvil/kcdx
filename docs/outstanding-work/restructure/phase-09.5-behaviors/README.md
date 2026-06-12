# Phase 9.5 — `kcdx.behavior.*` named-behavior catalog (two-tier)

**Status: IN PROGRESS.** The simple-modder surface: one line, never a function
name, statement, op, or address. Two tiers (engine catalog + plugin-declared)
through one model and one code path.

- **Settled design:** [`behavior-design.md`](behavior-design.md) (committed
  `94668ea`, soundness + fidelity gated) — the build authority every step
  back-pointers.
- **Shared spec + coverage map:** [`plan-spec.md`](plan-spec.md).
- Supersedes the pre-design 3-step ledger (the old step docs were removed when
  this tree was authored; detail lived in
  [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.5", now superseded
  by the design doc).

## Phase ledger (phase-grain)

Status: `NOT STARTED` · `IN PROGRESS` · `BLOCKED` · `DONE` · `NEEDS REWORK`.
Commit = short hash when `DONE`, `—` otherwise. A landed step flips its row in
its phase README; the last step of a phase flips that phase's row here; 9.5's
completion flips the restructure top-ledger row (the orchestrator owns the
cascade).

| Phase | Status | Commit |
|---|---|---|
| [1 — core surface (probe · registry · boundary · window law · toggle · edges · auto-order)](phase-01-core-surface/README.md) | IN PROGRESS | s1 736481d · s2 7080dd0 · s3 08e2f2a · s4 5295397 · s5 1176742 · s6 eb57d1d |
| [2 — C++ parity + the command queue](phase-02-cpp-parity/README.md) | NOT STARTED | — |
| [3 — the engine catalog](phase-03-catalog/README.md) | NOT STARTED | — |

## Build order rationale

Dependency-topological (`.claude/rules/incremental-delivery.md`): P1 s1 discharges
the design's four marked runtime assumptions before anything builds on them
(`.claude/rules/results-driven.md`); the registry and Lua surface stand up the one
model both tiers share; edges + auto-order complete the ordering story; C++ parity
(P2) mirrors a proven surface; the catalog (P3) loads through machinery P1/P2
proved. Each phase ends buildable; each step is independently verifiable when it
lands.
