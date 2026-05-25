# CAP-05 + CAP-11 — pak Lua runtime test

A pak mod that exercises `kcdx.memory.dynamic_hook` and
`kcdx.lua.cfunction_address` from pure pak Lua (no DLL). Reports
both CAP-05 and CAP-11 via `kcdx.test.report`.

## Build

```powershell
pwsh ./build-pak.ps1
```

Output: `build/kcdx_test_cap05/` containing `mod.manifest` and
`Data/kcdx_test_cap05.pak`.

## Install

Copy the built folder into the game's `mods/` directory:

```
<game>/mods/kcdx_test_cap05/
  mod.manifest
  Data/kcdx_test_cap05.pak
```

(Note: this lives in `mods/`, NOT `plugins/` — paks load via the
CryEngine mod system, not via kcdx's plugin loader.)

## Dependencies

Requires `hello-plugin` to be loaded (provides `kcdx.hello.greet`,
which the pak script hooks). Without hello-plugin, both CAP-05 and
CAP-11 will report FAIL with "kcdx.hello.greet not registered".

## Verifying

Launch the game with dev mode on (`<kcdx-engine>/engine.toml`
with `dev_mode = true`). The next test-suite summary in `kcdx.log`
should include both rows:

```
suite: X/Y passing as of kInputLoaded
  (CAP-05 + CAP-11 both report at OnSystemStarted, which fires
   after kInputLoaded — look for the update-tick summary)
```
