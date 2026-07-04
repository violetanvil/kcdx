# KI-0028 differential trace — HOP 8: the command-stream interpreter is OFF the live path

**Date:** 2026-07-03 · **Method:** live differential trace (PROBE Z10 + HOP-8 cmd_stream arm).
**Trust:** primary (live-run tallies, one log, one watcher).
**Log:** `kcdx-dev_2026-07-03_17-09-27.log` · swap-ON arm (kcdx-noswap ABSENT).

## What HOP 7 handed us (and what HOP 8 falsifies)

HOP 7 (static Ghidra) asserted the live pass-A submit path is:

> interpreter `FUN_18251bb1c` (opcode 4) → `FUN_1804e8d88` → dispatcher `FUN_180779534` → pass A submit

and pre-committed HOP 8 to decide "is the command STREAM empty swap-ON, or the same
stream missing its item-build opcodes" by hooking the interpreter at entry and reading
the stream length.

**HOP 8 refutes the premise of that question.** The interpreter is not on the live path
at all swap-ON — so there is no stream to measure.

## The decisive result — same run, same watcher (tid=11532)

| tally | fn | invokes swap-ON |
|---|---|---|
| `cmd_stream_tally` | interpreter `FUN_18251bb1c` (HOP 8) | **0** |
| `passa_dispatch_tally` | dispatcher `FUN_180779534` (HOP 6) | **13350** (`list_empty=13350`, all) |

- `render_trace_armed sites_armed=10` — the interpreter arm is the +1 over HOP-6's 9
  (`CmdStreamProbeArm` counted in the 10). No `hook_create_failed` / `hook_enable_failed`
  in the log. The dispatcher arm shares the SAME `MH_Initialize`; a broken init would have
  zeroed the dispatcher too — it fired 13350×. **So the interpreter hook installed and the
  interpreter genuinely never ran swap-ON.** (invokes=0 is a real observation, not a dead
  probe — H1 "hook never installed" is ruled out by the shared-init dispatcher firing.)

## The reframe — HOP-7's static call-chain is an unread live edge (AP19 shape)

HOP 7 read the interpreter's BODY and saw opcode-4's handler call `FUN_1804e8d88`. That
static edge is real in the decompile. But HOP 7 did NOT verify the LIVE dispatcher fires
THROUGH the interpreter — it inferred the runtime path from the static switch structure.
The run refutes it: **the dispatcher `FUN_180779534` is reached swap-ON by a caller that is
NOT the command-stream interpreter `FUN_18251bb1c`.** Same class of trap the trace already
named — a call-graph edge asserted from static structure, refuted by the live run.

What is NOT overturned (still solid, live-reconfirmed this run):
- **HOP 6 stands, reconfirmed:** dispatcher `FUN_180779534` fires 13350× swap-ON, obj
  non-null, item-list EMPTY on every fire (`list_empty=13350`; per-fire `passa_dispatch`
  `obj=…504992 item_count=0`, tid=37044 = render thread). The render-pass machinery runs;
  the item list is never filled.
- **HOP 3 stands:** the 3-condition-gate pass-caller-a `invokes=0` swap-ON; pass-caller-b
  (Bink, dropped HOP 4/5) runs 7451× but gated out (`tech_not_ready=7450`).
- **The IB/geo picture stands:** `ib_tally apply_invokes=0` swap-ON (the FUN_180501cb0
  indexed apply path is bypassed, HOP 2).

## The corrected map — what "the dispatcher runs but nothing enqueues" now means

Two live facts bound the frontier:
1. The dispatcher (render-pass caller) RUNS 13350× swap-ON but its item list is EMPTY.
2. The command-stream interpreter that HOP 7 named as the driver NEVER runs swap-ON.

So the question is no longer "what is in the interpreter's stream" — the interpreter is a
swap-OFF-only path (or a different-frequency path). The live swap-ON frontier is:
**what fills (or fails to fill) the dispatcher's render-item list `[obj+0x308..0x310]`,
on the caller path that actually reaches the dispatcher swap-ON** — which is NOT the
interpreter. The empty-list-with-machinery-intact signature (HOP 6/7 §"REFRAME") still
points upstream of the renderer: the scene/visibility set that would append compiled render
objects as pass items is producing nothing to enqueue swap-ON.

## OWED before HOP 9 (do NOT skip)

1. **Swap-OFF baseline for the interpreter arm** — set `kcdx-noswap`, relaunch, read
   `cmd_stream_tally`. Decides between:
   - interpreter fires swap-OFF (invokes>0) → the probe/RVA is CORRECT and the swap
     SUPPRESSES the interpreter path → the interpreter IS a real submit driver, just not
     the one reaching the dispatcher swap-ON (two submit paths; swap kills the interpreter
     one). Frontier = why the swap suppresses the interpreter path + who else fills the
     dispatcher list.
   - interpreter NEVER fires either arm (invokes=0 both) → the RVA `0x251bb1c` or the ABI
     assumption is wrong (the interpreter isn't the hot render driver at all) → HOP-7's
     whole interpreter identification is suspect; re-verify `FUN_18251bb1c`'s role before
     building on it.
2. Only AFTER the swap-OFF arm decides #1 → design HOP 9 against the CORRECT frontier.

## SWAP-OFF BASELINE RESULT (log `kcdx-dev_2026-07-03_17-18-16.log`, kcdx-noswap PRESENT)

`probe_f_swap_suppressed` confirms the noswap arm ran (vtable swap + index build SKIPPED,
all other kcdx init identical). Boot reached the interactive menu, renders normally.
`render_trace_armed sites_armed=10` — same 10 arms as swap-ON.

| tally | fn | swap-ON | swap-OFF |
|---|---|---|---|
| `cmd_stream_tally` | interpreter `FUN_18251bb1c` | 0 | **0** |
| `passa_dispatch_tally` | dispatcher `FUN_180779534` | 13350, `list_empty=13350`, `would_call=0` | **1592, `list_empty=0`, `would_call=1592`** |

**OWED #1 resolved — the SECOND branch: the interpreter NEVER fires, either arm.** invokes=0
on the normally-rendering menu too. The hook installed (dispatcher on the shared init fired
1592×; no arm error). So `FUN_18251bb1c` at RVA `0x251bb1c` is **NOT the hot render-command
driver** — HOP-7's interpreter identification is REFUTED by the run, not merely suppressed by
the swap. Do NOT build on the "command-stream interpreter" model; it was a static-only lead
the live trace killed. (If the interpreter is real at all, it runs at a frequency/path the
render hot loop never touches — irrelevant to KI-0028.)

**The dispatcher differential IS the confirmed frontier — the whole KI-0028 divergence in one
row.** Same dispatcher `FUN_180779534`, both arms:
- **swap-OFF:** fires 1592×, `list_empty=0` (render-item list FILLED every fire), `would_call=1592`
  (pass A actually called) → menu renders.
- **swap-ON:** fires 13350×, `list_empty=13350` (list EMPTY every fire), `would_call=0`
  (pass A skipped) → black.

The renderer + dispatcher are intact on BOTH arms. **The swap breaks the ENQUEUE of items into
the dispatcher's render-item list `[obj+0x308(begin)..0x310(end)]`.** HOP 6 saw the empty list
swap-ON; this baseline proves the SAME dispatcher FILLS that list swap-OFF. Frontier, unambiguous:
**what appends items to `[obj+0x308..0x310]` — and why the FS-takeover swap starves it swap-ON.**
The interpreter detour is closed; the enqueue path is the target.

(Higher swap-ON fire count — 13350 vs 1592 — is consistent with the black arm spinning the
render loop faster with nothing to submit; not load-bearing, noted.)

## HOP 9 — find the item-enqueue leaf that fills [obj+0x308..0x310]

The dispatcher CLEARS the vector inline (`*(obj+0x310)=*(obj+0x308)`, HOP 7 static) and READS
begin/end to walk items. The APPEND (push-back writing `+0x310` end-ptr) is a separate leaf,
called earlier in the frame on `obj` (the pass object `[dispatcher.param_1+0x378]`). HOP 7's
7b module-wide append-scan HUNG (whole-module decompile trap) — so it was never named. HOP 9
resumes THAT (the append-leaf hunt), now correctly scoped:
- Reuse-first: re-read `_research/.../_hop7b_append_scan.txt` + `_hop_caller_up_1805029f0.txt`
  for any partial before re-running Ghidra.
- Targeted decomp ONLY (single-function; never whole-module — the hang trap). Find the fn that
  writes `[obj+0x310]` (end-ptr advance) near reads of `[obj+0x308]`, i.e. the push-back into
  the pass-item vector; that leaf's caller is the scene/visibility walk the swap perturbs.
- Then a live arm on that leaf, both arms: fires swap-OFF (fills), absent/early-returns swap-ON
  (starved) → the enqueue leaf is confirmed; walk ONE up to the swap-perturbed producer.

## Method note

The pre-committed HOP-8 outcome map had THREE branches (len=0 / len>0-missing-build /
same-stream), all conditioned on `invokes>0`. The actual result — `invokes=0` — was OUTSIDE
the map. That is not a probe failure; it is the map's leading assumption (the interpreter
runs swap-ON) being falsified. Correct response: do not force the result into a pre-committed
branch; re-observe (the dispatcher cross-check in the same log did exactly that and sealed
it). The swap-OFF baseline is the ground-truth re-observation that seals whether the arm
CAN catch the interpreter at all.
