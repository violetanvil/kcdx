# file-system-takeover

**Active, in progress.** kcdx takes total ownership of the engine's `CCryPak`
file object — every file call dispatches into kcdx, every handle is operated on
kcdx's own CRT — eliminating the cross-CRT crash class (KI-0019/KI-0006) and
reading both vanilla paks and loose mod files itself.

Settled design: [`docs/design/file-system-takeover.md`](../../design/file-system-takeover.md) (`29a21c5`).
Shared spec + coverage map: [`plan-spec.md`](plan-spec.md).

> **PAUSED at Phase 3 step 3.2 — exact pickup point in [`RESUME.md`](RESUME.md).**
> Blocked on a pre-existing reference-DB integrity defect ([KI-0025](../../known-issues/KI-0025-refdb-dangling-survival-derives-from-kcdx-id-12.md)) that
> stops the slot-35 seed add. Decisions already made: merge 3.2+3.3 into one
> open+read cutover; slot map reconciled (`4ca0bae`); slot 38 is a READ slot.
> Read `RESUME.md` before resuming.

## Phase-grain status ledger

The canonical completion surface (`.claude/rules/doc-organization.md`). One row
per phase; flips to `DONE` when its last step lands. `/plan` authored all rows
`NOT STARTED`; the orchestrator writes transitions.

| Step | Status | Commit |
|---|---|---|
| Phase 1 — seating spike + in-flight cleanup ([phase-01](phase-01-seating-spike/README.md)) | NOT STARTED | — |
| Phase 2 — pak reader + unified index ([phase-02](phase-02-pak-reader-index/README.md)) | DONE | 63daea9 |
| Phase 3 — real file slots + seam subsumption ([phase-03](phase-03-file-slots/README.md)) | NOT STARTED | — |
| Phase 4 — verification + closure ([phase-04](phase-04-verification-closure/README.md)) | NOT STARTED | — |

## Phases at a glance

- **Phase 1** proves the load-bearing seating mechanism (P1 ctor timing, P2 swap
  acceptance, P4 thunk-compat) on a cheap reversible stub vtable, and clears the
  in-flight residue (KI routing, PROBE F) — before any large build.
- **Phase 2** builds kcdx's own pak reader (PKZIP/DEFLATE, own CRT) + the unified
  asset index — the byte-source layer, no engine ZipDir.
- **Phase 3** replaces the stub with the full kcdx file-slot family + settles the
  handle representation (P3), then subsumes the live `asset_overlay.cpp` seam.
- **Phase 4** ships the regression tests + subsystem doc and closes
  KI-0019/KI-0006 with a repro-clean launch.
