# Byte rewrite (↔ kcdx.bytes)
> Part of the [kcdx C++ API](index.md).

Rewrite bytes at a located site. Partly built, partly NYI.

The C++ surface today exposes the **runtime byte write** through
`kcdxMemoryInterface` (built); the **deferred, locator-based, conflict-resolved
registration** that Lua's `kcdx.bytes{...}` provides is **NYI**.

## Built — runtime byte write/read (`kcdxMemoryInterface`)

`kcdxMemoryInterface::WriteBytes` / `ReadBytes` are real, in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h):

```cpp
int (*WriteBytes)(uintptr_t addr, const void* bytes, size_t size);  // 1 ok, 0 fail (handles VirtualProtect)
int (*ReadBytes) (uintptr_t addr, void* out, size_t size);          // 1 ok
```

These write/read at an address you already hold (resolved via
[addr.md](addr.md) or [memory.md](memory.md) `ScanPattern`). `WriteBytes`
applies **immediately** and handles the page-protect dance; the same-length
single-instruction rewrite is the documented safety contract. Full method
details (args, returns, errors, snippet) are in [memory.md](memory.md), since
they belong to `kcdxMemoryInterface`.

```cpp
uintptr_t site = mem->ScanPattern("WHGame.dll", "44 8A F0");
const unsigned char nops[3] = { 0x90, 0x90, 0x90 };
if (site) mem->WriteBytes(site, nops, sizeof(nops));  // same-length rewrite
```

## NYI — the locator-based deferred mirror of kcdx.bytes

> **NYI** — mirror of [kcdx.bytes](../lua/bytes.md); lands in the C++ parity
> backfill. The Lua `kcdx.bytes{...}` is more than a raw write: it is a
> *registration* with a locator (`pattern` / `address_id` / `target_symbol`),
> an `original`-bytes verification check, `idempotent`/`offset`/`context`/
> `anchor_string` refinements, and **deferred apply through the conflict engine**
> in load order. There is no C++ interface for that registration model in the
> header yet.

Planned shape (model-level, per `.claude/rules/lua-api-surface.md`): an
options-struct (mirror of the Lua named table — `name`, locator, `replacement`,
`original`, `module`, `offset`, `idempotent`, `context`, `anchor_string`)
submitted to a registration method that returns the same handle type as the NYI
[hook.md](hook.md) interface, resolved at the end-of-zone apply pass. The exact
struct/signature is **not defined here** — it lands with the interface. Until
then, use the built `WriteBytes` for immediate writes against a held address.

This is the C++ mirror of [kcdx.bytes](../lua/bytes.md).
