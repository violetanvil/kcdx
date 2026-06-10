# 4.2 [ENG] The rank-1 observed-execution tier (hook-fire + pass-through; kcdx's own production call)

## What

Implement D36's **rank-1 tier** — the only tier that awards `verified_working` — by OBSERVING the
running engine's own use of a row's target, never by minting a synthetic call. Two observed
sub-paths (both zero-invoke-risk; the game calls it, the engine observes): **HOOKED** — for a row
kcdx hooks via the chain (`engine.*` `AddCEngine` entries: `lua_pcall`, `lua_newstate`,
`ccrypak_fopen`, `ccrypak_adjustfilename`, `serialization`, `CGame_Update`, `SaveGame`/`LoadGame`),
read the hook-chain fire-count + a correct pass-through this session → `verified_working` (rank 1);
**CALLED-by-kcdx** — for a row kcdx itself calls in production (`ICVar_GetIVal/GetFVal`,
`IConsole_ExecuteString/AddCommand`), the call already ran + returned correctly this session →
`verified_working` (rank 1). A row with no observed fire/call this session does NOT reach rank-1
(it falls to its next-strongest method per the ceiling rule); rank-1 is observation, never a fabricated
result.

## Scope

One commit in kcdx `src/survival_verify.{h,cpp}` (+ the cap-84 self-test): the rank-1 observation
hook — query `hook_chain` for a target's fire-count + pass-through status (the cap-47
breadcrumb-ring / `hook_chain::GetAllChainTargets` mechanism already exposes fire observation), and
a kcdx-own-call observation for the called set. A row resolves to rank-1 `verified_working` ONLY on
a positive observation; otherwise the verdict falls through to the static ceiling (4.1's mapping).
The matrix wiring that says WHICH rows are rank-1-eligible is 4.4; this step builds the observation
mechanism + the rank-1 verdict path.

## Test bar

cap-84 sub-check: assert a known-hooked engine row (e.g. `CGame_Update` or `lua_pcall`, which fire
every session) reads `verified_working` at `method_rank` 1 from an OBSERVED fire (fire-count > 0 +
pass-through), and a synthetic row with NO observed fire does NOT read `verified_working` (falls to
its static ceiling). FALSIFIABLE: a no-fire row reading `verified_working` fails the row; a hooked
row that fired reading anything below `verified_working`/rank-1 fails the row. Runnable AT this step
— the boot self-test runs after the engine's own boot hooks have fired (cap-47 already observes
fires at the per-tick self-report point). Per `.claude/rules/test-discipline.md`.

## Dependencies

- **4.1** — the 7-state enum + ceiling rule (rank-1 awards `verified_working` through that mapping).
- **Phase 3** — `survival_verify` + the hook-chain fire-observation surface (cap-47 mechanism).

## Reference

[`../plan-spec.md`](../plan-spec.md) — the rank-1 observed-execution definition + the
HOOKED/CALLED sub-paths.

## Design authority

`data/maintainer-tool/design.md` **D36** — rank-1 = "observed live execution (the function fired in
the running process AND passed through correctly — kcdx's own hook-chain fire-count + correct
pass-through, OR kcdx's own production call that already ran)"; "kcdx never mints a synthetic call to
a foreign function — the GAME calls it, we observe"; the no-pre-flight-corruption-check reasoning
(rank-1 from observation, never a guarded synthetic call). §11.6 names the rank-1 membership (the
`engine.*` chain set + the called accessors). Build to D36's observed-execution definition, not this
doc's summary.

## UX

Not a maintainer-tool UI step. The only user gesture is the boot launch.

## Disassembler-test / author-burden

None — engine internals; no author-facing input. The rank-1 observation reads kcdx's own existing
hooks/calls; it adds no new game-function target (no AP18 seed-row addition).
