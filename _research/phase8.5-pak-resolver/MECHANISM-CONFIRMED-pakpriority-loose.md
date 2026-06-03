# CONFIRMED MECHANISM — sys_pakPriority 0 + data-root loose staging (both classes)

Captured 2026-06-02 16:22. The asset-overlay mechanism, confirmed end-to-end,
confound-free. Supersedes the FOpen-redirect approach for the OVERRIDE path.

## The confirmed mechanism

**kcdx sets `sys_pakPriority = 0` pre-launch (via its launcher-owned `user.cfg`),
and an overlay file placed at its data-root-relative vpath (`<game>/Data/<vpath>`)
wins natively** via the engine's loose-first search — for BOTH asset classes. NO
per-open `CCryPak::FOpen` redirect hook is needed.

## Three independent evidence lines (all agree)

1. **Live, clean test (16:22):** a byte-exact `Data/Scripts/main.lua` + a file-scope
   `System.LogAlways("KCDX_S1_DATA_RELATIVE_LOADED")` marker — NO redirect hook
   (clean pass-through DLL), NO staging subdir, just the loose file at the data-root
   vpath + `sys_pakPriority = 0`. `kcd.log`: `value is 0` AND the marker PRESENT
   (count 1). The handle-consumed loose override loaded + executed natively.
2. **Live, prior (16:15):** `user.cfg sys_pakPriority = 0` is HONORED (`value is 0`)
   once the syntax is `cvar = value` (per the shipped system.cfg form). The §7
   "published game pins it to 2" claim is FALSIFIED.
3. **Binary RE (`pakpriority-cvar-registration-flags.md`):** `sys_pakPriority`
   registers with flags `VF_NULL` (0x0) — none of `VF_CONST_CVAR` / `VF_READONLY` /
   `VF_CHEAT` / `VF_REQUIRE_APP_RESTART`; default 2 is a default, not a pin; nothing
   re-writes it to 2 after cfg-load. Freely cfg-settable by design. The mode-0
   search-root is the game data root (`subresolver-decompiled-mechanism.md` Q2).

## Why this is the RIGHT mechanism (cornerstones)

- **General mechanism over special case:** ONE path for every asset class
  (memory-mapped .dds AND handle-consumed .lua/.xml), vs the FOpen-redirect's
  per-class split that failed for handle-consumed.
- **Disassembler-test author UX:** the author drops a loose file at the vpath; kcdx
  stages it to `<game>/Data/<vpath>` + flips the CVar. No engine knowledge, no
  per-class anything.
- **Dramatically simpler engine surface:** the per-open `CCryPak::FOpen` redirect
  hook (steps 1–2's body) + the per-class staging (step 4) are RETIRED for the
  override path. kcdx writes `user.cfg` (it owns the launcher) + stages files.

## What this retires / changes (design + plan — a user decision, surfaced)

- asset-design.md §4.3 (transparent per-class staging via the FOpen redirect) →
  replaced by "set pakPriority 0 + stage at the data-root vpath."
- The FOpen-redirect mechanism (the production hook body, steps 1–2's purpose) →
  no longer the override path. (The hook may still have a role for the by-name
  reference / get_by_path resolution — that is the open question for the redesign;
  the OVERRIDE path no longer needs it.)
- plan steps 1–4 → re-decompose against the confirmed mechanism.

## Open questions for the redesign (NOT yet settled — surface to user)

1. **Global resolution-order change:** `sys_pakPriority 0` makes ALL loose files in
   the install win (not just kcdx overlays) — a broader contract than the scoped
   per-overlay redirect. Confirm this breadth is acceptable (likely fine — it's the
   dev/modding-tools behaviour — but a real scope difference).
2. **The staging tree:** overlays stage to `<game>/Data/<vpath>` — the lifecycle
   (ephemeral-regenerate vs tracked), conflict-when-two-plugins-stage-the-same-vpath
   (the load-order winner's file is what's staged), and cleanup.
3. **Does the FOpen hook still serve the by-NAME reference path** (get_by_path /
   get_by_name / the navigable namespace) — or does native loose-resolution +
   path-translation cover that too? The override path no longer needs the hook;
   the reference path's mechanism is the next question.

## Live-install state (not committed)

`<game>/user.cfg` (`sys_pakPriority = 0`) + `<game>/Data/Scripts/main.lua` (the test
substitute) are live-install only — test artifacts, not committed (game-asset bytes
+ a CVar config the real feature will write via the launcher). `src/asset_overlay.cpp`
is reverted to the clean step-2 pass-through (no probe residue).
