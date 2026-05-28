# IDE Lua type stubs generated from the Address Library

## Status

Surfaced during Phase 2b sub-3 design pass. The Address Library
already carries a `description` column per entry (provenance,
signature, evidence trail). Today it lives only in the in-binary
table at `src/address_library.cpp::kEntries[]` and the source CSV
at `data/seeds/address_names_seed.csv`.

When mod authors write `kcdx.addr.lua_pcall` in their plugin.lua,
a Lua-aware editor (VS Code + sumneko/luals, JetBrains IDEA-Lua,
Neovim + lua-language-server, etc.) should show a hover tooltip
containing the description. That requires a static Lua stub file
the language server can parse — runtime data the engine knows
isn't reachable from the editor.

## Trigger to revisit

Either:

1. We ship a public release intended for third-party plugin authors
   (today's kcdx is unshipped). The release artifacts should
   include `kcdx-stubs.lua` so authors writing against `kcdx.addr.*`
   get hover docs out of the box.
2. A Phase-4-or-later test plugin author files feedback that
   discovering address-library entry meanings requires reading the
   CSV. The stub file solves it.
3. We add a public-facing IDE setup guide (likely Phase 8 docs); the
   stub file is the centerpiece of that guide.

## Design

A build-time generator reads `data/seeds/address_names_seed.csv` and
emits `kcdx-stubs.lua` — a Lua file the LSP parses but no game ever
executes. Annotations follow the LuaCATS standard
(<https://luals.github.io/wiki/annotations/>) so all major language
servers consume it.

Shape per entry:

```lua
---@class kcdx.addr
kcdx.addr = {}

--- BugSplat MiniDmpSender constructor.
--- Signature: void (wstr databaseName, wstr appName, wstr appVersion, wstr userKey, u32 flags)
--- Verified via: dumpbin /exports BugSplat64.dll | grep MiniDmpSender ctor
--- @type lightuserdata
kcdx.addr.MiniDmpSender_ctor = nil

--- WHGame.dll's compiled lua_pcall (Lua 5.1 C API).
--- Signature: int (ptr L, int nargs, int nresults, int errfunc)
--- Verified via: yobson1/kcd2lua AOB sig "48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8"
--- @type lightuserdata
kcdx.addr.lua_pcall = nil
```

The generator filters by game_version match + status="verified" —
same rule the runtime `kcdx.addr` namespace uses. Unverified rows
don't appear in stubs (their RVA isn't a contract).

Distribution: the stub file ships in the release zip at
`kcdx-stubs/kcdx-stubs.lua`. Authors point their LSP at it via
`.luarc.json` workspace config:

```json
{
  "Lua.workspace.library": [
    "C:/path/to/kcdx-stubs/"
  ]
}
```

## Files that need to change (when triggered)

- New: `tools/gen-lua-stubs.ps1` (or `.py`; pick whichever fits the
  release-tooling stack). Reads
  `data/seeds/address_names_seed.csv` → emits `release-staging/kcdx-stubs/kcdx-stubs.lua`.
- Modified: `package-release.ps1` — call the generator; bundle the
  output into the release zip.
- Modified: `docs/lua/index.md` — add an "Editor setup" section
  pointing authors at the stubs file and the `.luarc.json` config.
- Modified: README.md — mention IDE intellisense as a feature.

## Why we didn't ship it now

- Today's kcdx is unshipped; no third-party authors exist yet. The
  audience for hover-docs is hypothetical until we publish a release.
- Generating stubs requires the same package-release.ps1 changes
  that the broader Phase 8 (docs + examples + v0.1.0 release) work
  is already scoped for.
- The runtime engine doesn't need stubs — they're a strictly
  optional author convenience.

## Related: other Lua surfaces that benefit

The same stub-generation pattern applies to every Lua surface kcdx
exposes:

- `kcdx.hook`, `kcdx.bytes`, `kcdx.code`, `kcdx.on`, `kcdx.command`,
  `kcdx.cosave.*`, `kcdx.scan`, `kcdx.player.*`, etc.
- Each gets a `---@class` annotation with `@field` declarations for
  every method. Authors who type `kcdx.h<Tab>` see autocomplete +
  hover docs.

Scope this entry to the Address Library piece first; the other
surfaces follow the same template.
