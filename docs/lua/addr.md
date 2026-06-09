# kcdx.addr
> Part of the [kcdx Lua API](index.md).

A snapshot table of every Address Library name that resolves on the running
KCD2 build, each mapped to a `kcdx.memory.pointer` userdata. Built once at
startup. Names that do not resolve (wrong game version, unverified, or zero
RVA) are absent — indexing a missing name gives the normal Lua "attempt to index
nil" error, surfacing the unmet dependency immediately.

It is a plain table, so iterate it with `pairs`:

```lua
for name, ptr in pairs(kcdx.addr) do
    kcdx.log.info("ADDR", name .. " -> " .. tostring(ptr))
end

local p = kcdx.addr.lua_pcall    -- pointer userdata, or nil if unnamed here
```

There is no `kcdx.address(...)` function in the Lua surface — name lookup is
this table, and the hook/bytes name locators (a `kcdx.hook` sub-verb's name
target, `kcdx.bytes{ target = "<name>" }`, `address_id = "<name>"`) resolve
names directly.
