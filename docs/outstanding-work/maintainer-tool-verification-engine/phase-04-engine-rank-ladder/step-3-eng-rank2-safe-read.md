# 4.3 [ENG] The rank-2 safe-read tier (cvar read + read-only vtable_base walk)

## What

Implement D36's **rank-2 tier** — a safe-read exercise that reads a row's live target with ZERO
mutation, capping at `passed_not_verified` (a correct read proves the target yields a sane value,
not that its behavior works — so it is below rank-1, above the rank-3/4 static checks). Two
read-only methods: a **cvar read** (resolve the row's `ICVar` and read its live value via
`GetIVal`/`GetFVal` — the path `src/cvar.cpp` already exposes — asserting a sane return), and a
**read-only vtable_base walk** (walk the table's N qword entries asserting each resolves into live
`.text`, no call). A row exercised at rank-2 reads `passed_not_verified` at `method_rank` 2;
`invoke_attempted: true` with no `invoke_skip_reason` (a read is an attempt that ran). No target is
CALLED — a read is not an invoke; the uncallable-foreign-function class stays static (rank 3–4).

## Scope

One commit in kcdx `src/survival_verify.{h,cpp}` (+ the cap-84 self-test): the rank-2 safe-read
methods — cvar read (reuse the live `GetIVal`/`GetFVal` dispatch in `cvar.cpp`) + the read-only
vtable_base walk (the §11.6 vtable_base check: N-qword table-shape + each entry resolves into
`.text`). A row whose kind/target supports a safe read resolves to rank-2 `passed_not_verified`;
otherwise it falls to the static ceiling. The matrix wiring (which kinds are rank-2-eligible) is 4.4;
this step builds the two safe-read methods + the rank-2 verdict path.

## Test bar

cap-84 sub-check: assert a curated cvar row reads `passed_not_verified` at `method_rank` 2 via the
safe-read (the live value returned sane), with `invoke_attempted: true`; and a vtable_base row reads
`passed_not_verified` at rank-2/3 via the read-only walk (each entry resolved into `.text`).
FALSIFIABLE: a cvar row reading `verified_working` (rank-2 must NOT reach the top rung) fails the
row; a cvar row whose read faulted reading `passed_not_verified` (a faulted read is `error`, not a
pass) fails the row. Runnable AT this step — a cvar read needs no save-load (cvars resolve at boot).
Per `.claude/rules/test-discipline.md`.

## Dependencies

- **4.1** — the 7-state enum + ceiling rule.
- **4.2** — the rank-1 tier (the ladder is built rank-1 then rank-2; a rank-1-eligible row that
  also supports a read takes rank-1 by the ceiling rule).
- **Phase 3 + `src/cvar.cpp`** — the `survival_verify` pass + the live cvar read dispatch this reuses.

## Reference

[`../plan-spec.md`](../plan-spec.md) — the rank-2 safe-read definition (cvar read + read-only walk).

## Design authority

`data/maintainer-tool/design.md` **D36** — rank-2 = "safe-read exercise (read a live value with
zero mutation — a cvar read, a read-only vtable walk)"; the ceiling rule (rank-2 caps at
`passed_not_verified`). **§11.6** — the cvar-getter row → rank-2 `invoke_attempted: true`; the
vtable_base row → the read-only walk. Build to D36 + §11.6, not this doc's summary.

## UX

Not a maintainer-tool UI step. The only user gesture is the boot launch.

## Disassembler-test / author-burden

None — engine internals; the cvar read reuses the existing `ICVar_GetIVal/GetFVal` curated rows (no
new game-function target, no AP18 addition).
