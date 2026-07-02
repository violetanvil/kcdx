# KI-0028 — root-cause measurement plan (where we measure, and whether it reaches ROOT)

**Purpose:** the settled measurement plan for KI-0028 after the 2026-07-02 architecture review.
**Decision that frames it (user-settled, this session):** the full filesystem takeover STAYS — it wins
on Performance and, once working, "just works" = UX #1, trading nothing. The `no-thunk-back / kcdx owns
full init` invariant is reconsiderable but NOT the problem; the problem is we have never OBSERVED the
mechanism. So we measure the mechanism to ROOT — we do not scope back and we do not symptom-fix.

**Companion docs:** `KI-0028-FULL-HANDOFF.md` (the verified/falsified evidence base — read it first),
`docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md` (the chronological trail + CORRECTIONs).

---

## 0. The bar this plan must clear — ROOT, not symptom

kcdx's closure bar (AP17, CLAUDE.md hard rule): KI-0028 does not close until the Resolution names the
root-cause **mechanism in falsifiable terms** — *what value is wrong, who writes it, in what order, why
the original path made that wrong state inevitable.* "Black screen gone" / "boots now" is symptom
restatement, never root cause. Every measurement below is judged against THAT paragraph, not against
"did the screen change."

**The honest answer to "do these measurements guarantee we find the root?":**
**No single probe bottoms out at root.** A stack diff names WHERE the divergence surfaces, not WHY.
Root is reached by a CHAIN whose terminal condition is the AP17 paragraph. The measurements are the
first three links; the last two links are CODE READS, not probes. This plan is written as that chain
with its terminal condition explicit, precisely so we do not stop at a mid-chain "fact" and call it root
(the trap that produced four "real defect fixed, still black" cycles — §11 of the handoff).

---

## 1. The proven starting point (do NOT re-measure — handoff §12.B)

The divergence is pinned to ONE transition, by measured facts only:

- The engine streams 65k textures + 18k XML + shaders, presents a UI compositor at 120fps with real GPU
  scanout, and **stops advancing** — it never begins the work that first requests indexed geometry.
- `draw_indexed=0` and black are proven CONSEQUENCES of that stall, not independent faults.
- Every kcdx-served OUTPUT is measured correct/identical swap-on/off: bytes (`diffs=0`), handle
  value+semantics (PROBE T), object identity (PROBE U), the 4 slot return-contracts (A/B/C/D), the
  shader/PSO build paths (R/R2/R3/R4/P, `gfx_calls=1` both).

**Therefore the root is NOT in what kcdx SERVES. It is in a STATE or INIT-ORDER the swap perturbs that
the phase-advance gate depends on.** Everything below measures THAT — never another served-output byte.

**One grounded init-order fact (verified in source this session, `seating_hook.cpp:305-352`):** the seat
does `SwapVtableOnObject` → `BuildAssetIndexAtSeat()`, and that helper **blocks Main on
`WaitForSingleObject(overlayReadyGate, INFINITE)`** inside `CSystem::Init`. This is a real init-order
perturbation vanilla never performs (Main parks mid-Init waiting on a kcdx worker gate). It is a concrete
candidate for the "value right, but produced/run in the wrong ORDER" branch (§4) — not yet proven causal,
but it is why the plan carries an explicit order-vs-value fork rather than assuming a dropped value.

---

## 2. The root-cause CHAIN (the 5 links — where root actually lives)

Root is reached by walking this chain to its end. Each link's output is the next link's input. Links 1–3
are the measurements (§3); links 4–5 are code reads that turn a named divergence into the AP17 paragraph.

| Link | Question it answers | AP17 clause it supplies | Probe or read |
|---|---|---|---|
| **1** | WHERE does swap-ON control-flow diverge from swap-OFF? (which gate frame) | locates the gate (not yet a value) | **Measurement 1** — live self-dump stack diff |
| **2** | WHAT value does that gate read to decide "don't advance"? | *what value is wrong* | **Measurement 2** — instrument the named gate's read |
| **3** | Is the value WRONG, or RIGHT-BUT-LATE (order)? | *in what order* | **Measurement 2 fork** + **Measurement 3** (vanilla-diff) |
| **4** | WHO was supposed to write/set that value in vanilla? | *who writes it* | CODE READ — xref the value's writer; find the CCryPak slot / init step |
| **5** | WHY did the swap make the wrong state inevitable? | *why inevitable* | CODE READ — read the original slot/step body; prove the dropped side-effect or reordered wait |

**The terminal condition (root reached):** links 4–5 produce the falsifiable paragraph — "value V, read by
gate G to decide advance, is <wrong / set-too-late> because kcdx's slot S served bytes but dropped
side-effect E (or the seat's INFINITE wait reordered producer P after consumer G), which the original
engine path did unconditionally; therefore G never advances." Until that paragraph exists and is
verifier-checked (Gate B / root-cause-verifier), KI-0028 stays OPEN.

**Why the measurements alone do NOT guarantee root:** they deliver links 1–3 (where + what + order-class).
Links 4–5 are reads that could still surface a surprise — e.g. the gate reads a value written by an
init-ORDER interaction with no single "slot side-effect" owner, or a multi-hop chain where the wrong value
at link 3 was itself produced correctly but consumed against stale order. The plan's guarantee is not
"a probe bottoms out"; it is "the chain has a defined terminal (the AP17 paragraph) and every link is
either measured or read — no link is inferred." Inference at any link is the trap (§5).

---

## 3. The measurements — exactly what we measure, and where

### Measurement 1 — the live self-dump main-thread stack diff (THE decisive observation; build FIRST)

**Where:** inside `HookedUpdate` (via the `boot_watch` seam), on Main itself. No debugger — you cannot win
the attach race (the symptom IS the steady state; every prior invasive attempt died or caught a post-AltF4
process; `-pv` noninvasive misleads — handoff §13). kcdx self-captures from inside the process.

**What we measure:** Main's own return-address chain (`RtlCaptureStackBackTrace`, ~48 frames,
module-relative) + seat state (swapped-object ptr, live vtable ptr, gEnv pCryPak slot, index-build
completion), dumped ONCE when the stall-trigger fires. Swap-ON and swap-OFF (`kcdx-noswap`), diffed offline.

**The trigger (user-settled — arm-on-present + heartbeat-floor):**
- ARM when `present_count` starts climbing (streaming done, UI compositing — the proven boundary) AND a
  heartbeat floor has elapsed (not a transient).
- FIRE once when `draw_indexed` is still 0 after N frames past arming (geometry never requested).
- The detector arms BEFORE the `kcdx-noswap` early-return (the PROBE W/K/P pattern) so swap-OFF captures at
  the same phase.

**The sink (user-settled — raw addresses, symbolize offline):** raw module-relative address chain + seat
state → `kcdx-dev.log` under a stable tag (e.g. `STALL_STACK`). NO in-process symbolization (WHGame has no
PDB — nearest-export noise, §13; in-process symbolization adds fault surface on the stalled thread). Agent
symbolizes offline against kcdx PDB + the WHGame RVA-correction table (real RVA = nearest-export RVA +
offset — the standing trap).

**Pre-committed outcome map (`results-driven.md` — flat, falsifiable):**
- Swap-ON parked in a level/scene/sequencer-init frame that swap-OFF already passed → **gate NAMED** →
  link 2 (read the gate body, instrument the value it reads).
- Swap-OFF trigger never fires (it advanced) + swap-ON fires parked somewhere → the parked frame IS the
  gate; swap-OFF's non-fire confirms the gate is what diverges → link 2.
- Swap-ON parked in a frame with NO level/scene-init identity → **"level never loads" FALSIFIED** (it is
  currently only an INFERENCE from FS-trace absence — §5, handoff §13); axis moves to where Main actually
  sits.
- Capture fires but the stack is all-kcdx / all-present-loop frames → trigger MIS-ARMED (fired before the
  real stall) → re-tune the arm condition; NOT a result.

**Why this is link 1 and not root:** it names the frame. A frame is a WHERE. It does not say what value is
wrong or who wrote it. It is the indispensable first link because links 2–5 cannot even be specced until
the gate is named (you cannot instrument "the value the gate reads" before you know the gate) — this is why
M2–M4 are deferred until M1 lands (incremental-delivery: the dependency is named before its consumer).

### Measurement 2 — the gate's read (link 2 + the order-vs-value fork; specced AFTER M1)

**Where:** the gate function M1 named — instrument the specific STATE it reads to decide "advance."
**What we measure:** the value(s) the gate reads, swap-ON vs swap-OFF, at the decision point. Plus: WHEN it
is read vs WHEN its producer runs (timestamp/heartbeat-tick both), because of the fork below.

**The order-vs-value fork (the branch the current plan makes explicit — do not collapse it):**
- **Value WRONG** (swap-ON reads a different value than swap-OFF at the same phase) → link 4: xref who
  writes that value in vanilla → find the CCryPak slot whose side-effect sets it → link 5 (dropped
  side-effect class, handoff §9.5).
- **Value RIGHT but LATE** (swap-ON eventually reads the correct value, but AFTER the advance decision
  already happened, or the producer runs after the consumer) → link 4/5 is an INIT-ORDER root, not a
  dropped value. Prime suspect: the seat's `WaitForSingleObject(gate, INFINITE)` on Main (§1) reordering a
  producer after its consumer. A dropped-side-effect probe would find NOTHING here — this fork is what
  prevents another red-herring chase (like R2 / PROBE X).

**Why this is link 2/3 and not root:** it supplies *what value* and *in what order* (two AP17 clauses) but
not *who writes it* / *why inevitable* — those are the code reads (links 4–5).

### Measurement 3 — the vanilla-differential self-validation (PROBE W — designed, never built)

**Where:** on a SAFE read-only metadata/enum slot HIT, ALSO call the captured engine original (idempotent —
no handle, no cursor, no mutation) and log ONLY when kcdx's answer DIFFERS from vanilla's, with caller
return-address attribution (tag `VANILLA_DIFF`). SAFE ops ONLY (existence / IsFolder / attributes / stat /
size-by-name + the enum set); NEVER alongside open/read/write.

**What it catches that a byte-diff cannot:** kcdx serves CORRECTLY while the engine would have made a
DIFFERENT successful decision — a correct-but-divergent answer. This is the standing observability want
(`feedback_debug_reset_frame_after_two_same_axis`). It hunts the perturbed-STATE class directly and, if
kept, is permanent observability infrastructure, not a throwaway probe.

**Role in the chain:** it CORROBORATES link 3 (surfaces the divergent decision the gate consumes) and can
independently point at the slot for link 4. It is not on the critical path to naming the gate (M1 is), but
it is the cheapest way to catch a correct-but-divergent serve if M2's fork lands on "value wrong via a
metadata/enum decision."

### Measurement 4 — the unchecked backdrop premise (one cheap read; kills a standing trap)

**Where:** an FS-trace read on the WORKING swap-OFF menu run.
**What we measure:** does the working menu itself read `.cgf` / `mmrm` (mesh) files at all?
**Why:** "the level never loads swap-ON" and "the menu is a compositor over a backdrop level" both ASSUME
the working menu reads meshes (handoff §13). If the working menu reads ZERO meshes, "no level loaded"
cannot be the black-vs-menu differentiator — and a whole branch of the hunt is a dead end before we spend
a probe on it. This is a trap-closer, not a chain link; run it opportunistically with M1's swap-OFF launch.

---

## 4. Why the chain is complete (the guarantee, stated precisely)

The chain REACHES root because its terminal condition is the AP17 paragraph and every link is measured or
read — none inferred:

- Links 1–3 are DIRECT observation (self-dump stack, instrumented gate read, vanilla-diff) — ground truth,
  not theory. The order-vs-value fork at link 3 forecloses the single most likely wrong turn (hunting a
  dropped value when the truth is a reordered wait).
- Links 4–5 are code reads of NAMED targets (the gate's value-writer, the original slot/step body) — a
  read of real bodies, the same reuse-first RE ladder that settled the 4 slot-diff consumers.
- The terminal AP17 paragraph is verifier-checked (Gate B / root-cause-verifier, debug §3d) with the debug
  agent's chain-of-reasoning WITHHELD — an independent read of "does this mechanism actually hold."

**Where the guarantee is CONDITIONAL (stated honestly):** if link 1 names a gate whose divergence is a
MULTI-HOP indirect chain (a value produced correctly at phase X, consumed against stale order at phase Y, N
threads/stages later), links 2–5 may need to iterate (name the immediate gate → its input → that input's
producer). The chain STRUCTURE still holds (each hop is measured/read, terminal is AP17), but it may be
more than one pass. That is not a hole in the plan — it is the plan working: each hop is ground-truth, so
we converge instead of circling. The ONLY way this fails to reach root is if we abandon the chain and
symptom-fix a mid-chain fact (§5) — which is exactly what this doc exists to prevent.

---

## 5. The anti-symptom-fix guardrails (do not stop mid-chain)

Every prior "real defect fixed, still black" (handoff §11: `e88a9eb`, `d265732`, `0249b2e`, `83a9279`) was
a mid-chain fact fixed as if it were root. Each was a genuine correctness gain but NONE was the root,
because the fix landed on a served-output symptom, not on the phase-advance gate. Guardrails:

- **A fix is NOT root until the AP17 paragraph holds.** "Fixing X made the enum succeed" is a link, not a
  Resolution. Do not close on it.
- **"The level never loads swap-ON" is an INFERENCE, not a fact** (handoff §13). It rests on FS-trace
  ABSENCE (`.cgf`=0, `mmrm`=0), never on catching a load trigger fire-or-not. FS_BOOT_TRACE is file-ops-only
  — blind to reached-and-early-returned vs never-called. PROBE X after-hooked the candidate trigger
  `CResourceList::Load` and it fired ZERO times on the WORKING menu too (red herring). Do not build on
  "level never loads" until M1 gives positive evidence (a stack showing where Main actually is).
- **Nearest-export frame labels are NOISE** (no PDB). Add the export RVA before disassembling any offset
  (real RVA = nearest-export RVA + offset). NGX/FSR2 is not the subsystem.
- **After ONE failed fix, probe — never fix #2 on a new theory** (`results-driven.md`). If a link's fix
  does not advance the chain, re-observe ground truth; do not hop theories.
- **Observe what the swap CHANGES in engine state/order — not another per-frame global** (PROBE M: the
  wedged-stack frames/globals run IDENTICALLY swap-on/off — the per-frame trap). Any A/B probe is armed
  BEFORE the swap decision (the PROBE W/K/P trick), since swap-OFF emits no kcdx FS trace.

---

## 6. Build order (incremental-delivery — each step independently verifiable)

1. **The self-dump harness + Measurement 1** (both arms). Decisive: names the gate. Verifiable = a
   `STALL_STACK` dump appears in each arm's log; the diff names (or falsifies) a gate frame. Run
   Measurement 4 opportunistically on the swap-OFF launch (free FS-trace read).
2. **Measurement 2** (specced from M1's named gate) + its order-vs-value fork. Verifiable = the gate's read
   value is captured swap-on/off; the fork resolves to value-wrong or right-but-late.
3. **Measurement 3** (PROBE W differential) if M2's fork points at a metadata/enum decision, and as standing
   observability regardless. Verifiable = `VANILLA_DIFF` lines (or their proven absence) with caller
   attribution.
4. **Links 4–5 code reads** — xref the value-writer, read the original slot/step body, draft the AP17
   paragraph. Verifiable = the falsifiable mechanism paragraph, Gate-B verified.

M2–M4 are NOT specced before M1 lands: the gate must be named before its read can be instrumented
(the dependency lands before its consumer). Building them now would be speculative.

## 7. No-residue debt (working-artifacts.md — the harness is a probe)

The self-dump harness and every measurement are probes: agent writes / builds / deploys / hash-verifies /
reads the log; user launches. On closure, capture each probe's finding + wiring to `_research/probe-archive/`
and REMOVE from live source — no `#if 0`, no dormant flag, ZERO cost in live code. This joins the standing
armed-probe debt the handoff §12.A already tracks (`pso_probe`, `present_probe`, `boot_watch`,
`dispatch_probe`, `reswap_probe`, `drawcall_probe`, `boot_trace` differential, `levelload_probe`); the
`find_slots.cpp` synthetic-dir-entry is a REAL FIX to promote (drop only its logging).

## 8. The fix constraint (user-stated, binding — handoff §7)

The eventual fix MUST live inside kcdx's full-init ownership. NO thunk / hand-back to the original engine
for any part of init. (This invariant is reconsiderable if links 4–5 prove the root is unfixable within it
— but that is a decision for AFTER the mechanism is named, never a reason to scope back pre-emptively.)
