# Step 5b — stale-comment sweep (dotted `__index` resolution)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 5b.

## What

Sweep + correct stale comments / rule prose that obscure how kcdx resolves dotted
namespace segments dynamically via `__index` metamethods. During the design,
comments around the `kcdx.hook.<name>` smart-resolver (and rule prose) read as if
only pre-registered fields resolve — which briefly produced a wrong "you can't
dereference a namespace with dots in Lua" conclusion. The navigable namespace
(step 5) relies on that pattern being understood; the next reader must not be
misled the same way (`asset-design.md` §9.2).

## Scope

- Correct the binder comments around the `kcdx.hook` `__index` smart-resolver
  (`src/lua_bind_hook.cpp`) so they state plainly that `kcdx.hook.<name>` resolves
  arbitrary keys dynamically via the `__index` metamethod against engine-side data
  (not pre-registered fields).
- Correct any `.claude/rules/` prose (e.g. naming-namespaces / lua-api-surface)
  implying cross-plugin dotted access is impossible / quoted-string-only — state
  that both the quoted-string form (`target = "a.b.c"`) AND the bare-dotted
  navigable form (`kcdx.plugin.a.b.*`, `kcdx.hook.<name>`) are supported, resolved
  by `__index`.
- Scope is the comments/prose that MISSTATE the resolution mechanism — not a
  broad doc rewrite. Public-facing files: no private citation
  (`public-private-boundary.md`, AP16).

## Test bar

Documentation/comment change — no runtime behavior. The "test" is that the
corrected comments accurately describe the as-built `__index` resolution (verified
by reading the binder), and a `step-review` / `code-review` confirms no stale
"dotted-doesn't-resolve" prose survives. No test plugin (pure comment/prose).

## Dependencies

None blocking — the stale comments exist today. Naturally paired with **step 5**
(the navigable namespace whose mechanism these comments describe), so a reader
arriving at step 5's code finds correct comments. Land with or right after step 5.

## Disassembler-test / author-burden

None — comment/prose correction.

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§9.2 (the stale-comment-sweep deliverable).
