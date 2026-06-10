# 4.4 [ENG] The per-kind ceiling matrix wiring (§11.6)

## What

Wire the §11.6 **per-kind verification matrix** into `survival_verify` — the table that maps each
of the 9 curated kinds to its highest reachable rank + its default `invoke_attempted` /
`invoke_skip_reason`, so every row's attempt selects the strongest method its kind permits and the
ceiling rule (4.1) caps the verdict honestly. This is the dispatcher that decides, per row: a
`function` in the kcdx-hooked/called set → try rank-1 (4.2); a cvar getter or vtable_base →
rank-2/3 (4.3); a foreign uncallable `function` / `function_no_sig` / `function_variadic` → rank-4
static with `invoke_attempted: false` + `invoke_skip_reason` (`unsafe_to_call` /
`not_a_callable_kind`); `callsite` / `string_anchor` / `instruction_anchor` → rank-3/4 static;
`data_slot` → rank-5 derivation; `vtable_index` → `cannot_check` (deferred). Every row gets an
active attempt + a structured response; no kind is a passive non-result.

## Scope

One commit in kcdx `src/survival_verify.{h,cpp}` (+ the cap-84 self-test): the per-kind ceiling
table (the §11.6 rows) + the dispatch that, per row, selects the strongest applicable method (rank-1
observation 4.2 → rank-2 safe-read 4.3 → rank-3/4 static Phase-3 → rank-5 derivation), sets
`invoke_attempted` + `invoke_skip_reason` per the matrix default, and lets 4.1's ceiling rule
produce the verdict. This is the step that makes the ladder a complete per-kind dispatcher; 4.2/4.3
built the rank-1/rank-2 methods, this routes each kind to its ceiling.

**Carried-forward obligation from 4.3 (the vtable_base reachability route).** 4.3 built the rank-3
read-only loaded-image entry-walk (`WalkVtableBaseLive` — each of N table entries resolves into live
`.text`), but it does NOT yet replace `MapStaticVerdict`'s reachability for a `vtable_base` row: that
still tests the table BASE VA against `.text`, and a vtable base lives in `.rdata`, so a `vtable_base`
row currently lands `failed` ("resolved_va_not_in_live_text") in the live sweep instead of its §11.6
`passed_not_verified`. THIS step's dispatcher MUST route a `vtable_base` row's reachability to
`WalkVtableBaseLive` (the entry-walk IS its §11.6 rank-3 reachability, replacing the base-in-`.text`
test) so the 4 curated vtable_base rows (kcdx_id 119/138/139/140) read `passed_not_verified`, not
`failed`. A cap-84 sub-check asserts a `vtable_base` row resolves `passed_not_verified` via the walk,
NOT `failed` (FALSIFIABLE: a vtable_base row reading `failed` from the base-VA test fails the row).

## Test bar

cap-84 sub-check: assert each of the 9 kinds reaches its §11.6 ceiling on a synthetic/curated row —
a kcdx-hooked `function` → `verified_working`(rank-1); a cvar `function` → `passed_not_verified`
(rank-2); a foreign `function` → `passed_not_verified`(rank-4) + `invoke_attempted: false` +
`invoke_skip_reason: unsafe_to_call`; `function_no_sig`/`callsite`/`string_anchor`/
`instruction_anchor`/`data_slot`/`vtable_base` → `passed_not_verified` at their §11.6 rank +
`invoke_skip_reason: not_a_callable_kind`; `vtable_index` → `cannot_check`. FALSIFIABLE: any kind
reading a verdict above its §11.6 ceiling, or a callable-kind row carrying
`invoke_skip_reason: not_a_callable_kind` (or vice versa), fails the row. Runnable AT this step
(synthetic + curated rows, boot). Per `.claude/rules/test-discipline.md`.

## Dependencies

- **4.1** — the 7-state enum + ceiling rule.
- **4.2** — the rank-1 observed-execution methods.
- **4.3** — the rank-2 safe-read methods.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the per-kind matrix (the §11.6 companion).

## Design authority

`data/maintainer-tool/design.md` **§11.6** (the per-kind verification matrix — each kind's ceiling
verdict + default `invoke_attempted`, the exact table) + **D36** (the ceiling rule + the
`invoke_skip_reason` enum). Build to §11.6's table verbatim — it is the settled per-kind mapping;
this doc's prose is a pointer, the table is the authority.

## UX

Not a maintainer-tool UI step. The only user gesture is the boot launch.

## Disassembler-test / author-burden

None — engine internals; the matrix routes existing curated rows to existing methods. No new
game-function target, no AP18 addition.
