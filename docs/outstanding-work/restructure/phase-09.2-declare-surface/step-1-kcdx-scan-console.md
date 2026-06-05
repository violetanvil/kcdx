# Phase 9.2 step 1 — `kcdx_scan` console command

**Status: DONE** — shipped as **CAP-70**, live-verified PASS (kcdx-dev 2026-06-01
19:52, all three sub-tests). Ledger row: [`README.md`](README.md) → "kcdx_scan
console command". Closes Phase 9.2.

This step doc was authored by the `3ee79ee` restructure-tree split AFTER the
capability had already shipped, so it was created reading `NOT STARTED` and never
reconciled against the as-built command — pure tracking drift, not unbuilt work.
The capability is live: the engine-owned `kcdx_scan` `~`-console command is in
`src/console_commands_scan.cpp`, registered unconditionally at `console::Init`
(`src/hooks.cpp:451` → `console_commands_scan::Register()`), built
(`CMakeLists.txt:148`), and exercised by `test-plugins/cap-70-scan-console/`. No
`/execute` cycle is owed — the work below was satisfied on landing CAP-70.

## What

Ship the `kcdx_scan` **console command** — the in-game iterative AOB-discovery
verb. It is the author-facing console form distinct from the already-shipped
`kcdx.scan{...}` Lua diagnostic: it drives the discover-then-declare loop
in-game, so an author probing a pattern at runtime narrows it interactively and
then hands the result to `kcdx.declare`.

## Why

The discover-then-declare loop is gated behind it — without the console verb the
author can run the Lua diagnostic but cannot iterate on a pattern live at the `~`
console. It was the lone residual keeping Phase 9.2 from `DONE`; it shipped as
CAP-70 (below) and 9.2 is now `DONE`.

## As built (CAP-70)

The shipped command matches the settled scope's intent with two surface
specifics that landed differently than the sketch below — recorded here so the
spec stays the record of intent and this note is the as-built truth:

- **Registration path** — registers through the kcdx console interface wrapper
  (`console::GetInterface()->RegisterCommand(...)` in
  `src/console_commands_scan.cpp`), the engine-owned-command path, NOT a raw
  `IConsole::AddCommand` vtable call. The Phase-7 console-slot fact still holds
  underneath the wrapper; the command does not re-derive the slot itself.
- **Console output** — prints `[scan] <N> matches:` + one `<module>+0x<relOffset>`
  line per match, capped at 16 printed lines with a `... and <K> more` overflow
  line (the readable-cap guard). It does NOT paint the surrounding bytes to the
  overlay (the sketch's "surrounding bytes" landed only on the dormant
  `scan_engine` RunOne dev-log path, not the console overlay).
- **Resolver reuse** — invokes `scan_engine::RunScan` (which wraps
  `ResolveScan`), the exact shared path `kcdx.scan{...}` uses — no forked scan
  path, as the scope required.
- **Test** — `test-plugins/cap-70-scan-console/` (pure-Lua, drives the command
  via `kcdx.console.execute`): CAP-70-dispatch + CAP-70-badargv (`console` mode)
  + CAP-70-result (`boot-only`, asserts the resolve FINDS the verified
  deep-interior site over the shared `RunScan` path). All three live-verified
  PASS (kcdx-dev 2026-06-01 19:52).

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
