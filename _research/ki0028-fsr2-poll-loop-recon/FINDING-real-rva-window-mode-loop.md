# KI-0028 — the wedge frame is a WINDOW/DISPLAY-MODE loop, not entity-init (offset-vs-RVA correction)

**Date:** 2026-06-21
**Method:** static disassembly (`disasm_36eb39_outer_loop.py`), no launch.
**Trust:** primary evidence (binary read + three independent cross-checks). NOT hypothesis.

## The correction (load-bearing — overturns the KI's "entity-init" identification)

The PROBE L wedge stack frames are labeled **`WHGame!ffxFsr2ResourceIsNull+0x36eb39`**
etc. (`cdb_pl_probeL_wedge.txt` lines 274–287). `ffxFsr2ResourceIsNull` is a
**nearest-export label** — handoff §2.6 says discount it (the real function is MB past
the export). The KI body + `disasm_pj3_compute_frame.py` stripped the prefix and
disassembled the bare offset `0x36eb39` **as a raw RVA**, landing in an unrelated
entity-name-registration stub (which coincidentally holds `"dummy_no_ai"`/`"player"`/
GUID strings) — and concluded "the wedge is entity/AI init." That identification is an
**offset-vs-RVA conflation artifact.**

Real RVA = `ffxFsr2ResourceIsNull` export (`0x4fb100`) + offset:

| cdb frame | raw offset read as RVA (WRONG) | REAL RVA (= 0x4fb100 + off) |
|-----------|-------------------------------|------------------------------|
| `…+0x36eb39` (the "entity-init" frame) | 0x36eb39 (entity stub) | **0x869c39** |
| `…+0x36ff17` (frame above poll) | 0x36ff17 | **0x86b017** |
| `…+0x36af90` (the bounded focus poll) | 0x36af90 | **0x866090** |
| `…+0x16c7a0` ("update dispatcher") | 0x16c7a0 | 0x6678a0 |

**Cross-check that proves the offset base (3 independent):**
1. `0x4fb100 + 0x36af90 = 0x866090` = Main's confirmed focus-poll RIP (KI line 466 /
   FINDINGS: `0x866090 = inc edi` after `call Sleep`; poll body RVA `0x865fb4`). Exact match.
2. Disassembling raw `0x36eb39` finds a 0x159-byte tail-dispatch stub with **0 back-edges**
   — cannot be the looping wedge. Disassembling real `0x869c39` finds a real function with
   **6 back-edges enclosing the wedge call site.**
3. The real function carries the cvar string **`"r_Fullscreen"`** and polls the
   window-manager singleton at `0x492b890` (KI line 436–437 already ID'd `0x492b890` as
   "a window/system-manager singleton, gEnv-family… non-NULL at runtime").

## What the real function (RVA 0x869c39) actually is

A **window / display-mode / fullscreen management** routine. Evidence in its body:
- `lea rdx, "r_Fullscreen"` at `0x869b7f` → `call [rax+0xb8]` (a cvar/console get on a manager).
- Repeated vtable calls on the `0x492b890`-cluster singletons (`[+0x80]`, `[+0x740]`,
  `[+0x2a0]`, `[+0xb8]`, `[+0x10]`, `[+0x40]`) — `.data` manager-singleton pointers
  (`0x492b880/8a8/8c0/890/908`), the window/display manager family.
- The wedge call site is `0x869c36 call qword [rax+0x40]` (into the `0x549b4a0` object),
  return-into `0x869c39` — exactly the live stack's frame.

## The outer loop (the previously-unread load-bearing fact)

The call site sits inside two enclosing back-edges, both gated by **retry counters in `.data`**:
- `0x869c6f jne -> 0x869bb9` and `0x869c90 jmp -> 0x869bb9`, gated by
  `0x869c68 cmp dword [0x56628dc], -1`.
- `0x869ca8 jne -> 0x869b2d` and `0x869cc0 jmp -> 0x869b2d`, gated by
  `0x869ca1 cmp dword [0x56628d8], -1`.
- The loop re-enters at `0x869bb9`/`0x869b2d`, re-runs cvar/window-manager vtable calls
  (`call [rax+0x2a0]`, `call [rax+0x10]`, `call [rax+0x40]`), and compares results against
  `.data` flags `0x556d080` (byte) / `0x556d084` (dword) and the counters `0x56628d8/dc`.

So the infinite repetition is a **display-mode/window-state apply-and-poll loop** that
re-runs until a window/display-manager state (read through the `0x492b890`-family
singletons + the `0x556d080/084` flags) reaches the value it waits for — which, under the
FS-takeover swap, never does. This is consistent with the EARLIER static recon (KI line
430–453) that read the inner helper `0x866090` as a `GetActiveWindow`-vs-expected-handle
poll — the inner poll is bounded; THIS outer loop (0x869c39) is the unbounded one.

## What this means for the root-cause hunt

- The subsystem is **window/display-mode/fullscreen bring-up**, NOT entity/AI init. Every
  "entity-init" / "CreateInstance entity construction" statement in the KI trail downstream
  of the `0x36eb39`-as-RVA read is the same artifact and should be downgraded.
- This RECONCILES the otherwise-awkward facts: the menu video decodes + loops (RenderThread
  fine), audio plays, the tick advances — yet no frame presents and input is dead. A
  display-mode/fullscreen apply loop that never completes would leave the swapchain
  un-presentable (PROBE K: present frozen + swapchain `ERROR_BUSY`) while everything else runs.
- USER EVIDENCE that fits (KI line 450): a launch WITH window focus acquired still hung —
  the gate is not bare `GetActiveWindow`-match (the inner bounded poll), it is this OUTER
  display-mode loop's completion condition.

## Still UNKNOWN (do not overclaim — the mechanism is not yet pinned)

- **WHICH state the loop waits on, and HOW the FS swap perturbs it.** The loop reads
  window-manager singletons + `.data` flags; it is NOT yet shown which one the swap leaves
  wrong, nor by what path (the freeze window is FS-silent, so it is a state set EARLIER, or
  a non-FS side effect of the swap). The next probe must OBSERVE the loop's exit condition
  live (the values of `0x556d080`/`0x556d084`/`0x56628d8`/`0x56628dc` and the
  `0x492b890`-family vtable returns) swap-on vs swap-off — theory-independent.
- The two retry-counter globals at `0x56628d8`/`0x56628dc` are compared against `-1`
  (sentinel "not set"?) — their semantics are unread.
- `0x549b4a0` (the object the wedge `call [rax+0x40]` dispatches into) is unidentified.

## DEEPER READ (2026-06-21) — the exit condition is a critical-section-guarded completion token

`disasm_869c39_exit_cond.py` + the two helper disasms pin the loop's exit semantics:

- The loop body at `0x869c39` re-runs via `0x869c6f jne -> 0x869bb9` / `0x869ca8 jne ->
  0x869b2d`, each gated by `cmp dword [<counter>], -1; jne`. **It re-loops WHILE the
  counter is `!= -1`; it falls through to the `ret` (clean exit) when the counter is `-1`.**
  Two counters: `0x56628d8` and `0x56628dc` (both `.data`).
- Before each re-check the loop calls **`fn 0x1c1e988`** (passed the counter addr in rcx):
  `EnterCriticalSection` (`call [rip+...]`) → `cmp [counter],0; jne skip` →
  **`mov [counter], 0xffffffff`** (sets the `-1` "done" sentinel) ONLY if the counter was 0.
- **`fn 0x1c1e91c`** is the registration/increment side: `EnterCriticalSection` →
  `inc` a global id (`0x...`) → store it into `*counter` AND into a TLS slot
  (`gs:[0x58]` + index*8) → `LeaveCriticalSection`. A deferred-task / once-registration.
- The loop also reads result flags `0x556d080` (byte) / `0x556d084` (dword) set from the
  return values of vtable calls on the window-manager singletons (`[0x492b8c0]+0x2a0`,
  `[rsi]+0x10` where `rsi` = the `r_Fullscreen` cvar object), and the wedge call site
  `0x869c36 call [[0x549b4a0]+0x40]`.

**Mechanism shape (pinned to a class, NOT yet to the exact actor):** this is a
**critical-section-guarded producer/consumer completion handshake**. A display-mode
operation registers a task/id (`0x1c1e91c` sets the counter nonzero), then the loop spins
re-checking until the token flips to `-1` (= done, via `0x1c1e988` when the work reports 0).
Under the FS-takeover swap the completion never arrives → counter never reaches `-1` → the
loop never exits → Main re-enters the bounded focus-poll `Sleep` every iteration (which is
why every `-pv`/invasive sample catches it at `0x866090`).

This is **cross-thread** (a critical section + a TLS-indexed id => a producer on another
thread is expected to complete the task). That reconciles P-B (vanilla boots — the producer
runs) and P-F (swap is the differentiator — the swap stalls the producer or the state it
needs). The freeze being FS-SILENT fits: the perturbed state was set EARLIER (or is a
non-FS side effect of the swap), not an in-progress file op at wedge time.

## STATIC IS NOW EXHAUSTED (results-driven §4 boundary)

What static CANNOT settle, and the live probe owed:
- The live VALUES of `0x56628d8` / `0x56628dc` at the wedge (is it stuck nonzero = task
  registered, never completed? or `-1` already = the loop is NOT the wedge and the re-entry
  is from above each frame?).
- WHO registered the id (`0x1c1e91c` caller) and WHICH thread is the expected producer that
  flips it — and whether that producer is alive / stalled / never spawned under the swap.
- What `0x549b4a0`'s `[+0x40]` and the `r_Fullscreen`-object's `[+0x10]` return live.

**Owed live probe (theory-independent, swap-on vs swap-off):** read these exact globals +
the critical-section owner from a zero-perturbation watcher (or an invasive cdb `dd`
on `0x56628d8`/`0x56628dc` + `!cs` on the section `0x1c1e988`/`0x1c1e91c` enter). Outcome
map: counter stuck nonzero swap-on but reaches `-1` swap-off => the swap stalls the producer
of THAT task => identify the producer; counter already `-1` at wedge => this loop is not the
wedge, the per-frame re-entry from above is, widen up the stack. Fix stays in kcdx full-init
ownership (no thunk-back) on every branch.

## PROBE M RESULT (RAN 2026-06-22, swap-ON vs swap-OFF) — the loop's exit-condition globals are NOT the differentiator; the static loop theory is FALSIFIED

Live read of the six exit-condition globals (`loop_state_probe.{h,cpp}`, watcher thread,
WHGame_base + RVA), two launches: run 1 swap-ON (wedged, black screen, ~3 min),
run 2 swap-OFF (`kcdx-noswap` marker → reached `suite: 319/343`, the menu).

**A/B — the behavior is essentially IDENTICAL in both runs:**

| field | run 1 swap-ON (WEDGED) | run 2 swap-OFF (MENU) |
|-------|------------------------|-----------------------|
| counterA (`0x56628d8`) | `0` → `0x80002B37`, then frozen | `0` → `0x80002B7A`, then frozen |
| counterB (`0x56628dc`) | `0` → `0x80002B38`, then frozen | `0` → `0x80002B7B`, then frozen |
| flagByte (`0x556d080`) | oscillates 1/0 early, settles 0 | oscillates 1/0 early, settles 1 |
| flagDword (`0x556d084`) | `0` → `1` | `0` → `1` |
| singleton0 (`0x492b890`) | null → populated (~+10s) | null → populated (~+7s) |
| singleton1 (`0x492b8c0`) | populated throughout | populated throughout |

Both runs: singleton0 builds, the counters jump from `0` to a high-bit-set nonzero value
(`0x80002B3x` vs `0x80002B7x` — same shape, a per-boot-allocated id differing only in the
low bits, exactly what `0x1c1e91c`'s "inc a global id, store into the counter" produces),
flagDword→1, flagByte toggles. Then everything freezes (run 1 froze ~+71s; run 2 at the menu).

**FALSIFIED (the static loop theory):**
- "The loop exits when the counter `== -1`, and the swap leaves it stuck `!= -1`." The counter
  NEVER reaches `-1` — not even in the swap-OFF run that reaches the menu. It freezes at
  `0x80002B7x` there too. A value that is `!= -1` in the SUCCESS case cannot be the wedge gate.
- "`0x869c39` (the window/display-mode loop) is where the swap stalls boot." Its exit-condition
  globals evolve IDENTICALLY swap-on vs swap-off. The function runs the same way whether or not
  kcdx owns the filesystem → it is normal per-frame code, NOT the differentiator. Its presence on
  the wedged stack is the "a frame on the per-frame loop's stack ≠ the divergence point" trap, one
  level under the nearest-export trap: Reframe 6 already established the whole update loop runs
  every frame, so ANY per-frame frame appears on a sample of the wedged process.

**What this leaves standing (unchanged, still solid):**
- P-F: swap-ON wedges, swap-OFF reaches the menu. The swap IS the differentiator — that holds.
- The wedge is real and downstream of a completed FS takeover, FS-silent at wedge time.
- But the divergence is NOT observable in `0x869c39`'s loop state. The swap perturbs something
  ELSE; this loop is a red herring promoted by being on the stack.

**REFRAME (honest, no theory-hop):** static located a real function but the theory that it is the
wedge gate is dead. The probe target must go back to "what does the swap-ON path do that the
swap-OFF path does NOT" — observed directly, not inferred from a stack frame. P-F's swap-ON arm
still carries the unseparated differentiators (FS dispatch live + the index-build wait); the next
probe should isolate WHAT the swapped CCryPak serves/changes that diverges the boot, NOT another
WHGame global guessed from a static read of a per-frame function. The wedged-stack frames
(`0x869c39` etc.) are confirmed per-frame noise — stop chasing frames on that stack.

## STATIC SLOT-DIFF (2026-06-22) — 4 VERIFIED return-contract divergences; the wedge is in a SLOT OUTPUT, not a stack frame

After PROBE M killed the stack-frame chase, the right static target was the swap's OWN output: kcdx
swaps the CCryPak vtable per-slot (`src/fs_takeover/vtable_table.cpp` — MOST slots THUNK to the engine
original; a KCDX-owned family is replaced). The swap-ON vs swap-OFF divergence IS exactly that owned
set. A 4-way parallel static diff (each KCDX slot's return contract vs the engine original it replaces;
reuse-first off `_research/phase8.5-pak-resolver`, `ki0027-*`, the DB) found four VERIFIED divergences,
all matching the KI-0028 signature (FS-silent, served-early, surfaces-late):

| # | slot(s) | divergence (kcdx vs engine) | confidence | rank |
|---|---------|------------------------------|-----------|------|
| **A** | metadata 67/70/45/92/93 | **existence-TIMING / pakPriority-bypass** — kcdx's index reports every pak vpath EXISTS/sized from `CSystem::Init` onward, bypassing the engine's `pakPriority`/`location`/mount-timing gates → premature TRUE/size for a pak asset the engine original would call not-yet/not-here | kcdx side VERIFIED; boot-consumer branch INFERRED | **1** |
| **B** | find 63/64/65 | **handle-type straddle** — kcdx FindFirst returns a small int `(id<<1)\|1`; the engine returns a **refcounted `CCryPakFindData` object** pointer. A boot consumer that does anything but `-1<h`+pass-back (derefs `obj[1]`/`obj[3]`, refcounts, object-releases) operates `(void*)3` | handle types VERIFIED; the derefing consumer UNVERIFIED | **1** |
| **C** | slot 1 AdjustFileName | **un-normalized pak path** — on a pak hit kcdx returns raw `pName` (`%engine%/…`); the engine returns the normalized Data/-rooted path. A consumer branching on the returned string's FORM (not re-opening via FOpen) diverges. kcdx's "the consumer is always kcdx_FOpen" is an unproven runtime-mechanism claim | both bodies VERIFIED; consumer assumption UNVERIFIED | 2 |
| **D** | slot 66 FGetModificationTime | **pak mtime = 0** — kcdx returns epoch for a pak asset; the engine returns the entry's DOS time. A boot cache-freshness check (`source_mtime` vs `cache_mtime`) could mis-fire / never-settle | kcdx VERIFIED; engine DOS-time INFERRED; consumer UNVERIFIED | 3 |

Exonerated by the diff: the FOpen read-handle opaque-straddle (P3 — no engine consumer operates a
FOpen-class handle off-vtable; kcdx owning the whole read family is what makes the id safe). FGetSize
returning 0-not-(-1) on a bad handle is the KI-0026 FIX, not a regression.

**A and B are the strongest** — both FS-silent, both in slot families with prior FATAL precedent
(KI-0026 = metadata existence at graphics-init; KI-0027 = find-slot wrong-set fatals a load). The
wedge is "the next boot blocker after the table DB loads" — same find/metadata families, next consumer.

**THE ONE UNREAD LINK (every subagent flagged it, AP19):** WHICH boot consumer branches on these
answers. That call-edge is the missing piece between the divergence and the wedge — and it is STATICALLY
REACHABLE: find the graphics/window/swapchain/present-init code that calls slot 67/70/45 (existence
gate) or slot 63 (FindFirst) during the boot window, and read whether it branches on / derefs the
result. That is the owed next static step, BEFORE any launch. (A live probe is the fallback: instrument
slots 67/70/45/63/66 at boot to log `(vpath, kcdx-answer, would-be-engine-answer)` swap-on vs swap-off —
but read the consumer statically first; the diff already narrowed the target to ~5 slots.)

Provenance: 4 subagent digests, reuse-first off existing `_research/` Ghidra dumps (no fresh cold
analysis). kcdx-side contracts VERIFIED from source; engine-side from prior body-reads. Every
boot-consumer branch is UNVERIFIED (the unread link above).

## CONSUMER-SIDE STATIC READ (2026-06-22) — divergences A and B FALSIFIED by reading the real consumers

The slot-diff's one unread link (which boot consumer branches on the divergent answers) was read
statically. Both rank-1 candidates are killed — not by guessing, by reading the binary:

**A (existence-timing) — FALSIFIED as a DIRECT wedge driver.** A provenance-verified xref of the
`gEnv->pCryPak` global (`0x18492B850` = gEnv+0x50; 680 loads, matching phase8.5's count) found all 44
genuine existence/size consumers: slot 70 = **0 callers**; slot 67 = 41; slot 45 = 3. **Every one is an
asset / level / data loader — NONE is a window/swapchain/present/display-mode/device consumer.** The two
strongest boot consumers, body-read: `0x89682d` (`Menu.gfx` required-asset gate — a premature-TRUE
MASKS a fatal, doesn't wedge) and `0x244dd9c`/`+0x244deef` (menucommon level-cache `GetFileSize` gate —
the F6 size-mismatch shape, but gates level-pak loading, not present). So an existence-timing divergence
cannot DIRECTLY drive the no-present wedge. (Recon: `_research/ki0028-metadata-consumer-recon/`.)

**B (find-handle straddle) — FALSIFIED.** The engine FindClose body (`0x18097383c`) was read — it IS
the kill site (derefs `handle+0x8`, virtual-calls through `handle+0x0`, so a kcdx integer `3` WOULD
fault). Then ALL 53 genuine find-triplet consumers in WHGame were classified: **every one treats the
FindFirst return opaquely** (`-1<h` + pass-back to slots 64/65). The 5 a mechanical deref-scan flagged
were decompiler variable-reuse false positives, each body-read and cleared; 9 more were `+0x1f8`-on-a-
different-object collisions. **No consumer derefs the handle — kcdx's integer is safe against all of
them.** (Recon: `_research/ki0028-findfirst-straddle-recon/` — `_dump.txt`, `_triplet.txt`, 2 java workers.)

**C (un-normalized pak path, slot 1 AdjustFileName) — FALSIFIED as a DIRECT wedge driver.** Adapted the
same provenance-verified pCryPak xref to vtable +0x8: **37 call sites in 31 enclosing funcs**, every one
a FILE-OP consumer (result fed straight to FOpen/IsFileExist3/an fopen wrapper/a CryString copy-then-open/
FWrite/a config-XML open). **Zero funcs branch on the string's FORM** — no `%engine%`-vs-`Data/` prefix
test, no compare against a root literal; the only `cmp byte` is a generic trailing-separator append on a
copied local. The boot-relevant ones, body-read: `0x1e03c30` (DriverD3D.cpp — copies to a MAX_PATH local,
normalizes separators, uses as filename; never inspects root form), `0x245b5cc` (r_WindowType) +
`0x245df70` (r_Fullscreen — each builds a path → FOpens/streams; branches on a cvar global / the FOpen
handle, never the path form), `0x241b340` (level-cache → slot-67 existence bool, the same gate already
read from the 67 side). **No window/present/display consumer branches on the path form** — kcdx's
"consumer always re-resolves via a file op" assumption holds across all 31.
(Recon: `_research/ki0028-adjustfilename-consumer-recon/`.)

**D (pak mtime=0, slot 66 FGetModificationTime) — FALSIFIED.** Adapted the xref to vtable +0x210:
**3 vtable consumers**, and a whole-`.text` scan for direct rel32/address-taken callers of the engine
body (`RVA 0x241a3bc`) found **0** — so the 3 are the complete set, mtime is NOT reached via a wrapped
helper. Engine original's return CONFIRMED (was inferred): body `0x241a3bc` returns the pak entry's DOS
time on the in-pak arm (`[entry+0x28]`), OS file-time on the miss arm; kcdx returns 0 on the in-pak arm —
premise verified. **None of the 3 compares or gates on the mtime:** `0x9a2074` (boot subsystem-pointer
cache-assembly — stores the return to a global that a whole-`.text` scan shows is **never read back**, no
freshness use), `0x14d5580` (return used immediately as an object `this`), `0x235d7e4` (return passed
forward as a call argument). **The feared "mtime=0 → cache always-stale → never-settling rebuild" shape
does not exist in the consumer set; no boot/render/present consumer.** (Recon:
`_research/ki0028-mtime-consumer-recon/`.)

**Where static has now CONCLUSIVELY localized:** ALL FOUR return-contract divergences (A existence-timing,
B find-handle straddle, C un-normalized path, D pak mtime=0) are read on the consumer side and killed as
DIRECT wedge drivers — **no window/present/swapchain/display-mode consumer branches on any divergent slot
value.** This is a strong negative reached by reading real consumer bodies, not by inference. Residual
paths, precisely defined:
1. **Indirect / multi-hop:** a divergent value consumed at boot (a metadata premature-TRUE, a
   stored/forwarded mtime later diffed against a fresh stat) makes a downstream load take a wrong arm
   whose effect surfaces LATER as the un-presentable swapchain — N stages and threads removed from the
   slot call. NOT statically traceable (would require whole-engine symbolic data-flow across threads on a
   stripped binary); a live swap-on/off probe is the only path. Concrete probe targets surfaced by the
   consumer reads: `0x244dd9c`/`0x89682d` (A's level-cache + Menu.gfx gates), the `0x9a2074` cache-
   assembly globals (D's residual).

**C's residual is CLOSED by reading kcdx's own source (not just falsified).** The C-residual was
hypothesized as "the `%engine%/...` un-normalized form could mis-resolve inside kcdx_FOpen if its alias
re-resolution mismatches the index key (the KI-0026 class)." Read `src/fs_takeover/open_slots.cpp`: the
un-normalized return is a DELIBERATE, documented design (lines 344-393), not a divergence to chase. On a
pak hit, `kcdx_AdjustFileName` returns `pName` UNCHANGED specifically so the engine's subsequent
`kcdx_FOpen` re-resolves it through the SAME `asset_overlay::NormalizeVPath(pName)` +
`ExpandEngineAliasToIndexKey` strip that hit the pak entry — both `kcdx_AdjustFileName` (line 300) and
`OpenResolvedAndMint` (line 168) key the index through the IDENTICAL `NormalizeVPath(pName)` call, so the
form round-trips to the same key in both. Returning a `Data/`-normalized loose string instead would be the
BUG (FOpen's `engine/`-prefix would miss the strip → loose open → errno=2 → the 0xC8 fatal — the exact
KI-0026 failure the unchanged-return AVOIDS). So C is not a wedge lead at all; the divergence is the
correct behavior. C is fully closed.

**Honest status of "find it with static":** static FOUND the divergences (4, verified) AND read ALL of
their consumers, killing every one as a direct driver — the static method run to its conclusion, not
failing. What static canNOT settle is the INDIRECT multi-hop path (a wrong value consumed at load that
surfaces at present N stages later, across threads) — a runtime-only fact (results-driven §4). Static work
on the slot outputs is now COMPLETE; the remaining work is the live multi-hop probe.

## Ground-truth synthesis (2026-06-22) — the probe target the slot-output exhaustion points to

With all four slot-output divergences exonerated as direct drivers, the live evidence re-read end-to-end
converges on ONE concrete probe target. The facts, each from a source on disk:

- **The wedge is STEADY-STATE, not first-tick.** PROBE H (`boot_watch.h`:7-8) recorded: boot progresses
  ~41s, the update tick fires, the suite reaches 320 — THEN the dev log goes silent at the wedge onset.
  So `HookedUpdate`'s heavy first-tick synchronous block (NotifyVmReady → RegisterKcdxTable → RunCatalog →
  RunAll → ApplyAll → LogInventory, hooks.cpp:359-503) COMPLETED — plugins ran, suite advanced. The wedge
  comes later, on a steady-state tick. (This kills the tempting "stuck in kcdx's first-tick block" theory
  before any probe was built around it.)
- **Main parks in the `r_Fullscreen` window/display-mode loop.** The wedge stack's Main frame resolves to
  real RVA `0x869c39` (§ above) — a window/display-mode routine carrying `lea rdx,"r_Fullscreen"` that
  polls the window-manager singleton `0x492b890`. The job-worker pool (threads 3-6+ in the wedge capture)
  is idle in `SleepConditionVariableSRW` waiting for work Main would queue; Main never completes the tick,
  so no render work is queued, nothing presents, audio (separate thread) continues. "Runs but never
  presents" = Main parked in this loop.
- **The loop body itself is NOT the differentiator.** PROBE M's swap-on/off A/B showed the loop's counters
  evolve IDENTICALLY both ways (both freeze at 0x80002B7x, neither reaches -1). So the loop is normal
  per-frame window/display-mode code; what differs is the EXIT CONDITION it waits on never arriving with
  the swap on.

**The convergent probe target — corrected against PROBE M (do NOT re-run the killed probe).** The naive
"read the loop's exit-condition state swap-on/off" probe IS PROBE M, already run: it read the 6 `.data`
globals the `0x869c39` loop polls (counters `0x56628d8/dc`, flags `0x556d080/084`, window-mgr singletons
`0x492b890`/`0x492b8c0`) swap-on vs swap-off and found them **identical** — the directly-polled state does
NOT differ. So the loop's own polled fields are exonerated; re-reading them is theory-hopping back to a
killed probe. Two honest branches remain, and the probe must pick the FALSIFYING one:

1. **The loop is NOT the gate (PROBE M's null leans here).** If the polled state is identical swap-on/off
   yet the boot wedges only swap-on, Main being parked at `0x869c39` may be a SYMPTOM (where Main happens
   to spin) rather than the CAUSE. The differentiator is then elsewhere on Main's path — an earlier tick
   does something different swap-on that prevents the state the loop awaits from EVER being produced
   upstream. Ground-truth probe: bracket Main's steady-state tick (the `HookedUpdate` per-tick body +
   the engine Update it wraps) on the LAST few ticks before the heartbeat stops, swap-on vs swap-off, and
   log the first per-tick action that diverges. PROBE H already marks the onset; this reads what the tick
   does in the final pre-onset window.
2. **An UPSTREAM writer of the polled state differs.** The loop waits for a field some OTHER code writes
   (a render-device-ready flag, a swapchain-created signal, a window-shown event). PROBE M read the field
   VALUES; it did not find the WRITER. Probe: locate the writer of the one field the loop's exit tests
   (static — read `0x869c39`'s exit branch to find which `0x492b8xx` field gates exit, then xref its
   writer), then instrument whether that writer RUNS swap-on vs swap-off.

Branch 2 is statically startable (find the exit-gating field + its writer) and is the cheaper first move;
branch 1 is the live fallback if the writer is found to run identically. Both are theory-INDEPENDENT: each
has an outcome that says "the loop is not the gate, widen the frame" rather than only confirming a guess.

## CORRECTION (2026-06-22, exit-gate static read) — the loop is NOT a wait; the real gate is GetActiveWindow()

The branch-2 static read (`_research/ki0028-window-exit-gate-recon/`) overturns the loop-as-wedge reading
the whole chain carried — a third offset-vs-meaning correction in this investigation:

- **The `0x869c39` "exit-gating" counters (`0x56628d8`/`0x56628dc`, tested `cmp …,-1` at sites `0x869c68`/
  `0x869ca1`) are a `std::call_once` MAGIC-STATIC GUARD pair, not a cross-thread completion token.** The
  acquire fn `0x1c1e988` claims by `mov [counter],0xffffffff`; the publish fn `0x1c1e91c` drives it to
  `-1`. The SAME thread drives the once to completion — there is NO awaited external producer. The
  `0x80002Bxx` value PROBE M saw the counters freeze at is the normal in-flight call-once id, identical
  swap-on/off **because it is a local guard, not a differentiator.** PROBE M's null is now EXPLAINED, not
  just observed. The "stuck waiting for a producer that never fires" reading (handoff, KI body, every
  prior frame) was wrong — it was a `call_once` guard misread as a handshake.

- **Main's `0x866090` sample is a BOUNDED window-focus poll (`fn 0x865fb4`, ≤5×5 ms), called once per
  tick from the engine tick dispatcher `fn 0x667b24` — NOT an infinite loop.** Main shows up at this RVA
  on every cdb sample because it runs full per-frame ticks and this poll is where the sample lands (the
  per-frame-trap), NOT because it spins here. **Main is TICKING, not deadlocked in a loop.** This is
  consistent with PROBE H (the heartbeat advanced ~41s) and with the "runs but never presents" symptom.

- **The real gate is `GetActiveWindow() == <engine-expected HWND>`** (`test site 0x866029`; the expected
  handle is read from `[this+0x2d0] → [+0x740]`). The engine is waiting for ITS window to become the OS
  ACTIVE/foreground window. Swap-on, that never becomes true. **This matches the symptom exactly:** a
  window that exists but never becomes foreground/active → no input focus (no input), the render/present
  path gated on active-window → black screen, audio (a separate thread, ungated) keeps playing.

**The new, concrete, falsifiable mechanism candidate:** the FS-takeover swap perturbs window
creation/activation so the engine's window never becomes the OS active window — so `GetActiveWindow()`
never equals the expected HWND at `[window+0x740]`, and the engine never advances past the
active-window gate to present. NOT a slot-output divergence (all 4 exonerated); a window-lifecycle effect
of the swap.

**Wedge object identified (prior recon left it UNKNOWN):** `0x549b4a0` (the wedge `call [[0x549b4a0]+0x40]`
target) is a display/render-context object built per-call by factory `fn 0xda65e4` from parent
`0x549b498`, which `fn 0x1865a88` = `CSystem::Init` / CryENGINE bring-up writes (`"Failed to initialize
CryENGINE!"`).

**Next probe (live, theory-independent):** the expected-HWND at `[window+0x740]` is a runtime vtable
getter, not a `.data` field — live-only. Probe = at the active-window gate (`0x866029`), log BOTH
`GetActiveWindow()` AND the expected HWND `[window+0x740]`, swap-on vs swap-off. Outcomes:
(a) swap-on the two HWNDs never converge but swap-off they do → CONFIRMS the window-activation mechanism,
points at window creation/show as the swap-perturbed step;
(b) both converge swap-on too (gate passes) → the gate is NOT the wedge, Main advances past it and wedges
later → widen the frame to the next per-tick stall.
Falsifiable either way; observes ground truth (the two HWNDs) rather than confirming a theory.

## PREMISE CORRECTION (2026-06-22, PROBE W run 1) — the wedge is NOT a deadlock; the game TICKS but never PRESENTS

PROBE W's first live run (swap-ON) overturns the load-bearing premise the whole KI + handoff + every
prior frame carried. Ground truth from the run:

- **The heartbeat advanced CONTINUOUSLY to tick=7710, no stall, right up to the final logged second**
  (~107 ticks/s avg, ~35/s at the end). `BOOT_WATCH_STALL` NEVER fired. Suite reached 320; swap took
  (`seat_index_stored entries=307006`).
- **User-confirmed visual: black screen, no menu — THE WEDGE.** So `CGame_Update` fires ~35×/second the
  entire time the screen is black. **The game is NOT hung/deadlocked/parked in a loop. It TICKS but never
  PRESENTS a frame.**
- **PROBE W's `WINDOW_PROBE_CONVERGED` fired at the FIRST sample (wall_s=9025):** a process window
  (`0x210C08`) was visible AND OS-foreground immediately. So the engine's `GetActiveWindow()==expected`
  focus-gate (`0x866029`) is SATISFIED EARLY — **the window-activation theory is FALSIFIED.** `fg_is_ours`
  only dropped to 0 at the very end because the user quit the game (foreground drifts on teardown), not
  during the wedge. The gate is NOT the wedge.

**What this invalidates (the whole chain's framing was wrong):**
- "Main is parked/stuck/waiting in the `0x869c39` loop" — NO. Main runs full per-frame ticks. The cdb
  captures that showed "Main in a sleep loop" sampled a RUNNING tick at one instant (the bounded ≤25ms
  focus poll, which Main re-enters every frame) — not a hung thread. The per-frame-trap, exactly as the
  exit-gate recon's Reframe 6 warned.
- "stuck waiting for a producer / completion handshake" — NO (already shown to be a `call_once` guard).
- The window-activation gate — FALSIFIED, converges at second 1.

**The corrected premise (matches the ORIGINAL KI-0028 symptom statement):** the game runs the per-frame
loop but never produces a visible frame. This is a PRESENT / render-submission failure, not a control-flow
hang. The tick runs; the frame is either not built, not submitted to the swapchain, or not presented.
PROBE H (heartbeat-cessation detector) is the WRONG instrument for this wedge — it can never fire because
there is no cessation. The right instrument is the swapchain's own present counters.

**Next probe — PROBE K (already built, disarmed):** reads the DXGI swapchain's `GetLastPresentCount` +
`PresentRefreshCount` (no present hook). Its pre-committed outcome map (`present_probe.h`) partitions the
"ticks but never presents" wedge directly: present-count ~0 → loop never reaches present (upstream);
present-count>0 + refresh~0 → present called, no GPU scanout (present path); both advance → frames
presented, black screen is a surface/compositor issue; never captured → widen the capture point. Arm K
(beside W), rerun swap-ON, then swap-OFF for the baseline delta.

## RE-LOCALIZATION (2026-06-22, PROBE K run 2) — the wedge is BLACK FRAMES PRESENTED, not a present failure

PROBE K (vtable slots fixed to 16/17) run 2, swap-ON, user-confirmed **black screen the whole ~2 min**.
The corrected present counters are unambiguous and land on PROBE K's THIRD outcome branch:

- `hr_present=0`, `hr_framestats=0` (S_OK — valid reads this time).
- present=0 for the first ~3s (swapchain just created), then **`d_present=120, d_refresh=120` PER SECOND
  for the rest of the run** (~115s). `present_count` climbed 0 → 10516; `refresh_count` (GPU scanout)
  advanced in lockstep. The window converged at second 1; the heartbeat ran the whole time (tick=9518).

**Per PROBE K's pre-committed outcome map (`present_probe.h`): "both advance → frames ARE presented;
black screen is a surface/compositor association, NOT present."** The game is flipping the swapchain at
120fps with real GPU scanout, AND the screen is black → **the presented FRAMES ARE BLACK.** Present
succeeds; the content is empty.

**What this DEFINITIVELY rules out (every prior frame, by direct measurement):**
- Deadlock / hang — NO (heartbeat ran, 9518 ticks).
- Stuck in a loop / waiting on a producer — NO (call_once guard; Main ticks).
- Window activation (`GetActiveWindow`) — NO (converged at second 1).
- Present-submission failure — NO (present called 120×/s, succeeds).
- FS-slot-output divergence (A/B/C/D) — NO (all exonerated on the consumer side).
- "ticks but never reaches present" (PROBE K branch 1) — NO (present reached, advancing).

**The re-localized bug: a render-CONTENT failure.** The per-frame render runs and presents, but composites
to BLACK — the scene/UI is not drawn into the frame, OR is drawn to the wrong render target, OR a render
resource is missing/wrong so every frame is empty. The bug is UPSTREAM in the render pipeline (scene draw
/ render-target binding / a render asset), NOT in present, window, or FS dispatch control-flow.

**The FS-takeover connection (the swap IS still the differentiator — P-F):** swap-off reaches the menu,
swap-on is black. Present works both ways (to verify swap-off next). So the swap must be making a RENDER
resource wrong/missing — a shader, a pipeline-state-cache blob, a render config, a texture/material the
render pipeline needs to draw a non-black frame. This re-connects to the FS takeover but at the RENDER-
ASSET layer, not the slot-return layer: which render-critical asset does kcdx serve differently (wrong
bytes, missing, wrong variant) that turns every frame black? Candidates from prior recon: the
PipelineStateCacheManager (seen consuming AdjustFileName at `0x1e03c30` DriverD3D), shader paks, the
render config. NOT a slot-return divergence (those are byte-identical on hit) — a SERVED-CONTENT question:
does kcdx serve the RIGHT BYTES for render-critical assets?

**MECHANISM FOUND (same run's log, no new launch — reuse-first per results-driven §4):** the black frames
are caused by the FS takeover failing to serve RENDER-CRITICAL assets. Two failures, both in the swap-on
log:

1. **57 shader open failures** — `FS_OPEN loose_open_failed slot=FOpen vpath="data/gameshaders/<X>.ext"
   errno=2` for runtime.ext, water.ext, hair.ext, eye.ext, **scaleform4.ext** (the UI renderer),
   computeskinning.ext, particleimposter.ext, distanceclouds.ext, watervolume.ext, … + `shaders/cache/
   globals.txt` read `got=-1 FAIL`. **ZERO gameshaders were ever served from the index** (the index-pak
   trace for gameshaders is empty). Every shader the engine wants misses kcdx's index, falls to a loose
   disk open, and fails errno=2 (it lives in a pak, not loose). **232 total loose-open failures** this
   boot — systemic, not incidental.
2. **zip64 paks unsupported** — `PAK_READER CDR parse failed pak=IPL_Textures-part0.pak: zip64 sentinel
   in EOCD — unsupported` → `FS_INDEX pak_parse_skipped`. kcdx's pak reader cannot parse a zip64 central
   directory, so that pak is skipped from the index entirely (1 pak observed; large texture/streaming paks
   exceed the zip32 4GB/65535 limits and use zip64). Secondary to the shader misses but a real index gap.

**This is the KI-0028 mechanism, and it matches the symptom exactly:** no shaders (incl. the Scaleform UI
shader) + missing texture paks = the render pipeline runs and PRESENTS (PROBE K: 120fps, GPU scanout) but
every frame composites to BLACK because there is nothing to draw with, and no UI/menu because the
Scaleform shaders failed to load. Present succeeds; the content is empty. Swap-OFF the engine reads these
from its own pak path and renders normally — the swap is the differentiator (P-F) at the RENDER-ASSET
serve layer.

**Open sub-question (the precise index defect — checkable from the log/index):** WHY does the index miss
`data/gameshaders/*.ext`? Either (a) the `.ext` shaders live in a pak kcdx did not scan/index, (b) the
KI-0026 alias-namespace class — they are keyed in the index under a vpath that does not match the engine's
lookup form, so a present entry is not found, or (c) they are genuinely loose-expected and the takeover's
loose path differs from the engine's. The 307006-entry index built — so the question is whether these
specific shader vpaths are IN it under a different key, or ABSENT because their pak was skipped. Next:
locate where `water.ext` actually lives (which pak) and whether that pak was indexed (and under what key).

**Status:** KI-0028 is no longer mysterious — it is a render-asset-serving gap in the FS takeover (shader
paks not served + zip64 paks skipped), NOT a hang/present/window/control-flow bug. The root-cause
paragraph still owes the precise index-miss mechanism (a/b/c above) before the KI closes (AP17). Swap-OFF
baseline still owed to confirm these same assets serve correctly unswapped.

## ROOT-CAUSE MECHANISM (2026-06-22, log analysis — option (b) CONFIRMED) — gameshaders alias not folded

The precise index-miss mechanism is a **vpath alias-normalization gap** (the KI-0026 class: lookup key ≠
stored key), proven from the swap-on log without a new launch:

- **The shader paks WERE indexed.** `PAK_READER parsed Shaders.pak entries=201`, ShaderCache.pak=656,
  ShadersBin.pak=180; `FS_INDEX asset_index_built entries=307006 roots=2 paks=46`. The shaders are IN the
  index — not absent, not zip64-skipped (those paks parsed fine).
- **They are keyed under the pak's OWN entry name `shaders/X.ext`** (`read_entry pak="Shaders.pak"
  name="shaders/runtime.ext"`). The engine addresses the SAME file two ways:
  - `FOpen vpath="shaders/runtime.ext"` → `how=index-pak result=3` — **SERVED** ✓ (kcdx key matches).
  - `FOpen vpath="data/gameshaders/runtime.ext"` → `loose_open_failed errno=2`, `how=miss-original` —
    **MISSED** ✗ (no index key `data/gameshaders/...`; falls to a loose disk open that doesn't exist).
- **21 of the 57 failed `data/gameshaders/X.ext` are provably the SAME file already in-index as
  `shaders/X.ext`** (runtime, water, hair, eye, **scaleform4** [the UI shader], illum, vegetation,
  terrain, …). Pure key-namespace mismatch: kcdx has the bytes, keyed under `shaders/`, and does not fold
  the engine's `data/gameshaders/` alias to that key.
- A third shader namespace exists: `ShaderCache.pak`/`ShaderCacheStartup.pak` key entries as
  `%engine%/shaders/cache/...` (the `%engine%` alias kcdx handles elsewhere — KI-0026 — but the cache
  lookup forms may need their own fold).

**Root cause (falsifiable, AP17 grade):** the engine addresses shaders via the `data/gameshaders/...`
alias (and the cache via `%engine%/shaders/cache/...`), but kcdx's `asset_overlay::NormalizeVPath` /
`ExpandEngineAliasToIndexKey` does NOT map `data/gameshaders/` → the indexed `shaders/` key. The wrong
value is the normalized index-lookup key (it keeps `data/gameshaders/...` instead of folding to
`shaders/...`); kcdx's `NormalizeVPath` produces it on every shader lookup; the original path makes the
miss inevitable because the index is keyed by the pak's stored name (`shaders/...`) while the engine's
shader subsystem requests the alias form (`data/gameshaders/...`), and no alias fold bridges them — so the
lookup misses, falls to a loose disk open (no loose file exists; shaders live only in the pak), returns
errno=2, and the shader never loads. With the Scaleform UI shader and core render shaders absent, the
render pipeline presents (PROBE K: 120fps) but composites every frame to BLACK. This is the KI-0026
alias-resolution defect (kcdx owns alias resolution; this alias is uncovered) applied to the `gameshaders`
namespace.

**Honest scope correction (do NOT overclaim):** of the 57 failed gameshaders, 21 are PROVEN present
in-index under `shaders/` (pure fold gap). The other 36 (posteffects, deferredshading, light, common,
sunshafts, hud3d, scaleform3, …) were "not seen served under `shaders/`" ONLY because the failed lookup
aborted the load before any `shaders/`-form read — their in-index presence is UNPROVEN, not disproven (the
served-check is circular for a file whose load failed). They are very likely the same fold gap (they are
standard CryEngine `Shaders.pak` shader families), but that is a lead to confirm, not an asserted fact.
The zip64 `IPL_Textures-part0.pak` skip is a SEPARATE, secondary defect (a real reader gap, but textures
not shaders — it would degrade visuals, not blank the frame).

**Overall serve health (context):** `FOpen index-pak served=4890` vs `miss-original=381` this boot — the
takeover serves the large majority correctly; the failures are a concentrated alias-fold gap (heaviest in
shaders), not a broken index. This is consistent with "mostly works, but the missing shaders blank the
frame."

**The fix direction (a real fix, surfaced — design-authority):** kcdx's index-key normalization must fold
the engine's shader alias(es) — `data/gameshaders/X.ext` → `shaders/X.ext` (and confirm the
`%engine%/shaders/cache/...` cache forms resolve) — so every shader lookup hits the indexed entry. This is
the same alias-ownership pattern KI-0026 settled for `%engine%`; the gameshaders alias was simply not
covered. (The zip64 reader gap is a separate fix.)

**Still owed before the KI closes (AP17):** (1) confirm the 36 unproven gameshaders are the same fold gap
— NOTE the served-line check is CIRCULAR for them (kcdx logs `read_entry` only on a lookup HIT; a failed
lookup never produces a served line, so a failed shader can never appear as served under EITHER key). The
non-circular confirmation is to read `Shaders.pak`'s central directory directly (does it contain
`shaders/posteffects.ext`?) OR observe them serve on the swap-off baseline — NOT another served-line grep.
(2) the SWAP-OFF baseline run — confirm these exact assets serve under the engine's own pak path unswapped
(isolates the fold gap as the sole swap-on delta). (3) implement + verify the alias fold makes boot reach
the menu — the fix itself resolves the 36 naturally (if they are the same gap, folding `data/gameshaders/`
→ `shaders/` serves them; if some genuinely aren't in `Shaders.pak`, the fix surfaces exactly which remain
missing, a far smaller residual). The mechanism (the missing alias fold) is established at AP17 grade by
the 21 proven cases; the 36 are a scope detail the fix + baseline resolve, not an open mechanism question.

## FIX LANDED, PARTIAL (2026-06-22) — 21 shaders now serve, screen STILL black; the 36 look benign

The `data/gameshaders/` → `shaders/` alias fold (commit e88a9eb) WORKS at the live level — confirmed:
`FOpen how=index-pak vpath="data/gameshaders/runtime.ext" result=3` (served from Shaders.pak), cap-115 (e)
PASS, gameshaders misses 57 → 36. But the user-confirmed result is **STILL BLACK** — the fix was
necessary-not-sufficient.

**The 36 remaining misses appear BENIGN (do NOT chase them as the cause):** they are
`data/gameshaders/posteffects.ext`, `depthoffield.ext`, `deferredshading.ext`, `sunshafts.ext`, … — and
they go `how=miss-original result=0`: kcdx falls through to the ENGINE'S OWN resolver, which ALSO returns
not-found. There is no `posteffects.ext` anywhere in the paks (Shaders.pak has `hwscripts/cryfx/
posteffects.cfx` the source + ShadersBin.pak has `shaders/cache/d3d12/posteffects.cfxb` the compiled
binary; no bare `posteffects.ext`). These are OPTIONAL per-shader override `.ext` files absent in vanilla —
the unmodified engine gets the same not-found and renders fine. So the 36 are very likely NOT the
black-frame cause; "fold them too / find their pak" would be chasing a non-cause (results-driven: a fix on
a benign miss is fix #2 on a new theory).

**PROBE K this run:** present advances (~35/s) but `present_count` reached only 3744 (vs 10516 on the
pre-fix black run) — present is happening, fewer frames, still black. No new fatal/0xC8/device-lost. The
21 real shaders (runtime, scaleform4, …) now serve, yet black persists.

**Honest status:** one real fix landed (21 shaders served, a genuine defect removed) but did not resolve
the symptom. Per the results-driven floor (after ONE failed fix, probe — do NOT fix #2 on a new theory),
the next step is the variable-isolating probe I have NOT yet run: **the SWAP-OFF baseline.** It answers
the load-bearing isolation question — does the UNSWAPPED engine (a) also miss the 36 (confirming benign)
AND (b) reach the menu? That isolates what swap-ON still does differently now that the 21 shaders serve.
If swap-off renders despite the same 36 misses, the 36 are exonerated and the remaining black-frame cause
is elsewhere in what the swap changes (a different served-content gap, or a render resource beyond the
gameshaders alias). NOT another shader-alias fix until the baseline isolates the residual.

## SWAP-OFF BASELINE (2026-06-22) — corrects "benign": the 36 misses are swap-ON-INDUCED, residual isolated

The swap-off baseline (kcdx-noswap marker, fix-build engine) **reached the menu — success.** The decisive diff:

- **Swap-OFF: 0 total loose_open_failed, 0 gameshaders requests.** The unswapped engine NEVER asks for
  `data/gameshaders/*.ext` at all (the only 2 "gameshaders" lines are the cap-115 PASS string).
- **Swap-ON: 211 total loose_open_failed, 57→36 gameshaders misses** (21 fixed by the fold).

**This OVERTURNS the "36 are benign" read (my prior conclusion was wrong).** They are not vanilla-absent
files the engine also can't find — the unswapped engine never requests them. The `data/gameshaders/*.ext`
probes are **swap-ON-INDUCED**: something about HOW kcdx serves shaders drives the engine down a different
code path (probing for per-shader `.ext` files, hitting the shader cache 88+49 times) that the
binary-cache-driven unswapped path never takes.

**Measurement limit:** swap-off emits NO `FS_BOOT_TRACE open slot=FOpen` lines (the engine uses its own
CCryPak; kcdx's trace only fires on kcdx's slots), so a direct swap-off-vs-on open diff is impossible by
construction. The swap-off log shows the engine's behavior only through its absence from kcdx's trace.

**Residual ISOLATED (the falsifiable frontier):** swap-on now serves the 21 real shaders correctly
(`runtime.ext`/`scaleform4.ext` → index-pak result=3), yet the screen is still black AND the engine probes
36 nonexistent per-shader `.ext` files it never probes unswapped. The remaining cause is in WHAT kcdx
serves for shaders vs what the engine's own pak reader serves — candidate mechanisms (NOT yet probed, do
NOT fix on theory):
  (1) kcdx serves the wrong shader VARIANT (the source `.cfx`/`.ext` where the engine's cache path expects
      the compiled `.cfxb` in ShadersBin.pak / the per-permutation cache in ShaderCache.pak), so the engine
      treats the cache as invalid and falls into source-probing/recompile (the 36 `.ext` probes + the
      88/49 cache touches are the tell);
  (2) kcdx serves shader bytes that are byte-correct but with wrong METADATA (size/mtime/CRC) the engine's
      shader-cache freshness check rejects → invalidate → reprobe;
  (3) a shader-cache file kcdx serves from `%engine%/shaders/cache` differs from what the engine wrote to
      `%user%/shaders/cache`, so the validation mismatches.

**Results-driven status:** ONE fix landed (21 shaders, real defect removed, swap-off still reaches menu so
the fix did not regress the good path). The symptom persists with a NOW-ISOLATED residual. Per the floor,
theories (1)-(3) are accumulating on one symptom → the next step is a fresh-frame probe of the
shader-variant/cache-validation path (which exact bytes/metadata the engine rejects), NOT fix #2 on a
guess. This is a deeper render-subsystem investigation than the alias fold — a natural phase boundary.

## PROBE P RESULT (RAN 2026-06-22, swap-ON) — O5 CONFIRMED: the engine builds ONE trivial PSO, never the scene/UI pipelines

PROBE P (`src/fs_takeover/pso_probe.{h,cpp}`) hooked the CONSUMPTION side —
`ID3D12Device::CreateGraphicsPipelineState` (slot 10) + `CreateComputePipelineState` (slot 11), captured
via a one-shot `d3d12!D3D12CreateDevice` detour — which runs IDENTICALLY swap-on/off, escaping the trace
blind spot. It logs per PSO call: HRESULT, null-PSO, and each shader blob's len + DXBC magic. Ground truth,
swap-ON (user-confirmed black screen):

- **device captured, hooks armed (`ok=1`)** — no P* miss; the instrument worked.
- **`gfx_calls=1` for the ENTIRE run. `comp_calls=0`.** The engine created exactly ONE graphics pipeline
  and ZERO compute pipelines.
- **The one PSO is a trivial blit/present shader:** `vs_len=704 ps_len=752`, both `dxbc=1` (valid DXBC
  magic `0x43425844`), `hr=0`, non-null. A ~700-byte VS/PS pair is the swapchain's final copy-to-backbuffer
  pass — NOT a scene or material or UI pipeline.
- Present is the same as PROBE K: `d_present=35 d_refresh=120` (presenting the empty backbuffer at the
  display rate). Suite reached 320.

**O5 CONFIRMED, O1/O2/O3 ALL FALSIFIED by one measurement:**
- O1 (malformed served bytes) — FALSIFIED: the one blob seen is well-formed (`dxbc=1`).
- O2 (bytes fine, PSO assembly fails) — FALSIFIED: `gfx_failed=0`; the one PSO created SUCCEEDED. (O2 also
  requires MANY PSO calls to fail — there is only one, and it passed.)
- O3 (PSOs fine, black downstream of PSO) — FALSIFIED in its premise: O3 assumes the engine builds its
  normal pipeline set and the failure is after. It does NOT build them — `gfx_calls=1` is not "PSOs fine,"
  it is "the engine never asks for the scene/UI pipelines at all."

**The re-localized mechanism (a strong NEGATIVE, observed not inferred):** a CryEngine game rendering a
scene + menu creates DOZENS-to-HUNDREDS of graphics PSOs (one per shader permutation / material / pass).
The engine here creates exactly ONE — the present blit. So the render pipeline gets present up (the blit
PSO + the swapchain) but **never proceeds to build the pipelines that draw content.** The black frame is
NOT a broken shader, a bad PSO, or a downstream-of-PSO draw bug — it is that the render-content build
**stalls UPSTREAM of PSO creation**, so the engine never requests the scene/material/UI pipelines. This is
exactly O5: kcdx's FS serving diverts the engine off the render-build path before PSO creation.

**This reconciles the swap-on-only tells already captured:** the 36 phantom `data/gameshaders/*.ext`
probes + the heavy `%user%`/`%engine%` shader-cache dance (1256+1030 `lookupdata.bin` reads) are the engine
**stuck in shader-SYSTEM initialization** — trying to resolve/enumerate/compile the shader set and never
completing — so it never reaches material/scene PSO creation. Swap-off, that init completes (it reaches the
menu = many PSOs), so the differentiator is something kcdx's serving does to the shader-system init/enum
path, BEFORE any single PSO is built.

**Next probe target (the shader-system init stall, theory-independent):** the question moves from "are the
shader BYTES/PSOs right" (answered: the pipeline never gets there) to "WHERE does the engine's shader-system
initialization stall under the swap, and what file operation does it loop/block on." The 36 phantom `.ext`
probes + the `lookupdata.bin` re-reads are the lead — instrument what the engine does AFTER reading
`lookupdata.bin` / a `gameshaders/*.ext` miss, swap-on: does it retry, enumerate a directory, or wait? The
`got=-1` on `%user%/shaders/cache/globals.txt` (114×) is a concrete candidate — the engine reading an empty
globals.txt and re-deriving the shader globals each time. NOT a fix on theory — a probe that observes the
shader-init control flow's stall point. (Candidate seam: the shader-cache enumeration / globals-parse path,
or a FindFirst/FindNext enumeration over `shaders/` that kcdx's find-slots serve differently — KI-0027 was
exactly a find-slot mask divergence; the find slots are a prime suspect for an enumeration that returns the
wrong set and stalls shader-system init.)

**Status:** the bug is re-localized from "render content" to "shader-system INIT stalls before pipeline
build." O5 confirmed by direct measurement (1 PSO, not hundreds). KI-0028 OPEN; root cause still owes the
precise stall mechanism (AP17) — which file op the shader-system init loops/blocks on under the swap.

## ENUM DIVERGENCE FOUND (2026-06-22, log + real-pak CDR read) — kcdx's enumeration emits NO directory entries; the shader source tree is undiscoverable

The PROBE P re-localization ("shader-system init stalls before pipeline build") + the swap-on log's
find-slot calls pin a VERIFIED enumeration divergence (the KI-0027 class — a find-slot returns the wrong
set). Ground truth, three sources on disk:

- **The engine enumerates the shader source tree at boot and gets ZERO from kcdx** (swap-on log,
  `enum slot=FindFirst`):
  - `Shaders/*.ext` → matched=21 ✓ (the leaf shaders).
  - `Shaders/HWScripts/*.*` → **matched=0** ✗
  - `data/GameShaders/HWScripts/*.*` → **matched=0** ✗
  - `%ENGINE%/Shaders/Cache/D3D12//*.*` → **matched=0** ✗
- **The pak ACTUALLY CONTAINS those entries** (real `Shaders.pak` central directory, read via python
  zipfile — corrects the read_entry confusion: `read_entry name=` logs the engine's REQUESTED alias vpath,
  NOT the CDR stored name): 201 entries, stored under **`Shaders/`** (capital): `Shaders/ComputeSkinning.ext`
  (21) + **`Shaders/HWScripts/CryFX/*.cfx` (180)**. So the index keys (NormalizeVPath = lowercase) are
  `shaders/computeskinning.ext` and `shaders/hwscripts/cryfx/agcbuiltin.cfx`.
- **`IsFileExist3` on a `.cfx` HITS** (`data/GameShaders/HWScripts/CryFX/computeskinning.cfx` →
  `how=index-either result=1`) — a direct key lookup (alias fold) finds it. So the `.cfx` ARE in the index;
  only the ENUMERATION misses them.

**THE MECHANISM (verified, AP17-grade candidate):** kcdx's index-walk enumeration arm
(`enum_slots.cpp:185-201`) emits ONLY files at exactly the requested directory level and **NEVER emits a
directory entry** for an intermediate subdir. Line 197 skips any vpath with a `/` past the prefix
(single-level semantics). Enumerating `shaders/hwscripts/` over the index: the 180 `.cfx` all live one level
deeper under `cryfx/` (`shaders/hwscripts/cryfx/X.cfx` — has a `/` past the `shaders/hwscripts/` prefix →
SKIPPED), and there is NO `shaders/hwscripts/cryfx` DIRECTORY entry emitted to tell the engine the subdir
exists. So `FindFirst("Shaders/HWScripts/*.*")` returns 0. The engine's OWN `_findfirst64` returns
subdirectory entries (a dir walk yields files AND subdirs), so vanilla sees `CryFX` as a dir, recurses, and
finds the 180 source shaders. kcdx's enum returns an EMPTY `HWScripts/` → the engine never discovers the
`CryFX/` shader source tree → shader-system init cannot enumerate/compile its shader set → it stalls before
building any material/scene pipeline (PROBE P: gfx_calls=1). The leaf `Shaders/*.ext` enum works (matched=21)
ONLY because those 21 are files directly at the `shaders/` level (no intermediate dir) — which is why the
alias-fold fix served them yet boot stayed black: serving the leaves is not enough; the engine needs to walk
the HWScripts/CryFX source tree, and the enum hands it nothing.

**Two sub-divergences in the same enum path:**
1. **No directory entries emitted** (the primary — `shaders/hwscripts/` returns 0 because `cryfx/` is a
   subdir the index-arm never surfaces). The index stores only FILE entries; a CryEngine dir walk expects
   the subdir entry.
2. **The `%ENGINE%/Shaders/Cache/D3D12//*.*` cache enum** — uppercase `%ENGINE%` + a `//` double-slash;
   `FoldEngineAliasToIndexKey` folds `%engine%/` (lowercased by NormalizeVPath first, so case is handled)
   but the `//` double-slash is NOT collapsed by NormalizeVPath → the prefix `shaders/cache/d3d12//` cannot
   prefix-match `shaders/cache/d3d12/X` (the extra `/`). A separate enum-normalization gap.

**STILL TO VERIFY before the fix lands (AP17 — do NOT assert the fix without it):** that emitting directory
entries (and/or recursing) from the index-walk arm makes `Shaders/HWScripts/*.*` return the `CryFX` subdir,
the engine recurses, discovers the source tree, and boot reaches the menu. The mechanism is verified
(enum returns 0 for a populated tree; the engine needs that tree); the FIX's sufficiency is the live
acceptance. Candidate fix: the index-walk arm emits, for each prefix, the immediate child FILE entries AND
a synthetic DIRECTORY entry for each distinct immediate child SUBDIR (so a single-level `FindFirst` yields
both, matching `_findfirst64`), + collapse `//` in the enum prefix. This is the find-slot ABI work KI-0027
established (find-data attr@0x00 0x10=dir, name inline @0x24) — a directory entry sets the dir attr bit.

## PROBE Q RESULT (RAN 2026-06-22, swap-ON) — synthetic dir entries CONFIRMED correct; engine recurses; but NOT sufficient (1 PSO still)

PROBE Q (`find_slots.cpp` index-walk arm) emitted a synthetic DIRECTORY entry for each immediate child
subdir instead of skipping it. Ground truth, swap-ON:

- **The engine RECURSES on a dir entry — Outcome A CONFIRMED:**
  - `Shaders/HWScripts/*.*` → **matched=1 entries="cryfx"** (was 0). The synthetic `cryfx` dir entry is returned.
  - `Shaders/HWScripts/cryfx/*.*` → **matched=180** (water.cfx, glass.cfx, the FSR2 passes, …). The engine
    saw the dir entry and walked into it — exactly a real `_findfirst64` dir walk. The synthetic-directory-
    entry fix is CORRECT and is the right fix for the source-tree enumeration.
  - `Shaders/*.ext` → matched=23 (was 21 — the 2 synthetic subdirs `hwscripts` + `cache` now also surface).
- **PROBE Q caused no new errors** — the `libs/tables/` synth dirs (185 emissions) were harmless; no FS_FIND
  error/warn.

**BUT NOT SUFFICIENT — `gfx_calls` STILL = 1, screen STILL black.** Discovering + recursing the shader
source tree (180 shaders) did NOT make the engine build the pipeline set. So source-tree enumeration was a
real gap (now fixed) but NOT the last one.

**LIVE invasive cdb (game still running, `~*k`, qd — left running):** the whole engine is IDLE-TICKING, not
stalled. Main is in the per-frame focus-poll loop (`HookedUpdate+0x94a` → `0x16c7a0` → the `0x36eb39`/
`0x36ff17` window frames — the per-frame trap, ticking normally). ALL 14+ JobWorkers are IDLE, parked at the
identical `ffxFsr2GetUpscaleRatioFromQualityMode+0x152c749` → `0x4b34a2` → `0x567a86` (the SRW job-wait
idle). **Idle workers = the engine is NOT queuing shader-compile / pipeline-build work** — it reached
steady-state with no render content to build. The shader compiler IS loaded (`dxcompiler.dll`, `dxil.dll`,
`D3DCOMPILER_47.dll`) — the engine got far enough to load it — but never dispatches compile jobs.

**THE LAST REMAINING SHADER-ENUM FAILURE (isolated — the only shader/cache `matched=0` left):**
`%ENGINE%/Shaders/Cache/D3D12//*.*` → **matched=0**. The compiled-shader-CACHE directory enumeration fails.
Mechanism (two converging bugs):
1. **`//` double-slash not collapsed.** NormalizeVPath lowercases + folds `\`→`/` but does NOT collapse a
   `//`. The pattern's dir prefix becomes `%engine%/shaders/cache/d3d12//` — the extra `/` breaks the prefix
   match against keys `%engine%/shaders/cache/d3d12/X`.
2. **Alias-keying ASYMMETRY (the deeper bug).** Cache entries are keyed AT BUILD by `NormalizeVPath(pe.name)`
   = `%engine%/shaders/cache/d3d12/posteffects.cfxb` — the `%engine%/` prefix is RETAINED (the build path,
   `asset_index.cpp:112`, does NOT call `FoldEngineAliasToIndexKey`). But `IndexDirPrefix` (enum) AND
   `ResolveVPath` (open) DO call the fold, which STRIPS `%engine%/`. So the lookup key is `shaders/cache/...`
   while the entry key is `%engine%/shaders/cache/...` — they cannot match. (FOpen of a cache `.cfxb`
   nonetheless served `result=3/17` — so there is some compensating path for the OPEN that does NOT apply to
   the ENUM prefix; the asymmetry surfaces specifically on the directory enumeration. The exact open-vs-enum
   asymmetry is the next checkable unknown — read how the cache .cfxb OPEN matched its `%engine%`-retained
   key when the fold strips the prefix; the entries may ALSO be inserted under a stripped key, or the open
   fold differs. DO NOT fix on theory — read the keying both ways first.)

**Honest status:** PROBE Q is a CORRECT, evidence-backed fix for the source-tree enumeration (promote it).
But the symptom persists — the engine idle-ticks with no pipeline build, and the last shader-enum gap is the
`%engine%`-cache-dir enumeration (double-slash + alias-keying asymmetry). The connection from "cache enum
returns 0" to "no compile jobs queued" is INFERRED, not yet proven — the engine reaching steady-state with
idle workers is consistent with "it found no cache to load AND was never told to compile," but the causal
edge (cache-enum-0 → no-compile-dispatch) is the next thing to verify, not assert (AP17). Next: fix the
cache-dir enum (collapse `//` + resolve the alias-keying asymmetry so the enum prefix matches the stored
key), re-run, and check whether `gfx_calls` jumps + the workers get compile work.

## Reuse pointers

- PROBE Q: `find_slots.cpp` BuildUnifiedFindEntries index-walk arm — emits a synthetic dir entry per distinct
  immediate child subdir (deduped, `isDir=true`); CONFIRMED the engine recurses on it. The correct fix for
  source-tree enumeration; promote (drop the PROBE Q logging, keep the emission).
- ENUM divergence: `src/fs_takeover/enum_slots.cpp:185-201` (index-walk arm, single-level, no dir entries) +
  `find_slots.cpp` IndexDirPrefix. Real pak CDR: `python -c "import zipfile; zipfile.ZipFile(r'<Engine>/Shaders.pak').namelist()"` — entries under `Shaders/` (180 in `Shaders/HWScripts/CryFX/`).
- PROBE P: `src/fs_takeover/pso_probe.{h,cpp}` — D3D12 device + PSO-creation hook; the consumption-side
  instrument that runs identically swap-on/off (escapes the FS-trace blind spot). `gfx_calls` count is the
  ground-truth signal: ~1 = render-build stalls before pipelines; dozens+ = pipelines built.
- Script: `disasm_36eb39_outer_loop.py` (this dir) — targets the REAL RVAs; the offset base
  is `EXPORT_FFX = 0x4fb100`.
- Wedge stack: `cdb_pl_probeL_wedge.txt` lines 270–287.
- The offset-vs-RVA trap applies to EVERY `ffxFsr2ResourceIsNull+0x…` / `NVSDK_NGX_…+0x…`
  frame in the trail — add `0x4fb100` (or the NGX export RVA) before disassembling.
