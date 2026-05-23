# kcdx.cosave
> Part of the [kcdx Lua API](index.md).

Persist your plugin's state across saves — a counter, a settings table, a set
of flags — written when the player saves and read back when they load. The data
is tied to the specific save file (it lives in a `.kcdx` co-save next to the
game's `.whs`), so each save remembers your plugin's state independently.

This is a grouped domain (`kcdx.cosave.*`), not a top-level verb. Five
accessors: `on_save` / `on_load` register the bodies that run inside the
engine's save/load windows; `write` / `records` are the data calls used *inside*
those bodies; `set_uid` is an advanced override most plugins never touch.

## The two moments — put your logic in `on_save` / `on_load`, NOT `kcdx.on("save_game")`

Your cosave write/read logic goes in `kcdx.cosave.on_save` and
`kcdx.cosave.on_load` — **not** in a `kcdx.on("save_game")` handler. This is the
one thing to get right.

The cosave write window is open *only* inside the body you register with
`on_save`: that body runs while the engine is mid-writing the co-save file, so a
`write()` there actually lands. `kcdx.on("save_game")` fires *later* — after the
co-save file has already been written and flushed to disk — so a `write()` from
there hits a closed window and persists nothing.

Read the two as different questions:

- `kcdx.cosave.on_save(fn)` = *"write my data into the co-save now."* The write
  window is open inside `fn`.
- `kcdx.on("save_game", fn)` = *"a save happened"* — a notification, with no
  write window. Use it to react to a save, never to write co-save data.

The same split holds on load: read your records inside `kcdx.cosave.on_load`,
not from a load lifecycle event.

---

## `kcdx.cosave.on_save(fn)`

Register the body that writes your plugin's data into the co-save. `fn` takes no
arguments and runs inside the engine's open writer window, where
`kcdx.cosave.write(...)` works. Re-registering replaces the previous body.

**Returns** `true` on success; `(nil, err)` if `fn` is not a function, or if the
call is not attributed to a plugin (it ran from the console or an anonymous
script — cosave needs an owning plugin to derive a save identity, so the error
tells you to call it from a plugin's `plugin.lua`).

## `kcdx.cosave.on_load(fn)`

Register the body that reads your plugin's data back. `fn` takes no arguments and
runs inside the engine's open reader window, where `kcdx.cosave.records()`
yields your records. Re-registering replaces the previous body.

**Returns** `true` on success; `(nil, err)` on the same conditions as `on_save`
(non-function `fn`, or an unattributed/anonymous caller).

## `kcdx.cosave.write(tag, version, value)`

Write one record into the co-save. **Only valid inside an `on_save` body** — the
writer window is open only there.

| Arg | Type | Meaning |
|---|---|---|
| `tag` | string | A human-readable record name (e.g. `"player_hp"`). You read it back by this same string in `records()`. |
| `version` | integer | Your per-tag schema version — a positive integer. Start at `1`; bump it when you change what this tag stores, so an `on_load` body can tell old data from new. (Distinct from the wire-format version below — that one versions the byte layout itself.) |
| `value` | number / string / boolean / table | The value to persist. See [Wire format](#wire-format--the-per-value-serializer) for the serializable types and the precision contract. |

**Returns** `true` on success; `(nil, err)` when:

- it is called **outside** an `on_save` body (the window is closed) — the most
  common cause, and what the error leads with;
- your string `tag` collides with another tag's hash in this save (a rare
  hash collision; the error points you at `kcdx.log`, which names both tags);
- `value` is not serializable (a function, userdata, thread, a top-level `nil`,
  or a cyclic table — the serializer returns a teaching message saying which);
- an argument is missing or the wrong type (`tag` not a string, `version` not a
  positive integer).

## `kcdx.cosave.records()`

Iterate this plugin's records. **Only valid inside an `on_load` body** — the
reader window is open only there. Returns a Lua **iterator** for a generic
`for`:

```lua
for tag, ver, val in kcdx.cosave.records() do ... end
```

Each step yields three values — the string `tag`, the integer `version`, and the
deserialized `value` — for one record this plugin saved. The loop ends when there
are no more records. (Called outside the reader window it simply yields nothing,
so the loop body never runs.)

A record whose bytes are corrupt or incompatible is **logged and skipped**, not
fatal — one bad record never aborts the loop; the iterator advances to the next.

## `kcdx.cosave.set_uid(uid)` — advanced / expert override

> **You almost never need this.** By default kcdx derives your co-save section's
> identity automatically from your plugin's name — the common path is to OMIT
> `set_uid` entirely (see the snippet below; it has no `set_uid`). The engine
> carries the identity from the name; you hand-pack nothing.

`set_uid` pins an explicit 32-bit section id instead of the name-derived default.
Pin one **only to match a save format you already shipped** (e.g. you renamed
your plugin but want existing saves to keep loading).

| Arg | Type | Meaning |
|---|---|---|
| `uid` | integer | A positive 32-bit id in `[1, 0xFFFFFFFF]`. |

**Returns** `true` on success; `(nil, err)` if `uid` is `0` (the engine drops a
zero-uid section silently — the error says so), not a positive integer in range,
or the call is unattributed (console / anonymous). Once set explicitly, the
auto-derive never overrides it.

## Minimal snippet — the common path (no `set_uid`)

A complete fragment: persist a value on save, read it back on load. Runs as-is —
the auto-derived UID needs no setup.

```lua
local hp = 100
kcdx.cosave.on_save(function()
    kcdx.cosave.write("player_hp", 1, hp)
end)
kcdx.cosave.on_load(function()
    for tag, ver, val in kcdx.cosave.records() do
        if tag == "player_hp" then hp = val end
    end
end)
```

The C++ mirror of this surface is [`kcdxSerializationInterface`](../cpp/cosave.md).

---

## Wire format — the per-value serializer

A cosave stores Lua values (a counter, a settings table, a list of flags) into
the save-tied `.kcdx` co-save file. The serializer turns ONE Lua value into a
self-describing byte buffer and back into the exact same value. Those bytes are
the **data payload of one cosave chunk** — they sit inside the engine's outer
co-save container (see `kcdxSerializationInterface` in `include/kcdx/Interfaces.h`),
which adds its own per-chunk tag / version / length around them. This section
describes only the inner per-value bytes.

### Supported value types

`number`, `string`, `boolean`, and a `table` nesting any of those (arbitrarily
deep). A `table` key may be a `number` or a `string` only.

These are rejected with an author-facing error (the value is not written):
function, userdata, lightuserdata, coroutine/thread, a top-level `nil`, and any
table key that is not a number or string. A `nil` *field* inside a table is not
an error — it is simply an absent key, so it is skipped naturally.

Shared and cyclic table references are **not preserved**: if the same table is
reached twice on one path (including a table that references itself), the
serializer rejects it with a "cyclic table reference" error rather than looping
forever or silently duplicating it.

### Byte layout

All multi-byte integers are little-endian (KCD2 is x86-64).

**Header** (once, at the very start of the buffer) — 4 bytes:

| Bytes | Field | Value |
|---|---|---|
| 0 | magic 0 | `'K'` (0x4B) |
| 1 | magic 1 | `'S'` (0x53) |
| 2 | format version | `1` (`kSerialFormatVersion`) |
| 3 | `lua_Number` width | `sizeof(lua_Number)` on the writing build — `4` on this CryEngine (float) build |

The format-version byte versions the **wire format itself** — distinct from the
per-record `version` an author passes to the cosave write call (that is the
author's own per-tag schema version, a separate axis). On read, an unknown
format version is refused rather than mis-parsed. The `lua_Number` width byte
lets the reader refuse a buffer written by a build with a different numeric
format (e.g. a double build) instead of reinterpreting the raw float bytes and
silently corrupting every number.

**Value** — after the header, exactly one value follows. Each value is a
one-byte **type tag** then its payload:

| Tag | Type | Payload |
|---|---|---|
| `0x01` | number | `sizeof(lua_Number)` raw bytes — the exact `lua_Number` as the VM holds it (4 bytes on this build) |
| `0x02` | string | `[u32 length][length bytes]` — a byte string; may contain embedded NULs (length-prefixed, not NUL-terminated) |
| `0x03` | boolean | 1 byte: `0` = false, `1` = true |
| `0x04` | table | `[u32 entry-count]` then, per entry, `[serialized key][serialized value]` — each key and value is itself a tagged value (recursive). Keys are numbers or strings only. |

A table's entries are written in `lua_next` iteration order, which is
unspecified; do not rely on entry order round-tripping.

### Numeric precision

On this CryEngine build `lua_Number` is **float** (4 bytes), not the stock Lua
`double` — see [Lua number precision](../lua-number-precision.md). The
serializer stores the exact `sizeof(lua_Number)` bytes the VM already holds and
reloads them unchanged, so a number round-trips **exactly relative to the live
value**. It does not widen to double and narrow back (that would be a different
bug), and it cannot restore precision the VM discarded before the value was ever
serialized: if your value already lost low bits by being a Lua number (e.g. a
pointer-magnitude integer — see the precision doc), it is the already-rounded
value that is stored and restored.

### Robustness on read

The reader treats the buffer as untrusted (a co-save can be truncated, corrupt,
or written by a newer kcdx). Every read is bounds-checked against the buffer
length; a truncated or garbage buffer, an unknown format version, an unknown
type tag, a deeper-than-`kMaxDepth` (256) nesting, or trailing bytes after the
value all yield a clean failure with a teaching error — never a buffer
over-read or a crash. On failure nothing is reconstructed; on success exactly
the original value (and Lua type) is produced.
