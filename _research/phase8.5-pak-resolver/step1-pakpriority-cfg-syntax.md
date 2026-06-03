# Finding — user.cfg sys_pakPriority was REJECTED for SYNTAX, not pinned

Captured 2026-06-02 (live, `kcd.log` @ 16:13:06). Corrects a near-miss
interpretation: the `sys_pakPriority` CVar route is NOT proven dead — the probe
line was malformed for this engine's cfg parser.

## What happened

Wrote `user.cfg` at the game root with `sys_pakPriority 0` (the WIKI's
space-separated form). Live result:
- `Loading config file 'user.cfg' (user.cfg)` — the engine READ our user.cfg.
- `[Error] 'user.cfg' -> invalid configuration line: 'sys_pakPriority 0'` — the
  line was REJECTED as malformed.
- `CVar sys_PakPriority value is 2` — so the value stayed default BECAUSE our line
  was rejected, NOT because the game pins it.
- `KCDX_S1_DATA_RELATIVE_LOADED` absent — overlay still didn't resolve (expected:
  the CVar never changed).

## The fix (reuse-first — read the EXISTING system.cfg)

The shipped `<game>/system.cfg` shows the accepted cfg-line syntax: **`cvar = value`
with an `=` sign** — e.g. `r_ShadersAsyncCompiling = 3`, `sys_float_exceptions = 0`,
`ca_MemoryDefragEnabled = 0`. Its header explicitly says personal CVar settings go
in `user.cfg`. The wiki's `sys_pakPriority 0` (no `=`) is the wrong form for this
parser. The correct line is **`sys_pakPriority = 0`**.

## Conclusion: the §7 "published game pins pakPriority to 2" claim is NOT verified

The earlier `sys-pakpriority-not-viable.md` verdict rested on a wiki sentence. The
live evidence now shows: the engine reads `user.cfg`, parses it, and applies valid
CVar lines — our line just had wiki-wrong syntax. So the CVar route is OPEN pending
the corrected-syntax test.

## Still genuinely unknown (the thorough path — research, not guess)

1. **Is `sys_pakPriority` cfg-settable by design?** A CryEngine CVar may carry
   registration flags (`VF_CONST_CVAR` / `VF_CHEAT` / `VF_REQUIRE_APP_RESTART` /
   etc.) that reject or ignore a cfg-set even with correct syntax. The corrected
   line tests this empirically; the CVar's registration in the binary settles it
   DEFINITIVELY (and explains a refusal if one happens). → RE pass: find the
   `sys_pakPriority` CVar REGISTER call (the `"sys_PakPriority"` string @183a93c00
   is the anchor; the registrar passes the default + the VF_ flags), read its
   flags + default.
2. **Does sys_pakPriority 0 make a handle-consumed loose overlay resolve
   end-to-end?** The original question — confirmed only when the CVar takes AND the
   marker appears.

Next: corrected `user.cfg` (`sys_pakPriority = 0`) launch + the CVar-flag RE pass
in parallel; one launch then confirms CVar-takes (value is 0) AND overlay-resolves
(marker), with the RE explaining the mechanism.

## UPDATE 2026-06-02 16:15 — CVar HONORED (§7 falsified), but a confound remains

Corrected-syntax launch (`sys_pakPriority = 0`):
- `user.cfg` loaded, NO `invalid configuration line` error → the syntax fix worked.
- `CVar sys_PakPriority value is 0` → **the engine HONORED our user.cfg.** The
  §7 "published game pins pakPriority to 2" claim is **FALSIFIED live** — kcdx CAN
  set pakPriority pre-launch. (The background CVar-registration RE will confirm the
  by-design reason + whether any flag/path could refuse it in other conditions.)
- `KCDX_S1_DATA_RELATIVE_LOADED` STILL absent → the handle-consumed overlay did not
  resolve even at pakPriority 0.

**The confound (one-variable discipline):** this run had BOTH `sys_pakPriority = 0`
AND the instrumented hook STILL REDIRECTING `scripts/main.lua` →
`kcdx_overlay_staging/scripts/main.lua` (dev-log `redirect_armed` confirmed). So it
tested "pakPriority 0 + redirect-to-staged-path," NOT the actual design mechanism:
"pakPriority 0 + the author's loose file at the REAL vpath, NO redirect." With
loose-win native, the redirect is the WRONG thing to have active — it points the
open at a staging subdir instead of letting native loose-search find the file at
its real path.

## The refined, exact question (research, not guess)

At `sys_pakPriority 0`, WHICH loose roots does `CCryPak::AdjustFileName` (slot 1,
RVA 0x6205C — already decompiled) search, so kcdx knows where a loose overlay file
must sit to win natively? The wiki says "files outside paks have priority" but not
the search-root set. The decompile of the AdjustFileName root-prefixing branch at
mode 0 answers this definitively (the `this+0x188` data-root + the recognized-root
prefixes `languages\`/`mods\`/`editor\`/`engine\` the prior sub-resolver finding
named — re-read them for the MODE-0 path specifically).

**The clean test (once the search-root is known):** pakPriority 0 + redirect OFF +
a loose substitute placed at a verified-searched loose root at the real vpath →
does the marker appear? That is the genuine "does the CVar route deliver the design
mechanism" test, one-variable-clean. Do NOT launch again with the redirect active.
