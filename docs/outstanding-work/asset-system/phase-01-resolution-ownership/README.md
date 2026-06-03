# Phase 1 — resolution ownership (the TWO-hook seam, probe-gated)

kcdx takes ownership of asset resolution with TWO coordinated hooks (design §7):
HOOK 1 replaces `CCryPak::AdjustFileName` (slot 1, id 152) for the resolution
DECISION (which file wins, all classes + both byte-lanes, above the
`sys_pakPriority` gate); HOOK 2 returns kcdx's own CRT `FILE*` for the loose OPEN
(serving the loose file without depending on the engine's loose-search). The phase
is probe-gated: the seam-install ordering and the DirectStorage bypass are checkable
unknowns settled by a probe before the dependent code is built
(`.claude/rules/results-driven.md`, `incremental-delivery.md`).

## Step ledger

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash
when `DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — probe: seam-install ordering (`HookedCtor` vs first `FOpen`, sequence-counter)](step-1-probe-install-ordering.md) | DONE | (landed) |
| [2 — probe: does DirectStorage bypass the seam for textures?](step-2-probe-directstorage-bypass.md) | NOT STARTED | — |
| [3 — HOOK 1: REPLACE `AdjustFileName` (the resolution decision; remove FOpen/SEAM-A residue)](step-3-hook1-adjustfilename-decision.md) | NOT STARTED | — |
| [4 — HOOK 2: return kcdx's own CRT `FILE*` for the loose open](step-4-hook2-own-filehandle-open.md) | NOT STARTED | — |
| [5 — sidecar declarative model + load-order conflict report](step-5-sidecar-model.md) | NOT STARTED | — |

## Phase verification gate

- **Build green** (`pwsh ./build.ps1` → the three artifacts) after every step.
- **Steps 1–2** are probes — gate is each pre-committed outcome→meaning map read
  against the live log (the ordering fact captured at s1; the DS-bypass fact at s2;
  a falsifying outcome surfaced, not silently worked around).
- **Step 3 (HOOK 1)** verified by the resolution DECISION being observable: a
  declared overlay is CHOSEN (logged as the winning resolution) for a vanilla
  path, the MISS path falls through to the engine leaves (stock resolution
  byte-identical), and the dead FOpen/SEAM-A residue is gone from
  `src/asset_overlay.{h,cpp}`.
- **Step 4 (HOOK 2)** verified by a declared overlay's BYTES served end-to-end
  in-game — both lanes: a handle-consumed `.lua`/`.xml` served via the own-`FILE*`
  open, AND a vanilla (pak-resident) replace served via HOOK 1's redirect reaching
  the pak/mount lane.
- **Step 5** verified by a TOML-only declarative replacement applying in-game, a
  missing-target sidecar failing LOUD (AP14), and the load-order conflict report
  line for two declarations of the same target.
- Phase done when overlay add-new (loose lane) AND replace-vanilla (pak/mount lane)
  work in-game and a stock pak still resolves unchanged (US-7, fully proven in P3).
