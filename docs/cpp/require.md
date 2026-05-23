# Multi-file plugins (↔ require)
> Part of the [kcdx C++ API](index.md).

> **Single-surface: the language provides it.** Lua's `require` is a kcdx
> surface ([../lua/require.md](../lua/require.md)) because kcdx owns plugin-chunk
> resolution in the shared Lua state (per-plugin folder scoping + a namespaced
> module cache so two plugins' `require("helper")` don't collide). C++ has no
> equivalent kcdx interface and is owed none — the C++ toolchain handles
> multi-file composition natively. No NYI debt here.

A C++ plugin spans multiple translation units the ordinary way: split your code
across `.cpp`/`.h` files and link them into the one plugin DLL (`#include` for
headers, the linker for objects). There is no shared-state collision to manage —
each plugin DLL has its own static storage and its own image.

## Sibling assets at runtime

If you ship sibling files alongside your DLL (sub-DLLs you `LoadLibraryW`
dynamically, config files, data), address them via the **built**
`kcdxInterface::GetPluginPath`, in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h):

```cpp
const wchar_t* (*GetPluginPath)(kcdxPluginHandle handle);
```

Returns the absolute install-folder path for a handle (your own handle for
self-introspection), or `null` for an unknown handle. kcdx does **not** scan or
load these for you — you own them. kcdx's discovery walk stops at a folder's
`kcdx.toml`, so subfolders (`data/`, `extras/`) are invisible to discovery and
yours to use.

```cpp
const wchar_t* root = api->GetPluginPath(api->GetPluginHandle("my.plugin"));
std::wstring cfg = std::wstring(root) + L"\\data\\config.ini";
```

This is the C++ counterpart of [require](../lua/require.md).
