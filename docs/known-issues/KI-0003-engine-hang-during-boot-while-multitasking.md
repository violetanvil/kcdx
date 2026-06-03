---
id: KI-0003
opened: 2026-06-02
status: Open
commit_at_filing: 32df16dda2fa506ace4e16fdea5be5467f795f5d
---

# KI-0003 — engine hang during boot init while the machine was multitasking

**Status:** open

## Symptom

On a game launch, the engine hung during boot and never reached the test-suite
roll-up. The user was multitasking on the machine at the same time (other
foreground work running). The user force-quit the game via Windows when it hung.
The next launch (~16s later) booted and completed normally — the hang did not
recur.

## Trail

| Date       | Action                                   | Result |
|------------|------------------------------------------|--------|
| 2026-06-02 | Filed from the hung run's dev log; no investigation yet | symptom + log facts captured below |

## Facts

Verified from the hung run's dev log
`<game-bin>/kcdx-engine/logs/kcdx-dev_2026-06-02_17-42-20.log`'s predecessor,
`kcdx-dev_2026-06-02_17-41-28.log` (the actual hung run):

- The hung session is `kcdx-dev_2026-06-02_17-41-28.log`. It started at
  `17:41:28.768` and its log ends abruptly — there is **no** final
  `SUMMARY message_label="update tick"` line, which every clean run emits as its
  last line. (The two surrounding clean runs, `17-42-20` and `17-43-01`, both end
  with `SUMMARY ... passing=143 reported=152 pending=22 total=174`.)
- The run **did** reach the test phase before hanging: 615 `[TEST]` lines were
  logged, and three intermediate `SUMMARY` lines fired at the `kPostLoad`,
  `kPostPostLoad`, and `kLuaReady` lifecycle stages. The last `[TEST] RESULT` is
  `cap-46-session-stamp verdict=PASS` at `17:41:42.948`.
- After the last test result, the log's remaining ~1400 lines (to line 4657) are
  hook-chain installation + JIT-stub disassembly trace, all on thread `32036`,
  spanning `17:41:42` → `17:42:04.780`.
- The **last logged line** is, verbatim:
  `[17:42:04.780][DEBUG][engine][MID_HOOK] make_jit_midfunc.exit target_func_ptr=0x7FF8B08C0182 jit_buf=0x7FF8B08D1C20 jit_size=169 fnv1a=4165233598903600037 tid=32036`
  — i.e. the mid-hook JIT stub *finished* emitting (`.exit`, not `.enter`). The
  immediately-preceding mid-hook build belongs to plugin
  `cap_38_sig_mismatch_gate` (hook-chain install lines at `17:42:04.780` name
  `cap38_gate` / `cap38_lua_gate` at target `0x00007FF8B16715A4`).
- The last log flush was `17:42:04.780`; the next session log
  (`kcdx-dev_2026-06-02_17-42-20.log`) began ~16 seconds later — consistent with
  the user's force-quit-and-relaunch.
- The hang did NOT reproduce: the very next launch booted and completed the full
  test roll-up cleanly.

## Hypothesis (NOT verified)

- Hypothesis only — not verified: the hang occurred during or after mid-hook
  installation for `cap_38_sig_mismatch_gate`. The log ending at
  `make_jit_midfunc.exit` is the **last flushed line before the force-quit**, not
  a proven hang site — the engine may have stalled anywhere after that point with
  nothing further logged. The log cannot distinguish "hung inside the post-JIT
  install path" from "hung later in boot with no intervening log line."
- Hypothesis only — not verified: machine contention (the user was multitasking)
  contributed — a timing/scheduling-sensitive boot path (a hook install, a
  thread sync, a one-shot init race) that is normally fast enough to never stall
  could have been starved under load. The non-reproduction on an
  uncontended relaunch is consistent with this but does not prove it.

## Reproduction (if known)

Not reliably reproducible. Observed once, during boot, while the machine was
under multitasking load. A single uncontended relaunch booted normally. No
trigger isolated yet. A probe would need to capture engine state during boot
under deliberate CPU/scheduling contention, and/or instrument the post-test
hook-install boot phase (thread `32036`'s activity after `kLuaReady`) to localize
where the stall begins — the current evidence only bounds it to "after
`17:42:04.780`."

## What this report does NOT do

- Does not propose a fix.
- Does not assign root cause beyond labeled hypothesis.
- Closure handled by `/debug KI-0003` (which lands the fix and closes per
  doc-organization.md).
