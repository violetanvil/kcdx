# KI-0028 differential trace — HOP 4: pass A/B's LIVE callers (runtime-captured)

**Date:** 2026-07-03 · **Method:** `_ReturnAddress` capture at pass A/B entry (PROBE Z10
HOP-4 extension, module-attributed unique-caller tables). swap-OFF log `12-08-24`.
**Trust:** primary (runtime capture on the working arm).

## RESULT — each pass has EXACTLY ONE live caller

| pass | live caller ret-addr | count (≈2/frame A, 1/frame B) | identity |
|---|---|---|---|
| A (0x4ec3a0) | **WHGame+0x77956D** (mod_off 7837037, VA 0x18077956d) | 16576 | ret of call site **0x180779568 in FUN_180779534** — the FIRST static caller on the `_hop3_caller_a` list |
| B (0x5014a0) | WHGame+0x867D88 (mod_off 8813960, VA 0x180867d88) | 8288 | not on `_hop3_caller_b`'s list — that list was CAPPED at 24 entries; decomp names it |

(Hex-conversion trap hit once: 7837037 was first mis-read as 0x779A6D — the Ghidra
site-context showed float math with NO call instruction, the tell for a wrong address.
Correct: 0x77956D = call site 0x180779568 + 5. Verify decimal→hex with a tool, not
in-head.)

So of pass A's 24 static call sites, exactly ONE is live for the menu scene:
`FUN_180779534` (entry 0x180779534, in the render-thread 0x77xxxx cluster near the
flush fn 0x777f6c). The other 23 are cold for this scene.

Gate sanity this run (swap-OFF): pass A invokes=18302, gate_pass=18302 (100%); pass B
invokes=9151, tech_not_ready=9150 (red herring, per HOP 3).

swap-ON side of this instrument needs NO launch: HOP 3 proved pass A invokes=0 swap-ON ⇒
its caller table is empty by construction. The next question is static.

## HOP 5 (static, DONE) — pass A's caller GATES it on the render-item list being non-empty

`FUN_180779534` (0x779534, 296 bytes, `_hop5_live_caller_a_18077956d.txt`), single static
caller `FUN_1804e8d88`:

```c
void FUN_180779534(longlong param_1) {
  puVar1 = *(param_1 + 0x378);                 // the render-pass object
  if ( ((*(puVar1+0x310) - *(puVar1+0x308)) >> 3) != 0 ) {   // item list NON-EMPTY?
      FUN_1804ec3a0(puVar1 + 0x70);            // <-- PASS A, ctx = puVar1+0x70
      FUN_180501694(puVar1 + 0x70);
      *(puVar1+0x310) = *(puVar1+0x308);       // clear the list after submit
      *(puVar1+0x301) = 1;
  }
  ... (buffer recycle / release) ...
  *(param_1 + 0x378) = 0;                      // detach the pass object
}
```

**The item list checked is `[puVar1+0x308 .. +0x310]` — and pass A's ctx is `puVar1+0x70`,
so `passA_ctx+0x298 == puVar1+0x308`: this is the EXACT list the HOP-3 pass-gate sampler
read.** Pass A is called ONLY when that list is non-empty. This closes the loop with HOP 3:
swap-ON `list_empty`, so the `if` is false, pass A is skipped → invokes=0. There is no
separate "who calls pass A" mystery — its caller runs every frame and *decides* by the list.

**Pass B's caller `FUN_180867990` (0x867990) is the BINK VIDEO player** (string "BinkYCrCb",
YCrCb→RGB, aspect-ratio letterbox math) — the menu background video pass. Confirms pass B is
unrelated to world geometry; drop it.

## The sharpened frontier (HOP 6) — WHO fills the render-item list, and does its filler run swap-ON?

The divergence is now precise: **the render-item list `[obj+0x298..0x2a0]` (obj =
`[FUN_180779534.param_1 + 0x378]`) is EMPTY swap-ON, non-empty swap-OFF.** Two sub-questions,
one probe each:
1. Does `FUN_180779534` even FIRE swap-ON (caller runs, list empty) or is it never reached
   (the whole render-pass dispatch is gone)? → hook 0x779534, log fire + the item count
   `([obj+0x310]-[obj+0x308])>>3` at entry, both arms.
2. If it fires with an empty list → who ENQUEUES into `[obj+0x298..0x2a0]`? (static: xref the
   writes to `obj+0x2a0`, the vector-end pointer; that append leaf is the next decompile.)

HOP 6 probe = sub-question 1 (one site, fire + count, both arms) — built next.

## HOP 6 RESULT (swap-ON, log `12-19-23`) — the dispatcher RUNS; the list is EMPTY every frame

| swap-ON `passa_dispatch_tally` | value |
|---|---|
| invokes (FUN_180779534) | **6722** — runs every frame |
| list_empty (item_count==0) | **6722 / 6722 (100%)** |
| would_call (item_count!=0) | **0** — pass A never called |
| obj_null ([param_1+0x378]==0) | 0 — the pass object is present, just its list is empty |

**Sub-question 1 answered: caller-absent is FALSIFIED.** The render-pass dispatch runs
normally swap-ON (6722×, obj non-null); it finds the render-item list
`[obj+0x308..0x310]` EMPTY every single frame and correctly skips pass A (invokes=0,
matching HOP 3). Nothing downstream of the enqueue is broken — the geometry is simply
never enqueued.

**Frontier (HOP 7) — the render-item ENQUEUE into `[obj+0x298..0x2a0]`.** obj is the
pass object `[FUN_180779534.param_1 + 0x378]`; its item vector is `[obj+0x308(begin),
+0x310(end)]` (== passA_ctx `[+0x298,+0x2a0]`). Who appends? Static: xref writes to the
end-pointer `obj+0x310` / the vector-grow. This is the scene-traversal / cull / render-
item-build that feeds the pass — the compiled objects (CCRO, Step-1) exist but are never
turned into enqueued items swap-ON.

**HOP-6 swap-OFF baseline SKIPPED (redundant — not a new fact):** it would only show
`would_call>0`, which HOP 3/5 already proved (pass A gate_pass=3952/3952 = 100% swap-OFF
⇒ the list IS non-empty on the working arm). Re-running it re-observes a settled fact
(results-driven §5 — act on the result, don't re-confirm the known). HOP 7 goes straight
to the enqueue: static xref of writes to `obj+0x310` (the item-vector end-pointer), name
the append leaf, then one runtime probe on both arms (does the enqueue run swap-ON? how
many items?).
