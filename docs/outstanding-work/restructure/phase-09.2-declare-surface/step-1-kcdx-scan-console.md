# Phase 9.2 step 1 — `kcdx_scan` console command

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → "kcdx_scan console command". Closes Phase 9.2.

One `/execute` cycle. Source work-item for the cycle:
`docs/outstanding-work/restructure/phase-09.2-declare-surface/step-1-kcdx-scan-console.md → README.md "kcdx_scan console command"`.

## What

Ship the `kcdx_scan` **console command** — the in-game iterative AOB-discovery
verb. It is the author-facing console form distinct from the already-shipped
`kcdx.scan{...}` Lua diagnostic: it drives the discover-then-declare loop
in-game, so an author probing a pattern at runtime narrows it interactively and
then hands the result to `kcdx.declare`.

## Why

The discover-then-declare loop is gated behind it — without the console verb the
author can run the Lua diagnostic but cannot iterate on a pattern live at the `~`
console. It is the lone residual keeping Phase 9.2 from `DONE`.

## Scope (settled)

- Register a `kcdx_scan` console command through the proven console path
  (`IConsole::AddCommand`, vtable[33] per the Phase 7 console gotcha — verify
  against the binary, do not assume the index).
- The command invokes the same `scan_engine::ResolveScan` path the
  `kcdx.scan{...}` Lua diagnostic already uses (per-match module attribution).
  Reuse the shared `RunScan(ScanEntry) -> ScanResult` resolver — do not fork a
  second scan path.
- Output formats matches for console display: per-match module + offset + the
  surrounding bytes, with a count and a loud "refine your pattern" line when the
  match count exceeds a readable cap.

## Test bar

A suite-gated test plugin (or sub-test on an existing scan plugin) that drives
`kcdx_scan` with a known pattern and asserts the resolved match set equals the
`kcdx.scan{...}` Lua diagnostic's result for the same pattern (the two surfaces
of one capability — console + Lua — both under permanent regression). Declare the
test mode (`console` — the command string + the falsifiable observable in the
dev log).

## Files (sketch — the design step within the cycle settles exact shapes)

- the console-command registration site (alongside the existing `kcdx_*` console
  commands)
- the scan-resolver call into `scan_engine` (reuse, do not duplicate)
- the test plugin / sub-test + its `../../../../test-plugins/README.md` matrix row

## Reference

Full original spec: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.2"
→ "Scan workflow — `kcdx_scan` console command + the discover-then-declare loop".
