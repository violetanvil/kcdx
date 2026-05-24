# Author-declared targets (`targets.toml`)
> Part of the [kcdx Lua API](index.md).

Name a code site once, then hook or patch it **by name** — with no hex and no
hand-written ABI at the call site. When the engine does not already name the
function you need, you declare your own **named target** in a `targets.toml`
sidecar next to your `kcdx.toml`. Thereafter `kcdx.hook{ target = "<name>" }`
and `kcdx.bytes{ target = "<name>" }` resolve it like any engine name — and so
can **any other plugin**, by name, without ever touching the hex (the
disassembler-test share guarantee, `cornerstones.md` §36).

## The common path

Drop a `targets.toml` beside your `kcdx.toml`. Each `[[target]]` row is one
named site:

```toml
# targets.toml  (next to kcdx.toml; [plugin].author = "redmoon",
#                                  [plugin].name   = "outfit")
[[target]]
name      = "open_inventory"
pattern   = "48 89 5C 24 ?? 57 48 83 EC 40 33 C0 41 8B F8"
signature = "void (ptr self, i32 slot)"
```

```lua
-- plugin.lua — refer to it by the BARE name; the engine knows where AND the ABI
kcdx.hook{
    name   = "log_inventory_open",
    target = "open_inventory",   -- resolves address (from the pattern) AND signature
    before = function(self, slot) kcdx.log.info("INV", "opening slot " .. slot) end,
}
```

You wrote the AOB and the ABI **once**, in the target row. Every hook/byte
reference is by name — no repeated hex, and a teammate who installs your plugin
hooks `redmoon.outfit.open_inventory` without ever seeing the pattern.

## The `[[target]]` row

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** The bare target name. The engine stamps your `<author>.<plugin>` prefix (from `[plugin].author` + `[plugin].name`) automatically — you never type it. Charset `[a-z0-9_]`. |
| one locator | — | **Required, exactly one** of `pattern` / `rva` / `address_id` / `target_symbol` — see [Locators](#locators). |
| `signature` | string | The ABI string (same grammar as [`kcdx.hook`](hook.md#signature-grammar)). **Required in practice for `pattern` / `rva`** — there is no engine name to carry the ABI, so a `kcdx.hook` referring to the target by name needs the target itself to carry it. Optional for `address_id` (the seed row may carry one) and `target_symbol`. |

### The implicit namespace prefix

You declare `name = "open_inventory"`; the engine registers it as
`redmoon.outfit.open_inventory` (`<author>.<plugin>.<name>`, prefix derived
from `[plugin].author` + `[plugin].name`). You **never** type your own prefix
in your own declarations. The bare name is what you write in your own
`kcdx.hook`/`kcdx.bytes`; the prefixed form is how *other* plugins refer to
it. The reserved `kcdx` author is the engine's own namespace — engine seed
names live at the 1-dot `<kcdx>.<seedname>` form (the engine has no plugin
component); `[plugin].author = "kcdx"` is rejected for user plugins. Full
model: [`naming-namespaces.md`](../../.claude/rules/naming-namespaces.md).

## Locators

Set **exactly one** per row. The first two are the expert hex hatch — you
identify the site once and carry the ABI in `signature`; everyone else hooks it
by name.

- **`pattern = "<AOB>"`** — a byte/wildcard pattern scanned in the module
  (`"48 8B 88 ?? ?? ?? ??"`). Carry a `signature`. *This is the share-guarantee
  headline: an expert names an AOB once, non-experts hook it by name.*
- **`rva = <integer>`** — a raw module RVA. Carry a `signature`. Brittle across
  game patches — prefer `pattern` or an `address_id` where one exists.
- **`address_id = <integer>`** — an [Address Library](addr.md) id. The seed row
  supplies the address (and an ABI, if the row carries one), so `signature` is
  usually unneeded.
- **`target_symbol = "<name>"`** — another already-known target name (an engine
  seed name or another author target). An alias-by-declaration.

## Resolving a name — self > engine > other

A **bare** name resolves by precedence:

1. **self** — a target *your own* plugin declared (your `targets.toml`).
2. **engine** — an Address Library seed name shipped by kcdx.
3. **other** — a target another plugin declared.

So your own declaration always wins, and a later engine release that adopts a
name you already used never displaces you (precedence, not partition). To refer
to another plugin's target unambiguously, use the **explicit prefixed form**
`"<author>.<plugin>.<name>"` (e.g. `target = "redmoon.outfit.open_inventory"`)
— it resolves directly from anywhere and never warns.

### Collision warning

If a **bare** name is owned by more than one tier (e.g. two plugins both
declared `combat_check`), it still resolves by precedence, but the engine logs
a **once-per-session** warning (category `NAMESPACE`) naming the winner and the
shadowed owners, and teaching the fix: prefix any name you did not declare in
your own space. A **prefixed** reference never warns — it is already
unambiguous.

## Errors

- A `[[target]]` row missing `name`, missing a locator, or setting more than one
  locator is **rejected** with a teaching line (category `TARGETS`) naming the
  bad row; the other rows in the file still load.
- An invalid `[plugin].author` or `[plugin].name` (wrong charset, length, or
  `[plugin].author = "kcdx"` — the reserved engine root) makes the whole plugin
  a hard rejection — a bad prefix would corrupt every name it exports
  (`naming-namespaces.md`).
- A name that does not resolve at hook/byte time (unknown name, wrong game
  build, unverified row, typo) surfaces at the hook/byte call: a synchronous
  `(nil, err)` from the binder, or `:applied() == false` with a `:reason()` for
  a deferred apply-time miss. See [`kcdx.hook`](hook.md#errors).

## Aliasing

To give another plugin's long prefixed target a short local handle, use
[`kcdx.alias`](alias.md).

## Glossary

- **author-target / `targets.toml`** — a code site a plugin names itself in a
  `targets.toml` sidecar (`[[target]]` rows), then refers to by name from
  `kcdx.hook` / `kcdx.bytes`. The author-declared peer of an engine
  [Address Library](addr.md) name; shareable by name across plugins.
- **implicit namespace prefix** — the `<author>.<plugin>` the engine stamps on
  every shared name a plugin exports, derived from `[plugin].author` +
  `[plugin].name`. You write the bare `name`; the engine registers
  `<author>.<plugin>.<name>`. You never type your own prefix. Engine seed
  names live under the reserved `kcdx` author at the 1-dot
  `<kcdx>.<seedname>` form.
- **name precedence (self > engine > other)** — how a *bare* shared name
  resolves: your own plugin's declaration first, then an engine seed name, then
  another plugin's. The explicit `<author>.<plugin>.<name>` form bypasses
  precedence and is unambiguous from anywhere.
