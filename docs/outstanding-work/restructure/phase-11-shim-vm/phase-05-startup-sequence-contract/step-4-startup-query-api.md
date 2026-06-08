# P5 step 4 — the `kcdx.startup.*` query API (phase / at_least + C++ accessor)

## What

Make the startup timeline QUERYABLE: an author asks "where in startup am I right
now" rather than only reacting to a transition. A new `kcdx.startup` domain
sub-table — `kcdx.startup.phase()` (current named phase) + `kcdx.startup.at_least(phase)`
(reached-yet check) — plus a C++ accessor mirror (full parity). A thin READ surface
over the existing `g_phase` atomic + `init::Name()`. Lets a plugin loaded mid-startup,
or one called from multiple entry points, BRANCH on the current phase.

## Scope

- `kcdx.startup.phase()` (Lua) returns the current phase as a stable named token (the
  same author-friendly tokens the step-3 events use); `kcdx.startup.at_least(phase)`
  returns a boolean (current `g_phase` >= the named phase).
- A C++ accessor on the interface (`include/kcdx/Interfaces.h`, append-only) mirroring
  both — `kcdx.<domain>.<verb>` ↔ the C++ struct-method shape, per
  `.claude/rules/lua-api-surface.md` (one model, two languages, parity).
- Backed by the existing `g_phase` atomic + `kcdx::init::Name()` (a relaxed read +
  the name lookup) — NO new state, NO new ordering. The internal
  `KCDX_REQUIRE_PHASE` guard already proves the query's usefulness; this exposes the
  same to authors.
- `kcdx.startup` is a new domain sub-table (the author surface law's
  `kcdx.<domain>.<verb>`), NOT a new top-level verb.
- Docs (`docs-discipline`): `kcdx.startup.phase`/`at_least` get their `docs/lua/` +
  `docs/cpp/` per-call entries + a glossary term for "startup phase".

## Test bar

A `test-plugins/cap-NN-startup-query/` (Lua) + a C++ mirror (parity): `kcdx.startup.phase()`
returns the CORRECT current phase (a row that calls it at a known point and asserts
the token matches the actual `g_phase` — FAILS if they disagree); `at_least(p)`
returns the correct boolean for a past phase (true) and a future phase (false) — a
FALSIFIABLE row, not a tautology (it reads the actual `g_phase`, not a value it set);
the Lua and C++ accessors return the SAME value at the same point (the parity row —
FAILS if they diverge). PROBE Q silent. Confirmed by the user's launch + the agent's
dev-log read.

## Dependencies

P5 step 2 (the phase enum must carry the new phases for the query to return them) +
P5 step 3 (the author-friendly phase tokens are reconciled in step 3; the query
returns the same tokens). Can land after step 3; independent of steps 5-9.

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §5.2 (the query API — `phase()`,
`at_least()`, the C++ accessor, the thin read over `g_phase`) + §6 US-2 (the
acceptance) + `.claude/rules/lua-api-surface.md` (the `kcdx.<domain>.<verb>` law +
parity). Build to §5.2, not this summary.

## RE / author-burden note

No author hex. The query reads `g_phase` (an in-process atomic) + `Name()` — no
game-binary target, no DB rows. The author types a phase NAME, never an ordinal.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" row "`kcdx.startup.phase()` +
`.at_least()` (Lua) + C++ accessor"; design §5.2, §6 US-2.
