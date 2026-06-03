# Step 3a probe — launch 1 (discover) findings

Captured 2026-06-02 (live boot→menu run, `kcdx-dev_2026-06-02_13-40-22.log`).
The boot→menu `CCryPak::FOpen` read-open path list — the data the earlier U.1
run captured but did NOT preserve. Reuse-first source for picking redirect
targets; do not re-run discover.

## Both overlay classes fire at boot→menu (no in-game gesture needed)

12,434 distinct read-opens captured through the production hook at boot→menu.
By extension:

- **Memory-mapped class:** `.dds` 6758, `.gfx` 5 — the headline TC class IS
  reachable at boot→menu (textures + UI Flash load before/at the main menu). So
  launch 2 needs NO save-load gesture for the memory-mapped fork.
- **Handle-consumed class:** `.xml` 3211, `.lua` 387.
- Other (cfg/bank/ent/whs/…): the remainder.

## `nFlags` is NOT universally 0x10006 (corrects an assumption)

Distinct `nFlags` on the engine's own reads: `0x0` = 11601 (the vast majority),
`0x10010004` = 431, `0x10006` = 138, `0x4`/`0x6`/`0x2`/`0x8` = small. So the
engine reads MOST assets with flags `0x0`, NOT the OS-search flag. The archived
U.4 had to FORCE `0x10006` because the *substitute loose path* needed it to
resolve — that is about the redirect TARGET's resolvability, not the original
read's flags. Launch 2 must empirically test whether OR-ing `0x10006` is needed
for the plugin's `assets/`-dir path (it may not be, given most reads succeed at
`0x0`). One-variable: test the redirect with and without the flag if the first
combo misses.

## Step-2 overlay map verified live (end-to-end)

`ASSET_OVERLAY overlay_map_built plugins_with_assets=1 entries=1 suppressed=0
escaped=0` + `overlay_entry vpath="readme.txt" winner="probe_asset_overlay"` —
step 2's `[entrypoints].assets` parse + load-order map build WORKED on the live
game (the probe plugin's `assets/README.txt` was discovered, walked, normalized
to `readme.txt`, and mapped). The NormalizeVPath fold produced a lowercase key
as designed.

## Launch-2 candidate targets (known-safe, confirmed-firing)

- **Memory-mapped (visible observable):** `libs/ui//menu.gfx` or
  `libs/ui//buttons.gfx` (main-menu UI Flash — a visible change the user's eyes
  confirm). NOTE the doubled slash `libs/ui//` in the engine's own vpath — the
  redirect's vpath match must normalize to whatever the map key is; confirm the
  NormalizeVPath of the plugin's `assets/libs/ui/menu.gfx` matches the engine's
  `libs/ui//menu.gfx` read (the doubled slash is a real normalization edge —
  surface if it mismatches).
- **Handle-consumed (loggable observable):** a boot `.lua` that logs a distinct
  marker only the substitute prints (the U.4 `cheat_util.lua` model), or a boot
  `.xml` whose effect is observable.

## Open (launch 2 settles)

1. Does redirecting `pName` to the plugin's `assets/`-dir absolute path resolve
   (path-location)? With or without `0x10006`?
2. Does the override extend to the memory-mapped `.gfx`/`.dds` class, or only
   handle-consumed (the Before-vs-Around/Replace mode fork)?
3. Does NormalizeVPath handle the engine's doubled-slash (`libs/ui//`) vpath
   form so the map lookup hits?

## RESULTS (launches 2/3 + sub-probe 1 — ground truth, per-class)

Per-open FOpen redirect (rewrite pName to the target), one variable per launch:

| class | redirect target | flags | result |
|---|---|---|---|
| memory-mapped `.dds` (kcdlogo) | plugin abs `assets/`-dir path | `0` | **WORKS** (gray rectangle, by eye) |
| handle-consumed `.lua` (main.lua) | plugin abs `assets/`-dir path | `0` | FAILS (fell back to pak; marker absent) |
| handle-consumed `.lua` (main.lua) | plugin abs `assets/`-dir path | `0x10000` | FAILS (marker absent; redirect armed, new_nFlags=65536 confirmed) |

So: the **absolute `assets/`-dir path does NOT resolve for the handle-consumed
class**, even with the decompile-identified `0x10000` loose-search bit. The
decompile predicted `0x10000`-set skips re-rooting and opens the absolute path
verbatim — **FALSIFIED live for `.lua`**. (Memory-mapped resolved the same
absolute path at flag 0 — the classes genuinely differ at the caller/consume
level, not in AdjustFileName, which is extension-agnostic.)

The ONLY handle-consumed config ever confirmed working is the archived U.4
shape: **`Data/`-relative path + `0x10006`** (`probe-archive/fopen-override.md`).
Ruled out for `.lua`: absolute+`0`, absolute+`0x10000`. Next single-variable
test for mechanism 1 (if pursued): replicate U.4 exactly — stage the `.lua`
substitute under `<game>/Data/` and rewrite pName to that `Data/`-relative path
(NOT the plugin's `assets/`-dir absolute path). This returns to the one
observed-working configuration, not a new theory.

Architecture implication: a per-open redirect that works uniformly requires
STAGING handle-consumed overlays under `<game>/Data/` (the plugin's own
`assets/` dir is not a resolvable root for that class) — which is a material
cost. Mechanism 2 (search-path registration — register the plugin's root so the
resolver finds it by name) may avoid the staging entirely IF a registration API
exists + a registered root wins over the pak; that is the open RE pass.
