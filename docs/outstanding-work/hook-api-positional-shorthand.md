# kcdx.hook positional-shorthand call form

## Status

Surfaced during Phase 2b sub-2 design discussion. Decision: ship the
canonical table-of-keys form first; revisit the shorthand later.
May or may not act on it depending on real-world author feedback.

## Trigger to revisit

Either:

1. A plugin author files feedback that kcdx.hook is too verbose for
   common cases (e.g. "I just want a `before` hook with a callback,
   why do I have to write a table?").
2. Migration of the test-suite to kcdx.hook (Phase 4) reveals that
   80%+ of hooks are 2-3-field tables that would read more naturally
   as positional args.
3. Documentation review (Phase 2j docs/lua/hook.md) finds the table
   examples consistently confusing for newcomers vs. positional.

## Design

The shape under consideration:

```lua
-- Today's canonical form (lands in Phase 2b sub-3):
kcdx.hook(kcdx.addr.lua_pcall, {
    name      = "my_hook",
    mode      = "before",
    signature = "int (ptr L, int nargs, int nresults, int errfunc)",
    callback  = function(args) ... end,
})

-- Proposed shorthand for the trivial case:
kcdx.hook(kcdx.addr.lua_pcall, function(args) ... end)
-- Equivalent to: { callback = function(args) ... end }
-- with name=derived from locator, mode="before", signature="void ()".
```

Mechanism: when kcdx.hook's second arg is a function (not a table),
the binder treats it as a `{callback = fn}` with engine-provided
defaults for every other field. Authors who need more reach for the
table form.

This is **additive** to the table form. Both work; authors pick.

## Why we didn't ship it day-one

- Defaulting `signature` to `"void ()"` requires the hook engine to
  silently accept that any non-void original-function return will be
  ignored (or to fail loudly when the JIT detour sees mismatch). The
  policy isn't obvious — louder is safer but more annoying.
- Defaulting `name` to a locator-derived string means conflict logs
  reference machine-generated names that aren't grep-able. May
  reduce the value of the first-wins log messages.
- Forward-compat: every shorthand binding we offer becomes a thing
  we can't break later. We'd rather see real usage patterns first
  and pick shorthands that match what authors actually write often.

## Files that need to change (when triggered)

- `src/lua_bind_hook.cpp` — add a branch in the Lua_Hook entry point
  that detects `lua_isfunction(L, 2)` and synthesizes the opts table.
- `docs/lua/hook.md` — document the shorthand alongside the canonical
  form, with a worked example of when each is appropriate.
- The Phase 4 test-suite migration commits — opportunistically use
  the shorthand where it reads more naturally, leaving table-form
  for hooks with 3+ non-default fields.

## Related discussion

Originated in the Phase 2b sub-2 design pass (2026-05-21). User
pushback: "we expect people to make a full table in that? isn't
that really cumbersome?" Counterargument: most hooks have optional
fields, table form scales to any number of opts without forcing
positional padding. Resolution: ship the table form first, gather
data, revisit when we have signal.
