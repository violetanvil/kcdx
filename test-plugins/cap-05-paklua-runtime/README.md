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

Self-owned. The companion DLL `cap-05.dll` (built from `cap-05.cpp` via
`CMakeLists.txt`, declared in `kcdx.toml` as `[entrypoints].dll`)
registers cap-05's own callable cfunction `kcdx.cap05.probe`, which the
pak script hooks and calls. No external sample is required. If
`cap-05.dll` doesn't load (or `RegisterFunction` fails), both CAP-05 and
CAP-11 report FAIL with "kcdx.cap05.probe not registered" — that is a
real failure, not a missing external dependency.

### Building the companion DLL

`cap-05.dll` builds like the other C++ test plugins (cap-10, cap-20,
etc.) — the top-level test-plugin build invokes its `CMakeLists.txt`.
Deploy it alongside the sidecar `kcdx.toml` under
`kcdx-plugins/test-suite/cap-05-paklua-runtime/`.

## Verifying

Launch the game with dev mode on (`<kcdx-engine>/engine.toml`
with `dev_mode = true`). The next test-suite summary in `kcdx.log`
should include both rows:

```
suite: X/Y passing as of kInputLoaded
  (CAP-05 + CAP-11 both report at OnSystemStarted, which fires
   after kInputLoaded — look for the update-tick summary)
```
