# Phase 9.6 step 1 — narrow `kcdx.bytes` remit + final call-site migration

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

Narrow `kcdx.bytes` to its post-9.3 remit and migrate the call sites that no
longer belong to it.

## Scope

- **Narrowed remit:** raw byte rewrites OUTSIDE functions (data section, vtable
  slots, string tables) + the labeled-expert `pattern`-locator for AOBs without
  function context.
- Function-internal byte work now belongs in `kcdx.statement.replace_with` with a
  `kcdx.locator.*`. The narrowing removes engine-error space: bytes does what
  statement doesn't (non-function memory), statement does what bytes doesn't
  (function-internal + content locators + hash tracking). No overlap.
- Migrate any remaining test plugins / in-source call sites using the pre-9.3
  `kcdx.hook` mode-as-key shape to sub-verb shape; migrate function-internal
  `kcdx.bytes` call sites to `kcdx.statement.replace_with`.

## Deletion hygiene

Narrowing `kcdx.bytes`'s documented remit is a surface change — sweep `docs/`,
`.claude/rules/`, `CLAUDE.md` for surviving prose that describes function-internal
`kcdx.bytes` as the current path; fix in the same commit (deletion-hygiene).

## Test bar

Suite stays green post-migration; a narrowed-remit sub-test confirms
function-internal byte work now routes through `kcdx.statement.*`.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6" → "`kcdx.bytes`
narrowing" + "Final migration".
