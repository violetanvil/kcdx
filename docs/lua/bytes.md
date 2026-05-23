# kcdx.bytes
> Part of the [kcdx Lua API](index.md).

Rewrite bytes at a located site. Succeeds the v0.1 `[[patch]]` TOML schema. A
replacement must be the same length as the original it overwrites — adding code
goes through `kcdx.hook`.

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

The byte rewrite needs to find its site. The **common path** is by name:

- **`target = "<name>"`** — a named site. The name resolves the **address** (a
  byte rewrite is untyped, so unlike `kcdx.hook` no signature is involved). The
  name resolves by [precedence](targets.md#resolving-a-name--self--engine--other)
  (self > engine > other): an engine [Address Library](addr.md) name, one of
  your own [author-declared targets](targets.md) (including a `pattern`-located
  target, resolved by name end-to-end), or another plugin's by the explicit
  `"<pluginname>.<name>"` form.

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
