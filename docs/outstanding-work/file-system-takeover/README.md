# file-system-takeover

**Active, in progress.** kcdx takes total ownership of the engine's `CCryPak`
file object — every file call dispatches into kcdx, every handle is operated on
kcdx's own CRT — eliminating the cross-CRT crash class (KI-0019/KI-0006) and
reading both vanilla paks and loose mod files itself.

Settled design: [`docs/design/file-system-takeover.md`](../../design/file-system-takeover.md) (`29a21c5`).
Shared spec + coverage map: [`plan-spec.md`](plan-spec.md).

> **Next: Phase 3 step 3.2 — the open+read cutover. Exact pickup in [`RESUME.md`](RESUME.md).**
> KI-0025 (the reference-DB integrity defect that blocked the slot-35 seed add)
> is CLOSED; the slot-35 row (kcdx_id 160) is seeded and ready. The plan is
> updated: 3.2+3.3 merged into one atomic open+read cutover (open 1/35/36 + read
> 38/39/40/41/53/54/55/56 flip together, the cross-CRT class dies here); slot 38
> reclassified to the read family (recon `4ca0bae`); downstream steps renumbered
> (existence/enum 3.3, mgmt+table 3.4, seam 3.5). Read `RESUME.md` before resuming.

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
