# Writing safe patches

> **Vendored RE methodology reference.** Originally written for the
> predecessor declarative-patch engine; read every "mempatch" mention
> as the generic byte-patch tool — the safety model carries directly
> into kcdx's `[[patch]]` schema. Schema and example links are from
> the original doc and may not resolve in this repo.

A short guide for mod authors shipping a memory patch.

This doc assumes you already know **what** to patch — you have a target
address (or AOB), the original bytes there, and the replacement bytes
you want. If you don't yet, start with
[`finding-patch-sites.md`](finding-patch-sites.md) — the investigation
methodology that gets you from "I want to remove this popup" to a
concrete set of bytes.

## The risk

A memory patch writes bytes into the live game process. If your pattern
matches the wrong place, you write to the wrong instruction, and the game
crashes — or worse, runs subtly broken without crashing. Players who install
your mod and then can't load their saves blame your mod, not the game update
that drifted the pattern.

mempatch enforces three safety checks so you don't have to trust yourself
to pick a unique pattern. Use them.

## Pick a unique pattern

The single most important thing is that your `pattern` produces exactly one
match in `WHGame.dll`'s executable sections. The engine refuses to write if
the count is 0 or ≥2. To verify uniqueness before shipping:

1. Open `WHGame.dll` in Cheat Engine, IDA, Ghidra, or x64dbg.
2. Search for your pattern. If you get more than one hit, **extend the
   pattern** with surrounding bytes until you get exactly one.
3. Patterns shorter than ~10 bytes are rarely unique. Aim for at least 12,
   preferably 16+.

Common mistakes:

- Picking the 3-byte instruction you're patching as the pattern. (`44 8A F0`
  appears 169 times in current WHGame.dll.)
- Picking a generic prologue like `48 89 5C 24 ?` — also matches hundreds of
  function prologues.
- Picking a pattern that's mostly wildcards. Wildcards reduce uniqueness;
  use them only for register fields or call offsets you genuinely cannot
  pin down.

## Always declare `original`

Once your pattern produces 1 match, declare the bytes you expect to find at
`pattern_match + offset` as `original`. The engine verifies these bytes
before writing. If the game updates and changes the instruction at the patch
site (but keeps the surrounding pattern), the engine aborts cleanly rather
than corrupting a different instruction.

This is also what makes idempotency work — when bytes already equal
`replacement`, the engine knows you've already patched.

## Use `context` for extra safety

If your `pattern` is anywhere close to the floor (10–14 bytes), or matches
a generic CryEngine idiom (like `48 8B 01 FF 50 ??` — virtual call
dispatch), wrap it with a `context` field. The context is a longer
surrounding pattern that must also be unique and must contain your pattern.

```toml
pattern = "48 8B 01 FF 50 08 44 8A F0"   # could match 30+ places
context = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"   # 1 match
```

The engine refuses to write unless both match exactly once AND the pattern
match falls inside the context match window. This catches the case where
your pattern happened to match elsewhere even though it was unique against
the WHGame.dll you tested on.

## Use `anchor_string` for maximum safety

For patches where the surrounding function uses a unique string literal —
a UI message, a log statement, a localization key — declare it as
`anchor_string`. The engine:

1. Finds the string in `.rdata` (must be unique).
2. Finds the LEA xref to that string in `.text` (must be unique).
3. Resolves the enclosing function bounds via `.pdata`.
4. Requires the patch address to fall within those function bounds.

This is the strongest guarantee. A game update can shuffle code around all
it likes, but as long as the string still exists and is still referenced
from exactly one function, the engine will refuse to write to the wrong
place.

Not all patches have suitable anchors. Localization keys specifically
have **zero `.text` xrefs** — they're interned as integer IDs at game
startup, not referenced by pointer at runtime. See
[`finding-patch-sites.md`](finding-patch-sites.md) §5 ("The trap") for
the full explanation and how to tell when you've hit this case. The
short version: if the only candidate string you have is the
localization key for a popup, you can't use it as an `anchor_string`;
look instead for a code-referenced literal like an action-map name or
a Flash event name.

Use `anchor_string` when you can; skip it when there's no suitable
literal.

## Test against the dev build

The KCD2 Modding Tools install at
`<SteamLibrary>/steamapps/common/KCD2Mod/` includes a dev build of the game
at `Bin/Win64ReleaseSteamLTO_DLL/`. The dev build accepts `-console`, which
makes `mempatch.asi` mirror its log to a live console window. Test there
before installing into your real game.

## Dry-run before going live

When working on a new patch, set `[mempatch] dry_run = true` in your TOML.
The engine resolves all locators and logs what it would do, but writes no
bytes. Watch the log to confirm the match count is 1 for every locator and
the `original` bytes are what you expect. Then remove the `dry_run` line
and try for real.

## Don't lock to a hash

Tempting solution: "only apply if WHGame.dll has SHA-256 abcd...". Don't.
You'd be writing a patch that only works on one game build forever. The
whole point of AOB+verify is that good patches survive most updates.

## Conflicts with other plugins

When multiple plugins are installed, their byte footprints can
overlap. mempatch distinguishes three categories of overlap via a
**pre-flight pass** before any writes happen:

| Overlap | What it is | Engine behavior |
|---|---|---|
| **Incidental** | Plugin A writes bytes that fall inside plugin B's `pattern` or `context` (but B writes *somewhere else*) | Silent. Both plugins apply successfully because pre-flight resolved B's `patchAddr` against the pristine DLL — A's writes don't move B's target. |
| **Write-on-original** | Plugin A writes bytes that B's `original` field expects to verify | `[WARN]` at pre-flight. B's verify check will fail. B aborts cleanly with a log message naming A as the upstream culprit. |
| **Write-on-write** | Both plugins write to the same address(es) | `[INFO]` (full overlap) or `[WARN]` (partial overlap). Both writes happen in priority order; the later one's bytes win for the overlapping region. |

### The pre-flight pass

Before applying anything, the engine resolves every plugin's locators
against the pristine DLL and records each plugin's *write footprint*
and *read footprint* (split by field: `pattern`, `context`, `original`).
For each pair of plugins, pre-flight categorizes the overlap and emits
log lines for the genuine cases only.

This is why **plugins that reference the same code can coexist**.
Plugin B's `patchAddr` is determined against bytes the way they were
*before any plugin ran*, so plugin A modifying nearby bytes can't
"hide" plugin B's anchor.

### What the log looks like

```
# Write-on-original (genuine conflict):
[WARN] Plugin 'foo' modified bytes that plugin 'bar' needs to verify
       before patching (overlap at 0x...). The earlier mod stopped the
       later one from applying. Try removing or reordering one of them.

# Write-on-write, full overlap (mod B clobbers mod A — by design):
[INFO] Plugin 'bar' fully overwrote bytes already written by plugin
       'foo' (at 0x...). Both mods applied; 'bar' wins because it ran
       later. If you wanted plugin 'foo' to take effect at this address
       instead, give it a lower 'priority' number in its mempatch.toml.

# Write-on-write, partial overlap (risky):
[WARN] Plugin 'bar' partially overwrote bytes already written by plugin
       'foo' (overlap at 0x...). Both mods applied, but the result is
       a MIX of their bytes -- this may produce invalid instructions
       and crash the game. If the game becomes unstable, remove one of
       the two conflicting mods.
```

### Players hitting a conflict can:

- **Remove one of the conflicting mods.**
- **Reorder via `priority`.** Lower `priority` numbers apply first.
  For write-on-write conflicts, whichever runs *last* wins the
  contested bytes.

### Plugin authors:

- **Write-on-original conflicts** can be avoided by patching a
  slightly different instruction or moving the `original` field's
  bytes outside the common area.
- **Write-on-write conflicts** are intentional if you're publishing a
  "replaces mod X" plugin. Otherwise they indicate a genuine clash —
  coordinate with the other plugin's author or accept that your two
  plugins can't both run usefully at the same time.
- **Incidental overlaps** are silent and require no action; they're
  the normal case for plugins operating on adjacent code.

You can see all three diagnostics live by installing
[`examples/outfit-swap-in-combat/`](../examples/outfit-swap-in-combat/)
plus either
[`examples/conflict-test-incidental/`](../examples/conflict-test-incidental/)
(silent coexistence) or
[`examples/conflict-test-on-original/`](../examples/conflict-test-on-original/)
(genuine conflict). Both synthetic plugins always abort or no-op, so
they're safe to drop into a real game folder.

## Code injection — use kcdx instead

mempatch only handles **same-length byte rewrites**. If you need to
**add code** to the game — a new branch, a function detour, a code
cave, anything where `replacement.length != original.length` — the
right tool is [**kcdx**](https://github.com/violetanvil/kcdx), the
SKSE-class extender for KCD2.

kcdx provides:

- **`[[hook]]` and `[[mid_hook]]`** — declarative function hooks
  (entry-point or any instruction), with raw-byte detour bodies or
  typed Lua callbacks.
- **`[[trampoline]]`** — allocate named executable memory and
  reference it from other plugins by symbol.
- **Full SKSE-style C++ plugin API** — `kcdxPluginVersionData`
  exported data block, `kcdxPlugin_Load`, typed interfaces
  (Messaging, Trampoline, Serialization, Task, Scripting), the
  lifecycle messages SKSE plugin authors already know
  (`kPostLoad`, `kPostPostLoad`, `kSaveGame`, etc.).
- **Same pre-flight conflict detection model** as mempatch, extended
  to trampoline allocations and hook collisions.

If your mod needs *both* byte rewrites and hooks, ship both a
`mempatch.toml` and a `kcdx.toml` in the same plugin folder — the two
engines coexist by filename. See the README's "Coexistence with kcdx"
section.

## When your patch breaks: read the log

`mempatch.log` says exactly what went wrong: "pattern matched 3 times",
"locator disagreement", "bytes at patch site don't match", or a
conflict warning naming an upstream plugin. The user can file a bug
with one line copied from the log; you can usually fix it by
lengthening the pattern, moving to a context-based locator, or — if
the cause is another mod — coordinating with that mod's author.
