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
| one locator | — | **Required, exactly one** of `pattern`, `address_id` (number), `target_symbol`. |
| `original` | string | Optional verification bytes; if set, must match what is at the site, and must be the same length as `replacement`. |
| `module` | string | Module to resolve against (default `"WHGame.dll"`). |
| `offset` | integer | Offset from the located point (default `0`). |
| `idempotent` | bool | Skip the write if already applied (default `true`). |
| `context` | string | Optional context pattern to disambiguate the locator. |
| `anchor_string` | string | Optional anchor-string locator refinement. |

## Returns / Errors

A handle (same as `kcdx.hook`). Returns `(nil, err)` when: the argument is not
a table; zero or more than one locator is set; `replacement` is missing; a
pattern/bytes string fails to parse; or `original` length ≠ `replacement`
length.

## Minimal snippet

```lua
local h = kcdx.bytes{
    name        = "nop_check",
    address_id  = 12345,
    original    = "44 8A F0",
    replacement = "90 90 90",
}
```
