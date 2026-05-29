# kcdx.bytes
> Part of the [kcdx Lua API](index.md).

Rewrite bytes at a located site. Succeeds the v0.1 `[[patch]]` TOML schema. A
replacement must be the same length as the original it overwrites — adding code
goes through `kcdx.hook`.

## Common path — `kcdx.bytes.<name>{...}`

The everyday shape: name the site, pass the rewrite payload in a single
table.

```lua
local h = kcdx.bytes.outfit_swap_callsite_aob{
    original    = "44 8A F0",
    replacement = "90 90 90",
}
```

`<name>` is any name in the unified named-target table — a curated engine
entry, your own [author-declared target](targets.md), or a name you exposed
via `kcdx.declare`. Bytes is a single-mode verb, so the userdata returned by
`kcdx.bytes.<name>` is directly callable with the options table — no per-mode
access. The locator is fixed by the name; the options table carries the
rewrite payload (`replacement` is required; `original` /
`offset` / `idempotent` / `context` / `anchor_string` are optional).

**Fail-loud resolution** (every wrong step errors at the wrong-step's access,
not later at install time):

1. **Typo at `<name>`** → the engine returns `nil` from the name index, so
   `kcdx.bytes.outfit_swap_calsite_aob{...}` raises Lua's "attempt to index
   a nil value" naming the typoed slot. The author sees WHICH name they got
   wrong.
2. **Missing options table** → calling the resolved userdata without a table
   raises a teaching error naming the required `replacement` key.
3. **Forbidden key in the options table** (`target` / `pattern` /
   `address_id` / `target_symbol` — the locator-providing keys, fixed by the
   name) → a teaching error pointing at the conflicting key.
4. **Valid resolve + valid options** → the engine dispatches the rewrite
   with the site pre-resolved.
5. **Apply-time failure** (locator misses on the running build, the site's
   current bytes don't match `original`, a conflict is lost) → registration
   still returns a real handle; `:applied()` reads `false` and `:reason()`
   carries the apply-time message. Read those in a `kcdx.on("ready", ...)`
   callback (same contract as the flat-table form below).

## Fallback — `kcdx.bytes{...}` flat-table form

The flat-table form remains available for cases the smart-resolver shape does
not cover: the advanced/expert locators (`pattern` / `address_id` /
`target_symbol`) for sites the name table cannot yet name.

**Call shape:** a single named-field table. Returns a **handle** on successful
registration, or `(nil, err)` on a bad call. Like `kcdx.hook`, the actual write
is [deferred](index.md#2-glossary) to the apply pass.

## Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | Optional label (default `"lua_bytes"`). |
| `description` | string | Optional free text. |
| `replacement` | string | **Required.** The bytes to write, e.g. `"45 31 F6"`. |
| one locator | — | **Required, exactly one** — see [Locators](#locators). |
| `original` | string | Optional verification bytes; if set, must match what is at the site, and must be the same length as `replacement`. |
| `module` | string | Module to resolve against (default `"WHGame.dll"`). |
| `offset` | integer | Offset from the located point (default `0`). |
| `idempotent` | bool | Skip the write if already applied (default `true`). |
| `context` | string | Optional context pattern to disambiguate the locator. |
| `anchor_string` | string | Optional anchor-string locator refinement. |

## Locators

The flat-table form needs to find its site. By name (the same name the
smart-resolver shape consumes):

- **`target = "<name>"`** — a named site. The name resolves the **address** (a
  byte rewrite is untyped, so unlike `kcdx.hook` no signature is involved). The
  name resolves by [precedence](targets.md#resolving-a-name--self--engine--other)
  (self > engine > other): an engine [Address Library](addr.md) name, one of
  your own [author-declared targets](targets.md) (including a `pattern`-located
  target, resolved by name end-to-end), or another plugin's by the explicit
  `"<author>.<plugin>.<name>"` form (e.g.
  `target = "redmoon.outfit.outfit_swap_callsite"`).

The remaining locators are the **advanced/expert escape hatch** for sites the
name table cannot name yet. Set **exactly one**:

- **`pattern = "<AOB>"`** — a byte/wildcard pattern scanned in `module`.
- **`address_id = <number>`** — a numeric Address Library id.
- **`target_symbol = "<name>"`** — an exported [symbol](code.md) name.

`context` and `anchor_string` refine a `pattern` locator.

## Returns / Errors

A handle (same as `kcdx.hook`). Returns `(nil, err)` when: the argument is not
a table; zero or more than one locator is set; `replacement` is missing; a
pattern/bytes string fails to parse; `original` length ≠ `replacement` length;
or a `target` name does not resolve.

## Minimal snippet

```lua
local h = kcdx.bytes{
    name        = "nop_check",
    target      = "outfit_swap_callsite_aob",   -- name resolves the address
    original    = "44 8A F0",
    replacement = "90 90 90",
}
```

## Coexisting with a hook on the same site

A `kcdx.bytes` patch at a site that also carries a [`kcdx.hook`](hook.md)
applies **before** the hook: the patch rewrites the bytes first, then the hook
detours the patched prologue. So a patch and a hook on the **same** function
coexist — both apply. This ordering also means your `original` byte-verify sees
the **pristine** bytes (the hook hasn't run yet), so a patch + hook on one site
is the normal, supported case rather than a conflict. Ordering is by *kind* (a
patch always precedes a hook at the same site), not by declaration or load
order.
