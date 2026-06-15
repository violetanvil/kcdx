# Phase 3 — real file slots + seam subsumption

Replace the Phase-1 stub vtable with the full kcdx file-slot family — resolution,
open, read, existence, metadata, enumeration, pak-management, search-path/alias,
delete/copy — all reading through the Phase-2 index + reader, all operating
handles on kcdx's CRT. Then subsume the live `asset_overlay.cpp` two-hook seam
(its replacement is now live in the kcdx slots). The cross-CRT crash class dies in
step 3.3 (kcdx owns the read family).

Depends on Phase 1 (seating proven) + Phase 2 (the reader + index exist). Step 3.1
(P3) precedes the open/read slots that rest on the handle representation it
settles.

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 — probe P3 (off-vtable raw-handle access) + handle rep](step-1-probe-handle-rep.md) | DONE | 4f2c32d |
| [3.2 — slot-1 AdjustFileName + open slots](step-2-resolution-open-slots.md) | NOT STARTED | — |
| [3.3 — read family on kcdx CRT](step-3-read-family.md) | NOT STARTED | — |
| [3.4 — existence/metadata + enumeration slots](step-4-existence-enum-slots.md) | NOT STARTED | — |
| [3.5 — pak-mgmt/search-path/delete + finalize table](step-5-mgmt-slots-table.md) | NOT STARTED | — |
| [3.6 — subsume the asset_overlay.cpp seam](step-6-subsume-overlay-seam.md) | NOT STARTED | — |

## Verification gate (phase done when)

- 3.1 (P3): the off-vtable raw-handle question is settled (no off-vtable access →
  a kcdx handle-id is safe; off-vtable access found → kcdx's handle is a
  FILE*-shaped object) — agent-read from the probe log; the handle representation
  is decided before 3.2.
- 3.2–3.5: each slot family builds green + its regression sub-test passes; the
  per-slot table is the single point of slot ownership (reviewed — no code outside
  it assumes a slot's owner). A launch confirms a vanilla asset + a loose override
  both serve through the kcdx slots.
- 3.3: the KI-0019 repro (load save → enter world → open inventory) runs clean —
  the read family is kcdx's, no engine `ucrtbase` operates a kcdx handle (the
  cross-CRT class structurally removed). Agent-read from `kcdx-dev.log`.
- 3.6: HOOK 1 + HOOK 2 + the overlay-map globals are removed from
  `src/asset_overlay.cpp`; a launch confirms asset serving is unchanged (the kcdx
  slots carry it). No coverage gap — the seam is removed only after its
  replacement is live.
