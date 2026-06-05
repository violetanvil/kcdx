# Finding — seam-A hook installs TOO LATE for boot/menu asset resolution

Captured 2026-06-02 (live, 3 runs, dev logs kcdx-dev_2026-06-02_17-41/42/43). The
seam-A Around hook on CCryPak::AdjustFileName (id 152) is the right mechanism but
installs after the engine has already resolved its boot/menu assets. `hits=0`.

## The run-1 hang was NOT our change (cleared)

3 runs: run 1 hung (force-quit via Windows), runs 2+3 booted clean to the full
TEST SUMMARY (passing=143/total=174). All 3 installed the seam-A hook IDENTICALLY
(`seamA-install=1` each). Run 1's last log line is in MID_HOOK / make_jit_midfunc
(a DIFFERENT plugin's JIT mid-hook codegen — comp_15 / a cap plugin), NOT the
asset seam. No crash bundle (consistent with a force-quit hang, not a crash). Our
hook is inert on the boot path (hits=0 in the CLEAN runs too) — it cannot have
caused a hang it never fired into. Non-reproducible-on-identical-binary + stalled-
in-unrelated-code = environmental (the user was multitasking in run 1), not our
change. CLEARED.

## The seam-A result: hits=0, marker absent — a TIMING finding, not a capability one

Timeline (clean run, 17-43-01.log):
- 17:43:02.006–02.375 — FIRST asset reads (.dds/.gfx/.xml) — boot/menu resolution
  ALREADY underway.
- 17:43:02.116 — RefdbOpened; 17:43:02.870 — EngineHooksInstalled.
- 17:43:03.270 — overlay map built (entries=2: kcdlogo.dds + main.lua, both mapped
  correctly — the step-2 map works).
- 17:43:03.403 — FOpen production hook installed.
- 17:43:03.466 — **seam-A AdjustFileName Around hook installed** (id 152, by name,
  at the correct VA — the install itself is CORRECT).

Both engine hooks install from dllmain's worker-thread init (NOT the after_game
plugin zone — the probe plugin's `zone=after_game` is its own load-order slot,
irrelevant to where the engine hook installs). But they install at **17:43:03.4 —
over a SECOND after the first asset reads began at 17:43:02.0.** The menu logo +
boot main.lua are resolved during early init, BEFORE dllmain's worker reaches the
hook-install point. So `hits=0`: the hook arms after the assets it would override
were already resolved.

## The finding (precise)

Replacing CCryPak::AdjustFileName IS the right seam (the install is correct, by
name, at the right address). But to own resolution for BOOT/MENU assets, the hook
must install BEFORE the engine's first asset read — i.e. in the before_game /
pre-init window (the DllMain/LDR-timing window kcdx's before_game hooks use), NOT
in the worker-thread post-RefdbOpened path where both the FOpen and seam-A hooks
currently install. This is an INSTALL-TIMING constraint, not a capability failure;
the seam's override capability is still UNPROVEN (the hook never fired on the test
assets to test it).

This mirrors kcdx's known before_game-vs-after_game hook-timing model. The hook is
engine-internal (installs via kcdx's own early init, like the bugsplat builtin's
early DllMain path), NOT a plugin hook — so it is not blocked by the Phase-11 Lua
before_game gating; it needs the install moved earlier in kcdx's own startup.

## Open / next

1. CONFIRM the timing read: does the menu logo go gray (the .dds override) in a
   run? If NO (expected, hits=0) → timing confirmed. If YES despite hits=0 →
   something else resolves it; re-investigate. (Awaiting the user's logo
   observation from runs 2/3.)
2. The fix to TEST the seam's capability: install the AdjustFileName Around hook
   EARLY (before the first asset read) — the before_game/pre-init window. Then a
   re-run actually exercises the hit path. This is a real design point for the
   seam (install timing) + a probe re-run, surfaced to the user.
3. Is installing an engine hook that early (before RefdbOpened) even possible for
   a NAME-resolved target? The hook resolves id 152 via refdb, which opens at
   RefdbOpened (17:43:02.116) — so the EARLIEST a name-resolved hook can install
   is just after RefdbOpened, which is STILL after the 17:43:02.006 first reads.
   This may force: resolve the address earlier, OR accept that the very-first boot
   assets can't be overridden (only post-init assets — which may be fine: a menu
   logo loads once at boot, but most overridable game assets load on
   level/save load, well after init). A real design fork.
