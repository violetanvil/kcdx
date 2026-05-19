# sol2 (vendored)

[github.com/ThePhD/sol2](https://github.com/ThePhD/sol2) at release
**v3.3.0**.

License: MIT. See [LICENSE.txt](LICENSE.txt).

## What we vendored

Single-header release artifacts:
- `sol.hpp` (1.0 MB) — the entire library
- `forward.hpp` (38 KB) — forward declarations for header-only build
- `config.hpp` (2 KB) — configuration knobs

That's the official distribution form; no source compilation needed.

## How this was vendored

```sh
for f in config.hpp forward.hpp sol.hpp; do
  curl -L "https://github.com/ThePhD/sol2/releases/download/v3.3.0/$f" -o "$f"
done
curl -L "https://raw.githubusercontent.com/ThePhD/sol2/v3.3.0/LICENSE.txt" -o LICENSE.txt
```

## How kcdx consumes it

`#include <sol/sol.hpp>` from the RoM-borrowed source files
(`src/rom_borrowed/memory.cpp` and friends). CMakeLists adds
`vendor/sol2/` to the kcdx target's include directories. Headers
only — nothing to link.

## Lua VM compatibility

Sol2 v3.3.0 supports Lua 5.1, 5.2, 5.3, 5.4, and LuaJIT. kcdx ships
Lua 5.1 (inherited from kcd2-mempatch's CryEngine 5.2.3 SDK Lua,
which is what KCD2 itself uses internally). Sol2 auto-detects the
Lua version from the `lua.h` it was compiled against.

## When to update

Bump when:
- A bug we hit gets fixed in upstream
- A new Sol2 feature is genuinely needed

Procedure: re-run the curl commands above with the new tag, update
the version reference at the top of this file.
