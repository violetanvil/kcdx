---
id: KI-0019
reported: 2026-06-13
status: Open
area: asset-system / engine.ccrypak_fopen hook re-entrancy (HOOK 2 own-FILE* loose open)
discovered_by: Phase-10 verification-probe launch (cap-105/106/107) — crash on inventory open, 2026-06-13 11:09:56 run
commit_at_report: f115640
---

# KI-0019 — ACCESS_VIOLATION on inventory open: runaway re-entrant `engine.ccrypak_fopen` recursion

## Symptom

Repro (user, 2026-06-13): launch KCD2 → load save → enter world → **open inventory** →
ACCESS_VIOLATION crash. Boot + reaching the world is clean; the crash fires on the
inventory-open gesture.

## Evidence (ground truth, agent-read from kcdx-dev_2026-06-13_11-09-56.log + the .dmp)

- **The AV** (dev log L17748): `FAULTED site=unhandled plugin=(none) code=ACCESS_VIOLATION
  rip=0x7FF9575F2632 module=WHGame.DLL module_rva=27731506`. FAULTED_REGS:
  `rax/rcx/rdx/rsi/rdi/r8/r9` all `0x0` (null/freed-pointer deref).
- **The faulting fire** (L~17816+): a `FAULTED_FIRE` stack of repeated
  `plugin=kcdx hook=engine.ccrypak_fopen va=0x7FF955FE14A0` with `seq` counting DOWN
  from **568978** — a runaway re-entrant `FOpen` recursion half a million fires deep.
- **Frames**: frame 0 = WHGame.DLL @ rva 27731506; frames 1-9 = `ucrtbase.dll`
  (CRT heap/string family); frames 10-18 = WHGame.DLL in the `0x7FF955FE0xxx-FE2xxx`
  range (the `ccrypak_fopen` VA neighborhood). A CRT-heap fault reached through the
  pak-FOpen path.
- **Fault inventory** (L17815): `total=66 plugin_hook=48 engine=6 lifecycle=5 probe=2
  bytes=5`. The `probe=2` is ENGINE-internal diagnostics (PROBE Q dummynode etc.,
  L4363-4366), NOT the cap-105/106/107 probes.

## What this is NOT (yet — to confirm)

- **NOT (almost certainly) caused by the Phase-10 probes.** cap-105/106/107 all
  `REPORT pass=false "kcdx.hook returned nil — registration failed (raw-RVA expert
  hatch rejected at register time)"` (dev log L3889/3893/3897) — they installed ZERO
  hooks. The faulting hook is `engine.ccrypak_fopen`, kcdx's own asset hook, present
  before the probes. (Probe A below confirms by removal.)
- **NOT (yet confirmed) the same bug as KI-0012.** KI-0012 (CLOSED) was the SAME hook
  + SAME signature (ACCESS_VIOLATION, `ccrypak_fopen` re-entrancy spiral, seq counting
  down) but a different TRIGGER (boot/DLSS-FSR2 graphics init) and a different root
  cause (pak-less plugin records polluting the engine MOUNT list). KI-0012's mount-list
  gate is firing correctly this run (the log shows cap-105/106/107
  `enabled_list_plugin_no_pak ... excluded from the engine MOUNT list (KI-0012)`). So
  KI-0012's fix held; this is a DISTINCT re-entrancy on the same hook, at a different
  trigger.

## Reframe (the leading frame, to falsify not confirm)

The `ccrypak_fopen` hook (specifically the asset-system **HOOK 2 own-FILE* loose-open**
path, `9590dd4`, and the AdjustFileName resolver HOOK 1, `4a687f3`) was substantially
reworked in the asset-system phase AFTER KI-0012 closed. Inventory-open drives many pak
FOpen calls (item icons/defs). Leading (UNVERIFIED) frame: a re-entrancy guard gap in
the asset-system FOpen detour lets an inventory-driven FOpen re-enter itself unboundedly
→ stack/heap blowup → AV. To be PROVEN by probe, not asserted.

## Probe plan (persisted before running — plan-persistence)

| # | Probe (one variable) | Status | Result |
|---|---|---|---|
| A | Remove cap-105/106/107 from the live install; repro inventory-open | pending | — |
| B | (gated on A) read-only: instrument the `ccrypak_fopen` detour to log re-entrancy depth + the path argument on inventory-open, capture where depth spirals | pending | — |

Probe A is the cheapest most-falsifying step (exonerates or implicates the probes in one
launch). Probe B is designed only after A's outcome; if A still crashes (probes
exonerated), B observes the re-entrancy ground truth in the asset hook directly.

## Facts

- The crash is an ACCESS_VIOLATION in WHGame.DLL with all-zero arg registers, reached
  through `ucrtbase` CRT-heap frames, under a `ccrypak_fopen` re-entrant fire stack
  (seq from 568978). (run 2026-06-13 11:09:56)
- The 3 Phase-10 probes installed zero hooks (kcdx.hook returned nil). (same run)
- KI-0012's mount-list gate excluded the 3 pak-less probes from the engine MOUNT list
  this run. (same run)

## Open questions (causal — NOT facts)

- Is the inventory-open crash caused by the probe deployment, or pre-existing/unrelated
  to it? (Probe A)
- Is the re-entrancy in the asset-system HOOK 2 own-FILE* path (`9590dd4`) or HOOK 1
  AdjustFileName (`4a687f3`)? (Probe B, gated on A)

## Separate, lower-priority defect (NOT this KI — flag for a later /execute)

The raw-VA `address = <pointer userdata>` entry-hook form (docs/lua/hook.md:215-218) was
REJECTED at register time for all 3 probes (`kcdx.hook returned nil`). Either the doc'd
form is wrong, or `get_module_base_address():add(rva)` doesn't produce what the hook
layer expects, or raw-VA entry hooks need something the docs omit. This blocks the
Phase-10 verification probes but is a separate fix from this crash.
