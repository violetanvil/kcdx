# Scanning for an AOB pattern
> Part of the [kcdx Lua API](index.md).

Scanning has two surfaces, one conceptual area:

- **`kcdx_scan` — the in-game `~`-console command** (below). The common path
  for **iterative AOB discovery**: type a pattern in the console, see matches
  in seconds. No edit-build-launch loop.
- **`kcdx.scan{...}` — the Lua runtime-scan verb** (further below). The
  runtime-conditional alternative: scan from `plugin.lua` and branch on the
  structured result.

## `kcdx_scan` — the in-game console command (common path for AOB discovery)

`kcdx_scan` is a `~`-console command for **in-game iterative AOB discovery**.
You are hunting a code site and do not yet have a pattern that resolves
uniquely: type a candidate pattern into the `~` console, read the match count
and addresses, tighten the pattern, type it again — all in seconds, without an
edit-build-launch cycle. It is the documented common path for finding an
un-named site's address.

It is a **console command, not a `kcdx.*` Lua call** — so it does not appear in
the [Lua API call map](index.md#3-the-map) (that map is the kcdx.* surface; a
console verb is not part of it). You type it at the `~` console; you do not call
it from `plugin.lua`. (To run *any* console command line from Lua — including
this one — see [`kcdx.console.execute`](console.md).)

**Single-surface, by design.** `kcdx_scan` is the cross-surface in-game
discovery tool; it has **no Lua/C++ parity mirror** and owes none — it is not a
`kcdx.*` surface with a C++ binding, so there is no NYI mirror entry to track.
The Lua `kcdx.scan{...}` verb below is a *separate* runtime-scan surface, not
the console command's mirror.

### Syntax

```
kcdx_scan <module> "<AOB pattern>"
```

- `<module>` — the module to scan, first (e.g. `WHGame.dll`).
- `"<AOB pattern>"` — the AOB byte string, **quoted**. A pattern has spaces
  between its bytes, so the surrounding quotes keep it one argument. Optional
  `??` wildcards are allowed (e.g. `"48 8B 88 ?? ?? ?? ?? 48"`).

### What it prints

Output goes to the `~`-console overlay (you are typing there, so results appear
there) **and** to `kcdx-dev.log`:

- On a resolved scan with `N` matches:

  ```
  [scan] N matches:
    <module>+0x<offset>
    <module>+0x<offset>
    ...
  ```

  One `<module>+0x<offset>` line per match (capped at 16 lines; a degenerate
  over-broad pattern that exceeds the cap prints `... and K more`).

- On no match: `[scan] 0 matches`.
- On a module that is not loaded: `[scan] module '<module>' not loaded`.
- On a malformed pattern: a teaching parse-error line naming what was wrong, then
  the AOB-format reminder. On missing arguments: the usage line
  `kcdx_scan <module> "<AOB pattern>"  (e.g. ...)`.

A bad input never crashes the game — it prints the teaching line and returns.

### Example

```
kcdx_scan WHGame.dll "48 8B 88 ?? ?? ?? ?? 48"
```

Expected output shape (a unique site):

```
[scan] 1 matches:
  WHGame.dll+0x1A2B3C4
```

### The discover-then-declare loop

`kcdx_scan` is the **discovery** half; naming is the **declare** half:

1. **Discover.** Type a candidate pattern at the `~` console. Read the match
   count. If it matches more than one site (or zero), tighten the pattern and
   type it again — iterate until it resolves to exactly one site.
2. **Declare.** Once the pattern resolves uniquely, paste the working pattern
   into your authoring surface as a declared pattern target — a
   [`kcdx.declare`](declare.md) entry with that `pattern` — and give it a name.
3. **Use by name.** From then on, refer to the site **by its name** in
   `kcdx.hook` / `kcdx.bytes` (`target = "<name>"`). You write the hex once, in
   the declaration; the name carries the address thereafter, and the declared
   target is shareable so another author hooks it by name without ever touching
   the hex.

`kcdx_scan` is the dev-time tool that ends with a name — never a per-hook hex
burden. The everyday hook path is a `target = "<name>"`, where the engine
resolves both the address and the verified signature.

---

# kcdx.scan — the Lua runtime-scan verb

Resolve a hand-written AOB byte pattern against a module's executable sections,
log a concise diagnostic (match count + per-match module/address), and return a
structured result you branch on. `kcdx.scan` is the dev-time
**address-discovery / pattern-validation workbench** — the step you run to
answer *"did my pattern resolve, and to how many sites?"* **before** you commit
that pattern to a hook. It is the runtime-conditional alternative to the
[`kcdx_scan` console command](#kcdx_scan--the-in-game-console-command-common-path-for-aob-discovery)
above: use it when you want to scan from `plugin.lua` and branch on the result
in code, rather than type a pattern at the console.

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
(0 matches)`. (The full byte-dump diagnostic was part of the now-removed
legacy scan path, not this Lua path.)

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
