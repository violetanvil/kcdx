# kcdx.declare / kcdx.declared
> Part of the [kcdx Lua API](index.md).

Declare a per-version named target your plugin owns, and read declared
**value** entries back. The Lua peer of the C++
`kcdxDeclareInterface::Declare` / `Get` surface ([../cpp/declare.md](../cpp/declare.md)).

A **declared target** is a name your plugin owns — when you write
`kcdx.declare("WHGame.dll", "combat_resolver", {...})`, the engine stamps
the bare name as `<author>.<plugin>.combat_resolver` from your
`[plugin]` manifest. From inside your plugin you refer to it bare
(`combat_resolver`); from another plugin you write the prefixed form
(`"<author>.<plugin>.combat_resolver"`). The engine's smart resolver
walks **self > engine > other** precedence, so your own declarations
resolve first, then engine-shipped names, then other plugins' names.

There are **two population sources** for the unified named-target table
the hook / bytes verbs consume:

- **Curated** — engine-shipped names maintained by the kcdx maintainer;
  live in the engine seed, pre-checked for byte-survival across game
  versions.
- **Declared** (this surface) — names your plugin supplies in its own
  source, with one or more per-version locator entries the engine
  resolves at launch against the running game version.

A declared name and a curated name are indistinguishable to the
consumer: both reach the hook / bytes verbs through the same resolver
and install identically (`kcdx.hook.<name>.before(fn)`,
`kcdx.bytes.<name>{...}`).

## `kcdx.declare(module, name, [versions_kv])`

Register a per-version named target.

### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `module` | string | **Required.** The module the declared target lives in (e.g. `"WHGame.dll"`). No default — a defaulted module silently misroutes when secondary modules become a concern. |
| `name` | string | **Required.** The bare name your plugin is declaring. The engine stamps it as `<author>.<plugin>.<name>` from your `[plugin]` manifest. Charset `[a-z0-9_]`, 2..128 chars. |
| `versions_kv` | table | **Required.** The per-version payload — three valid shapes (see below). Omitted or empty rejects with a teaching error. |

### The three valid `versions_kv` shapes

**Per-version map.** Keys are version strings (exact `"1.5.1164953"` or
wildcard `"1.5.*"` / `"1.*.*"` — any trailing component may be `*`).
Each value is either a literal (integer / string — a **value entry**)
or a sub-table `{ pattern = ..., signature = ..., kind = ... }` (a
**pattern entry** that resolves to an address via AOB scan at launch).

```lua
kcdx.declare("WHGame.dll", "combat_resolver", {
    ["1.5.1164953"] = { pattern = "48 8B 05 ?? ?? ?? ?? 8B", signature = "i32 (ptr)" },
    ["1.6.*"]       = { pattern = "48 8B 0D ?? ?? ?? ?? 8B", signature = "i32 (ptr)" },
})

kcdx.declare("WHGame.dll", "combat_state_mask", {
    ["1.5.1164953"] = 0x0F,
    ["1.6.*"]       = 0x1F,
})
```

**Flat single-entry shape.** Top-level keys are `pattern` / `signature`
/ `kind` directly. The engine synthesises a single `versionKey = "*"`
entry that matches every running game version (lowest-priority
fallback). This shape REQUIRES `pattern` — a flat `{ kind = "data_slot" }`
without a pattern is nonsense; use the per-version map for a
value-only entry.

```lua
kcdx.declare("WHGame.dll", "combat_resolver",
    { pattern = "48 8B 05 ?? ?? ??", signature = "i32 (ptr)" })
```

**Omitted / empty table — rejected.** A declared name with no payload
is an author bug; the reject log line points at the three valid shapes.

The two shapes are disambiguated by **inspecting the keys**: a key
matching `^\d+(\.\d+|\.\*)*$` is version-shaped (per-version map); a
key in `{pattern, signature, kind}` is option-shaped (flat shape).
Any other string key, or a mix of the two, is a rejection.

### Per-version entry fields (sub-table OR flat shape)

| Field | Type | Meaning |
|---|---|---|
| `pattern` | string | The AOB byte pattern at the site (e.g. `"48 8B 05 ?? ?? ?? ?? 8B"`). Setting it makes the entry a **pattern entry** (address-bearing). |
| `signature` | string | The ABI signature DSL (e.g. `"i32 (ptr)"`) for a pattern entry. **Required** when `pattern` is set AND the entry is intended for hook use — the engine cannot infer an ABI from a pattern. |
| `kind` | string | The entry-kind tag. Default for a pattern entry is `"function"` (the kind that triggers the pattern-without-signature rejection); set to `"data_slot"` / `"value"` / etc. to opt out of hook-mode usage and bypass that rejection. |

### Per-version VALUE entries (per-version map only)

A value-shaped entry is written as a bare integer or string directly as
the version map's value (no sub-table):

```lua
kcdx.declare("WHGame.dll", "combat_state_mask", {
    ["1.5.1164953"] = 0x0F,        -- integer literal
    ["1.6.*"]       = "x86_64-v2", -- string literal
})
```

Value entries are read back via `kcdx.declared(name)` below; pattern
entries are consumed by the hook / bytes verbs by name.

### Returns / Errors

Returns `true` on accept; `false` on reject. The engine writes a
structured KV log line to the dev log for every reject path under
category `DECLARED_TARGET_BIND` (binder-layer rejects — bad arg shape,
mixed key shapes, etc.) or `DECLARED_TARGET` (store-layer rejects —
name charset, version-key syntax, pattern-without-signature). The
author reads the cause in the dev log.

| Reject reason (logged under category) | Cause |
|---|---|
| `bad_arg_module` (`DECLARED_TARGET_BIND`) | arg 1 missing / not a string. |
| `bad_arg_name` (`DECLARED_TARGET_BIND`) | arg 2 missing / not a string. |
| `missing_versions_kv` (`DECLARED_TARGET_BIND`) | arg 3 omitted or nil. |
| `bad_arg_versions_kv` (`DECLARED_TARGET_BIND`) | arg 3 is not a table. |
| `unknown_key` (`DECLARED_TARGET_BIND`) | a top-level key in arg 3 is neither version-shaped nor an option name. |
| `empty_versions_kv` (`DECLARED_TARGET_BIND`) | arg 3 is `{}`. |
| `mixed_key_shape` (`DECLARED_TARGET_BIND`) | arg 3 mixes version-shaped and option-shaped keys. |
| `bad_version_entry` (`DECLARED_TARGET_BIND`) | a sub-table has a bad option type, or a per-version value has an unsupported Lua type. |
| `bad_flat_entry` (`DECLARED_TARGET_BIND`) | flat shape with no `pattern`. |
| `empty_author` / `empty_plugin` (`DECLARED_TARGET`) | the calling plugin has no `[plugin].author` / `[plugin].name` (unattributed). |
| `empty_module` (`DECLARED_TARGET`) | arg 1 was an empty string. |
| `invalid_name` (`DECLARED_TARGET`) | arg 2 fails the declared-name charset (`[a-z0-9_]`, 2..128 chars). |
| `invalid_version_key` (`DECLARED_TARGET`) | a version key has bad syntax (embedded `*`, empty component, etc.). |
| `pattern_without_signature` (`DECLARED_TARGET`) | a pattern entry has no `signature` AND no `kind` opt-out. |

### Idempotency

A second `kcdx.declare` for the same `(author, plugin, name)` triple
**replaces** the first cleanly. The prior memoization is dropped so the
new declaration resolves fresh — useful for a plugin that re-runs its
declarations across a development reload, no special teardown required.

### Minimal snippet

```lua
-- Declare a per-version constant your plugin reads back.
kcdx.declare("WHGame.dll", "combat_state_mask", {
    ["1.5.1164953"] = 0x0F,
    ["1.6.*"]       = 0x1F,
})

-- Declare a per-version function locator your plugin (or others) hooks
-- by name. signature= is required because this is intended for hook use.
kcdx.declare("WHGame.dll", "combat_resolver", {
    ["1.5.1164953"] = { pattern = "48 8B 05 ?? ?? ?? ?? 8B",
                        signature = "i32 (ptr)" },
})

-- The hook verb consumes the declared pattern by name — same shape as
-- a curated engine name.
kcdx.hook.before("WHGame.dll", "combat_resolver", function(arg) -- ...
end)
```

## `kcdx.declared(name)`

Read a declared **value** entry's payload. Pattern entries are consumed
by the hook / bytes verbs (not through this accessor) — calling
`kcdx.declared` on a pattern name returns `nil`.

### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | Either a bare **1-segment** name (resolves against the calling plugin's own declarations — the SELF tier only) OR a **3-segment** `"<author>.<plugin>.<bare>"` explicit form (resolves against the named plugin's declared store directly, mirroring the cross-plugin reference shape). No other dot count is meaningful — anything else returns `nil`. |

### Returns

The declared value (a Lua number for an integer entry, a Lua string for
a string entry) on a Value-entry hit on the running game version;
`nil` for every miss case (`kind == Pattern`, `kind == VersionMismatch`,
`kind == NoEntry`). A bare name from a caller with no owning plugin
(e.g. an anonymous console / pak Lua call) returns `nil` — no SELF
tier is reachable.

### LUA_NUMBER=float precision

CryEngine's Lua 5.1 build uses `LUA_NUMBER=float`, so integers pushed
via `lua_pushinteger` round through a single-precision mantissa: values
under 2^24 round-trip exactly, larger values silently lose precision.
The example value-entry payloads (`0x0F`, `0x1F` — small bitmasks) sit
well under the threshold and round-trip cleanly.

A pointer-magnitude declared value (anything ≥ 2^24) MUST be declared as
a string and parsed in Lua — declaring it as an integer hands you back
a rounded value with no error. The C++ peer
([`../cpp/declare.md`](../cpp/declare.md)) has no such threshold and
preserves the full `int64`.

### Minimal snippet

```lua
-- Declare a small bitmask.
kcdx.declare("WHGame.dll", "combat_state_mask", {
    ["1.5.1164953"] = 0x0F,
    ["1.6.*"]       = 0x1F,
})

-- Read it back. Bare name = SELF tier (your own declarations).
local mask = kcdx.declared("combat_state_mask")
-- mask == 0x0F on 1.5.1164953, 0x1F on a 1.6.x build.

-- Cross-plugin read: 3-segment explicit form.
local other = kcdx.declared("redmoon.outfit.combat_state_mask")
```

---

See also: [../cpp/declare.md](../cpp/declare.md) (the C++ peer),
[hook.md](hook.md) (consume a declared PATTERN entry as a hook target
by name), [bytes.md](bytes.md) (consume a declared PATTERN entry as a
byte rewrite by name), [addr.md](addr.md) (curated engine-name
resolution — the other population source in the unified named-target
table), and [index.md](index.md) for the surface map.
