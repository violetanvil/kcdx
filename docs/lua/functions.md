# kcdx.functions
> Part of the [kcdx Lua API](index.md).

A **function reference** names a function — a game-engine function or another
plugin's — and carries its address (and, where known, its verified signature).
You reach one through `kcdx.functions.*`; you hand it to a hook or statement verb
as the *what to hook*. You never write an address or an ABI — the name is the
whole input.

```lua
-- A game-engine function, by name: the engine resolves address AND signature.
local save = kcdx.functions.WHGame.SaveGame

-- The same function, by its stable-across-versions id.
local save_by_id = kcdx.functions.by_id[131]

-- Another plugin's declared function (see kcdx.dll.declare in dll.md).
local can_swap = kcdx.functions["redmoon.outfit_mod"].CanSwapInCombat
```

## Two kinds of function, two stems

`kcdx.functions.*` carries two structurally-distinct populations, and the stem
tells them apart at a glance:

- **Game-engine functions — a dot-free stem** (the DLL filename without its
  extension): `kcdx.functions.WHGame.SaveGame`. These come from the reference
  database the engine ships — the engine carries the address and the verified
  signature for them, because the game's binary is stripped and the database is
  how the engine knows their names, addresses, and ABIs. Use `kcdx.functions.by_id[N]`
  to reach a game function by its stable id (ids are stable across game updates;
  the address behind one may shift, the id does not). **`by_id` is game functions
  only.**
- **Plugin functions — a dotted `<author>.<plugin>` stem**, bracket-indexed
  because the stem has dots: `kcdx.functions["redmoon.outfit_mod"].SomeFn`. These
  come from the owning plugin itself — its own declaration
  ([`kcdx.dll.declare`](dll.md)) and/or its shipped PDB (see
  [Internal functions from a shipped PDB](#internal-functions-from-a-shipped-pdb)).
  The author wrote the DLL and knows every function's name and signature, so a
  plugin function does NOT go through the reference database and is not tracked
  against it.

The dotted-vs-undotted stem is structurally disjoint — a game DLL never has a
dotted stem, a plugin stem is always `<author>.<plugin>` — so the two
populations never collide.

## Accessing a function reference

| Access | Returns |
|---|---|
| `kcdx.functions.<stem>.<name>` | a game-engine function reference (dot-free stem) |
| `kcdx.functions["<author>.<plugin>"].<name>` | a plugin function reference (dotted stem, bracket-indexed) |
| `kcdx.functions.by_id[N]` | a game-engine function reference by stable id `N` |

An access always returns a reference **value** (it never errors on an unknown
name) — the miss surfaces when you resolve it or hand it to a verb, with a
reason, not a silent `nil`. This keeps the value's type uniform for the verbs
that consume it.

## `value:resolve()` — inspect what a reference points at

Every function-reference value carries a `:resolve()` method that resolves it and
returns a result table. Use it to confirm a function is reachable before you wire
it into a hook, or to read its address and signature.

```lua
local ref = kcdx.functions.WHGame.SaveGame
local r = ref:resolve()
if r.found then
    kcdx.log.info("MYMOD", "SaveGame at " .. tostring(r.address)
        .. " sig " .. r.signature)
else
    kcdx.log.warn("MYMOD", "SaveGame did not resolve: " .. r.reason)
end
```

**Returns** a result table:

| Field | Type | Meaning |
|---|---|---|
| `found` | boolean | `true` when the reference resolved |
| `is_game` | boolean | `true` for a game-engine (database-sourced) reference; `false` for a plugin (declared) reference |
| `stem` | string | `"WHGame"` (game) or `"<author>.<plugin>"` (plugin) |
| `name` | string | the bare function name |
| `signature` | string | the verified ABI (game: from the database; plugin: from the author's `kcdx.dll.declare`); `""` when the function carries none |
| `has_address` | boolean | `true` when the address resolved to a real value |
| `address` | [pointer](memory.md) \| nil | the resolved address as a `kcdx.memory.pointer` userdata (never a Lua number — a pointer-magnitude value would lose precision through a Lua number); `nil` when `has_address` is false |
| `reason` | string | present only when `found == false` — a token (`name_unknown`, `db_not_loaded`, `not_declared`) |

A game-engine reference resolves to an address **and** a signature (both live in
the reference database). A plugin reference resolves to its declared **signature**
immediately — that is the one thing a callback hook needs and the engine cannot
read from a compiled DLL — while its **address** comes from the owning plugin's
shipped PDB (next section); until an address is available `has_address` is
`false`, which is not a failure (the signature half is what a callback hook
depends on).

`:resolve()` reads the reference database, which is open after the engine finishes
loading — call it from a `kcdx.on("ready", …)` callback (or later) when you need
it at a defined point.

## Internal functions from a shipped PDB

Ship your plugin DLL's PDB next to the DLL and **every internal (non-exported)
function becomes reachable by name** — no `kcdx.dll.declare`, no disassembly. The
engine reads the PDB once at plugin load and fills in the address for each of your
DLL's own functions, so another plugin can name `kcdx.functions["you.yourmod"].SomeInternalFn`
and apply a [static byte op](statement.md) to it. The address is the part that is
otherwise hard to get; the PDB hands it over for free.

**Only your OWN functions are exposed.** Your DLL links the C runtime, so the
compiler and linker pull their own internal functions (`operator delete`,
`_set_new_handler`, and the like) into the image. The engine filters those out by
source file and records only the functions from your own source — so your
`kcdx.functions["you.yourmod"]` namespace stays clean and holds the code you wrote,
not the runtime plumbing nobody hooks.

**Build your PDB with `/DEBUG:FULL`** — this is the one requirement. In Visual
Studio set *Linker → Debugging → Generate Debug Info* to **Generate Debug
Information (`/DEBUG:FULL`)**; on a CMake/MSVC target add `/DEBUG:FULL` to the link
options. The default since Visual Studio 2017 is `/DEBUG:FASTLINK`, which produces
a PDB that only points back at the object files on your build machine — it carries
none of your functions' symbols once the DLL is shipped to someone else's game.
`/DEBUG:FULL` produces a self-contained PDB that travels with the DLL. Ship the
DLL and the `.pdb` together; that is the whole step.

A callback hook on a PDB-sourced internal still needs the function's signature —
the address comes from the PDB, but compiled C++ carries no runtime-readable ABI,
so either declare the function ([`kcdx.dll.declare`](dll.md)) or supply the
signature where you hook it. A **static byte op** needs only the address and works
with the PDB alone.

**It is purely additive — ship no PDB and you lose nothing.** Your declared
functions and your DLL's exports still resolve exactly as before. When a PDB is
absent or the wrong kind, the engine logs one teaching line and falls back to
exports + declared functions:

| What you shipped | What happens | Log line |
|---|---|---|
| A `/DEBUG:FULL` PDB beside the DLL | Internal functions resolve by name | `PDB auto-load populated internal-function addresses` |
| No PDB | Exports + declared functions only | `no PDB beside plugin DLL; internal-function auto-load unavailable` |
| A PDB that doesn't match the DLL (stale / rebuilt) | Exports + declared functions only | `PDB for plugin doesn't match its DLL` |
| A `/DEBUG:FASTLINK` PDB | Exports + declared functions only | `plugin ships a FASTLINK PDB; rebuild with /DEBUG:FULL for internal-function auto-load` |

The last row is the one to watch: a FASTLINK PDB *loads* but carries none of your
internals when deployed, so the log tells you to rebuild with `/DEBUG:FULL` rather
than leaving you guessing why your internals did not appear.

## Glossary

- **function reference** — a value naming a function (a game-engine function or a
  plugin's), carrying its address and (where known) its verified signature.
  Produced by a `kcdx.functions.*` access; consumed by a hook or statement verb as
  the *what to hook*. The name supplies the address and the ABI — you never write
  hex.
- **stem** — the part before the function name in `kcdx.functions.<stem>.<name>`.
  A dot-free stem (a DLL filename minus its extension, e.g. `WHGame`) names a
  game-engine function; a dotted `<author>.<plugin>` stem names a plugin function.
- **signature** — the function's ABI (argument and return types). For a game
  function it comes verified from the reference database; for a plugin function it
  comes from the author's [`kcdx.dll.declare`](dll.md). A callback hook needs it;
  a static byte op does not.

---

See also: [dll.md](dll.md) (`kcdx.dll.declare` — how a plugin's own functions
enter this namespace), [addr.md](addr.md) (the name → pointer snapshot for a
single Address-Library name), [locator.md](locator.md) (a finer-grained value
naming *where within a function* an op applies), [`../cpp/functions.md`](../cpp/functions.md)
(the C++ mirror), and [index.md](index.md) for the surface map.
