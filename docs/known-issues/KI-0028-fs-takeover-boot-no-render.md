---
id: KI-0028
opened: 2026-07-03
status: open
---

# Boot renders nothing when the kcdx filesystem takeover is active

**Status:** open

## Symptom

kcdx takes over the engine filesystem at boot by swapping its own `CCryPak`
implementation onto the engine's CCryPak object (the "FS swap") — after which
every engine file operation routes through kcdx. With the swap **ON** (the
normal product path), the game boots but the screen stays **black**: the engine
draws nothing (no 2D UI, no 3D scene). The process runs — it does not crash or
hang.

With the swap **OFF** — a `<game-bin>/kcdx-engine/kcdx-noswap` marker file
present, which skips the vtable swap so the engine keeps its own filesystem —
the game boots normally to the main menu and renders.

So: **swap ON → black; swap OFF → renders.** The kcdx filesystem takeover is
the perturbation that causes the black screen.

## The plan — find the FIRST behavioral divergence, from the swap forward

Instrument the boot from the FS-swap point FORWARD and find the **first** point
where the swap-ON run behaves differently from the swap-OFF run. That first
divergence is the lead; everything after it is downstream consequence.

Method discipline:

- **Forward, in boot order.** Start at the swap and walk forward through the
  boot phases. The first swap-ON-vs-OFF divergence is the target — do not start
  at the black screen and reason backward.
- **100% fact, zero inference.** Every log line states what the code did and
  where: which function ran, what path/argument it was given, what it returned.
  Never a verdict ("X failed", "kcdx diverges") — only the raw observation.
- **Both arms, same instrumentation.** Arm any probe BEFORE the swap decision so
  it fires identically swap-ON and swap-OFF; the diff of the two runs is the
  signal.
- **One variable per probe. After one failed fix, re-observe — never fix #2 on a
  new theory.** (`.claude/rules/results-driven.md`.)
- **Reuse-first; the agent builds/deploys/reads logs, the user only launches.**
  (`.claude/rules/agent-builds-and-deploys.md`.)

## The tool already in place

`src/fs_takeover/boot_trace.h` logs every kcdx CCryPak slot the swap serves
during the boot window, under the `FS_BOOT_TRACE` tag: the slot name, the path,
which resolution branch ran (`how`), and the raw result — pure fact, gated to
near-zero cost after boot. The swap-OFF control arm (the `kcdx-noswap` marker)
is in `src/fs_takeover/seating_hook.cpp`. Build: `pwsh ./build.ps1`; deploy the
engine DLL to `<game-bin>/kcdx-engine/kcdx.dll`; enable dev mode; read
`<game-bin>/kcdx-engine/logs/kcdx-dev_<ts>.log`.

## What "done" requires (AP17)

The fix is not landed until the Resolution states the mechanism in falsifiable
terms: which file operation kcdx answered differently under the swap, what the
engine did with that answer, and why that made the black screen inevitable —
not "the screen renders now."
