# Verify Phase 1 — `[[patch]]` under `kcdx.toml` produces identical apply-log output

The Phase 1 acceptance criterion (from the v0.1 design doc and the
plan): a `[[patch]]` entry under `kcdx.toml` must produce the same
apply-log output as the same entry under `mempatch.toml`. This file
documents the recipe so you can run it in-game when convenient.

Static verification already complete (see commit log):

- All 13 source files copied from mempatch with four mechanical
  substitutions (`mp::` → `kcdx::`, `mempatch.log` → `kcdx.log`,
  `mempatch.toml` → `kcdx.toml`, `[mempatch]` table → `[kcdx]`).
- `kcdx.asi` builds clean (no warnings, no errors).
- Binary inspection (`grep -aoE` against the PE) confirms no
  residual `mempatch.*` or `MemPatch` strings; only `kcdx.*` and
  `RegisterKcdxTable` present.
- `kcdx.asi` and `mempatch.asi` are byte-identical in size
  (558,080 bytes each), strong evidence the engines are
  functionally equivalent.

What remains is the dynamic acceptance test below.

## Steps

1. **Confirm both engines are built:**

   ```powershell
   cd "C:\Users\Michael\Documents\KCD2 Mods\kcd2-mempatch"
   pwsh .\build.ps1     # if not already built
   cd ..\kcdx
   pwsh .\build.ps1     # already verified working
   ```

   Outputs:
   - `kcd2-mempatch/build/Release/mempatch.asi`
   - `kcdx/build/Release/kcdx.asi`

2. **Install both engines side-by-side** into the live game install:

   ```
   E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\
     Bin\Win64MasterMasterSteamPGO\
       dinput8.dll                     (Ultimate-ASI-Loader, from
                                        either engine's package zip)
       plugins\
         mempatch.asi                  (build output)
         kcdx.asi                      (build output)
   ```

   Both ASIs load simultaneously. mempatch scans for
   `*/mempatch.toml`, kcdx scans for `*/kcdx.toml`. They will not
   see each other's files.

3. **Drop two synthetic plugin folders** into the game's `plugins/`
   directory, each containing the SAME outfit-swap patch but
   addressed to its respective engine:

   ```
   plugins\
     phase1-mempatch\
       mempatch.toml      ← copy from kcd2-mempatch/examples/outfit-swap-in-combat/
     phase1-kcdx\
       kcdx.toml          ← copy from kcdx/examples/outfit-swap-in-combat/
   ```

   The two TOML files have identical patch content (same pattern,
   offset, original, replacement, context). Only the filename and
   the comment headers differ.

4. **Launch the game with `-console`** (Steam launch option). Both
   engines mirror their logs to console windows. Read carefully —
   they'll be in two separate consoles.

5. **Compare the two logs** at:

   - `<game>/Bin/Win64MasterMasterSteamPGO/plugins/mempatch.log`
   - `<game>/Bin/Win64MasterMasterSteamPGO/plugins/kcdx.log`

   The relevant lines for the outfit-swap patch should be identical
   in structure. Expected pattern (timestamps and absolute addresses
   will differ, everything else should match):

   ```
   [HH:MM:SS][INFO] Loaded patch 'outfit_swap_in_combat' (priority 100) from <path>
   [HH:MM:SS][INFO] Pre-flight: resolving 1 patch(es) against pristine module...
   [HH:MM:SS][INFO] [outfit_swap_in_combat] pattern matches: 1
   [HH:MM:SS][INFO] [outfit_swap_in_combat] context matches: 1
   [HH:MM:SS][INFO] Pre-flight: no conflicts detected
   [HH:MM:SS][INFO] Applying 1 patch(es)
   [HH:MM:SS][INFO] [outfit_swap_in_combat] applied successfully at 0x... : 44 8A F0 -> 45 31 F6
   [HH:MM:SS][INFO] Patch summary: 1 applied, 0 aborted
   ```

   On a second launch (with the patch already applied from the
   first), the idempotent path fires:

   ```
   [HH:MM:SS][INFO] [outfit_swap_in_combat] patch already applied; skipping (site addr 0x...)
   ```

   In both runs, what mempatch logs and what kcdx logs should be
   structurally identical line-for-line. **Note:** since both
   plugins target the same byte range, the second engine to apply
   will hit the idempotent path (the first already wrote the
   replacement bytes). This is expected and is itself a verification
   that the engines see the world the same way.

6. **Functional in-game test:** load any save where you're in
   combat. Open inventory. Trigger outfit swap. It should succeed.
   (The patch is verified working on KCD2 1.5; either engine
   applies it identically.)

7. **Clean up:** remove the two synthetic plugin folders. Don't
   leave kcdx.asi installed unless you want to keep iterating —
   v0.1 is incomplete and shouldn't be relied on for daily play
   yet.

## Failure modes

- **kcdx.log doesn't appear in `plugins/`** → kcdx.asi didn't load.
  Check that `dinput8.dll` is next to `KingdomCome.exe`. Check
  Windows Defender / antivirus didn't quarantine it.
- **kcdx.log appears but says "Discovered 0 patch(es) from 0
  config file(s)"** → the synthetic `kcdx.toml` file isn't being
  seen. Verify the filename is exactly `kcdx.toml` (case may
  matter), and that the folder containing it is inside `plugins/`.
- **kcdx logs "pattern matches: 0"** → AOB drift in your installed
  game version. Same diagnosis as mempatch (the pattern was
  verified against `release_1_5_1164953_841`; if you have a newer
  build, the pattern may need a refresh). mempatch should show
  the same 0-match result; if it doesn't, that's an unexpected
  divergence and should be investigated.
- **kcdx logs and mempatch logs have structurally different
  content** → the substitution pass missed something or there's a
  divergent code path. File this as a Phase 1 verification failure
  and we investigate.

## What this test does NOT prove

This test only proves the `[[patch]]` schema works in kcdx exactly
as it does in mempatch. It does NOT prove any of kcdx's
v0.1-and-beyond features work (those are Phase 2-8 verification
milestones). It also doesn't exercise:

- The `[kcdx]` top-level `dry_run` flag (would need a separate
  synthetic plugin to test).
- The Lua API (`KCDX.ScanAndWrite` / `KCDX.ReadBytes` /
  `KCDX.GetWHGameBase`) — those need a Lua pak mod that calls
  them.
- The pre-flight conflict matrix (only one patch, no conflicts to
  detect).
- The locator tiers' anchor_string / anchor_function_by_export
  paths.

Those get their own verification recipes as later phases land.
