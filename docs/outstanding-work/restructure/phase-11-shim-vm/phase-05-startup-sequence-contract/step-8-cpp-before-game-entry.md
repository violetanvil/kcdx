# P5 step 7 — the kcdx-driven C++ before-game entry

## What

Give C++ plugins the same early window the Lua `lua_before` slot gives (US-4): a NEW
exported function the worker invokes at the before-game early-slot phase, with a live
root pointer, so a C++ plugin does early work (register assets, queue a before_game
hook, register a listener) WITHOUT writing a DllMain detour. The common-path C++
early entry, symmetric with `lua_before` — distinct from the self-registration expert
hatch (retained, not built here).

## Scope

- **Resolve the export NAME at this step's head** (design §8 claim 3 — a build-time
  determination, UNVERIFIED in design): READ where `kcdxPlugin_Preload` fires today
  (`src/dllmain.cpp` / the plugin-load path) relative to the VM build + boot open. If
  Preload already fires in the before-game window → reuse it (clarify its timing
  contract); if NOT → a NEW export `kcdxPlugin_BeforeGame` (the lean — no risk to
  existing Preload users). This read settles the name BEFORE the export is built (a
  checkable fact, not a guess — `.claude/rules/results-driven.md`).
- Declare the export in `include/kcdx/Interfaces.h` (append-only) + the worker
  before-game runner (step 5) invokes it for each before_game-zoned C++ plugin, with
  the read-only root pointer, at the before-game early-slot phase.
- Full Lua+C++ parity (`.claude/rules/lua-api-surface.md`): the C++ entry mirrors
  `lua_before` (same window, same what's-callable rule §7.3); the NYI mirror entry the
  Lua step (step 5) left is resolved to built here.
- Docs (`docs-discipline`): the C++ before-game entry gets its `docs/cpp/` entry +
  the glossary term; the `docs/lua/` `lua_before` entry's parity row updates from NYI
  to built.

## Test bar

A `test-plugins/cap-NN-cpp-before-game/` (a C++ DLL plugin): the before-game export
is INVOKED on the WORKER tid, pre-boot-open (a row self-reports it ran + the tid +
`g_phase` — FAILS if it ran on game-main or after the boot open); a declarative early
call from it (e.g. `kcdx.assets.register` via the C++ interface) takes effect at the
gated point exactly as the Lua slot's does (a parity row vs the Lua `lua_before`
behavior). PROBE Q silent. Confirmed by the user's launch + the agent's dev-log read.

## Dependencies

P5 step 5 (the worker before-game runner must exist — it invokes the C++ entry; the
before-game early-slot phase must be in place). The export-name read (this step's
head) depends only on the existing Preload fire site (readable now).

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §7.1 (the kcdx-driven C++
before-game entry, mirroring the existing `kcdxPlugin_*` set) + §8 claim 3 (the
export-name build-time determination) + §6 US-4 (acceptance) +
`.claude/rules/lua-api-surface.md` (parity). Build to §7.1, not this summary.

## RE / author-burden note

No author hex. The author exports a named C++ function (mirrors `kcdxPlugin_Load`);
the engine owns the invocation timing. The export-name read is a static read of
kcdx's own plugin-load source, not a game-binary target. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" rows "The kcdx-driven C++ before-game
entry export" + "C++ export-name determination"; design §7.1, §8.3, §6 US-4.
