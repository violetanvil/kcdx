# P5 step 5 — the `lua_before` early slot + the worker before-game runner

## What

Build the before_game early-slot phase: a new `lua_before` `[entrypoints]` key a
`before_game`-zoned plugin declares, run on the worker (against the fully-initialized
kcdx from step 2) at the before-game window, before the engine's boot-asset open. The
worker before-game runner is the new coordinator unit — it runs each `lua_before`
entrypoint + signals the Phase-4 event gate. An out-of-window call (a verb needing the
LIVE GAME) fails LOUD (US-7).

## Scope

- `lua_before` `[entrypoints]` key in `src/config.cpp` (the allowlist + parser,
  string-or-array), mirroring the existing `lua_after`; `luaBeforeEntrypointsRel` in
  `src/plugin_loader.h` (`PluginManifest`), mirroring `luaAfterEntrypointsRel`.
- The worker before-game runner (in `src/lua_vm_build.cpp` / `src/dllmain.cpp`,
  post-VM-publish, after kcdx-subsystems-ready): for each `before_game`-zoned plugin,
  run its `lua_before` entrypoint on the published VM via the existing
  `RunOneEntrypointFile` (`src/lua_plugin_loader.cpp` — SEH-guard, owner-attribution,
  reused), then `AdvanceTo` the before-game early-slot phase + SIGNAL the event gate.
- The early-bind surface: bind the kcdx subsystems' author surfaces the early slot
  needs on the worker before the slot runs (needs-only-kcdx vs needs-the-live-game —
  design §7.3; the subsystems are up from step 2).
- **US-7 — out-of-window fails loud:** a verb that needs the LIVE GAME called from
  `lua_before` fails with a structured teaching error (AP14, `.claude/rules/logging.md`)
  naming the constraint + the phase to use — never a silent no-op.
- The cross-thread effect (a `kcdx.assets.register` the boot open must see) is ordered
  by the Phase-4 event gate (the slot signals; the boot-open path waits-and-blocks) —
  the gate is the Phase-4 foundation, reused.
- Docs: `lua_before` gets its `docs/lua/` entry + a `before_game` early-slot glossary
  term; the C++ before-game entry's mirror entry is NYI here (built in step 7).

## Test bar

A `test-plugins/cap-NN-lua-before/` (suite-gated): a `before_game`-zoned plugin's
`lua_before` RUNS on the WORKER tid, pre-boot-open (a row reads `GetCurrentThreadId()`
+ `g_phase` at entrypoint time — FAILS if it ran on game-main or after the boot open);
an out-of-window call (a live-game verb) FAILS LOUD (a row asserts the reject is a
structured error + reads the actual reject path, not a tautology — AP15). PROBE Q
silent. Confirmed by the user's launch + the agent's dev-log read.

## Dependencies

P5 step 2 (kcdx must be fully initialized on the worker — the slot runs against it)
+ P5 step 1 (the worker GC-safety probe must confirm the early-bind is safe). The
Phase-4 foundation (the event gate + the CAS) — reused, must be in place for the
gate signal.

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §7.1 (the `lua_before` key + the
worker runner) + §7.3 (needs-only-kcdx vs needs-the-live-game — what's callable) +
§7.4 (the early-bind surface + the event gate) + §6 US-3/US-7 (acceptance) + §9 (the
units). [`../lua-vm-design.md`](../lua-vm-design.md) §5 (the event gate mechanism).
Build to §7, not this summary.

## RE / author-burden note

No author hex. The author declares `lua_before` by name (a TOML key) + `zone =
"before_game"`; the engine owns the timing (the disassembler-test posture —
`.claude/rules/cornerstones.md`). No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" rows "New ctx-B phase: before_game
early-slot", "`lua_before` `[entrypoints]` key", "The worker before-game runner",
"Declarative / needs-only-kcdx early-bind surface", "US-7 out-of-window call fails
loud"; design §7.
