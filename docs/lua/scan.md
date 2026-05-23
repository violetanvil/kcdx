# kcdx.scan
> Part of the [kcdx Lua API](index.md).

Resolve a hand-written AOB byte pattern against a module's executable sections,
log a concise diagnostic (match count + per-match module/address), and return a
structured result you branch on. `kcdx.scan` is the dev-time
**address-discovery / pattern-validation workbench** — the step you run to
answer *"did my pattern resolve, and to how many sites?"* **before** you commit
that pattern to a hook.

`kcdx.scan` takes a hand-written `pattern` because that is its whole job: it is
the **labelled expert AOB hatch**, by design. It is NOT the everyday way to find
an address. The common path for actually hooking a function is
`kcdx.hook{ target = "<name>" }` ([hook.md](hook.md)), where the engine resolves
both the address **and** the verified ABI from a name — you do not write hex.
Use `kcdx.scan` as the expert workbench to discover and validate a site that has
no engine name yet; once it resolves uniquely, you name it and refer to it by
name thereafter.

**Call shape:** a single named-field table. `kcdx.scan` runs **immediately at the
call** (it is not deferred): it resolves the pattern now, logs the diagnostic,
and returns the result table. The result is **always a table** — a no-match is a
`count = 0` result, not `nil`. Returns `(nil, err)` only on a bad call.

## Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** Label used in the diagnostic log lines (e.g. `name = "find_outfit_swap"`). |
| `pattern` | string | **Required. The expert hatch.** An AOB byte pattern with optional `??` wildcards, e.g. `"48 8B 88 ?? ?? ?? ?? 48"`. |
| `module` | string | Optional. Module to scan. Default `"WHGame.dll"`. |
| `offset` | integer | Optional. Added to each pattern hit to compute that match's apply address. Default `0`. |
| `context` | string | Optional. A second AOB pattern used as a uniqueness-disambiguation signal; its match count is logged. |
| `anchor_string` | string | Optional. Restrict matches to within `max_anchor_distance` of this string literal. One anchor only. |
| `anchor_function_by_export` | string | Optional. Anchor to an exported function by name. One anchor only. |
| `anchor_symbol` | string | Optional. Anchor to a resolved symbol. One anchor only. |
| `max_anchor_distance` | integer | Optional. Anchor search radius in bytes. Default `4096`. |

`anchor_string` / `anchor_function_by_export` / `anchor_symbol` are **mutually
exclusive** — declare at most one.

## Returns / Errors

On a resolved scan, returns a single result table:

| Key | Type | Meaning |
|---|---|---|
| `count` | integer | Number of pattern matches. May be `0`. |
| `matches` | array | One sub-table per match: `{ addr = <pointer>, module = <string>, offset = <int> }`. `addr` is a [`kcdx.memory.pointer`](memory.md) userdata (the apply address); `offset` is the module-relative offset of that apply address. Empty when `count == 0`. |
| `addr` | pointer / nil | The first match's `addr` (a [`kcdx.memory.pointer`](memory.md)), or `nil` when `count == 0`. |

The return is **always a table**, never `nil`-on-no-match. A `count = 0` result
covers both *the pattern matched nothing* and *the module is not loaded* — these
are real diagnostic outcomes you may branch on, distinguished only in the log
(see below). `addr` being `nil` is the no-match signal within the table.

Returns `(nil, err)` with a teaching error **only on bad input**: the argument
is not a table; `name` is missing or not a string; `pattern` is missing, not a
string, or fails to parse; `context` is the wrong type or fails to parse; more
than one anchor is declared; or `module` / `offset` / `max_anchor_distance` is
the wrong type.

`kcdx.scan` LOGS as it runs (the primary workbench feedback): `[scan 'name']
pattern matches: N`, an optional `[scan 'name'] context matches: N`, and one
`[scan 'name'] match N: <module>+0x<rel> -> apply addr 0x<addr>` per hit. When
the module is not loaded it logs `[scan 'name'] module 'X' not loaded
(0 matches)`. (The full byte-dump diagnostic lives in the `[[scan]]` TOML
primitive, not in this Lua path.)

## Minimal snippet

```lua
-- Workbench idiom: validate that a hand-written pattern resolves to exactly
-- one site before you commit to it. Once unique, you would NAME that site and
-- hook it by name (kcdx.hook{ target = "<name>" }) — pattern is the expert form.
local r = kcdx.scan{
    name    = "find_outfit_swap",
    pattern = "48 8B 88 ?? ?? ?? ?? 48",   -- expert AOB hatch
}
if r.count == 1 then
    kcdx.log.info("MYMOD", "pattern is unique at " .. tostring(r.addr))
else
    kcdx.log.warn("MYMOD", "pattern resolved to " .. r.count .. " sites — not unique")
end
```
