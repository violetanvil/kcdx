# Phase 3 — real file slots + seam subsumption

Replace the Phase-1 stub vtable with the full kcdx file-slot family — resolution,
open, read, existence, metadata, enumeration, pak-management, search-path/alias,
delete/copy — all reading through the Phase-2 index + reader, all operating
handles on kcdx's CRT. Then subsume the live `asset_overlay.cpp` two-hook seam
(its replacement is now live in the kcdx slots). The cross-CRT crash class dies in
step 3.2 (the open+read cutover — kcdx mints the handle-ids AND owns the read
family that operates them, in one atomic flip).

Depends on Phase 1 (seating proven) + Phase 2 (the reader + index exist). Step 3.1
(P3) precedes the open+read cutover that rests on the handle representation it
settles. **Open and read flip in ONE step (3.2)** — a kcdx handle-id is operable
only by kcdx's own read slots, so the read family cannot stay thunked while the
open slots mint handle-ids (it would `fread` a handle-id on the engine's CRT — the
cross-CRT straddle). The original 3.2-mints / 3.3-reads split was merged for this
reason (user-decided).

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 — probe P3 (off-vtable raw-handle access) + handle rep](step-1-probe-handle-rep.md) | DONE | 4f2c32d |
| [3.2 — open + read cutover (slot 1/35/36 open + 38/39/40/41/53/54/55/56 read, flipped together) + wire BuildAssetIndex](step-2-open-read-cutover.md) | DONE | 842e5d5 |
| [3.3 — existence/metadata + enumeration slots](step-3-existence-enum-slots.md) | DONE | (landed) |
| [3.4 — pak-mgmt/search-path/delete + finalize table](step-4-mgmt-slots-table.md) | BLOCKED | — |
| [3.5 — subsume the asset_overlay.cpp seam](step-5-subsume-overlay-seam.md) | NOT STARTED | — |

## Verification gate (phase done when)

- 3.1 (P3): the off-vtable raw-handle question is settled (no off-vtable access →
  a kcdx handle-id is safe; off-vtable access found → kcdx's handle is a
  FILE*-shaped object) — agent-read from the probe log; the handle representation
  is decided before 3.2. **DONE** (`4f2c32d`).
- 3.2 (the open+read cutover): builds green; `BuildAssetIndex` is wired into the
  seating path; the cap-113 regression plugin's full open→read→close on BOTH a
  Loose and a Pak source passes; and **the KI-0019 repro (load save → enter world
  → open inventory) runs clean** — the read family is kcdx's, no engine `ucrtbase`
  operates a kcdx handle (the cross-CRT class structurally removed). Agent-read
  from `kcdx-dev.log`. This is the gate that closes the cross-CRT class
  (KI-0019/KI-0006 closure carried to 4.2).
- 3.2–3.4: each slot family builds green + its regression sub-test passes; the
  per-slot table is the single point of slot ownership (reviewed — no code outside
  it assumes a slot's owner). A launch confirms a vanilla asset + a loose override
  both serve through the kcdx slots.
- 3.5: HOOK 1 + HOOK 2 + the overlay-map globals are removed from
  `src/asset_overlay.cpp`; a launch confirms asset serving is unchanged (the kcdx
  slots carry it). No coverage gap — the seam is removed only after its
  replacement is live.
