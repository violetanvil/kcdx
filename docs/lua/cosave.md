# kcdx.cosave
> Part of the [kcdx Lua API](index.md).

**Surface not yet callable.** The `kcdx.cosave.*` author surface (the
`write` / `read` call shapes) is still [Planned](planned.md) — it lands with
the cosave binder. This file currently documents only the **wire format**: the
byte layout the cosave serializer produces for one Lua value. The call-shape /
arguments / returns / snippet sections are filled in when the binder ships.

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

---

*The author-facing call surface (`kcdx.cosave.write` / `kcdx.cosave.read`, their
arguments, return values, error behaviour, and a runnable snippet) lands in this
file when the cosave binder ships, along with this call's row in the
[index map](index.md) and its glossary terms.*
