# Phase 1 step 4 — HOOK 2: return kcdx's own CRT `FILE*` for the loose open

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 4.

## What

HOOK 2 of the two-hook seam (design §7). When HOOK 1's resolution decides a
declared loose overlay wins, kcdx must SERVE that file's bytes — and it does so by
opening the loose file ITSELF and returning its OWN CRT `FILE*`, rather than
relying on the engine's loose-search finding it. The gated FOpen-handle finding
(`e6e8e27`) proved FRead's OS arm serves any `FILE*` whose `handle−1` exceeds the
pak-handle count (a real heap `FILE*` always does), so a kcdx-opened
`_wfopen`/`fopen` handle returned from the seam is read by the engine's unmodified
read family. This is the hook that serves **add-new** assets and the loose side of
replace, for every asset class, without depending on the engine's loose-search
(the layer the prior path-redirect FAILED at). Settled (design §12): return-our-own-
`FILE*` over a path-only redirect, so kcdx is never barred by a vanilla-engine
loose-search unknown.

## Scope

- `src/asset_overlay.cpp` (extending HOOK 1's seam): on a declared-overlay HIT for
  a loose file, kcdx opens the file (CRT `_wfopen`/`fopen` on the plugin's
  `assets/` path) and the seam returns that `FILE*` as the resolved handle, threaded
  back through the resolver/FOpen `rv` slot via the hook chain. Handle lifecycle
  (close) follows the engine's normal `FClose` on the returned handle (the read
  family + FClose dispatch on the handle tag — touch nothing there).
- Install alongside HOOK 1 in the ready-bracket window (both hooks live before the
  first asset read, design §8).
- The cFn-ABI pointer-return (a `FILE*` into the `rv` slot through the kcdx hook
  chain) is the build mechanic to confirm here (design §9 item 3) — verify the
  chain threads a pointer-width return cleanly; surface if it does not.
- Allocation-light on the HIT path is less critical than HOOK 1's MISS path (a HIT
  is a declared override, rare), but the open is a one-shot per file open, not
  per-read (`memory.md`).

## Test bar

A behavior step proven LIVE end-to-end, BOTH lanes (the runtime acceptance of the
gate-verified static mechanism): (a) a **handle-consumed** `.lua`/`.xml` declared
overlay served from the plugin's `assets/` dir reads kcdx's bytes in-game (the
overlaid script's effect, or a logged read-back marker — the class the prior
path-redirect FAILED to serve); and (b) a **memory-mapped** `.dds` overlay still
serves (the already-live-verified case, now through the two-hook seam). Build green.
The cFn pointer-return confirmed clean (no crash, the handle reads). The probe
findings captured to `_research/`; any in-source diagnostic removed. Permanent
regression row is step 10.

## Dependencies

**Step 3** (HOOK 1 — the resolver decision that routes a HIT to this open; HOOK 2
serves what HOOK 1 decided). Ordered immediately after HOOK 1 so the two hooks form
the complete seam and the end-to-end serve is verifiable when this step lands
(`incremental-delivery.md`). HOOK 1 alone decided-but-didn't-serve; this step
completes the serve.

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§7 (HOOK 2 — the own-`FILE*` open) + §9 (the cFn-ABI pointer-return) + §12 (the
own-`FILE*`-vs-path-redirect decision). Shared spec: [`../plan-spec.md`](../plan-spec.md).
Gated mechanism: `_research/asset-fopen-handle-recon/FINDINGS.md`.

## Disassembler-test / author-burden

The hook is engine-internal — the author drops a loose file in `assets/` and never
sees the handle mechanism or the asset class (design §4.3, the disassembler test).
No author-facing input, no hand-written hex/ABI.
