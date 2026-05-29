# kcdxDeclareInterface (↔ kcdx.declare / kcdx.declared)
> Part of the [kcdx C++ API](index.md).

Declare a per-version named target your plugin owns, and read declared
**value** entries back. The C++ mirror of the Lua
`kcdx.declare(module, name, versions_kv)` write surface +
`kcdx.declared(name)` read accessor ([../lua/declare.md](../lua/declare.md)).

A **declared target** is a name your plugin owns — when you say
`K.declare->Declare("WHGame.dll", "combatResolver", entries, count, K.self)`,
the engine stamps the bare name as `<author>.<plugin>.combatResolver` from
your `[plugin]` manifest. From inside your plugin you refer to it bare
(`combatResolver`); from another plugin you write the prefixed form
(`<author>.<plugin>.combatResolver`). The engine's smart resolver walks
**self > engine > other** precedence, so your own declarations resolve
first, then engine-shipped names, then other plugins' names.

There are **two population sources** for the unified named-target table the
hook / bytes verbs consume:

- **Curated** — engine-shipped names maintained by the kcdx maintainer; live
  in the engine seed, pre-checked for byte-survival across game versions.
- **Declared** (this interface) — names your plugin supplies in its own
  source, with one or more per-version locator entries the engine resolves
  at launch against the running game version.

A declared name and a curated name are indistinguishable to the consumer:
both reach the hook / bytes verbs through the same resolver and install
identically.

This page documents `kcdxDeclareInterface` **v1** as built and verified
(`kcdxDeclareInterface_Version == 1`,
[`Interfaces.h`](../../include/kcdx/Interfaces.h)).

## Fetching the interface

Like every capability interface, fetch it once via `QueryInterface` and cache
the pointer (its lifetime is the engine's). With the [`Kcdx.h`](wrapper.md)
wrapper it is the pre-fetched `K.declare` field:

```cpp
auto* declare = static_cast<const kcdxDeclareInterface*>(
    api->QueryInterface(kcdxInterface_Declare, kcdxDeclareInterface_Version));
if (!declare) { /* engine version mismatch — fail loud, do not skip silently */ }
```

A null return means the running engine does not implement that
interface/version.

## When to call `Declare` / `Get`

Both calls are **launch-time** and reach the engine's declared-targets store
through the same gate the Lua binder uses — they are safe from
`kcdxPlugin_Load`, `kcdxPlugin_PostGameLoad`, and any `kcdxMessage_*`
listener. They are **NOT** safe from a `DllMain`-time hook or any
pre-engine-init context: the store needs the running game version string
(populated when refdb is open) and the scan engine reachable.

## The surface — two methods

```cpp
bool              (*Declare)(const char* module,
                             const char* bareName,
                             const kcdxDeclareEntry* entries,
                             size_t count,
                             kcdxPluginHandle owningPlugin);
kcdxDeclaredValue (*Get)    (const char* name,
                             kcdxPluginHandle owningPlugin);
```

`Declare` is the **write** surface (one per `Declare` call = one declared
name); `Get` is the **read** surface for VALUE entries (the bitmask /
constant form). PATTERN entries are consumed by name through the hook /
bytes verbs (their resolved address feeds the smart resolver), not through
this accessor.

## `Declare(module, bareName, entries, count, owningPlugin)`

Register a per-version named target.

### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `module` | `const char*` | **Required.** The module the declared target lives in (e.g. `"WHGame.dll"`). No default — a defaulted module silently misroutes when secondary modules become a concern. |
| `bareName` | `const char*` | **Required.** The bare name your plugin is declaring. The engine stamps it as `<author>.<plugin>.<bareName>` from your `[plugin]` manifest. Charset `[a-z0-9_]`, 2..128 chars. |
| `entries` | `const kcdxDeclareEntry*` | **Required.** Pointer to an array of `count` per-version entries. The engine copies every field it needs at `Declare` time — the array and its string contents need not outlive the call. |
| `count` | `size_t` | **Required.** Number of entries in the array. Must be > 0. |
| `owningPlugin` | `kcdxPluginHandle` | **Required.** Your plugin handle (from `api->GetPluginHandle("<[plugin].name>")`). Drives the `<author>.<plugin>` prefix the engine stamps on the name, AND the self-tier of self > engine > other for any later resolution from inside your plugin. Passing `kcdxInvalidPluginHandle` rejects the declaration. The wrapper threads this for you as `K.self`. |

### `kcdxDeclareEntry`

POD, C-ABI struct ([`Interfaces.h`](../../include/kcdx/Interfaces.h)).
Default-zero every field (`kcdxDeclareEntry e = {};`), then set only the
fields you use. Sentinel for unset: `null` for strings, `0` for numerics.

| Field | Type | Meaning |
|---|---|---|
| `versionKey` | `const char*` | **Required.** The version string this row applies to: exact (`"1.5.1164953"`) or wildcard (`"1.5.*"`, `"1.*.*"` — any suffix component may be a bare `*`). The store matcher picks exact > longest-wildcard. |
| `patternStr` | `const char*` | The AOB byte pattern at the site (e.g. `"48 8B 05 ?? ?? ?? ?? 8B"`). Setting this makes the entry a PATTERN entry (address-bearing). null / empty = no pattern (this is a VALUE entry, see `valueInt` / `valueStr`). |
| `signatureStr` | `const char*` | The ABI signature DSL (e.g. `"i32 (ptr)"`) for a pattern entry. **Required** when `patternStr` is set AND the entry is intended for hook use — the engine cannot infer an ABI from a pattern. null = unset. |
| `kindTag` | `const char*` | The entry-kind tag. Default for a pattern entry is `"function"` (the kind that triggers the pattern-without-signature rejection); set to `"data_slot"` / `"value"` / etc. to opt out of hook-mode usage and bypass that rejection. null = engine substitutes the default per the entry's shape. |
| `valueInt` | `int64_t` | The integer payload for a VALUE entry; populated when `valueIsString` is false. Ignored when `patternStr` is set. The C++ side has no LUA_NUMBER=float threshold — the full int64 round-trips. |
| `valueStr` | `const char*` | The string payload for a VALUE entry; populated when `valueIsString` is true. The engine copies the bytes at `Declare` time. Ignored when `patternStr` is set. |
| `valueIsString` | `bool` | Discriminator for the two value slots. Ignored when `patternStr` is set. |

### Return value

- **`true`** = the declaration was accepted; the name is now resolvable as
  `<author>.<plugin>.<bareName>` (and bare from inside your plugin).
- **`false`** = the declaration was rejected. The engine logged the teaching
  reason to the dev log under category `DECLARED_TARGET_BIND` (binder-layer
  rejects — bad arg shape, unattributed handle, missing entries, missing
  `versionKey` on an entry) or `DECLARED_TARGET` (store-layer rejects — name
  charset, version-key syntax, pattern-without-signature). The author reads
  the cause from the dev log and surfaces it to the user (the failure is
  loud, never silent).

### Idempotency

A second `Declare` for the same `(owningAuthor, owningPlugin, bareName)`
triple **replaces** the first cleanly. The prior memoization is dropped so
the new declaration resolves fresh — useful for a plugin that re-runs its
declarations across a development reload, no special teardown required.

### Errors

| Reject reason (logged under category) | Cause |
|---|---|
| `bad_arg_module` (`DECLARED_TARGET_BIND`) | `module` is null or empty. |
| `bad_arg_bareName` (`DECLARED_TARGET_BIND`) | `bareName` is null or empty. |
| `unattributed` (`DECLARED_TARGET_BIND`) | `owningPlugin` is `kcdxInvalidPluginHandle` or an unknown handle. |
| `missing_versions` (`DECLARED_TARGET_BIND`) | `entries` is null or `count == 0`. |
| `bad_version_entry` (`DECLARED_TARGET_BIND`) | One of the `kcdxDeclareEntry` rows has an empty `versionKey`. |
| `name_*` / `version_key_*` / `pattern_no_signature` (`DECLARED_TARGET`) | Store-layer validation (see [the Lua declare doc](../lua/declare.md) for the full list — same rules apply). |

## `Get(name, owningPlugin)`

Read a declared VALUE entry's payload.

### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `name` | `const char*` | Either a bare 1-segment name (resolves against the calling plugin's own declarations — the SELF tier only) OR a 3-segment `"<author>.<plugin>.<bare>"` explicit form (resolves against the named plugin's declared store directly). No other dot count is meaningful for declared-value reads — anything else returns a miss. |
| `owningPlugin` | `kcdxPluginHandle` | Drives the SELF tier of the 1-segment form. Passing `kcdxInvalidPluginHandle` on a 1-segment name reads with an empty owner (no self tier); the 3-segment explicit form is unaffected by the owner. The wrapper threads this for you as `K.self`. |

### Return value — `kcdxDeclaredValue`

POD, returned by value ([`Interfaces.h`](../../include/kcdx/Interfaces.h)):

| Field | Type | Meaning |
|---|---|---|
| `found` | `bool` | `true` iff the name resolved to a VALUE entry on the running game version. PATTERN entries, no-match-version entries, and unknown names all return `false` — PATTERN entries are consumed through the hook / bytes verbs, not this accessor. |
| `isString` | `bool` | Discriminator for the two payload slots; meaningful only when `found`. |
| `intValue` | `int64_t` | The integer payload; populated when `found && !isString`. The C++ side has no LUA_NUMBER=float threshold — the full int64 is preserved. |
| `stringValue` | `const char*` | The string payload; populated when `found && isString`. The pointer aliases into the declared-targets store's node-stable container, so it survives a subsequent `Declare` on a DIFFERENT `(author, plugin, bareName)` triple from any plugin (the node-stable storage guarantees prior nodes never move when new triples append). A re-`Declare` of the SAME triple from your own plugin currently invalidates every prior `stringValue` you cached for that name — re-`Get` after re-`Declare`. The same-triple invalidation will be removed in a follow-up change that routes `valueStr` storage through a process-lifetime arena; at that point the pointer becomes process-lifetime unconditionally. null when `found` is false or when the payload is an integer. |

### Errors

`Get` returns `found == false` for every miss path; the engine logs a
once-per-`(plugin, name, running-version)` warn at category
`DECLARED_TARGET` when a declared name exists but no version row matches the
running game version (the same warn the Lua accessor produces). NoEntry and
PATTERN-entry misses are silent — they are normal lookup outcomes, not
errors.

## Lua precision

The Lua `kcdx.declared(name)` accessor pushes integer values via
`lua_pushinteger`, which on CryEngine's Lua 5.1 build (`LUA_NUMBER=float`)
rounds values >= 2^24 through a single-precision mantissa. The C++ surface
documented here has **no such threshold** — `kcdxDeclaredValue::intValue` is
a full `int64_t`. A plugin that needs a pointer-magnitude integer through
this accessor on the Lua side declares it as a string and parses it in Lua;
the C++ side never needs that workaround.

## Minimal snippet

A copy-paste-runnable declare-then-read sequence using the
[`Kcdx.h`](wrapper.md) wrapper's pre-fetched `K.declare`:

```cpp
#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

static Kcdx K;

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "my_author", "my_plugin")) return true;  // logs why
    if (!K.declare) return true;  // engine version mismatch — fail loud upstream

    // Declare a per-version bitmask constant — the value form.
    const kcdxDeclareEntry maskEntries[] = {
        { /*versionKey=*/   "1.5.1164953",
          /*patternStr=*/   nullptr,
          /*signatureStr=*/ nullptr,
          /*kindTag=*/      nullptr,
          /*valueInt=*/     0x0F,
          /*valueStr=*/     nullptr,
          /*valueIsString=*/false },
        { /*versionKey=*/   "1.6.*",
          /*patternStr=*/   nullptr,
          /*signatureStr=*/ nullptr,
          /*kindTag=*/      nullptr,
          /*valueInt=*/     0x1F,
          /*valueStr=*/     nullptr,
          /*valueIsString=*/false },
    };
    if (!K.declare->Declare("WHGame.dll", "combatStateMask",
                            maskEntries,
                            sizeof(maskEntries) / sizeof(maskEntries[0]),
                            K.self)) {
        return true;  // engine logged the teaching reason
    }

    // Read it back. Resolves under your plugin's SELF tier — bare name.
    kcdxDeclaredValue v = K.declare->Get("combatStateMask", K.self);
    if (v.found && !v.isString) {
        // v.intValue is 0x0F on 1.5.1164953, 0x1F on a 1.6.x build, etc.
        // ...consume via your own constants table.
    }

    // Declare a per-version PATTERN entry — the address form. Consumed by
    // the hook / bytes verbs by name; this accessor returns found == false
    // for pattern entries.
    const kcdxDeclareEntry resolverEntries[] = {
        { "1.5.1164953", "48 8B 05 ?? ?? ?? ?? 8B", "i32 (ptr)",
          nullptr, 0, nullptr, false },
    };
    K.declare->Declare("WHGame.dll", "combatResolver",
                       resolverEntries, 1, K.self);
    // Now `combatResolver` is hookable / byte-rewritable by name from any
    // C++ plugin (via the hook / bytes interfaces) or any Lua plugin (via
    // kcdx.hook.combatResolver / kcdx.bytes.combatResolver).
    return true;
}
```

---

See also: [../lua/declare.md](../lua/declare.md) (the Lua peer),
[hook.md](hook.md) (consume a declared PATTERN entry as a hook target by
name), [bytes.md](bytes.md) (consume a declared PATTERN entry as a byte
rewrite by name), [addr.md](addr.md) (engine-shipped name resolution — the
curated track that lives alongside the declared track in the unified
named-target table), and [cross-cutting.md](cross-cutting.md) (the ABI
append-only discipline + threading).
