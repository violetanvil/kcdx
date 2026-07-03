---
id: KI-0028
opened: 2026-06-20
status: open
commit_at_filing: 4befc07
---

# Boot hangs at UI/render bring-up (sound loads, no video, no input) — after KI-0027's table-DB load succeeds

**Status:** open

> **AUTHORITATIVE SUMMARY:** `_research/ki0028-fsr2-poll-loop-recon/HANDOFF.md` is the single
> verified/inferred/open split (no unproven confidence). This trail below is the chronological
> investigation log; where an earlier entry states something with more confidence than the handoff,
> the **handoff governs**. Several earlier "PROVEN"/"converged"/"exonerated" claims have been
> downgraded in place — read the handoff first.
>
> **CORRECTION (2026-06-21) — the "entity-init" frames are an offset-vs-RVA artifact.** Every
> "entity-init" / "`0x36eb39`" / "`CreateInstance` entity construction" claim in this trail is an
> **offset-vs-RVA conflation**: the cdb frames are `ffxFsr2ResourceIsNull` (nearest-export NOISE)
> **+ offset**; the bare offset `0x36eb39` was disassembled as a raw RVA (an unrelated entity-name
> stub). The real frame is RVA `0x869c39` (= export `0x4fb100` + `0x36eb39`), a
> window/display-mode/fullscreen function. Do not read "entity-init" as the subsystem.
>
> **CORRECTION 2 (2026-06-22) — PROBE M falsified "the `0x869c39` loop is the wedge gate."** A live
> swap-ON vs swap-OFF read of that loop's exit-condition globals (counters `0x56628d8/dc`, flags
> `0x556d080/084`, singletons `0x492b890`-family) showed them evolving **IDENTICALLY** in the wedged
> run and the menu-reaching run — counters freeze at `0x80002B7x` in BOTH, never reaching the `-1` the
> static read predicted as the exit sentinel. So `0x869c39` is **normal per-frame code that runs the
> same with or without the swap**, on the wedged stack only because the whole update loop runs every
> frame (Reframe 6) — NOT the differentiator. The swap IS the differentiator (P-F holds), but the
> divergence is **not** in this loop's state. STOP chasing frames on the wedged stack. Full A/B + the
> reframe: `_research/ki0028-fsr2-poll-loop-recon/FINDING-real-rva-window-mode-loop.md` §"PROBE M RESULT".
>
> **CORRECTION 3 (2026-06-22) — NOT a hang; re-localized to "render-build stalls before pipeline
> creation."** The whole "deadlock / hang / no-present" framing is dead by direct measurement: PROBE W
> showed the game TICKS ~35/s, PROBE K showed it PRESENTS at 120fps with GPU scanout → frames are
> presented BLACK (a render-CONTENT failure, not a hang). A real defect was found + FIXED (`e88a9eb`: the
> `data/gameshaders/` shader alias was not folded to the indexed `shaders/` key — 21 shaders incl. the
> Scaleform UI shader never loaded); still black after. The swap-off baseline reaches the menu (the swap is
> the differentiator). PROBE P then hooked `ID3D12Device::CreateGraphicsPipelineState` (the consumption
> side, identical swap-on/off) and found the engine creates **exactly ONE graphics PSO** the whole run (a
> ~700-byte present-blit shader) and **zero compute PSOs** — vs the dozens-to-hundreds a rendering
> CryEngine builds. So the render pipeline gets present up but **never builds the scene/material/UI
> pipelines**: the shader-SYSTEM init stalls UPSTREAM of PSO creation (consistent with the swap-on-only 36
> phantom `data/gameshaders/*.ext` probes + heavy `lookupdata.bin` re-reads). Served shader BYTES are proven
> correct (cap-109 DEFLATE + want==got), so it is NOT corrupt content — it is the engine's shader-system
> init not completing under the swap. Status stays OPEN; root cause (the precise stall mechanism, AP17)
> still owed. Full trail: the FINDING §"PROBE P RESULT".
>
> **CORRECTION 4 (2026-06-22) — the whole shader/PSO axis AND the entire kcdx-SERVED-OUTPUT class are
> exonerated; the wedge is a kcdx-perturbed STATE/ORDER, not anything kcdx serves.** PROBE P swap-OFF
> overturned CORRECTION 3's premise: `gfx_calls=1` on the WORKING menu too — the engine builds its menu
> pipelines from an on-disk cache through an API PROBE P doesn't hook, so `gfx_calls=1` is NORMAL, not a
> stall. Six probes (R/R2/R3/R4/P) confirmed the shader-cache-validation / offline-precache /
> runtime-precache / lazy-create / device-PSO-create paths ALL run identically swap-ON vs swap-OFF.
> PROBE S then measured the draws: swap-ON records MORE draws (9500 vs 1383) but **zero indexed-geometry
> draws** (vs 96 swap-OFF) to valid, non-null render targets. PROBE T (this session) then forced kcdx
> handles out of the engine's pak-index alias range (bit-40 encoding) to test the handle-straddle (H3a):
> **STILL BLACK, `draw_indexed=0` unchanged, serve health byte-identical, no fault** — falsifying the
> straddle AND proving the engine treats the kcdx handle as a fully opaque token (never tag-tested,
> dereferenced, or truncated). **Net: every kcdx OUTPUT the engine consumes — served bytes (`diffs=0`),
> handle value/semantics, sizes, enumeration counts — is now measured identical-or-correct swap-ON, yet
> the menu's indexed geometry is never built and the frame composites black.** The divergence is NOT in
> what kcdx SERVES; it is in a STATE or init-ORDER the swap perturbs that the geometry-build path depends
> on (the H4-class P-F only killed for kcdx's added THREADS, never for the swap's effect on engine init
> order/state). OPEN; next probe targets the kcdx-perturbed state the indexed-geometry build reads, not a
> served file. Full trail: `_research/ki0028-cshaderman-pso-consumer-recon/FINDINGS.md` + `HANDLE-STRADDLE-LEAD.md` §"PROBE T RESULT".
>
> **CORRECTION 5 (2026-06-22) — a REAL FS-resolution gate was found + FIXED (`83a9279`); black persists, REINFORCING
> CORRECTION 4.** The vanilla-vs-kcdx end-to-end FS map (`_research/ki0028-vanilla-init-fs-map/`) found a genuine
> defect the prior render-axis probes were downstream of: the asset index keyed every pak entry by the BARE
> central-directory name, dropping each pak's BIND-ROOT (the mount-point prefix vanilla's OpenPack slot-7
> auto-derives from the pak's dir). A nested level pak (`Data/Levels/<lvl>/level.pak`) stores `leveldata.xml`
> bare, but the engine requests it as `Levels/<lvl>/leveldata.xml` (body-read pathbuilder `0x4dd384`/`0x4dcbb3`);
> kcdx stored it bare → every level-resource request MISSED. `BindRootOf` + the `<bind-root>/<name>` keying fixes
> it (collision-safe: cross-pak collisions DROP 448→182; cap-112 (c) is the permanent regression). **Live verify:
> the documented abort is CLEARED** — index built (77 paks, 516k entries), FS served 4435 pak reads with ZERO
> level-resource misses, and an invasive capture shows the main thread in a healthy `PeekMessageW` pump with
> `C_Game::CreateInstance` running on workers and NO `RaiseException`. **Black screen STILL persists** — exactly
> CORRECTION 4's verdict: every kcdx-SERVED OUTPUT is now correct (the FS gate is closed) yet the frame
> composites black, so the wedge is a kcdx-perturbed STATE/init-ORDER the geometry build depends on, NOT a served
> file. The FS-resolution axis is now EXONERATED end-to-end (served bytes correct AND every requested path now
> hits). OPEN; the next probe targets the perturbed state/order (CORRECTION 4's lead), with a fresh invasive
> capture (use `qd`, not `q`). Full trail: `_research/ki0028-vanilla-init-fs-map/POST-FIX-LIVE-CAPTURE.md` +
> `ROOT-CAUSE-bind-root-prefix.md`.
>
> **CORRECTION 6 (2026-07-02) — architecture decision + the root-cause measurement plan.** After an
> architecture review (is owning the whole filesystem worth it?): the full FS takeover STAYS — it wins on
> Performance and, once working, "just works" = UX #1, trading nothing; the problem is we have never
> OBSERVED the mechanism, so we measure it, not scope back. The settled measurement plan is a 5-LINK
> root-cause CHAIN whose terminal condition is the AP17 mechanism paragraph (NOT "a probe bottoms out"):
> **(1)** self-dump main-thread stack diff → names the gate frame; **(2)** instrument that gate's read →
> the wrong value; **(3)** order-vs-value fork → wrong, or right-but-late; **(4)** xref the value's writer
> (code read) → who writes it; **(5)** read the original slot/step body (code read) → why the swap made it
> inevitable. The measurements deliver links 1–3; links 4–5 are code reads. Full plan (honest about where
> the guarantee is conditional): `_research/ki0028-fsr2-poll-loop-recon/KI-0028-ROOT-CAUSE-MEASUREMENT-PLAN.md`.
> One grounded init-order fact (verified `seating_hook.cpp:305-352`): the seat blocks Main on
> `WaitForSingleObject(overlayReadyGate, INFINITE)` inside `CSystem::Init` — a real init-order perturbation
> vanilla never does; it is why the plan carries an explicit order-vs-value fork, not a dropped-value
> assumption. **PROBE Y (Measurement 1, the decisive never-obtained observation) is NEXT** — see the probe
> plan below (`## PROBE Y`). The existing `boot_watch` PROBE-H machinery already builds the capture
> (`DumpAllThreads`: suspend→walk-raw→resume→log, Gate-A-blessed); Y only adds the trigger it lacks (our
> stall keeps the heartbeat ALIVE, so PROBE H's cessation-trigger never fires on our case). The fix must
> stay inside kcdx's full-init ownership (no thunk-back); closure needs the AP17 paragraph, never "black
> gone."

With the file-system-takeover directory-enumeration triplet live (KI-0027 fixed,
`4befc07`), the boot now passes the table-database load and proceeds — but **HANGS**
at UI/render bring-up: **sound loads, no video ever appears, and the game accepts no
input** (the user had to kill the process via Task Manager — not a crash, a hang; no
crash dump produced). This is a NEW failure the KI-0027 fix unblocked the boot far
enough to reach — the same chain pattern as KI-0026 → KI-0027 (each fix exposes the
next latent boot blocker).

## PROBE Y — Measurement 1: the self-dump main-thread stack diff (CURRENT, build pending Gate A)

The decisive never-obtained observation (handoff §12.B; plan Measurement 1). Names the boot-phase
sequencer gate by ground truth — a live main-thread stack captured DURING the stall, swap-ON vs swap-OFF,
diffed offline. No debugger (the symptom IS the steady state — the attach race is unwinnable; every prior
invasive attempt died or caught a post-AltF4 process; `-pv` misleads). kcdx self-captures from inside
`HookedUpdate` via the existing `boot_watch` machinery.

**Why the existing PROBE H does not already do this:** PROBE H's `WatcherMain` fires `DumpAllThreads` only
on heartbeat CESSATION (`kStallMs` stall). Our proven case keeps the heartbeat ALIVE (Main ticks ~35/s;
geometry just never requested), so PROBE H never arms. PROBE Y adds the trigger it lacks and reuses its
Gate-A-blessed capture path verbatim.

**One variable:** the trigger condition (present-climbing + heartbeat-floor + `draw_indexed==0`). Capture
path unchanged. Sink: raw module-relative address chain (`DumpAllThreads` already emits `module_rva`),
symbolized offline vs kcdx PDB + WHGame RVA table (real RVA = nearest-export RVA + offset).

**Pre-committed outcome→meaning map (flat, falsifiable):**
- swap-ON parked in a level/scene/sequencer-init frame swap-OFF already passed → **gate NAMED** → link 2.
- swap-OFF trigger never fires (advanced) + swap-ON fires parked → the parked frame IS the gate → link 2.
- swap-ON parked in NO level/scene-init frame → **"level never loads" FALSIFIED** → axis moves to where
  Main actually sits.
- capture fires on all-kcdx/all-present-loop frames → trigger MIS-ARMED (fired before the real stall) →
  re-tune the arm condition; NOT a result.

**RESULT (RAN 2026-07-02, swap-ON `kcdx-dev_2026-07-02_09-44-29.log`; swap-OFF control
`…_09-41-45.log`).** PROBE Y's TRIGGER works perfectly, validated on BOTH arms:
- **swap-OFF (control):** `STALL_STACK_ARMED` (present=574, ticks=208) → `STALL_STACK_ADVANCED`
  (draw_indexed=10320) → NO dump. Correctly detected the working outcome (geometry WAS requested). ✓
- **swap-ON (repro):** `STALL_STACK_ARMED` (present=3362, draw_indexed=0, ticks=214) →
  `STALL_STACK_FIRED` (present=4513, **draw_indexed=0**, confirm_polls=40) → dumped 256 threads. ✓
- **NEW MEASURED FACT (was only INFERRED before): the stall signature is real** — swap-ON, present
  climbs 3362→4513 (120fps) while `draw_indexed` stays EXACTLY 0 across the whole 40-poll confirm
  window. "reached streaming/UI, never requested geometry" is now GROUND TRUTH, not FS-trace absence.
- **Main (tid=42548, confirmed = heartbeat tid, still ticking 1579):** stack is the KNOWN per-frame
  window/display loop — frame 13 `WHGame 0x869C39`, frame 3 `0x866090` (the focus-poll), frame 10
  `kcdx.dll 0x42A1A` (HookedUpdate), KingdomCome.exe main below. NO level/scene/geometry-init frame.
  The deep non-Main threads are OS input (`CoreMessaging`/`inputhost`) + Bink video (`bink2w64`) — all
  normal, none parked in engine geometry work.
- **VERDICT — outcome 3 (partial) + a CAPTURE-DESIGN LIMITATION, not the gate named.** Main is in NO
  level-init frame → "level never loads" is now POSITIVELY SUPPORTED (Main never entered a load path),
  and the axis moves to "where Main sits" = the per-frame window loop `0x869C39`/`0x866090` — which
  PROBE M ALREADY proved runs IDENTICALLY swap-on/off (the per-frame trap, §5.4). So a single Main-stack
  sample does NOT name the sequencer gate: **Main is not PARKED at a divergence point, it is LOOPING**
  a full per-frame cycle that never DISPATCHES the geometry-request work. A call-stack snapshot of a
  running loop catches the loop, not the branch-not-taken / job-not-dispatched that is the real
  divergence (the Reframe 6 sampling-artifact, now confirmed for the stack-diff approach too). AND the
  swap-OFF arm ADVANCED (no dump), so there is no swap-OFF Main stack to diff against regardless.
- **What Measurement 1 DID deliver:** the stall is a positively-measured fact (not inference); Main is
  confirmed looping-not-parked; "level never loads" gains positive support; the render/present/window
  axes are re-confirmed off the table (present advancing, window healthy, no parked render thread). What
  it did NOT deliver: the named gate — because the divergence is a NOT-TAKEN transition inside a running
  loop, not a parked frame. The next probe must observe the DECISION (what per-frame check Main evaluates
  to decide "advance to geometry" and why it evaluates false swap-ON), NOT another stack snapshot.

**Gate A verdict (architect-review, 2026-07-02): PROCEED-with-conditions — one BLOCKING never-fire hole
closed by re-design.** The blocking finding (correct): the sibling probe SUMMARY WATCHERS self-terminate on
bounded read budgets (`present_probe.cpp:118` after 120 reads ~2min; `drawcall_probe.cpp:242` after 40
reads ~2min). If the streaming→geometry transition happens AFTER those budgets expire, a cached present
count reads FLAT, PROBE Y never arms, and the silence is indistinguishable from "the bug didn't occur" —
the exact never-fire trap the plan exists to avoid. **Re-design that closes it (Gate A option a+c):** PROBE
Y depends on NOTHING the sibling watchers keep alive — it reads the RAW underlying state that lives the
whole process: `g_drawIndexed` (a live atomic incremented in the D3D12 hook — never bounded) directly, and
`GetLastPresentCount` off the captured swapchain ptr (`g_swapchain`, valid process-life) directly, NOT a
watcher-cached last-read. Plus PROBE Y emits a DISTINGUISHABLE terminal signal for every outcome
(`STALL_STACK_ARMED` / `STALL_STACK_FIRED` / `STALL_STACK_NEVER_ARMED` with the reason: present never
climbed / swapchain never captured) so a never-fire is an OBSERVED outcome, never silence. Gate A confirmed:
reuse of `DumpAllThreads` is sound (F1/F2 suspend-window discipline is inside it, untouched by a trigger
change); cross-probe atomic coupling is fine for a probe (do NOT centralize — scope creep vs no-residue);
no hook-arming hazard vs the swap A/B; nothing crosses into a user-owned design decision (pure diagnostic,
Performance-only, no-residue throwaway). Relaxed ordering on the reads is correct (a real stall holds the
condition many frames; one-sample lag tolerated) — carry a why-comment per the `g_lastMs` precedent
(`boot_watch.cpp:24`).

**Probe plan (persisted status list — §B.4; flip each row as it lands):**

| Step | Status | Note |
|---|---|---|
| Y.0 Gate A — architect-review the trigger-addition design | DONE | PROCEED-with-conditions; never-fire hole closed by the re-design above |
| Y.1 Read accessors on the two probe headers: `DrawcallProbeIndexedCount()` (reads live `g_drawIndexed`) + `PresentProbeLastCount()` (reads `GetLastPresentCount` off the captured swapchain, NOT a cached value) | DONE (`d43cc75`) | Gate-A fix: independent of the sibling watchers' bounded lifetimes |
| Y.2 PROBE Y in its own file `stall_stack_probe.{h,cpp}`: arm on present-climbing + heartbeat-floor, fire `BootWatchDumpAllThreads("stall_no_geometry")` on `draw_indexed==0` after N wakes; emit `STALL_STACK_ARMED`/`FIRED`/`ADVANCED`/`NEVER_ARMED` | DONE (`d43cc75`) | own file (no-monolith line cap); reuses the Gate-A-blessed capture; dump-latch added (2 watcher threads); build GREEN, step-review PROCEED |
| Y.3 Build + deploy both arms (swap-ON, `kcdx-noswap`), hash-verify, enable dev mode | DONE | deployed `E93CCDD0…`, dev mode on, stale noswap marker removed for swap-ON |
| Y.4 User launches swap-OFF (control) + swap-ON (repro); agent reads `STALL_STACK` frames | DONE | both ran; trigger validated both arms; swap-ON FIRED + dumped 256 threads |
| Y.5 Symbolize offline + diff → name the gate frame (or falsify "level never loads") | DONE (partial) | stall signature MEASURED; Main confirmed LOOPING (not parked) on the per-frame trap `0x869C39`; gate NOT named — divergence is a not-taken transition, not a parked frame. See RESULT above |
| Y.6 (opportunistic on the swap-OFF launch) FS-trace read — does the WORKING menu read `.cgf`/`mmrm`? | DONE (2026-07-02) | Answered via DRAW_PROBE (kcdx FS-trace is blind swap-OFF — slots bypassed). Working menu `draw_indexed=68024` (vs swap-ON `0`) → backdrop-premise trap CONFIRMED real; "no geometry loaded" IS the differentiator. NEW: working path holds `draw_indexed=0` for ~27s then a TRANSITION fires — swap-ON never fires it. See `_research/ki0028-tick-geometry-dispatch-recon/Y6-workingmenu-draw-progression.md` |

**PROBE Y → Measurement 2 REFRAME (2026-07-02, reuse-first off `_research/ki0028-window-exit-gate-recon/`).**
PROBE Y's Main stack (`0x869C39` window-focus loop, `0x866090` bounded poll) is EXACTLY the region the
window-exit-gate recon already mapped AND PROBE M already exonerated (loop fields evolve identically
swap-on/off). Combined with PROBE Y's NEW fact (`draw_indexed`=0, but PROBE K proved present SUCCEEDS),
the conclusion sharpens: **the window-focus loop Main was sampled in is NOT the geometry-dispatch path** —
it is one per-frame sub-call of the tick. The geometry-request decision lives ELSEWHERE in the per-frame
tick, dispatched from the tick dispatcher (`0x667b24`; PROBE Y frames 9/11/12 = `0x667DE2`/`0x6678A0`/
`0x532FB5` around `HookedUpdate` `0x42A1A`). PROBE Y merely caught Main in the window-poll sub-call, not
the geometry sub-call. **Measurement 2 RE target (decided — static-first):** disassemble the tick
DISPATCHER (`0x667b24` + the `0x532FB5`→`0x6678A0`→`0x667DE2` call chain above `HookedUpdate`) to find
where the per-frame tick CONDITIONALLY dispatches scene-geometry/level-advance work, and what state gates
it — then probe that state swap-ON vs swap-OFF. NOT another stack snapshot; observe the not-taken
dispatch DECISION. Existing recon's surviving candidates to carry in: the `0x549b4a0` display-context
(`[+0x40]`, CSystem::Init lineage `0x1865a88`) and G6's engine-expected-HWND (`[this+0x740]`) — both
runtime-vtable, static-unresolvable, now lower-priority since the axis moved from window/present to
geometry-dispatch.

**Measurement 2 RESULT (2026-07-02) — the tick render-dispatch axis is EXONERATED; it is NOT the differentiator.**
Static disassembly of the tick dispatcher `0x667b24` (`_research/ki0028-tick-geometry-dispatch-recon/`, gated body-read verifier PROCEED) found the tick's ONE per-frame render-dispatch gate: `0x667ed0 cmp qword [0x492b908], r14 ; je 0x667f84` — when render singleton `0x492b908` is null, the tick skips the block dispatching `[obj+0x240/0x250/0x248]` (via the singleton) + `[rsi+0x430/0x428]` (via CSystem), with float/resolution args = an IRenderer/I3DEngine interface. `0x492b908` has ZERO static writers (gEnv-table pointer, runtime-installed) — its null-ness swap-on/off is not statically decidable.

**BUT the existing draw-count evidence already settles the runtime half, killing this as the gate:** `RECONCILE-render-vs-levelload-2026-06-23.md` records swap-ON `draw_instanced=9500` (vs `1383` swap-OFF) alongside `draw_indexed=0`. The renderer is issuing 9500 instanced draws swap-ON → the render tick block RUNS → `0x492b908` is NON-null swap-ON → the `0x667ed0` gate is TAKEN (not skipped) on both paths. This is my FINDINGS "Outcome B": the render dispatch executes swap-ON; `draw_indexed=0` originates DEEPER, not at this gate. **Measurement 2 confirms Reframe 7/8's conclusion from the code side: `draw_indexed=0` is a downstream symptom of "no level geometry loaded," NOT a render-routing/dispatch gate.** The per-frame render tick is alive and un-perturbed by the swap; the missing thing is UPSTREAM (the level-load trigger Reframe 8 left unproven).

**This does NOT re-open a one-deep probe** (per the Reframe-8 step-back directive): it EXONERATES the tick-dispatch axis and returns the question to the user-directed reassessment — "what does the swap provably perturb, before the next probe." The render pipeline is now exonerated at three layers (shader/PSO by Reframe 7's six probes; window/present by PROBE M + window-exit recon; per-frame render-dispatch by this Measurement 2). The surviving un-exonerated axis is the level-load TRIGGER (Reframe 8), still the reassessment's subject.

## Relationship to KI-0027

KI-0027 (the table-DB load failing because kcdx's fs-takeover did not serve the
`<base>__*.xml` override-glob directory enumeration) is **FIXED + verified** in this
same run: zero "Database system error" / `err_id=259` in `kcd.log`, the table globs
return correct match counts (`matched=0` for vanilla `__*` overrides, not the
pre-fix 528 whole-directory over-match), and the test suite ran to `passing=320/343`.
KI-0028 is a DISTINCT subsystem — render/UI device bring-up, not filesystem — that the
boot only NOW reaches. The fs-takeover enumeration is functioning correctly; this hang
is downstream of it.

## Symptom

- **User-reported** (the live experience): game launched with the KI-0027-fixed engine
  (`4befc07`): sound loaded, "no video", input unresponsive; the user killed the process
  via Task Manager (a hang, not a crash; no crash dump).
- **Log-corrected** (see §Reframe — the user report was a perception, not ground truth):
  `kcd.log` proves the **main menu DID render** — `PlayVideoOnly 'main_menu_kutnohorsko3'`
  (menu background video), `[MFX] Loading FXLib` (material effects), and `[Pros]` online
  banners downloaded over HTTP (200 OK) and shown. The boot completed to a live, rendering,
  networked main menu, THEN hung — input dead, no further log output on the single boot
  thread. The "no video" was almost certainly "menu up but frozen / no gameplay", not a
  black screen.

## Reframe (2026-06-20 — /debug §B static-log pass; corrects the original premise)

The original Facts were authored from the **kcdx-dev log tail alone** and never read
`kcd.log`. Reading both logs together by timestamp corrects the picture: the menu
rendered, and the cursor serve the original facts blamed actually **succeeded**.

**Corrected boot timeline (single thread, `tid=46452`, both logs merged):**

| Time | Log | Event |
|------|-----|-------|
| 15:58:24–26 | dev | fs-takeover `FindFirst` enum over saves / materialeffects / table `__*` globs — all `matched=N` correct (KI-0027 holds) |
| 15:58:26.472 | dev | `read_entry ... cursor_green.dds` (name **`engineassets/...`**, no alias) served OK → boot continues 24 s more |
| 15:58:46.584 | dev | suite SUMMARY `passing=320 ... pending=21 total=343` |
| 15:58:47.549 | kcd | LAST `kcd.log` line: live menu — banners over HTTP 200, MFX libs, `PlayVideoOnly 'main_menu_kutnohorsko3'` |
| 15:58:50.093 | dev | LAST dev line: `read_entry ... cursor_green.dds` (name **`%engine%/engineassets/...`**, aliased) — same `data_off=79371331`, `usize=4232`, **served OK** |
| (after) | — | nothing further in EITHER log; process hangs; user kills it |

## Facts

- The table-DB load SUCCEEDS — zero "Database system error" / `err_id=259` in
  `kcd.log` this run (the KI-0027 fix holds). (FACT — `kcdx_2026-06-20_15-58-02.log`)
- **The main menu rendered** — `kcd.log` (652 lines, ZERO error/fatal/assert) ends at a
  live menu: `PlayVideoOnly 'main_menu_kutnohorsko3'`, MFX FXLibs loaded, 3 online banners
  downloaded (HTTP 200) and shown. The original "menu never renders" is disproven.
  (FACT — `kcd.log` lines 600–652, `game/kcd.log` in the crash zip)
- **The last cursor serve SUCCEEDED — kcdx did NOT wedge inside the read.** The final dev
  line logs a completed `read_entry` (valid `data_off`, `usize=4232`); PAK_READER logs the
  successful serve, not an attempt. The engine got its bytes and then stopped asking kcdx
  for anything. The hang is in the engine's consumer of those bytes / the single boot
  thread, NOT in the kcdx FS read. (FACT — `kcdx-dev` line 83562 + `src/fs_takeover` read path)
- **The `%engine%/` alias prefix on the last read resolves correctly** — `data_off` is
  identical to the earlier un-aliased serve of the same file, so `ExpandEngineAliasToIndexKey`
  mapped it to the same pak entry. The alias is not failing. (FACT —
  `src/fs_takeover/asset_index.cpp` `ExpandEngineAliasToIndexKey` + the two matching `data_off`)
- **There is no engine crash.** The crash zip at the failing timestamp is the watchdog's
  kill-time snapshot; `crash/bugsplat_F62P7UL5.log` is empty (BOM only). Confirms hang, not
  fault. (FACT — `crash_2026-06-20_15-58-02.zip` contents)
- The `[Warning] Unknown command: kcdx_find WHGame.dll --string "..."` in `kcd.log` is a
  benign red herring — a config/autoexec replay of a leftover console line, not on the hang
  path (`[Pros]` banner activity continues normally after it). (FACT — `kcd.log` line 645 +
  `src/console_commands_find.cpp` registration)
- Everything ran on ONE thread (`tid=46452`). A block on that single thread explains why
  BOTH logs stop simultaneously with no further FS or engine output. (FACT — every dev line
  tagged `tid=46452`)
- The boot reached deep init: trampoline pool, LUA_SHIM passes, FOREIGN_HOOK selftest,
  320 suite tests passing — engine, hooks, Lua VM, FS all up. (FACT — `kcd.log` + dev tail)
- **The P-E run PROGRESSES for ~41s, then the log goes SILENT, then the dump (37s later) shows the
  wedge.** The dev log emits 30 `[TEST] SUMMARY` lines (`HookedUpdate` steady-state ran), first
  `20:29:02.380`, last `20:29:43.475` (`passing=320`); PAK_READER continues to `20:29:43.455`. The
  log then STOPS at `20:29:43.475`. The cdb capture was taken at `20:30:20.935` — 37s AFTER the last
  log line. So the wedge ONSET is `~20:29:43` (where the log goes silent), after a window of real
  progress — NOT at boot start, NOT absent. (FACT — P-E dev log SUMMARY timestamps + capture-file
  mtime `20:30:20.935`)
  - **WITHDRAWN (was an over-read):** "the tick firing proves the engine got past
    `C_Game::CreateInstance`." The update tick runs on a different path; ShaderCompile is still INSIDE
    `CreateInstance` at capture. The tick firing does NOT prove `CreateInstance` returned.
- **At capture (`20:30:20`), three threads are parked in NGX/FSR2/CreateInstance.** RenderThread
  (`b1cc.3144`=tid 12612, cdb-named "RenderThread"): `NtWaitForAlertByThreadId ←
  RtlSleepConditionVariableSRW ← _Cnd_wait ← NVSDK_NGX_UpdateFeature+0x20139e ← ffxFsr2ResourceIsNull`.
  ShaderCompile (`b1cc.b2b0`): `SleepEx ← C_Game::CreateInstance+0x46514`. Main:
  `SleepConditionVariableSRW ← NVSDK_NGX_UpdateFeature`. (FACT — `ki28_pe_allthreads.txt` resolved
  stacks)
- **No `kcd.log` evidence exists for the P-E run's menu state** — the `kcd.log` on disk has mtime
  `20:40` (the P-F swap-OFF run that reached the menu), which OVERWROTE the P-E (`20:28`) engine log.
  Any "the menu rendered" claim for a swap-ON run is from a DIFFERENT run, not P-E. (FACT — `kcd.log`
  mtime `20:40:42` ≠ P-E `20:28`)
- **The FS_BOOT_TRACE (kept diagnostic) recorded 46,762 render-window ops this run** — the
  render thread `tid=12612` is a heavy kcdx-FS caller (19,060 ops: FReadRaw_byPakIndex 8034,
  FSeek 6216, FOpen 1710, FClose 1668, FTell 953); the table/script thread `tid=46280` is the
  other (27,183 ops). how-distribution: index-pak 7434, index-pak-serve 2094, miss-original 1397,
  original 354. (FACT — P-E dev-log `FS_BOOT_TRACE` lines, tid + slot + how tallies)
- **During the render thread's 14.5s stall (`20:29:08.451` → `20:29:22.933`) the table thread
  `tid=46280` is ACTIVELY churning** — thousands of `PAK_READER read_entry` (Scripts.pak /
  IPL_GameData.pak flownodes + entity Lua) plus 47 `[LEGACY] hook_chain: re-entrant dispatch
  depth=2` events. The system is NOT idle-wedged during the slow window; script/table load + the
  legacy hook chain are running concurrently while the render thread waits. (FACT — P-E dev log
  `20:29:20`–`20:29:21` PAK_READER + hook_chain lines, tid=46280)
- **ZERO `double_close` / `bad_handle` errors logged the entire P-E run.** `Close()` logs
  `double_close` on an already-closed slot and `bad_handle` on a bad-tag/out-of-range id; neither
  appears. So the render thread's 884-`FClose handle=3` vs 883-`FOpen→3` count imbalance is NOT a
  stale/double close hitting the kcdx pool (it would have logged) — the recycled-handle-id
  corruption theory is FALSIFIED. (FACT — `grep -c double_close|bad_handle` P-E dev log = 0;
  `src/fs_takeover/file_handle.cpp` `Close` lines 552–588)

## P-A — live thread-stack capture (RAN 2026-06-20, cdb on the hung process)

Attached `cdb -pn KingdomCome.exe` to the live hung process, dumped all 199 thread stacks,
and re-sampled the main thread twice to confirm a true wedge vs. progress.

**Result: the main thread (#0 "Main") is genuinely wedged (two samples byte-identical), in
a stack that passes THROUGH `kcdx!HookedUpdate`:**

```
ntdll!NtDelayExecution -> KERNELBASE!SleepEx+0x91      <- TOP: sleeping (not a lock wait)
WHGame!...+0x36af90
WHGame!wh::game::C_Game::CreateInstance+0x2e8c63
WHGame!wh::game::C_Game::CreateInstance+0x2e8d7d
WHGame!...+0x16cce2   (ret-addr 0x91b42a15 - in kcdx range)
kcdx!kcdx::hooks::`anonymous namespace'::HookedUpdate+0x945   <- OUR per-frame update hook
WHGame!...+0x16c7a0   (the engine's update dispatcher - calls HookedUpdate)
KingdomCome+0x36db / +0x4ad5 / +0x898a (main)
```

- **FACT — no DEADLOCK.** The other 198 threads are all idle worker pools
  (`NtWaitForSingleObject` x127, `NtWaitForAlertByThreadId` x34, `NtWaitForWorkViaWorkerFactory`,
  etc.). No thread is blocked on a kcdx lock; `g_poolLock` is not held anywhere. (PROBE P-A)
- **FACT — the wedge is a `SleepEx`, on the MAIN thread, inside WHGame's
  `C_Game::CreateInstance` -> FSR2 code path** — reached via our `HookedUpdate` trampoline
  calling the game's original `update`. The thread is sleeping/spinning in the GAME's own
  upscaler/instance-create code, NOT in kcdx code. (PROBE P-A)
- **FACT — kcdx does NOT hook any FSR2 / render / present / swapchain / d3d12 function.** The
  ONLY per-frame kcdx hook is `update` itself (`HookedUpdate`); `find_slots` (the Phase-5
  triplet) is file-ops only, off the frame path. So the FSR2 frames above `HookedUpdate` are
  the GAME's code reached through our pass-through update hook, not a kcdx FSR hook. (FACT —
  `grep` of `src/*.cpp` install sites + `src/fs_takeover/find_slots.*`)
- **CORRECTION to a sample-1 reading:** the FIRST cdb sample (before symbols fully reloaded)
  showed the frame as a bare `WHGame!...` address and I read it as "no kcdx frame on the
  stack." Samples 2/3, with kcdx symbols loaded, resolve it to `kcdx!HookedUpdate` — kcdx IS
  on the wedged stack (as the per-frame update pass-through). The corrected fact supersedes
  the sample-1 reading. (Per results-driven: re-observe, don't carry a stale read.)

## P-B — vanilla (no-kcdx) control launch (RAN 2026-06-20, kcdx.disabled switch)

Dropped a `kcdx.disabled` marker next to `kcdx.exe` (the launcher's pre-everything disable
switch → `LaunchGameVanilla`, zero injection / zero engine / zero logging), launched, then
removed the marker.

**Result: VANILLA boots clean to an interactive main menu — no hang, no crash, and no new
kcdx log produced (the disabled path sets up no logging, so zero new logs is the success
signature).** This is P-B's decisive outcome.

- **FACT — the hang is kcdx-introduced (H1), not engine/environment (H2).** Same game, same
  machine, kcdx the only variable: disabled → interactive menu; enabled → wedge in
  `C_Game::CreateInstance`/FSR2. H2 (vanilla stalls identically) is FALSIFIED — vanilla does
  not stall. (PROBE P-B)

## Reframe 2 (2026-06-20 — /debug §B code read of `HookedUpdate`; corrects P-A's next-probe premise)

The "Open questions" below proposed the next probe as "bypass `hook_chain::DispatchPre` in
`HookedUpdate`." Reading `HookedUpdate`'s body (`src/hooks.cpp` 358–1027) shows **it makes no
`DispatchPre`/`DispatchPost` call** — the per-frame chain dispatch happens INSIDE the game's
`g_orig_update` via the MinHook detours `hook_chain` installed on OTHER engine functions, not
as an explicit call in `HookedUpdate`. The design comment at `hooks.cpp:1094` ("drives the
chain's per-frame DispatchPre/Post") is an unproven runtime-mechanism assertion the code read
disproves (results-driven §"a design clause asserting a runtime mechanism is a probe target").

`HookedUpdate`'s ACTUAL per-frame body (steady state, after the one-shot `done` latch):
1. `lua_registry::ApplyZone(AfterGame)` — idempotent drain, no-op when queue empty (line 692)
2. `task::DrainQueue()` — runs plugin AddTask work on the main thread (line 696)
3. `test::EmitSummaryIfChanged(...)` + several `static bool`-latched cap-NN report blocks (702–1024)
4. `g_orig_update(p1, p2, p3)` — the GAME's original update; the wedged FSR2 frames are below THIS (line 1026)

So the correct next probe bypasses kcdx's whole per-frame body, not a non-existent `DispatchPre`.

## P-C — bypass kcdx's per-frame body (RAN 2026-06-20, live cdb on the hung process)

Built + deployed `// === DIAGNOSTIC (PROBE C)` early-jump in `HookedUpdate` (`src/hooks.cpp`
683) — first tick runs full one-shot init (VM/plugins/InputLoaded unchanged), every tick then
calls ONLY `g_orig_update` and returns, skipping the per-tick ApplyZone drain + DrainQueue +
cap-NN report blocks. Engine `kcdx.dll` redeployed (hash-verified), dev mode on. User launched.

**Result: HANGS IDENTICALLY.** Attached `cdb -pv -p <pid>` to the live hung process; dumped
all ~199 thread stacks.

- **FACT — kcdx's per-frame body is INNOCENT.** With the whole steady-state body bypassed the
  wedge is unchanged → `HookedUpdate`'s per-tick work (ApplyZone / DrainQueue / reports) does
  not cause the hang. (PROBE C)
- **FACT — ZERO kcdx frames on ANY of the ~199 threads.** No thread is executing kcdx code at
  hang time (grep of the full `~*k` dump for `kcdx` → empty). The hang is entirely inside the
  game's NGX/FSR2 init. (PROBE C, `cdb ~*k 8`)
- **FACT — the wedge is an NGX feature-update deadlock, not a kcdx file-read.** Main thread
  (`Main`): `NtWaitForAlertByThreadId` ← `RtlSleepConditionVariableSRW` ←
  `SleepConditionVariableSRW` ← `WHGame!NVSDK_NGX_UpdateFeature+0x368f0` — waiting on an SRW
  condition variable for an NGX feature update to complete. An NGX/FSR2 `JobWorker_NN` thread
  (stack base `fb2ff…`) is spinning in `KERNELBASE!SleepEx` ← `NVSDK_NGX_UpdateFeature+0x1eea94`
  — the producer that should signal the main thread's condvar is itself stuck spinning inside
  NGX, never completing. The other `JobWorker_NN` threads idle in `RtlSleepConditionVariableSRW`
  (no jobs). A producer-never-signals deadlock INSIDE `NVSDK_NGX_UpdateFeature`. (PROBE C, live cdb)
- **FACT — NGX/FSR2 modules ARE loaded** (`_nvngx`, `nvngx_dlss`, `amd_fidelityfx_upscaler_dx12`,
  `nvngx`) — not a missing-DLL load failure. NO thread is blocked on `NtReadFile`/`NtCreateFile`
  at hang time — so the deadlock is NOT a kcdx FS read blocking in the act. (PROBE C, `lm` + IO grep)

## Gate A (architect-review, 2026-06-20) + P-D — remove the live PROBE N confound

Dispatched `architect-review` cold (leading theory withheld) on the H3/H4 root-cause +
fix-direction, with the **no-thunk full-init-ownership invariant** as a binding constraint
(every thunk-back option cut before surfacing). Key results:

- **FALSIFIED the lock-inversion theory by code read:** `g_poolLock` (`src/fs_takeover/file_handle.cpp:40`)
  is a documented LEAF lock — never calls outward under hold, cannot self-deadlock; P-A
  confirms no thread holds it at hang time. A kcdx-internal lock-order inversion is NOT it.
- **Found PROBE N LIVE in HEAD** (`vtable_swap.cpp` `KcdxFOpenMarker`): on EVERY boot-window
  FOpen, on EVERY thread the takeover dispatches on (~45k+28k hits across tids incl. worker
  threads), it ran the engine's ORIGINAL FOpen+FClose (`g_probeN_orig*`) + four 0x400-byte
  whole-object snapshots, THEN the real kcdx open. A `working-artifacts.md` no-residue
  violation AND the strongest M1 suspect — it double-opens every boot file through the engine
  CRT cross-thread, exactly the perturbation P-C points at. **Every prior KI-0028 probe ran
  with this confound in the tree** (its `objdiff` output was never even examined).
- Architect verdict `re-task`: remove PROBE N first (mandatory cleanup + cheapest falsifying
  test), re-launch; if it persists, run the engine-original-thunk tid/lock-ordering probe.

**P-D (done, commit `678fd4f`):** removed PROBE N (marker image-diff block + captured
`g_probeN_orig*` + swap-time capture) and the dead PROBE G/J scaffolding (both compile-time
`false`, zero runtime effect — so the behavioral delta is exactly "PROBE N gone", re-test
stays one-variable). `KcdxFOpenMarker` keeps its production logic (first-fire cap-108 seating
signal → delegate to real `kcdx_FOpen`). Build green; engine redeployed (hash-verified).

**P-E (next — the falsifying re-launch):** boot with PROBE N gone.
- Boots past the menu (interactive) → **M1 confirmed**: PROBE N's per-open engine-CRT
  re-entry across worker threads was the root cause (write the AP17 mechanism paragraph: the
  engine-original FOpen+FClose, run re-entrantly from FSR2 JobWorker threads at an NGX-init
  point the engine never called it from, deadlocked NGX's `UpdateFeature` job).
- Still hangs → M1 ruled out; run architect Option B (instrument the engine-original thunks —
  index-miss + 8 metadata-miss arms — for tid + lock-acquire ordering during boot, stack-capture
  an NGX JobWorker entering a kcdx slot). Outcome map there decides M2 (serialize/confine the
  thunks — a CLEARLY-GATED kcdx resolver lock, never a timing fix) vs a state-perturbation
  upstream of the resolver.

## P-E — falsifying re-launch with PROBE N gone (RAN 2026-06-20, clean build, live cdb)

Boot with PROBE N removed (commit `678fd4f`). **Result: STILL HANGS — audio, no menu.**
Live cdb on the clean-build hung process (PID 45516), all-thread dump:

- **FACT — M1 (PROBE N re-entry) FALSIFIED.** Same wedge with PROBE N gone → the per-open
  engine-CRT re-entry was a real rule violation but NOT the hang cause. (PROBE E)
- **FACT — M2-as-live-contention FALSIFIED.** The ONLY kcdx frame on any of ~199 threads is
  `HookedUpdate` (the expected per-frame update pass-through). **NO CCryPak / FOpen /
  AdjustFileName / engine-original-thunk frame on ANY thread.** No NGX/FSR2 worker is sitting
  inside a kcdx file slot or a resolver thunk at hang time — the architect's "FSR2 JobWorker
  blocked inside a kcdx thunk" hypothesis is disproven. (PROBE E, `~*k` grep)
- **FACT — the takeover COMPLETED cleanly before the wedge.** `seat_index_stored entries=307006`,
  cap-108 seating PASS, every serve `diffs=0`. kcdx's file work is DONE; the wedge is downstream
  of a finished takeover. (PROBE E, dev log `kcdx-dev_2026-06-20_20-28-59.log`)
- **FACT — the wedge runs INSIDE the update loop.** Main-thread stack:
  `KingdomCome` → FSR2 frames → `kcdx!HookedUpdate+0x945` → engine update dispatcher →
  `C_Game::CreateInstance` → `NVSDK_NGX_UpdateFeature` → `SleepConditionVariableSRW`. So FSR2/NGX
  `UpdateFeature` is called EACH FRAME from the original update, and each frame blocks on the NGX
  condvar. The one worker in `UpdateFeature` SleepEx is named `SteamRequestThread(NoCfgFound)`
  (a Steam/NGX-library thread name — NOT verified to be a live this-boot signal; do not over-read
  it). (PROBE E, live cdb)

## Reframe 3 — the mechanism is a state-perturbation UPSTREAM of the resolver, still UN-PINNED

Every concrete theory is now falsified: not wrong file content (`diffs=0`), not the per-frame
body (P-C), not PROBE N (P-E), not a live kcdx-thunk contention (no thunk frame on any thread),
not a kcdx-internal lock inversion (`g_poolLock` is a leaf, unheld). What remains is the
architect's third branch: **kcdx's (now-completed) FS takeover changed some boot STATE that NGX's
async `UpdateFeature` depends on, and NGX never signals its condvar.** kcdx is not on the stack
because its file work already finished; the perturbation persists after it. The MECHANISM is not
yet observed — per AP17 this does NOT close, and per results-driven (theories hopped 2+ times,
same wedge re-confirmed 4×) the next step is a fresh-frame, ground-truth probe of what the NGX
condvar waits on, NOT another theory or another stack dump.

- **P-F (next — fresh-frame designed): observe what NGX `UpdateFeature`'s condvar is waiting to be
  signalled BY, and which takeover side-effect breaks that signal.** Candidate instrumentation
  (the fresh-frame subagent designs the exact probe): trace every file/registry/D3D12-resource
  request NGX/FSR2 makes during init and diff vs. what kcdx served (a MISS kcdx returns where the
  original engine would have HIT — the KI-0027 class one layer over: an alias, a search-path
  order, or a `FindFirst` pattern the FSR2 init uses that kcdx enumeration doesn't satisfy). The
  fix, whatever it is, stays INSIDE kcdx's full-init ownership — no thunk-back (user-confirmed
  hard invariant).

## P-F — swap-suppression bisection (RAN 2026-06-20, kcdx-noswap marker, clean build)

Dropped a `<kcdx-engine>/kcdx-noswap` marker → the seating hook skipped ONLY
`SwapVtableOnObject` + the index build; every other kcdx init (ctor bracket, worker
threads, `g_kcdxReadyEvent`, overlay map) ran identically; the engine kept its own CCryPak
vtable. **Result: REACHED THE MENU (interactive).**

- **FACT — H4 (init timing/threading side-effects) FALSIFIED; H3 (FS-takeover dispatch) CONFIRMED.**
  With kcdx fully initialized (all threads/bracket/ready-event ran — `probe_f_swap_suppressed`
  logged) but the swap suppressed, boot reaches an interactive menu. The hang REQUIRES the
  FS-takeover dispatch to be live. The cause is what the swapped CCryPak serves/returns to the
  NGX/FSR2 init — NOT kcdx's added threads or reordered timing. (PROBE F)
- **FACT — kcdx served ZERO file ops this run** (`FS_BOOT_TRACE count=0`) yet booted fine →
  confirms the engine owned the filesystem and the menu came up without kcdx's dispatch. The
  swap being live is the single differentiator between hang and menu. (PROBE F)

## Reframe 4 — H3 sub-mechanism: the swap perturbs NGX WITHOUT NGX opening a file through kcdx

Tension to resolve: the live-swap runs showed NO NGX/DLSS/FSR-named file op routed through kcdx
(`FS_BOOT_TRACE` NGX-class count = 0), and at hang time no NGX thread is inside a kcdx FS frame —
yet P-F proves the swap is the cause. So the swap perturbs NGX through something OTHER than NGX
directly opening an NGX-named file via kcdx. Candidate sub-mechanisms (the next probe splits them):

- **H3a (opaque-handle straddle):** NGX/FSR2/Streamline/Steam does memory-mapped, OVERLAPPED-async,
  or `DuplicateHandle` I/O on a file the engine opened THROUGH the swapped CCryPak — getting back a
  kcdx OPAQUE handle-id (`src/fs_takeover/file_handle.cpp:45` `Encode=(id<<1)|1`), NOT a real OS
  HANDLE. Any Win32 API that treats that id as a real handle (CreateFileMapping, an async wait,
  DuplicateHandle) operates garbage → an async completion that never signals → the condvar wedge.
- **H3b (a non-NGX-named file NGX init depends on):** an engine file the FSR2/upscaler init reads
  via kcdx under a generic name (a shader/pipeline/D3D12 cache, a config) where kcdx's serve is
  subtly wrong on THIS path (a method/size/handle-semantics difference the `diffs=0` object-compare
  doesn't catch — `diffs=0` compares the CCryPak OBJECT bytes, not the returned handle's I/O semantics).
- **H3c (handle-type/return-value contract):** a slot kcdx owns returns a value with different
  semantics than the engine original (e.g. `FGetCachedFileData` returns a pointer into a kcdx
  `std::vector` valid only until Close — `read_slots.cpp:91`; or a handle the engine passes to an
  API expecting an OS fd) that an NGX-init code path consumes.

- **P-G (next): instrument the swapped slots to log EVERY file op whose RETURNED handle/pointer
  could be consumed as a real OS handle** — tag each open/read by the calling tid, and flag any op
  on a tid that is (or spawns) an NGX/FSR2/Steam worker, plus any `FGetCachedFileData`/mapping/
  duplicate path. Diff what kcdx returns vs the engine-original handle semantics for those ops.
  The decisive question: which file, opened through which kcdx slot, hands NGX a kcdx-opaque value
  it then uses as a real OS handle. The fix stays inside kcdx's full ownership (no thunk-back) —
  e.g. kcdx mints a REAL OS handle for ops whose consumer needs one, still owning the open.

## P-G.0 — read-only narrowing on the P-E live-swap log (no launch)

Before instrumenting, exhausted the captured P-E ground truth:

- **FACT — slot 40 `FGetCachedFileData` (the mmap/cached-data H3c suspect) was NEVER called**
  this boot (`FGetCachedFileData` count = 0). The cached-data/mmap-lifetime mechanism is
  FALSIFIED — NGX does not use it. (read-only, P-E dev log)
- **FACT — the `(NoCfg)`/`(NoCfgFound)` thread-name suffix is a RED HERRING.** `AudioThread(NoCfg)`
  carries the same suffix and audio works — it is an engine thread-naming convention, NOT a live
  this-boot config-miss signal. Do not read `SteamRequestThread(NoCfgFound)` as a config failure.
  (cdb thread-name dump)
- **FACT — the second-heaviest caller of kcdx's FS slots is the `RenderThread`.** Two threads
  dominate the kcdx FS ops: tid 46280 (27k ops, main/boot) and tid 12612 = `0x3144` = **`RenderThread`**
  (19k ops). The FSR2/NGX upscaler init runs on the render path — so NGX's dependency on kcdx is the
  RenderThread requesting files through the swapped CCryPak. Other graphics threads present:
  `ShaderCompile`, `PSOCompilationWorker_0/1`, `D3D Background Thread 0-3`, `Streaming File IO HDD/Optical/InMemory`.
  (P-E dev log tid census + cdb thread names)

So the H3 mechanism is: a file the RenderThread (FSR2/NGX init) opens/reads/stats through a kcdx
slot gets an answer whose SEMANTICS differ from the engine original (not content — `diffs=0` — but
handle type, return contract, or an existence/enumeration answer), and NGX's init wedges on it.

- **The render/shader path through kcdx (P-E live-swap log):** the RenderThread (tid 12612) +
  a shader worker (tid 40364) read shader `.cfxb` / PSO-cache files. Engine paks serve fine via
  kcdx (`%engine%/shaders/cache/d3d12/helper.cfxb` → `how=index-pak result=3`); the `%user%/shaders/
  cache/d3d12/*` loose reads MISS (`errno=2`, first-run cache not built — vanilla misses these too,
  NOT anomalous on their own). Only 5 FWrite ops total, none to shaders/cache → the shader cache is
  NOT being written this boot (consistent with a render init that wedges BEFORE the cache-write phase).
  The single differing op is not yet isolated from the log alone.

- **P-G (next — instrument the graphics-thread kcdx FS ops): log every kcdx slot call from the
  RenderThread / ShaderCompile / PSO / D3D / Streaming tids with vpath + slot + exact return
  (handle id, size, exists-bool, attr), AND capture the engine-original answer for the same op
  inline** (call the original alongside kcdx's and log both — a same-run A/B, since the P-F
  swap-suppressed run served nothing and can't be diffed op-by-op). The op whose kcdx return
  differs from the engine's on the render path is the cause. Fix stays inside kcdx ownership (kcdx
  returns the correct handle-type/contract the render path needs, still owning the open) — no thunk-back.

## P-H — boot-progress telemetry + auto-stackdump (DESIGNED, Gate A cleared 2026-06-20; build pending)

The logging-defect audit (per the user: "any unknown is a log defect at this point — prove
everything with logs, no eyeball") found the gap: at the wedge window kcdx logs its own update-tick
artifacts but ZERO engine-boot-phase markers, so "log goes silent at 20:29:43" is ambiguous and the
latency-vs-deadlock fork currently rests on the user's eyeball ("audio, no menu"). P-H closes that.

**The fork P-H resolves:** is the ~20:29:43 NGX wait PERMANENT (deadlock) or LATENT (takeover makes
NGX/FSR2 init take minutes)? The captured logs cannot tell (one 37s-late snapshot ≠ "never wakes").

**Three additions, all reusing machinery kcdx already has. Build status: ledger below.**

| # | Step | Status | Commit |
|---|------|--------|--------|
| H1 | Per-tick heartbeat in `HookedUpdate` — integer-second transition edge (NOT a timer). Cessation = the wedge signature. | DONE | 90d2ef0 |
| ~~H2~~ | ~~menu-pump marker on id 4~~ — **DROPPED** (user, 2026-06-20): requires a new engine-direct hook for a signal the architect rated a weak floor; H1's heartbeat already resolves the fork. | DROPPED | — |
| H3 | Watcher-thread auto-stackdump on heartbeat stall (N=10s), +30s second dump. Dedicated watcher, suspend→capture-raw→resume→log. | DONE | 90d2ef0 |

### P-H RESULT (RAN 2026-06-20 22:34–22:38, live cdb on the still-running process)

**The heartbeat NEVER stalled — the Main update tick is healthy. The wedge is NOT a deadlock and NOT a wedged Main thread. It is a non-progressing `SleepEx` POLL-LOOP inside `C_Game::CreateInstance` → FSR2 code.** (RAN — log + live dump)

PROVEN facts:
- **197 `BOOT_WATCH heartbeat` lines, tick 1→47539, advancing continuously for 3m16s** (`22:35:03`→`22:38:19`, the last log line = file mtime). **No gap >2s** the entire run; zero `BOOT_WATCH_STALL`, zero `BOOT_DUMP` — the watcher never fired because the tick never stalled. (PROVEN — `_research/probe-archive/ki0028-ph-boot_watch-heartbeat.txt`)
- **The Main thread (`90c4.adc0`, "Main") is in a SLEEP-RETRY LOOP, not an event wait.** Stack: `NtDelayExecution ← RtlDelayExecution ← KERNELBASE!SleepEx ← WHGame!ffxFsr2ResourceIsNull+0x36af90 ← C_Game::CreateInstance+0x2e8c63 ← +0x2e8d7d ← ffxFsr2ResourceIsNull+0x16cce2 ← …`. The top is `NtDelayExecution`/`SleepEx` (a TIMED sleep), NOT `SleepConditionVariableSRW` (the P-E capture's event wait). **Two samples 2s apart are BYTE-IDENTICAL** (same RVAs) → a non-progressing loop, not forward progress. (PROVEN — `ki0028-ph-main-renderthread-deep-22-38.txt` + the two A/B samples)
- **`CreateInstance` NEVER returns** → game instance construction never completes → menu never loads; Main owns the window message pump but is buried in the FSR2 sleep-loop → it never pumps messages → **Alt+F4 is ignored** (user-observed, mechanism-explained). (PROVEN — Main stack + user report)
- The heartbeat runs because the engine pumps `CGame::Update` (the hook source) on a DIFFERENT thread while Main is still in `CreateInstance`. So "heartbeat alive" ≠ "Main alive at the menu" — it proved Main is not HUNG (the tick advances), which correctly distinguished this sleep-LOOP from a lost-wakeup deadlock. (PROVEN — Main in CreateInstance ≠ CGame::Update)

**Verdict on the latency-vs-deadlock fork: NEITHER.** It is a busy SLEEP-poll loop on a condition FSR2 init checks that, under the FS takeover, never becomes true. FSR2 (`ffxFsr2ResourceIsNull` neighborhood) reads resources/files; the takeover serves what it reads. The loop polls for some resource/state to become ready that never does. This is a kcdx-served-content / resource-readiness problem on the FSR2 init path — back to an H3-class root cause (the swapped object serves FSR2 something it then waits on), now with the wait mechanism PINNED (a SleepEx retry-poll, not an SRW condvar).

**The earlier P-E "SleepConditionVariableSRW in NGX UpdateFeature" capture was a DIFFERENT wait** than this `SleepEx`/FSR2 loop — either a different point in the same stuck init, or the P-E capture caught a transient. The decisive, reproducible state is THIS one: the byte-identical SleepEx/FSR2 loop in CreateInstance.

### Static recon (`/research-disassembly`, 2026-06-20) — the loop is a WINDOW-FOCUS poll, NOT FSR2/filesystem

Disassembled the `SleepEx` frame (`ffxFsr2ResourceIsNull+0x36af90`, RVA 0x865fb4) — body-read, verified:
- **It is a window-activation poll, not an FSR2 resource wait.** `ffxFsr2ResourceIsNull`
  is just the nearest export symbol. The loop calls **`USER32!GetActiveWindow`**, compares
  the active window to an expected handle (`rsi`), and **`KERNEL32!Sleep(5)`s up to 5×**
  then returns — BOUNDED ~25ms, not the infinite hang itself. (FACT — resolved IAT slots:
  Sleep @ RVA 0x3a02738, GetActiveWindow @ RVA 0x3a03260; `_research/ki0028-fsr2-poll-loop-recon/`)
- The polled globals g1/g2 both resolve to one window/system-manager singleton at **RVA
  0x492b890** (a gEnv-family global, adjacent to gEnv id-11 base 0x492b800; NULL in the
  static image, runtime-populated). (FACT — rip-relative resolve)
- Single call site `0x667ddd` inside a larger frame/tick-step fn; no local back-edge. The
  infinite repetition is `CreateInstance`'s OUTER loop re-running this step — that outer
  body is NOT yet read (AP19: not asserting its exact exit condition). (FACT — caller scan)

**LEAD (NOT yet a grounded conclusion — corrected 2026-06-21):** the inner `SleepEx` helper
(RVA 0x865fb4) is a window/activation poll, but it is **BOUNDED to 5 iterations** (~25ms) and
returns regardless — it is NOT itself the infinite hang. P-H's byte-identical 2s-apart samples
caught Main inside THIS bounded helper's `Sleep`, but the never-ending repetition is the OUTER
`CreateInstance` loop (above call site 0x667ddd), whose exit condition is **UNREAD**. So
"KI-0028 is a window-focus handshake that never completes" is an **extrapolation from the inner
helper, not yet verified** — the earlier "DIRECTION CHANGE" framing over-claimed it.

USER EVIDENCE that the window-focus story is at least incomplete: launches WITH focus acquired
(mouse cursor changed on click → the window WAS active at the OS level) ALSO hung. If the gate
were purely "window never becomes active," a focused launch would proceed. It didn't. So the
gate is NOT simply GetActiveWindow-matching; re-observe, do not theory-hop.

**The unread load-bearing fact (next, static — no launch):** read the OUTER `CreateInstance`
loop body around 0x667ddd for its TRUE exit condition (AP19 — read the caller's body, do not
infer). Batch with: what `[vtbl+0x2d0]/[+0x740]` returns (the engine's "expected window" rsi),
the four inner early-exits (`[rcx+0x5c1]`, `[rcx+0x5b1]`, the two `[rip]` globals), and the
identity of the singleton at 0x492b890. Only once the outer gate is known is a launch probe
(launcher-method vs in-process-takeover) the right spend.

### Step-by-step static verification (2026-06-21) — a VERIFIED CONTRADICTION, probe owed

Verified each part against the binary; results graded:
- **VERIFIED** — Main's RIP `0x866090` = `inc edi` immediately after `call Sleep` (the helper's
  Sleep-return); RVA mapping `ffxFsr2ResourceIsNull(0x4fb100)+0x36af90` confirmed.
- **VERIFIED** — the helper (RVA 0x865fb4) is BOUNDED: `mov ecx,5; call KERNEL32!Sleep; inc edi;
  cmp edi,5; jl 0x866023`. `Sleep(5)`, 5 iterations max, `edi` never reset → ~25ms then returns.
  (Re-read twice; imports resolved: Sleep @ IAT 0x3a02738, GetActiveWindow @ 0x3a03260.)
- **FALSIFIED** — `0x667ddd` was NOT the caller (a stray E8-scan match in an unrelated fn). The
  REAL caller return addr is `CreateInstance(0xda65e4)+0x2e8d7d = 0x108f361`; the frames read
  around it are bounded count-loops + an epilogue, no infinite loop.
- **THE CONTRADICTION (the real signal):** the three captures (22:38 all-threads + sampleA +
  sampleB, ~2s apart) show Main at the IDENTICAL RIP `0x866090` AND identical RSP chain
  (`8010d0e8/d120/d1b0/d1e8` byte-for-byte). A bounded ~25ms helper CANNOT hold Main 2s, and an
  outer-loop re-call would SHIFT the RSP. Identical RIP+RSP across 2s with a provably-bounded
  body is impossible under normal execution.
- **The likely confound (checkable only by a fresh probe):** the cdb captures were `-pv`
  NONINVASIVE ("Process is not attached as a debuggee; debug events will not be received") on a
  process that may already have been frozen / mid-teardown (post-AltF4). A dead/suspended process
  yields identical snapshots trivially — so "identical 2s apart" may be an ARTIFACT of a
  non-running process, NOT evidence of a hang-in-place. This cannot be resolved from the stale
  captures.

**PROBE OWED (user-chosen: probe the helper directly).** Instrument helper `0x865fb4` ENTRY on a
FRESH live run: log a fire counter + the caller return address (read `[rsp]` at entry) under a
stable tag, bounded to the first ~200 fires. Outcome map: fires ONCE and Main genuinely sits in
it → the "bounded" read is somehow wrong OR Sleep isn't returning (re-examine); fires THOUSANDS
of times → an outer loop exists and the logged caller RVA names it exactly (no more guessing);
never fires but boot still hangs → the helper is innocent and the old captures were a
dead-process artifact (the wedge is elsewhere). Built on the existing boot_watch wiring.

### PROBE I RESULT (RAN 2026-06-21 09:25–09:28) — FS FULLY EXONERATED; the wedge is a RenderThread SRW-condvar wait (subsystem UNidentified — NGX/FSR2 labels are nearest-export NOISE)

PROBE I extended the FS_BOOT_TRACE window 600 frames past the first tick (the render/UI-init
phase the original gate left dark). Outcome: **PROBE I's Outcome 2 — the filesystem is exonerated
and the wedge is downstream, in the render path.** (RAN — `_research/probe-archive/ki0028-probei-*`)

PROVEN:
- **The extended window covered the FULL menu/render init** — the trace shows the menu's own
  assets served correctly: every `shaders/*.ext`, `textures/.../*.dds`, and the MAIN-MENU BACKGROUND
  VIDEO `videos/menu/main_menu_kutnohorsko3/main_menu_kutnohorsko3.bk2` (Bink) opened and streaming
  (a `FReadRaw handle=3` re-read ~3/sec = the looping menu video). The engine REACHED the menu-load
  stage and got all its assets. (PROVEN — PROBE I FS trace, `ki0028-probei-fstrace-fullwindow.txt`)
- **ZERO anomalous FS results in the render-init window** — no `size=0`/`size=-1`/error/false-exist
  on any render/shader/UI asset. kcdx served every render asset correctly. (PROVEN — grep of the
  extended trace = empty)
- **NGX's own files NEVER route through kcdx** — `nvngx|dlss|fsr|ngx` served-path count = 0. The
  FS is doubly exonerated for the NGX wedge. (PROVEN — grep = 0)
- **The RenderThread (`17d8.42a8`, cdb-named "RenderThread") is blocked on an SRW CONDITION
  VARIABLE:** `NtWaitForAlertByThreadId ← RtlSleepConditionVariableSRW ← SleepConditionVariableSRW
  ← [WHGame, unidentified function] ← [WHGame] ← [WHGame] ← BaseThreadInitThunk`. The
  `SleepConditionVariableSRW` wait is REAL (a kernel call, not a label). (PROVEN —
  `ki0028-probei-allthreads-0928.txt`)
- **⚠ THE `NVSDK_NGX_UpdateFeature` / `ffxFsr2ResourceIsNull` FRAME NAMES ARE NEAREST-EXPORT NOISE —
  NOT verified as NGX/FSR2.** WHGame has no PDB; cdb labels every address by the nearest export
  below it. The frame offsets are **2–9 MB PAST the export** (`ffxFsr2ResourceIsNull` export = RVA
  `0x4fb100`; frames at `+0x4b1cfb`/`+0x567a86`/`+0x866ca3` = real RVAs `0x9acdfb`/`0xa62b86`/
  `0xd61da3`; `NVSDK_NGX_UpdateFeature` export = `0x1be7ef0`; frame `+0x20139e` = RVA `0x1de928e`,
  2 MB past). A real function is a few KB, not megabytes — these are unrelated WHGame functions
  across a multi-MB span. **This is the EXACT KI-0026 trap:** there the identical
  `NVSDK_NGX_UpdateFeature+…`/`ffxFsr2ResourceIsNull+…` labels were read as an NGX/FSR2 abort and
  were WRONG — the real function was `CSystem::FatalError` (a CryEngine config-load failure), the
  labels nearest-export noise. The real functions at these RVAs are NOT yet identified. (PROVEN —
  export RVAs computed; offsets 2–9 MB; KI-0026 closed/ precedent)
- **The heartbeat (Main) is STILL advancing** — tick=13377 at `09:28:20`, ~240fps. Main is not
  hung; audio plays (audio thread independent). (PROVEN — dev-log heartbeat)

**CONVERGENCE — the investigation has eliminated the FILESYSTEM; the wedge is in the render path
(subsystem NOT yet identified):** the engine fully loads the menu (all FS correct, menu video
streaming) and begins rendering, but the **RenderThread is blocked forever on an SRW condition
variable that is never signaled → no frame ever presents → no visuals** (audio independent). The FS
is doubly exonerated (PROBE I: clean render-init stream; NGX/dlss/fsr files never route through
kcdx). **What the RenderThread is waiting IN is NOT yet known** — the `NGX/FSR2` frame names are
nearest-export noise (above), the same mislabel that misdirected KI-0026. It could be the upscaler,
the swapchain/present, a render-resource fence, or any render-path condvar. The kcdx connection is
NOT "serves wrong content" (FS is correct) — it is "what STATE does the takeover change that makes a
render-path thread wait forever on a condvar nothing signals." OWED next: identify the REAL
functions at RVAs `0x1de928e` / `0x9acdfb` / `0xa62b86` (disasm the containing functions + their
string refs, KI-0026's method) BEFORE naming any subsystem. KI stays OPEN; no root-cause mechanism,
no subsystem named yet (AP17 — do not assign blame to FSR2/NGX on a nearest-export label).

### PROBE J — static identification EXHAUSTED → invasive live cdb (2026-06-21)

Followed the owed step: ran KI-0026's identify-by-string-refs on the three real RVAs
(`_research/ki0028-fsr2-poll-loop-recon/disasm_identify_renderwait.py`).

PROVEN (static, no launch):
- **All three RenderThread wait-frames are anonymous sync/dispatch leaf helpers with
  ZERO string literals** — `0x1de928e` (the `_Cnd_wait` caller, 0x3e bytes), `0x9acdfb`
  (its caller, 0x2b bytes), `0xa62b86` (0xea bytes). KI-0026's method needs a string
  to name the function (`CSystem::FatalError` had a config-path string); these condvar
  primitives carry none. **The string-ref method cannot name them.** (PROVEN — disasm)
- **`0xa62b86` is the generic thread-pool trampoline, NOT render-specific** — it is the
  IDENTICAL bottom frame across RenderThread (`17d8.42a8`), ShaderCompile (`17d8.93d0`),
  AND AsyncCommandQueue (`17d8.9620`): `…+0x567a86 ← ucrtbase!thread_start ←
  BaseThreadInitThunk`. So the earlier "three threads parked in NGX/FSR2" reading
  OVERCOUNTED — the shared NGX-labeled bottom frame is the worker-pool entry, mislabeled
  by nearest-export. Only the distinguishing upper frames matter. (PROVEN — PROBE I dump,
  threads 25/26/27)
- **∴ static is exhausted.** What the condvar waits to be signaled BY is a RUNTIME
  relationship (the live `CONDITION_VARIABLE`/`SRWLOCK` address + who else references it)
  that no static read settles (results-driven §4). (PROVEN — negative result recorded)

| # | Probe | Status | Outcome |
|---|-------|--------|---------|
| J.1 | Static identify the 3 wait-frame RVAs (KI-0026 method) | DONE | anonymous sync helpers + thread-pool trampoline, 0 strings — un-nameable static |
| J.2 | INVASIVE cdb on the live hung process: read the RenderThread's condvar/SRWLOCK address, find who else holds/waits it | pending | — |

**P-J.2 (live, user-chosen option 1):** the game is live + wedged (PID 6104, 2.2 GB).
Attach cdb INVASIVELY (`-p`, not `-pv`) — the prior captures were `-pv` noninvasive,
which Reframe 6 proved mislead on a running game. Break in, locate the RenderThread,
read the actual condvar + lock address its `RtlSleepConditionVariableSRW` is parked on
(walk the frame locals / the `SleepConditionVariableSRW` args), then search all threads
+ memory for who else references that same address (the signaller). Outcome map:
- A distinct subsystem's thread holds/owns the lock → names the real owning subsystem
  (NOT from export labels) → next: how kcdx's takeover perturbs that subsystem's state.
- No other thread references it / it is an orphaned wait → a lost-signal: the producer
  that should signal never ran or signaled a different object → trace the producer.
- cdb cannot hold an invasive break (x64dbg-class instability) → fall back to the
  in-engine condvar-wait probe (option 2).

### PROBE J.2 RESULT (RAN 2026-06-21 09:54–09:57, INVASIVE cdb + live heartbeat) — IT IS LATENCY, NOT A DEADLOCK; the heartbeat STALLED 17s THEN RECOVERED

Attached cdb **invasively** (`-p`, not `-pv` — the prior captures' flaw) to a fresh hung
session (PID 14512), captured all-thread verbose stacks, re-sampled, detached with `qd`
(game left running). Then read the live `BOOT_WATCH` heartbeat. **The decisive P-H
falsifier fired: the heartbeat RESUMED after the stall → LATENCY.** (RAN —
`_research/probe-archive/ki0028-pj2-allstacks.txt`, `-resample.txt`, dev log
`kcdx-dev_2026-06-21_09-54-27.log`)

PROVEN:
- **The heartbeat STALLED 17,031 ms then RECOVERED and is still advancing.** The P-H
  watcher fired `BOOT_WATCH_STALL stalled_ms=17031 last_tick=3424` at `09:55:57` (its
  auto-dump ran). But the heartbeat then RESUMED: tick 3726 @ `09:56:06` → tick 5270 @
  `09:56:51`, ~35 ticks/s, mtime current. **Per the pre-committed P-H outcome map
  (Reframe 6, F6: "Heartbeat RESUMING is the primary, decisive falsifier"): heartbeat
  resumes after the stall ⇒ LATENCY, not deadlock.** (PROVEN — dev-log heartbeat stream
  + the single `BOOT_WATCH_STALL`)
- **The heartbeat tid is `21632` — NOT Main (`5480`), NOT RenderThread (`a77c`).** Reframe 6
  assumed heartbeat-tid = Main; that was WRONG. `CGame::Update` (the hook source) fires on
  its own thread (21632). So "Main parked in CreateInstance" and "the update loop is alive"
  are BOTH true simultaneously — they are different threads. (PROVEN — heartbeat `tid=21632`
  vs cdb Main `38b0.5480`)
- **The RenderThread (`a77c`) is doing forward WORK, not wedged — it changed stacks between
  captures.** Pass-1: `BinkNextFrame ← BinkOpenXAudio27 ← XAUDIO2!CX2Engine::CreateSourceVoice
  ← …CLeapSystem::AddToSkinList` (decoding the menu background video + creating its XAudio2
  voice). Resample: `C_Game::CreateInstance+0x46514 ← SleepEx`. A thread that changes stacks
  between samples is progressing, not parked. The PROBE I "RenderThread blocked on SRW condvar"
  was a one-instant noninvasive sample, not a wedge. (PROVEN — pass-1 vs resample stacks differ)
- **Main (`5480`) sits in `C_Game::CreateInstance+0x2e8c63 → SleepEx` (RVA 0x36af90, the
  bounded `Sleep(5)` window-pacing helper).** Byte-identical across both invasive resamples —
  but this is the SAME per-frame pacing helper the static recon already read as bounded (5×
  `Sleep(5)`), and the update loop (tid 21632) advances regardless. So Main looping in this
  pacing sleep is the normal frame cadence, not the wedge. (PROVEN — resample A/B + static recon)

**~~VERDICT — KI-0028 is BOOT LATENCY~~ — PARTIALLY WITHDRAWN (user perceptual signal +
P-J.3, below):** the heartbeat-resume DID falsify a lost-wakeup deadlock on the tick thread,
but the user confirmed LIVE (47 min in) that **the screen is STILL black, audio still playing,
still not interactive** — so it is NOT "slow boot that recovers to a menu." The tick recovering
≠ the game presenting. The true shape is below.

### PROBE J.3 RESULT (RAN 2026-06-21 ~09:58, INVASIVE cdb, game 47 min into the black-screen state) — NOTHING IS WEDGED; every thread runs; the failure is NO-PRESENT + NO-INPUT on a LIVE loop

With the user confirming the screen is still black/silent-of-menu/non-interactive 47 min in,
re-attached invasively and sampled Main (`5480`) twice + read the live window globals.

PROVEN — **every thread is making forward progress; none is parked:**
- **Main (`5480`) changed stacks across captures.** P-J.2 resample: `CreateInstance+0x2e8c63 →
  SleepEx`. P-J.3: `ffxFsr2ResourceIsNull+0x3b020` (RVA 0x536120) running `movss xmm0,[rcx+0x1460]`
  — a different RVA chain (`0x536120←0x536018←0x534135←0x53322e←0x53212e←0x36eb39`), pure compute,
  no sleep. A thread at a different RIP each sample is RUNNING, not wedged. (PROVEN — P-J.2 vs P-J.3
  Main stacks differ)
- **The tick thread (`21632`) advances ~35/s; RenderThread (`a77c`) changes stacks** (Bink decode →
  CreateInstance). **All three key threads run.** (PROVEN — heartbeat stream + P-J.2/J.3 stacks)
- **Live window-mgr singleton @ `WHGame+0x492b890` = `0x7ff9cc35fe60`** (non-NULL — a populated
  vtable'd object; the static image had NULL). So the window-manager object EXISTS. (PROVEN — `dq`)

**CORRECTED VERDICT — KI-0028 is a NO-PRESENT + NO-INPUT failure on a RUNNING game, NOT a hang,
NOT a deadlock, NOT recoverable latency.** The whole update/render/compute loop executes
continuously (heartbeat 5k+ ticks, Main + RenderThread both progressing through different code each
sample), yet: (a) no rendered frame is ever PRESENTED to the display (black screen), and (b) the
window never processes input (dead to clicks / Alt+F4). Audio is independent (its own thread) so it
plays. P-F proved the FS-takeover SWAP is the single differentiator (swap-off → interactive menu).
So the swap perturbs the **present/swapchain + window-message path**, NOT FSR2 and NOT file content
(both already exonerated). The `NGX/FSR2` labels throughout were nearest-export noise (KI-0026 trap),
confirmed.

**The pinned question (next probe):** what does the swapped CCryPak change about **window /
swapchain creation / frame presentation / the window message pump** such that the loop runs but
never presents a frame or pumps input? Candidates: the present/flip call path reads a
state/resource the takeover altered; the swapchain or window was created against a surface the
takeover's index perturbed; the window-message pump (Main) never reaches its `PeekMessage`/`Present`
because it is buried in a per-frame `CreateInstance` sub-loop that the swap makes non-terminating
(distinct from a deadlock — Main RUNS, it just never escapes the loop to pump). The fix stays inside
kcdx full-init ownership (no thunk-back).

### PROBE J.4 RESULT (RAN 2026-06-21, Win32 window query, ZERO process perturbation) — the window is VISIBLE + RESPONDING; only PRESENT-to-it fails

Queried the live process's top-level windows via `EnumWindows`/`IsWindowVisible`/`GetWindowRect`
+ `Process.Responding` (no debugger, no perturbation).

PROVEN:
- **The game window EXISTS, is VISIBLE, correctly sized + styled.** `hwnd=0x60c64`, `vis=True`,
  rect `(0,0)-(2560,1440)` 2560×1440, `style=0x94000000` (WS_VISIBLE|WS_CLIPSIBLINGS|
  WS_CLIPCHILDREN — normal fullscreen game window), title "Kingdom Come: Deliverance II". The
  black screen is NOT a missing/hidden/zero-size window. (PROVEN — `[W]::Find(14512)`)
- **`Process.Responding = True` — the window IS pumping OS messages.** Windows' hung-window
  detection (a `SendMessageTimeout` ping) reports the window answers. So Main's message pump is
  NOT dead at the OS level — the "Main never pumps" sub-theory is FALSIFIED. (PROVEN —
  `Get-Process(14512).Responding`)

**This SHARPENS the reframe to a single mechanism:** the window is live, visible, and pumping OS
messages — yet no rendered frame appears in it. ∴ the failure is specifically in **frame
PRESENTATION (the DXGI/D3D12 swapchain Present/flip path), not the window and not the message
pump.** Frames are computed (Main+RenderThread run) but never presented to the visible swapchain —
OR present is called and produces black. The user's "not interactive" is downstream of the black
screen (nothing rendered = nothing to interact with), not a separate input-pump failure (the pump
works). 

**Decisively narrowed next probe:** instrument the engine's **swapchain Present / flip** call —
is it called each frame? Never called → the per-frame loop never reaches present (stuck short of
it, e.g. an unterminating sub-loop before the present). Called but black → present runs against a
swapchain the takeover-perturbed render init left wrong. This is the in-engine probe (the earlier
option 2) with a now-PRECISE target: the present call, not "the render path". Fix stays in kcdx
full-init ownership (no thunk-back).

### PROBE J.5 — Gate A architect-review REDIRECTED the present-hook; P-J.3 frame is ENTITY-INIT, not render (2026-06-21)

Dispatched `architect-review` (Gate A, leading theory withheld) on the present-probe design. It
flagged the "present failure" framing as **one inference too far** — built on a deduction
(window-pumps + Main-runs-compute ∴ present-fails), not an observation — and named the owed §4
static read I had skipped: identify the function Main was ACTUALLY in at P-J.3 (`0x536120`).

PROVEN (static, `disasm_pj3_compute_frame.py`):
- **Main's P-J.3 compute frame is ENTITY / AI / game-object INIT, not render/present.** The
  `0x53xxxx` cluster Main ran is math leaves (no strings); the function CALLING them (`0x36eb39`)
  carries `"dummy_no_ai"`, `"player"`, `"<INVALID>"`, and **8 entity-class GUIDs** — CryEngine
  entity-archetype / component identifiers. This is `C_Game::CreateInstance` doing
  instance/entity construction (consistent with `CreateInstance` on Main every sample). NOT
  present, NOT swapchain — another nearest-export label avoided. (PROVEN — string-ref disasm)

**REDIRECT (architect flag B confirmed):** the wedge is more likely UPSTREAM of present — **the
game stuck in `CreateInstance`/entity-init, never reaching the steady-state render/present loop**
(architect outcome-map row 1: "loop never reaches present"). The menu-video RenderThread + the
tick run INDEPENDENTLY of a stuck instance-init. The swapped CCryPak likely perturbs something the
entity/instance init consumes (an entity-def, a flownode/Lua entity script, a game-data pak the
entity system reads) so `CreateInstance` loops without completing.

**Architect-prescribed probe ORDER (gated, not the present hook first):**
1. (DONE) §4 static read of the P-J.3 compute frame → entity-init, above.
2. **DXGI present-COUNT delta read** (NOT a present hook) — read `IDXGISwapChain::GetLastPresentCount`
   / `GetFrameStatistics` swap-on vs the swap-off P-F baseline, from the zero-perturbation watcher.
   Outcome map: delta≈0 → FALSIFIES present-failure, wedge is upstream (entity-init) → identify
   `0x36eb39`'s caller + what entity-init blocks on; delta>0 but PresentRefreshCount≈0 → present
   IS the problem, THEN the system-DLL present hook is justified; both advance ≈ baseline → frames
   present but to a non-composited surface. Counter read is theory-INDEPENDENT (can kill the
   present framing); the present hook can only confirm it → present hook is probe #2, gated on this.
3. Present hook only if step 2's outcome points there.

The fix stays inside kcdx full-init ownership (no thunk-back) on every branch.

### PROBE K — present-count delta (BUILT + DEPLOYED 2026-06-21, awaiting launch)

Built the architect-approved step-2 probe (`src/fs_takeover/present_probe.{h,cpp}`, armed
from `seating_hook.cpp` beside the boot watcher; `dxgi` added to the kcdx link line). Build
green, `kcdx.dll` deployed + hash-verified, dev mode on.

**What it does (NO present hook — reads a counter):** one-shot MinHook on
`dxgi!IDXGIFactory::CreateSwapChain` (slot 10) + `IDXGIFactory2::CreateSwapChainForHwnd`
(slot 15) on the shared class vtable captures the engine's `IDXGISwapChain*` the moment it
is created; a watcher thread then reads `GetLastPresentCount` (slot 13) + `GetFrameStatistics`
(slot 18) off it every 1s and logs the per-interval delta under tag `PRESENT_PROBE`. It NEVER
patches Present (slot 8) — reads counters, hands nothing back (no-thunk).

| # | Probe | Status | Outcome |
|---|-------|--------|---------|
| K.1 | one-shot swapchain capture (factory vtable, slots 10/15) | BUILT | — |
| K.2 | present-count + refresh-count delta read (1s, ×120) | DONE | d_present=0 / d_refresh=0 across all 65 reads; counts FROZEN at present=3840/refresh=2160; GetLastPresentCount returns ERROR_BUSY (0x800700AA) |

### PROBE K RESULT (RAN 2026-06-21 10:21–10:23) — PRESENT IS FROZEN (count stuck, swapchain BUSY); "present failure" FALSIFIED, wedge is UPSTREAM

PROVEN (`kcdx-dev_2026-06-21_10-21-52.log`, tag `PRESENT_PROBE`):
- **The swapchain WAS captured** — `swapchain_captured via=CreateSwapChain swapchain=0x1F5C4C1B060`
  at 10:21:57 (the engine uses the older `IDXGIFactory::CreateSwapChain` slot-10 path, not
  CreateSwapChainForHwnd). The probe is reading the engine's real swapchain. (PROVEN)
- **Present is FROZEN, not absent.** `present_count=3840`, `refresh_count=2160` — both NON-ZERO
  but STUCK at the identical value across ALL 65 one-second reads (10:21:59→10:23:03, 64 s).
  So the engine presented ~3840 frames during boot/early init, then present STOPPED and never
  advances again. `d_present=0` / `d_refresh=0` every interval. (PROVEN — 65 identical reads)
- **The swapchain reports BUSY.** `GetLastPresentCount` returns `hr_present=0x800700AA` =
  `HRESULT_FROM_WIN32(ERROR_BUSY)` ("resource in use") every read — that is why `last_present=0`
  (the call failed, out-param untouched). `GetFrameStatistics` succeeds (S_OK) and returns the
  frozen 3840/2160. The swapchain is in a busy/blocked state, present no longer pumping. (PROVEN)

**VERDICT — outcome-map ROW 1: "present is the problem" is FALSIFIED.** Present is not failing;
it is simply NOT BEING CALLED anymore (count frozen) — the per-frame loop stopped reaching the
present call. Combined with P-J.5 (Main running entity/instance init) this confirms **the wedge is
UPSTREAM of present**: the engine got through enough boot to present 3840 frames (the early
loading/menu-bring-up frames — consistent with PROBE I's menu video decoding), then **the
instance/entity-init path (`C_Game::CreateInstance`) stopped completing frames** and present went
idle + the swapchain went BUSY. The ~3840 presented frames are the boot frames BEFORE the freeze,
not live menu frames.

**The ERROR_BUSY is a secondary lead:** a swapchain goes BUSY when a Present is in-flight/blocked
or the device/queue is occupied — consistent with the render path waiting on a resource the stuck
init holds. But present is the SYMPTOM (idle because the loop upstream stopped), not the cause.

**OWED next (no present hook — that branch is closed):** identify what the entity/instance-init
loop blocks on. Re-mine the captured FS_BOOT_TRACE for the LAST entity/flownode/game-data reads
before present froze (≈ when present_count hit 3840), and/or instrument `C_Game::CreateInstance`'s
entity-init step. The swapped CCryPak perturbs something that init consumes (KI-0026/KI-0027 class,
now on the entity path). Fix stays in kcdx full-init ownership (no thunk-back).

### PROBE K — cross-reference with heartbeat + FS in the SAME run (10:23, game still live)

Read the rest of the same log while present sat frozen. Two facts REFINE (and partly temper) the
"stuck in entity-init" reading:
- **The tick heartbeat KEEPS advancing while present is frozen** — tick 3050 @ 10:23:50, ~35/s,
  ZERO stalls. The update loop runs; only PRESENT is frozen. Loop and present are decoupled.
  (PROVEN — `BOOT_WATCH heartbeat` advancing + present_count stuck at 3840 concurrently)
- **The MENU FULLY LOADED — its assets are all served and the video LOOPS.** The freeze-window FS
  is 121 `FReadRaw_byPakIndex` on handle=3 = the menu background video
  (`main_menu_kutnohorsko3.bk2`) re-reading ~30 ms (a live looping decode), plus the menu's
  shaders/particle-textures/cursor all served `result=13`/OK earlier. So the engine reached
  "menu loaded, video playing", NOT "stuck before the menu". (PROVEN — FS vpaths + handle=3 loop)
- A repeated `AdjustFileName "kcd.log" how=miss-original result=0` recurs — the engine's own log
  file open missing; likely benign (a write-path retry), NOT yet shown on the wedge path. (NOTED,
  not a conclusion)

**TENSION to resolve (two readings, do NOT pick by guesswork — `results-driven.md`):**
- (A) the loop never reaches present (stuck in `CreateInstance`/entity-init, P-J.5) — but the menu
  assets fully loaded + video loops, which sits awkwardly with "never reached the menu".
- (B) present WAS running (the 3840 frames = the menu rendering), then present FROZE in place while
  the tick + video-decode kept running. The non-zero-but-stuck count (3840, not ~0) + `ERROR_BUSY`
  fits (B): if present had never started, the count would be ~0. **3840 presented frames ⇒ present
  was live, then stopped.**

Reading (B) now looks MORE consistent with the counter (frozen-nonzero, not zero) than (A). The
probe's falsifiable result stands regardless: **present is not advancing now** (d_present=0).

### PROBE K.3 — invasive cdb settles (A) vs (B): NO thread is in Present; both Main + RenderThread are in CreateInstance → reading (A) CONFIRMED

Re-attached invasively (`-p` + `qd`) to the still-live black-screen game (PID 18616), dumped all
threads, searched for any thread inside DXGI/D3D12/Present.

PROVEN:
- **NO thread is inside Present / DXGI / the swapchain.** The nvwgf2umx (NVIDIA driver) threads are
  the driver's idle worker pool (`NVDEV_Thunk` waiters), not the engine presenting. No `dxgi!`,
  `d3d12!Present`, `D3DKMTPresent`, or swapchain frame on ANY engine thread. (PROVEN — grep of the
  all-thread dump)
- **Main is in `C_Game::CreateInstance`** — `HookedUpdate ← CreateInstance+0x2e8c63 ← the SleepEx
  window-pacing helper (0x36af90) ← 0x36eb39` (the entity-init fn with the "dummy_no_ai"/"player"/
  GUID strings). Main runs the per-frame tick THROUGH a CreateInstance call; it is NOT in present.
  (PROVEN — Main stack, PID 18616)
- **RenderThread is in `C_Game::CreateInstance+0x5a9ce5` running a big `memcpy`**
  (`VCRUNTIME140!memcpy_avx512`) — copying resource/instance data deep in a CreateInstance
  sub-call. NOT in present, NOT in DXGI. It is doing CreateInstance WORK. (PROVEN — RenderThread
  stack)

**∴ reading (A) CONFIRMED, (B) FALSIFIED.** Present is not blocked in-flight (no thread is in a
Present call) — present is simply **not being called** because BOTH Main and the RenderThread are
busy inside `C_Game::CreateInstance` and the loop never reaches the present step. The 3840 presented
frames were the **startup/splash video** (`startup_01.bk2`, seen in the FS trace) presented BEFORE
`CreateInstance` was entered; once `CreateInstance` began and stopped completing, present went idle
and the swapchain went `ERROR_BUSY`.

## WORKING MECHANISM (NOT fully proven — see the verified/inferred split) — wedge is in `C_Game::CreateInstance`

> CORRECTION (2026-06-21): this section was previously titled "CONVERGED MECHANISM (proven)". That
> overstated it. The authoritative verified/inferred/open split is in the handoff doc
> (`_research/ki0028-fsr2-poll-loop-recon/HANDOFF.md`); this section is the working summary, with the
> over-confident links downgraded below. Read the handoff for what is actually proven.

The evidence points at one area; NOT every link is proven:
1. **NOT "the sole differentiator".** P-F's swap-ON arm changed FOUR things vs swap-OFF (FS dispatch,
   the BootWatch watcher thread, the PresentProbe thread, and `BuildAssetIndexAtSeat`'s on-Main
   `WaitForSingleObject(INFINITE)`). PROBE L removed 2 (present-probe, watcher) and the wedge
   persisted → the cause is in {FS dispatch, the index-build INFINITE wait}, NOT proven to be the FS
   dispatch alone. (VERIFIED: swap-off→menu, swap-on→wedge; INFERRED: which differentiator.)
2. FS content diffs=0 for the files PROBE I covered — not "all files exonerated". (VERIFIED for the
   covered files only.)
3. The game is NOT hung — the main-thread heartbeat advances continuously (VERIFIED, 2.2 of handoff).
4. Present is idle because never called, not blocked (PROBE K, on a pre-PROBE-L build). (VERIFIED on
   that build.)
5. **Main is at the wedge in a WINDOW/DISPLAY-MODE loop (RVA `0x869c39`)** (VERIFIED — stack §2.3 +
   the offset-vs-RVA correction at the top of this file + `FINDING-real-rva-window-mode-loop.md`). The
   earlier "Main is in `C_Game::CreateInstance` entity-init" framing was the offset-vs-RVA artifact —
   `C_Game::CreateInstance+0x2e8c63` is itself a nearest-export-relative label; the real fn is the
   window/display-mode `0x869c39`. The RenderThread "also in CreateInstance" claim was already flagged
   as nearest-export NOISE (§2.6) and stays withdrawn.

**ROOT-CAUSE QUESTION (still open) — now PINNED to a mechanism class:** the window/display-mode loop
`0x869c39` is a critical-section-guarded **completion-token spin** (re-runs while `0x56628d8`/
`0x56628dc` `!= -1`; helper `0x1c1e988` flips to `-1` on completion, `0x1c1e91c` registers the task).
It waits for a registered task to complete; under the swap it never does. OPEN: WHICH task/producer,
WHICH thread completes it, and HOW the swap stalls it (runtime facts). The index-build INFINITE wait is
near-eliminated (it logs `seat_index_stored` on wedging runs — it returned). FS content was byte-correct
where checked and the freeze is FS-silent (not an in-progress wrong serve AT wedge time), but a value
set wrong EARLIER, or a non-FS swap side effect that stalls the producer, is not ruled out. Next: a live
theory-independent read of the counters + the critical-section owner swap-on vs swap-off (the owed probe
in `FINDING-real-rva-window-mode-loop.md`). Fix stays in kcdx full-init ownership (no thunk-back). KI
stays OPEN until the Resolution names the mechanism (AP17).

### FS-op logging contract upgrade + freeze-window capture (2026-06-21) — the wedge is FS-SILENT compute, NOT a filesystem serve

User flagged the logging as too weak (cost cycles). Upgraded the FS-op trace contract (commit
`48165ca`): every read line now names its FILE (vpath resolved from the handle) + bytes want/got +
ok/FAIL; FindFirst logs the returned ENTRY NAMES (capped), not just a count. Then widened the trace
window past the freeze + filtered the innocent looping menu video (commit `50bbb92`).

Two launches with the upgraded logging:
- **Ruled out the KI-0027 suspect FROM THE LOG ALONE** (the upgrade's first payoff): the entity
  enum `Libs\Tables/ai/smartEntity/SmartEntity__*.xml matched=577` is mask-CORRECT — every returned
  name is `smartentity__*.xml`, ZERO non-`__` over-match (KI-0027 was a 528 whole-dir over-match;
  this is not that). The 316× `gfxfontlib_glyphs.gfx` re-read is normal (26 ms, progressing chunks,
  not a spin). Both eliminated without cdb. (PROVEN — enum entry-name sets)
- **THE FREEZE WINDOW IS FS-SILENT.** With the window covering the freeze (run open 6 min,
  heartbeat → tick 121853), the 4-minute freeze gap (10:53–10:57) has **ZERO FS operations**. The
  only freeze-period FS is benign `kcd.log` open-misses (the engine's own log, a write retry) + one
  periodic cursor-texture reload. NO entity reads, NO enumerations, NO game-data loads, NO failures
  during the wedge. (PROVEN — `grep FS_BOOT_TRACE` in 10:53–10:57 window = 0)

**REFRAME (partially verified — read with care):** the freeze window is FS-SILENT (VERIFIED, above).
Main is in `C_Game::CreateInstance` at the wedge (VERIFIED — stack). The "RenderThread is also in
CreateInstance (PROBE K.3)" claim rests on nearest-export-NOISE frames (§2.6) and is NOT reliably
established — treat only the Main half as proven.
- **VERIFIED:** no file op is in progress DURING the freeze (FS-silent), and FS content was
  byte-correct for the files PROBE I covered.
- **INFERRED, NOT proven:** "the wedge is compute/sync, not a filesystem serve." The FS-silent freeze
  rules out an in-progress wrong serve AT wedge time; it does NOT rule out a value served wrong
  EARLIER in boot whose effect surfaces in the entity-init loop, and PROBE I covered SOME files, not
  all the entity system reads. So "the KI-0026/KI-0027 wrong-served-file class is RULED OUT" OVERSTATES
  it — downgraded to "not the proximate cause AT wedge time; an earlier wrong serve is not excluded."
- The blocking primitive at the wedge is a `SleepEx` inside the BOUNDED focus poll, re-entered by an
  unread outer loop (§2.5, AP19). What that outer loop spins on (a compute value, a sync object, a
  state flag, an entity-registry value) is NOT determined.

**OWED next (off the FS axis entirely):** instrument `C_Game::CreateInstance` INTERNALS, not the
filesystem. Identify what the entity-init loop (`0x36eb39` + its caller) spins/waits on in compute —
a state flag, a counter, a cross-thread sync object, an entity-registry value — that the swap leaves
in a never-satisfied state. Candidate probes: (a) read `0x36eb39`'s caller body for the loop's exit
condition (static, AP19 — read the body); (b) a swap-on vs swap-off diff of the non-FS state the
swap touches at seating (what besides the CCryPak vtable does the swap change?); (c) instrument the
CreateInstance loop's condition variable / counter directly. Fix stays in kcdx full-init ownership
(no thunk-back). KI OPEN (AP17 — no mechanism paragraph yet).

**Outcome→meaning map (pre-committed, flat — first row FALSIFIES the present framing):**
- `d_present ≈ 0` each interval → present essentially never called → loop never reaches
  present → **wedge is UPSTREAM (entity/instance init, P-J.5)** → next: identify `0x36eb39`'s
  caller + what `CreateInstance`/entity-init blocks on. (FALSIFIES "present is the problem".)
- `d_present > 0` but `d_refresh ≈ 0` → present called, no GPU scanout → **present path IS the
  problem** → next: the system-DLL present hook (capture return value + flags + swapchain ptr).
- both advance at a live rate → frames ARE presented → **black screen is a surface/compositor
  association**, not present → next: compare swapchain→HWND association swap-on vs swap-off.
- `swapchain_captured` never logs (only `factory_hooks_armed`) → the engine creates its
  swapchain by a path this hook misses (or before seating arms) → widen the capture point.

### P-L — disarm the 2 probe threads (RAN pending 2026-06-21) — decompose the P-F confound

**Why:** the converged mechanism rests on P-F ("the swap is the sole differentiator"), but reading
`seating_hook.cpp` shows P-F's swap-ON arm changed FOUR things vs its swap-OFF arm, not one:
(1) the FS dispatch (engine vtable → kcdx vtable), (2) `BootWatchStart()` arms a watcher thread,
(3) `PresentProbeStart()` arms a present-poll thread, (4) `BuildAssetIndexAtSeat()` runs an
`INFINITE WaitForSingleObject` on Main inside `CSystem::Init`. The swap-OFF arm returns early
(line 162) and skips (2)(3)(4). So P-F never isolated the FS swap — the wedge could be a probe
thread interacting with entity-init, not the swap. (A probe that changed N things is N findings —
results-driven §"one variable per probe".)

**PROBE L: comment out `BootWatchStart()` + `PresentProbeStart()` in the swap path (keep the FS
swap + the index build live), rebuild, deploy, launch.** One variable: the two diagnostic threads,
present vs absent. The FS dispatch + index-build wait stay identical to the wedging build.

**Outcome→meaning map (pre-committed, flat — first row OVERTURNS the converged mechanism):**
- Boots to interactive menu → the wedge was a PROBE THREAD interacting with entity-init, NOT the
  FS swap → the converged mechanism (swap perturbs CreateInstance) is WRONG; the diagnostic
  threads themselves caused the black screen. Next: bisect BootWatch vs PresentProbe.
- Still wedges (black screen) → the two probe threads are EXONERATED → the suspect narrows to
  FS-swap-or-index-wait (the remaining 2 of P-F's 4 differentiators). Next: P-L.2 suppress only
  the index-build INFINITE wait, isolating swap-dispatch vs the on-Main wait.

**Result (RAN 2026-06-21, PROBE L build deployed `6F86DC56…`, hash-verified, injected per launcher
log; session `11-13-16`):** **STILL WEDGES — same black screen + audio** (user-observed; process
PID 18100 ran ~9 min, `Responding=True`, 325+ CPU-s). VERIFIED from the PROBE L dev log (read AFTER
the process exited — see the 0-byte CORRECTION below):
- **PROBE L disarmed TWO of the three diagnostic mechanisms, NOT both "probe threads".** The log
  proves: `PresentProbeStart` OFF (0 `PRESENT_PROBE` lines), `BootWatchStart`'s stall-dump watcher
  thread OFF (0 `BOOT_WATCH_STALL`/`RESUMED` lines). BUT the per-frame heartbeat tick
  (`BootWatchTick()`, a SEPARATE call site at `hooks.cpp:1033`, never commented out) STAYED LIVE —
  537 `BOOT_WATCH heartbeat` lines, tick=1→41686. (VERIFIED — `kcdx-dev_2026-06-21_11-13-16.log`.)
- **So PROBE L did NOT test a fully-instrumentation-free boot.** It removed the present-probe + the
  watcher thread; the per-frame heartbeat logging was still firing every frame.
- **What this DOES establish (VERIFIED):** removing the present-probe + the stall-dump watcher did
  NOT change the wedge → those two threads are not the cause.
- **What it does NOT establish:** "both probe threads exonerated" / "the FS swap is the cause".
  Commit `f0b1a3f`'s message OVERSTATES this — corrected here. The per-frame heartbeat tick was not
  disarmed (LOW likelihood as a cause — single atomic + once-per-second log, and it ALSO ran in the
  pre-PROBE-L wedging build — but not ruled out by a probe). And the FS swap vs the index-build
  INFINITE wait are still two unseparated differentiators (P-L.2 owed: suppress only
  `BuildAssetIndexAtSeat`'s on-Main `WaitForSingleObject(INFINITE)`, keep swap).

**CORRECTION — the "0-byte log" was a FILESYSTEM-METADATA ARTIFACT, not a logging failure (do not
repeat this misread).** While PID 18100 RAN, PowerShell reported its engine logs (`kcdx_*.log`,
`kcdx-dev_*.log`) as 0 bytes for ~10 min. This drove an (incorrect) line of reasoning that the engine
wedged before logging. It was WRONG. After the process EXITED, the same files were 461 KB / 11.9 MB —
fully written, with the banner, 30 suite summaries, and 537 heartbeat lines. kcdx `fflush`es every
line (`log.cpp:238`), so the log was being written all along; the OS just did not update the on-disk
file SIZE/mtime until a flush/close boundary while the process held the handle. The earlier "two
readings (A)/(B)" framing rested on a false premise (the log was never empty). LESSON: never infer
"logging failed / wedged early" from a 0-byte log size on a STILL-RUNNING process — read it after
exit, or read kcdx's in-memory log state via cdb.

### P-L resolved by INVASIVE cdb on the live PROBE L process (RAN 2026-06-21, PID 18100, `qd`-detached)

Invasive `cdb -p 18100` (whole capture one pass, `qd` to leave running), `cdb_pl_probeL_wedge.txt`.
**Main's stack is BYTE-FOR-BYTE the converged wedge** — and it resolves the anomaly AND advances the
mechanism:

```
ntdll!NtDelayExecution → RtlDelayExecution → KERNELBASE!SleepEx+0x91
WHGame!…+0x36af90                          ← window/focus poll (RVA 0x865fb4, bounded 5-iter, FINDINGS)
WHGame!C_Game::CreateInstance+0x2e8c63
WHGame!C_Game::CreateInstance+0x2e8d7d
WHGame!…+0x16cce2
kcdx!HookedUpdate+0x94a                     ← our per-frame hook
WHGame!…+0x16c7a0                           ← engine update dispatcher
WHGame!…+0x36eb39                           ← ENTITY-INIT fn (carries "dummy_no_ai"/"player"/8 GUIDs)
WHGame!…+0x36ff17                           ← the frame BETWEEN entity-init and the focus poll
KingdomCome+0x36db / +0x4ad5 / +0x898a (main)
```

- **PROVEN: PROBE L wedges with Main's stack BYTE-FOR-BYTE == P-A.** Main is in the SAME
  `CreateInstance`/entity-init wedge; the symptom + stack are unchanged from the pre-PROBE-L runs.
  (PROVEN — resolved stack, kcdx PDB loaded.)
- **RESOLVED (post-exit log read): the 0-byte log was a metadata artifact; the disarm is CONFIRMED.**
  The earlier "reading (A) vs (B)" question is moot — the log was never empty (see the CORRECTION in
  the Result block above). Reading the populated post-exit log VERIFIES: present-probe disarmed (0
  `PRESENT_PROBE` lines), watcher thread disarmed (0 stall/resume lines), per-frame heartbeat tick NOT
  disarmed (537 lines). So the disarm took for the two intended threads, and the wedge persisted with
  them gone. (VERIFIED — `kcdx-dev_2026-06-21_11-13-16.log`.)
- **ROBUST CONCLUSION (what the run DOES establish):** PROBE L did not change the wedge — same stack,
  same symptom — so the present-probe + watcher threads are NOT the cause and the FS-swap mechanism
  remains the live suspect. NOT established: that the FS dispatch ALONE causes it (the index-build
  INFINITE wait is an unseparated second differentiator, P-L.2 owed), nor that the un-disarmed
  per-frame heartbeat tick is innocent (low likelihood, not probed).
- **`0x36eb39` IS ON THE STACK, directly above `0x36ff17` above the focus poll.** The decoded call
  chain at the wedge: `HookedUpdate → [update dispatcher 0x16c7a0] → 0x36eb39 (entity-init) → 0x36ff17
  → 0x36af90 (focus poll, bounded) → SleepEx`. The focus poll is BOUNDED (5 iter, FINDINGS); the
  INFINITE repetition is `0x36eb39`'s own outer loop re-running this chain — `0x36eb39` (entity-init)
  loops on a completion condition that never flips. **The precise static target is now `0x36eb39`'s
  loop body + `0x36ff17` (the frame it calls) — read for the exit condition the swap leaves
  never-satisfied (AP19).** (PROVEN — resolved stack; the outer back-edge is the unread piece)
- **The "NGX/FSR2" nearest-export frames are NVIDIA DRIVER threads** — other threads park in
  `NvTelemetryAPI64!FreeTelemetryString+…` and `nvcuda64!cuProfilerStop+…` (real modules), confirming
  the `ffxFsr2ResourceIsNull`/`NVSDK_NGX` labels on WHGame frames are nearest-export noise, and the
  driver threads are idle waits, not the wedge. (PROVEN — resolved module names)

**NEXT (sharpened, no launch): static-read `0x36eb39` + `0x36ff17` for the outer-loop exit
condition** — the entity-init completion flag/counter/registry value the swap leaves never-satisfied.
This is the recorded candidate (a), now PINNED to two exact RVAs by the live stack (was an un-located
"caller of the compute cluster"). Fix stays in kcdx full-init ownership (no thunk-back). KI OPEN
(AP17 — no mechanism paragraph yet).

### Reframe 6 (2026-06-21) — THE GAME IS NOT HUNG; Main runs the full frame loop at ~240fps

The confound check RESOLVED the contradiction and overturned the whole "init hang" premise:
- **The heartbeat tid is `44480` = `0xadc0` = the SAME thread cdb labeled "Main"** (`90c4.adc0`),
  and `CGame::Update` (the heartbeat source) is "Main-thread by construction" (seed id-2). (PROVEN
  — heartbeat `tid=44480`; Main `90c4.adc0`; `0xadc0`=44480.)
- **The heartbeat advanced to tick=58253 at 22:43:29** — long AFTER the cdb samples (22:40:11–14)
  and the first all-threads capture (22:38:54). Main ran `HookedUpdate` continuously the whole
  time. (PROVEN — dev-log heartbeat stream.)
- **∴ Main is NOT stuck.** It runs the normal per-frame loop; each frame it passes through the
  window/`Sleep(5)` helper (per-frame pacing/yield, bounded ~25ms). The cdb `-pv` NONINVASIVE
  samples just repeatedly caught Main during that recurring per-frame Sleep at the same call
  depth — NOT a freeze. "Identical RIP+RSP across 2s" was a sampling artifact of a RECURRING
  per-frame Sleep, not a pinned thread.

**The premise INVERTS:** the game's main loop is RUNNING at full rate (58k+ ticks), yet the user
sees no menu and Alt+F4 doesn't close it. This is NOT an init-never-completes hang — it is a
RUNNING game that does not RENDER / does not process window input. The whole "C_Game::CreateInstance
never returns" framing is WRONG: `CreateInstance` (or the frame path cdb labels with its nearest
export) is being entered every frame as part of the live update; the labels are nearest-export
noise, not a stuck init.

**Investigation reset (theories hopped 3×: deadlock → latency → sleep-loop → not-hung-at-all).**
Per `results-driven.md` (re-observe, don't theory-hop) + `/debug` §B.5 (2+ hops → fresh frame):
the next step is NOT another guess. Direct observable: a RUNNING main loop + no render + no input.
The probe target shifts from "find the hang" to "the game runs but doesn't present a frame / pump
window messages — why." Owed: fresh-frame observation of the render/present + window-message path,
NOT the (innocent, bounded, per-frame) Sleep helper.

**Gate A corrections (architect-review, BINDING — the build MUST honor these):**
- **F1 (CRITICAL, source-confirmed):** every `LOG_*_KV` takes the stream mutex (`src/log.cpp:520/529/538`).
  Suspending threads from the tick callback and then logging their frames DEADLOCKS the game (a
  suspended thread mid-log holds the mutex the dumper needs → the probe becomes a second wedge,
  destroying the evidence). Fix: dump from a dedicated watcher thread; NO `LOG_*_KV` while ANY thread
  is suspended — snapshot raw CONTEXT+frame data inside the suspended window, resume all, THEN format
  and emit through the logger. Suspend → capture-raw → resume → log.
- **F2:** the native-unwinder `ReadProcessMemory(GetCurrentProcess(), Rsp, …)` of another thread's
  stack is valid ONLY while that thread is suspended — the walk runs strictly inside the per-thread
  suspended window.
- **F4:** H2's marker means "UI-pump path executed," not "menu interactive." Do not assert a menu-ready
  semantic (a real menu-ready edge — e.g. the `this->byte_at_0x2A2F` the pump writes — would need its
  own probe; not built now, results-driven).
- **F5:** N=10s (user-set) — trigger on HEARTBEAT STALL (main thread stopped ticking), not on a
  missing menu fire.
- **F6:** a single onset dump only re-shows the parked NGX waits we already saw (the 37s-snapshot
  flaw). Dump at onset AND +30s so "zero progress on any thread across the interval" is OBSERVED, not
  inferred. **Heartbeat RESUMING is the primary, decisive falsifier.**

**Outcome→meaning map (pre-committed, one primary variable = main-thread liveness):**
- Heartbeat RESUMES after the stall → **LATENCY** (NGX init is slow, not deadlocked). Decisive, single signal.
- Heartbeat NEVER resumes + the two dumps (onset, +30s) show IDENTICAL parked NGX/SRW waits, zero
  progress on every thread → **DEADLOCK** (high confidence).
- H2 marker NEVER fires → wedge is upstream of UI pumping (deadlock-before-UI), narrows the site.
- H2 marker fires, heartbeat then stalls → UI path reached, wedge is later (in/after NGX), consistent
  with the cdb capture.

P-H is a probe (no thunk-back, no coexistence-fix — architect F7 clean). On retirement it captures to
`_research/probe-archive/` then removes from live source (the heartbeat may graduate to a kept
boot-progress diagnostic like FS_BOOT_TRACE if the user wants it permanent — decide at retirement).

## Reframe 5 (2026-06-20 — P-G mined the captured logs; CORRECTED after a premature "just slow" claim)

P-G's data was ALREADY captured — the FS_BOOT_TRACE kept diagnostic logged every render-window op,
the dev log holds the full tick stream, and the P-E cdb capture holds the resolved thread stacks.
A first pass over-read this as "boot is progressing, just slow"; that was a JUMP. Re-mined strictly
against the logs, separating PROVEN from INFERRED:

**PROVEN (log/dump-cited):**
- **The P-E cdb capture was taken at `20:30:20.935`** (capture-file mtime), **37s AFTER the dev
  log's last line at `20:29:43.475`** (the log goes silent there). (PROVEN — file mtime + dev tail)
- **At capture time (`20:30:20`), RenderThread (`b1cc.3144` = tid 12612, cdb-named "RenderThread"),
  ShaderCompile (`b1cc.b2b0`), and the main thread are ALL parked in NGX/FSR2/CreateInstance
  waits.** RenderThread: `NtWaitForAlertByThreadId ← RtlSleepConditionVariableSRW ← _Cnd_wait ←
  WHGame!NVSDK_NGX_UpdateFeature+0x20139e ← ffxFsr2ResourceIsNull...`. ShaderCompile: `SleepEx ←
  C_Game::CreateInstance+0x46514`. Main: `SleepConditionVariableSRW ← NVSDK_NGX_UpdateFeature`.
  (PROVEN — `ki28_pe_allthreads.txt` stacks)
- **The update tick ran 30 SUMMARY emissions, first `20:29:02.380`, last `20:29:43.475`** — the
  `HookedUpdate` steady-state body executed many times, suite reached `passing=320`. (PROVEN — 30
  `[TEST] SUMMARY` lines in the P-E dev log)
- **The dev log STOPS at `20:29:43.475`** and is silent for the 37s up to the capture. (PROVEN)
- The recycled-handle-id corruption theory is FALSIFIED — zero `double_close`/`bad_handle` logged,
  though `Close()` logs both. (PROVEN — `grep -c` = 0)

**The reconciliation (what the timeline actually means):** boot is NOT "permanently wedged from the
start" (the tick loop ran 41s, suite hit 320) AND it is NOT "just slow / progressing fine" (the dump
shows three threads hard-parked in NGX). The precise, log-proven shape: **kcdx-on boot PROGRESSES for
~41s (ticks firing, suite climbing to 320), then the log goes SILENT at `20:29:43`, and the dump 37s
later shows RenderThread + main + ShaderCompile parked in `NVSDK_NGX_UpdateFeature` / `CreateInstance`
waits.** The wedge ONSET is `~20:29:43`, after a window of real progress — not at boot start, not
absent.

**INFERRED, NOT yet proven (must not be stated as fact):**
- That the wedge is PERMANENT (the dump is a single 37s-later snapshot; it proves "still parked at
  20:30:20", NOT "never wakes"). A longer wait or a second capture is owed to prove permanence.
- That "the engine got past `C_Game::CreateInstance`" — the tick loop firing does NOT prove
  `CreateInstance` returned; the update tick runs on a different path, and ShaderCompile is still
  INSIDE `CreateInstance` at capture. This earlier claim is WITHDRAWN as unproven.
- Whether the menu ever renders in the P-E (swap-on) run — the current `kcd.log` on disk is from the
  P-F (`20:39`, swap-OFF) run, which OVERWROTE the P-E engine log. There is NO `kcd.log` evidence
  for P-E's menu state. (PROVEN that the evidence is absent — `kcd.log` mtime `20:40` ≠ P-E `20:28`.)

So the P-G per-op A/B-trace plan stays the WRONG next probe (it hunts a differing return for a wedge,
but the wedge is an NGX condvar wait, and kcdx is on no NGX stack). But the prior "just slow"
reframe is ALSO withdrawn. The pinned-down question is now narrow and falsifiable (below).

## Reframe 13 (2026-07-02) — the current wedge is NO INDEXED GEOMETRY on a LIVE, PRESENTING game; NOT an abort, NOT a stall; the bind-root fix is live+working; 4 root causes now overturned

Re-read the SAME `kcdx-dev_2026-07-02_21-05-00.log` (the black run) after the fresh-frame reframe. Every prior root-cause banner is overturned by the log's own ground truth:

- **The bind-root fix (`83a9279`) is LIVE and WORKING.** `asset_index_built entries=516363 paks=77 roots=2` (vs pre-fix 46 paks/307k). Level-pak files resolve by their `levels/kutnohorsko/`-prefixed keys — `level.pak` → `IsFileExist3 result=1`. The FS serves correctly (confirms Z5 post-fix). So ROOT-CAUSE-bind-root-prefix.md (the 4th "confirmed" root cause) fixed the FS-miss but did NOT fix the black screen.
- **The `0xD2` / `CET_PrepareLevel` / `CreateInstance` abort does NOT fire in this run.** Zero `RaiseException(0xD2)`, zero `MessageBoxA`, zero "can't be loaded". The abort in the `06-22` dumps was an EARLIER failure the bind-root fix cleared. The fresh-frame subagent's abort-gate probe (getter `0x66bbf0`, `[0x88]/[0x58]`, name-length) is aimed at a gate that no longer fires — its Outcome C ("abort is not the mechanism") is the real, already-observed outcome.
- **The game is ALIVE and PRESENTING at ~310 fps** — `PRESENT_PROBE d_present≈311/s`, `present_count` climbing 12,415→14,908, `hr_present=0`. NOT a deadlock, NOT a stall, NOT no-present. The `BOOT_WATCH stall_no_geometry` label is a MISNOMER for this run (heartbeat + present both alive).
- **The single differentiator is `draw_indexed=0` / `ia_set_ib=0`.** `DRAW_PROBE summary: draw_instanced=35479 draw_indexed=0 ia_set_ib=0 ia_set_vb=35479 ia_set_topo=35479 first_ib_va=0 first_ib_size=0`. The engine runs all non-geometry passes (fullscreen/post/clear/sky via DrawInstanced) but draws ZERO indexed world geometry → black/sky-only at full framerate. IASetIndexBuffer is NEVER called; no IB ever bound.
- **Window activation is NOT the cause either.** `WINDOW_PROBE fg_is_ours=1` at 21:05:01–21:05:21 (the window DID activate); the recon's "swap prevents window activation" mechanism is falsified for this run.

**VERDICT — KI-0028 is now: on a live, presenting full-swap game, indexed-geometry mesh draw is ABANDONED UPSTREAM of D3D12 command recording (no IB ever created/bound).** Two undiscriminated branches remain (the real frontier, = Z6 option (b)):
- **(a) IB resources never CREATED** — the world's index buffers are never made (device CreateCommittedResource/CreatePlacedResource for IB never runs swap-ON), OR
- **(b) the scene/world render PASS is never ENTERED** — a higher-level engine decision (visibility / scene-graph / render-list population) drops all real geometry before it reaches D3D12.

`first_ib_va=0/first_ib_size=0` is consistent with both. The next probe must discriminate (a) vs (b). This is a genuine fresh-frame situation: 4 overturned root causes (abort, resourcelist, bind-root, window-activation); the current ground truth (draw_indexed=0 on a presenting game) differs from all of them. FS is fully exonerated (Reframe 12) and stays so.

## Reframe 12 (2026-07-02) — PROBE Z4+Z5 EXONERATE the FS: files read correctly on the full swap; the black screen is NOT a filesystem problem

PROBE Z4/Z5 (run `kcdx-dev_2026-07-02_21-05-00.log`, full-swap `mask=15`, `draw_indexed=0`) overturned the entire Reframe-10 fix direction:

- **Z4 — the raw path is NEVER taken.** The engine reader at `0x460b64` takes the ABSTRACT-stream path (`[wrapper+0x110]` non-null, a stable heap object) on ALL 40 fires; zero raw-CRT-path fires the entire boot. So `fileno`/`fread` on kcdx's handle-int (the Z2.3-open CRASH mechanism) does NOT fire on the full swap — `0x460b64` is exonerated as the full-swap stall.
- **Z5 — the abstract read SUCCEEDS for every asset.** The abstract stream's `[vtable+0x170]` read returns a plausible, CORRECT, non-zero size for every pak+loose asset (`stars.dat`=106956, `engine_core.thread_config`=20096, the cvargroups/xml/lua/ent all real sizes; zero `result_size=0`). kcdx serves every file correctly on the full swap.
- **Z4.1 — ucrtbase resolves fully** (14/14 stdio exports by name), so fix a′'s fn-table is buildable — but a′ targets the raw path the full swap never takes.

**VERDICT: the FS takeover is EXONERATED as the KI-0028 black-screen cause (the 3rd exoneration — Z2.2 mechanism-innocent, PROBE W zero-divergence, now Z5 reads-succeed).** Files load correctly; `draw_indexed` is still 0. The wedge is DOWNSTREAM of file I/O — in what the engine does with correctly-loaded data under the swap (a pointer/object identity the swap changes, a control-flow branch, or a swap side effect), NOT the file bytes. The Reframe-10 fix (FOpen returns a real FILE*, a′/b/c) fixes the SEPARATE Z2.3-open crash (open-only arm), not the full-swap black. Detail: `_research/ki0028-tick-geometry-dispatch-recon/Z5-abstract-read-succeeds-fs-exonerated.md` + `Z4-crt-reader-abstract-path-not-raw.md`.

**Next: a STEP-BACK / fresh-frame reframe** — the FS-read frame is exhausted (3 exonerations); the swap-perturbs-something-non-file question is the new axis. A live invasive cdb capture of the wedged process (KI-0028 memory: `-p`+`qd`) is owed on the next black run to see the wedged main/render thread. The Z4/Z5 probe wiring (`crt_reader_probe.*`) stays armed but its question is answered — retire-and-capture pending the reframe direction.

## Reframe 11 (2026-07-02) — Gate A on fix a′ cleared; PROBE Z4 (pak-reaches-0x460b64 + ucrtbase-resolution) is the next launch, folded into one

Gate A (`architect-review`, WITHHELD) on the concrete fix a′ (open loose on the engine's ucrtbase) returned **`forward-and-wait`** — a′'s LOOSE direction is sound and its routing claims are verified from existing recon (a real ucrtbase `FILE*` fails the `handle-1 < pakEntryCount` tag test → routes to the engine's OS/`FILE*` arm; `asset-fopen-handle-recon/FINDINGS.md:49-55`, gated). But it caught a coupled fork the fix-scope map glossed: **the pak arm carries the IDENTICAL raw-CRT-reader exposure a′ closes for loose.** a′ makes loose and pak DIVERGE (loose → real ucrtbase `FILE*`; pak → still the kcdx handle-int `(id<<1)|1`), and if a pak-resident asset ever reaches the engine's `0x460b64` raw-CRT reader with `[+0x110]==null`, it calls `_fileno(handle-int)` and crashes exactly as loose does today. Whether it does is the **unproven runtime link** (F2) — `Z2-3-open-crash:50-54` never traced the `[+0x110]==null` branch for a pak-resident backdrop.

**User's decision (fork surfaced from architect §3): probe the pak case FIRST, then settle the pak shape on the result.** (Option A — wins on UX-via-correctness; prevents shipping a pak-shaped instance of the exact black-screen bug. Options B "build loose now / defer pak" and C "unify loose+pak on a real FILE*" both declined pending the probe.)

**PROBE Z4 — two checkable unknowns, ONE launch (both read-only observers on the full-swap arm):**

| Sub | Question | Observer | Outcome → meaning |
|---|---|---|---|
| Z4.1 (F4) | Does the engine's ucrtbase export the stdio family in-process by name? | At fs-takeover init: `GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), "_wfopen"/"fread"/"_fseeki64"/"_ftelli64"/"_fileno"/"fclose"/…)`, log each non-null. | all non-null → build the ucrtbase fn-table (fix a′ mechanism); any null → fall back to `GetModuleHandleW` on the already-loaded apiset `api-ms-win-crt-stdio-l1-1-0.dll` (NOT `LoadLibrary` — architect F4). |
| Z4.2 (F2) | Does a PAK FOpen's handle-int ever reach the engine's `0x460b64` raw-CRT reader on the full swap? | One-shot canary at kcdx's pak-FOpen return (log the minted handle + vpath) + a one-shot at the `0x460b64` `[wrapper+0x108]` read (log the value it reads + whether `[+0x110]` was null). | pak handle SEEN at `0x460b64` → pak needs its own `FILE*`-shaped object (Fix-pak is real: option C-shape or a memstream) → surface the pak-shape fork. pak handle NEVER at `0x460b64` (only loose paths reach it) → Fix-pak is a no-op; pak stays the handle-int untouched; build loose-only (option B outcome). |

Z4.1 is theory-independent (raw export enumeration). Z4.2 is the fork-decider and is theory-independent (observe which handle-KIND reaches the raw-CRT site, not "test whether pak crashes"). Both are read-only; no mutation; the full-swap arm (`kFamAll`) reproduces the black screen so the observers fire on the real repro.

**Carried corrections (architect F5, in the eventual fix, not the probe):** the falsified-invariant comments to sweep now include `file_handle.h:6-9` + `file_handle.h:20-38` (the handle-representation "belt-and-suspenders" rationale that `0x460b64` disproves) on TOP of the fix-scope map's `file_handle.h:262-267` + `open_slots.cpp:22-26`.

## Reframe 10 (2026-07-02) — FIX SETTLED: keep full takeover, FOpen returns a real `FILE*` — probe-first (Gate A cleared, both decisions the user's)

Root cause proven (Reframe 9). Gate A architect-review dispatched on the two coupled forks; both surfaced to the user; both settled:

- **Q1 (strategic) — KEEP full FS takeover** (not coexistence). Rationale: wins on Capability (kcdx IS the filesystem — one unified index, uniform enumeration/precedence); the FOpen fix below is smaller AND more complete than perfecting the handle-id (it closes the whole raw-CRT-consumer class); preserves the settled §1 total-ownership vision.
- **Q2 (tactical) — FOpen returns a real `FILE*`**, tracked side-band so kcdx's own read/seek/close slots still map it. The engine's raw-CRT path (`ftell`/`fseek`/`fread`/`fileno` on `[wrapper+0x108]`) then gets exactly what it expects; every raw-CRT consumer (proven + latent) is satisfied by one representation change at `open_slots.cpp` (return `fp`, not the minted handle `h`).

**HARD PRECONDITION (probe-first, per results-driven) — PROBE Z3.** Whether the engine's ucrtbase CRT can operate a `FILE*` that kcdx's statically-linked CRT opened is a **cross-CRT unknown of the SAME class that produced KI-0019** (kcdx's CRT freeing a foreign object). It is NOT assumed — it is PROBED before the fix lands. If the probe shows the engine's CRT cannot operate a kcdx-CRT `FILE*`, the fix shape changes (an OS-`HANDLE` adopted via `_open_osfhandle` on the engine's CRT, or Q1 re-opens toward coexistence) — so the probe result feeds back into Q1/Q2.

**Two mandatory corrections carried regardless (architect-review §4):**
1. **`docs/design/` (the FS-takeover TRD) P3/§4.4 is FALSIFIED and must be re-opened.** P3 asked "does any engine code BYPASS the vtable and operate a handle directly?" and resolved "outcome 1 — no" by checking only two off-vtable candidates (the streamer's `m_zipFile`, DirectStorage) — it MISSED the file-wrapper reader at `0x460b64`. The handle-id representation (§4.4, SETTLED) rests on that falsified probe. This is the "settled clause asserting an unobserved runtime mechanism" defect (`results-driven.md`) — a `/design` revision (present-tense body edit + changelog), the user's call to trigger.
2. The pak-entry case (Q2 residual): a pak asset has no real `FILE*` (it's an in-memory inflate), so pak reads need a `FILE*`-shaped object too (a memory-stream `FILE*` or a temp real file) — its own probe-first sub-step, not folded into the loose-file fix.

**Probe-first plan (ordered, incremental-delivery):**

| Step | What | Status | Gate |
|---|---|---|---|
| Z3 (probe) | Cross-CRT precondition: can the engine's CRT operate a `FILE*` kcdx opened? | **DONE — RESOLVED STATICALLY (2026-07-02), no launch needed** | probe (results-driven §4) |
| Fix-loose | FOpen gives the engine a `FILE*` **the engine's own ucrtbase can operate** (shape re-forked by Z3 — a′/b/c below). | BLOCKED on the re-forked Q2 (Gate A + user) | Gate B (root-cause-verifier) |
| Fix-pak | Pak entries return a `FILE*`-shaped object (memory-stream or temp file) the engine's raw-CRT path can operate. | BLOCKED on the re-forked Q2 | Gate B |
| Design-fix | Re-open the FS-takeover TRD P3/§4.4 (falsified); present-tense body edit + changelog. | NOT STARTED | `/design` |

**Z3 RESULT (static, decisive) — the literal Q2 fix is UNSAFE; Q2 re-forks.** `_research/ki0028-tick-geometry-dispatch-recon/Z3-static-cross-crt-file-layout.md`:
- kcdx links `/MT` (its OWN static CRT baked into `kcdx.dll` — `CMakeLists.txt:384`); WHGame links ucrtbase DYNAMICALLY (`api-ms-win-crt-stdio-l1-1-0.dll` → `_wfopen`/`fread`/`_fileno`/…, from the WHGame import table). **Two distinct CRT instances = two distinct `FILE*` stream tables.**
- A `FILE*` is CRT-instance-private. Handing WHGame's ucrtbase a kcdx-static-CRT `FILE*` is the **KI-0019 cross-CRT straddle** (KI-0019 = the heap analogue; this = the stdio analogue). So **"FOpen returns a real FILE*" as literally stated (Q2-a) is unsafe** — it swaps the fileno-on-an-int crash for a fileno-on-a-foreign-FILE* crash, same class.
- The fix must give the engine a `FILE*` **the engine's OWN ucrtbase opened**. Re-forked Q2 (Gate A + user's call): **a′** kcdx opens loose files by calling the ENGINE's ucrtbase `_wfopen` (read slots then operate via ucrtbase too); **b** kcdx opens an OS `HANDLE`, the engine adopts it via `_open_osfhandle`+`_fdopen` → a ucrtbase `FILE*`; **c** force the engine's abstract-stream `[+0x110]` path so its raw-CRT reader never runs. **No launch was owed — the CRT-instance split settles the hazard statically; the literal live probe would only test a form already shown unsafe.**

The FOpen fix is NOT built — the re-forked Q2 (a′/b/c) is the gate, the user's call.

## Reframe 9 (2026-07-02) — the REASSESSMENT: the differentiator is a ~27s backdrop-load TRANSITION; PROBE Z2 (slot-family bisection) is the theory-independent next probe

The user-directed step-back (Reframe 8: "stop probing individual functions, reassess what the swap provably perturbs before the next probe") is now answered. The reassessment rests only on confirmed ground truth.

**PROBE Z2 bisection — run ledger (signal = DRAW_PROBE `draw_indexed`: climbs = RENDERS/transition fires; stays 0 = BLACK):**

| Run | Mask (marker) | Status | Result |
|---|---|---|---|
| Z2.1 | `kFamAll` (no marker) — confound self-check | DONE (2026-07-02) | `probe_z_live_mask=15`, 41 samples all `draw_indexed=0` → **BLACK reproduced; self-check PASSES.** The full swap is a genuine repro; the bisection is valid. |
| Z2.2 | `kFamNone` (`kcdx-thunkswap`) — mechanism-vs-logic split | DONE (2026-07-02) | `probe_z_live_mask=0`, `kcdx_owned=0` (every slot thunks), yet the transition FIRES at ~33s (`draw_indexed` 0→152742) → **RENDERS. The swap MECHANISM is INNOCENT** (object overwrite/seat/index all happen). The culprit is a slot family's LOGIC → per-family build-up (Z2.3). |
| Z2.3 | one `kcdx-live-*` at a time — per-family build-up | DONE (open run decisive) | Z2.3-open CRASHED at the FOpen-return→fileno site + the bridge is proven — the OPEN family / FOpen-return-type is the root. read/metadata/enum runs NOT needed (culprit found). |

**ROOT CAUSE (proven — bridge confirmed 2026-07-02):** kcdx's `FOpen` (open family, slot 36) returns a **kcdx handle (int token), not a CRT `FILE*`** (`open_slots.cpp:119`). The engine has a file-wrapper reader (`WHGame` RVA `0x460b64`, on the `C_Game::CreateInstance` path) that consumes FOpen's return via **raw CRT** — `ftell`/`fseek`/`fread`/`_fileno`/`_fstat64i32` on `[wrapper+0x108]` — bypassing kcdx's CCryPak read slots entirely (taken when the abstract stream `[+0x110]` is null). open-only live → `_fileno(handle)` derefs `[handle+0x18]` → AV (Z2.3-open crash). Full swap → the same raw-CRT ops run on the handle-int → the backdrop asset never loads → the ~27s transition never fires → BLACK. kcdx owning the read SLOTS is irrelevant — the engine calls the CRT directly on FOpen's return. The full-takeover handle model silently breaks FOpen's contract ("return a value the engine can `fread`/`fileno`"). See `_research/ki0028-tick-geometry-dispatch-recon/Z2-3-open-crash-fileno-handle-mismatch.md`. **The fix is settled (Reframe 10): keep full takeover, FOpen returns a real `FILE*`, probe-first.**

**Z2.3 per-family build-up (each = ONE family live, the other three thunk; `draw_indexed` climbs = that family is INNOCENT, stays 0 = that family is the CULPRIT):**

| Sub | Marker (only one live) | Status | Result |
|---|---|---|---|
| Z2.3-open | `kcdx-live-open` (1/35/36 AdjustFileName/FOpen) | DONE (2026-07-02) | **CRASH** (third outcome) — `mask=1`, `kcdx_owned=3`. Dump: AV `NULL_CLASS_PTR_READ ucrtbase!fileno`, `mov [rcx+18h]` with **`rcx=3`** — the engine called `fileno()` on a `FILE*` of `3`. kcdx's FOpen returns a **kcdx HANDLE (int token), not a FILE\*** (`open_slots.cpp:119` MintLoose `return h`); the engine calls `fileno()` DIRECTLY on FOpen's return (read/metadata thunked → engine's own path), gets kcdx's handle-int → deref → AV. **The OPEN family (FOpen return TYPE) is directly implicated.** Stack up through `C_Game::CreateInstance`. See `_research/ki0028-tick-geometry-dispatch-recon/Z2-3-open-crash-fileno-handle-mismatch.md` |
| Z2.3-read | `kcdx-live-read` (38..66 handle reads) | NOT STARTED | |
| Z2.3-metadata | `kcdx-live-metadata` (13/45/67../93 exist/size/stat) | NOT STARTED | |
| Z2.3-enum | `kcdx-live-enum` (14/63/64/65 ForEachFile/FindFirst/Next/Close) | NOT STARTED | |

**What is CONFIRMED (this session):**
- **The differentiator is a specific, time-located event, not a vague "never loads."** Y.6 (DRAW_PROBE on the working swap-OFF menu) measured: the working path holds `draw_indexed=0` / `ia_set_ib=0` for its first **~27s** (instanced UI draws only — the SAME signature as the swap-ON black screen), then a **transition fires at ~10:29:42** and backdrop geometry drawing begins and climbs to `draw_indexed=68024`. The swap-ON black screen never fires that transition (`draw_indexed=0` forever). The pre-transition state is IDENTICAL on both arms — the divergence is purely whether the transition fires. (`_research/ki0028-tick-geometry-dispatch-recon/Y6-workingmenu-draw-progression.md`.)
- **The render side is fully exonerated** at three layers: shader/PSO (Reframe 7's six probes), window/present (PROBE M + window-exit recon), per-frame render-dispatch (Measurement 2, `ki0028-tick-geometry-dispatch-recon/FINDINGS.md`). The render tick is alive and un-perturbed; it draws whatever geometry exists. So the missing thing is that the backdrop geometry never gets loaded, NOT that it fails to draw.
- **The swap's entire blast radius is file I/O.** The FS takeover overwrites the CCryPak vtable so all four kcdx slot families route through kcdx: **open** (1/35/36), **read** (38..66), **metadata** (13/45/67../93), **enum** (14/63/64/65). These four families are the COMPLETE surface of what the swap changes — nothing the swap does is outside them. So the ~27s transition that never fires swap-ON must depend on a file operation one of these four families serves differently from the engine original.

**What the reassessment RULES OUT as next probes (dead leads, do not re-chase):**
- `CResourceList::Load @ 0x4dcb60` — falsified (Reframe 8; fires 0× on BOTH arms, not on the menu/backdrop path).
- Any single-function "find the level-load trigger" hook — the same one-deep pattern that dead-ended twice. The transition trigger is unknown and hunting it function-by-function is the pattern the step-back rejected.

**The theory-independent next probe — PROBE Z2 slot-family bisection (already built, `cd1a126`):**
Instead of guessing WHICH file op perturbs the transition, PROBE Z2 answers it BY CONSTRUCTION. `SwapVtableOnObject(liveFamilyMask)` runs the swap MECHANISM identically (object overwrite, seat timing, index build) but lets only the named slot families run kcdx logic — the rest thunk to the engine original. Marker files in `<kcdx-engine>/` select the mask (`kcdx-thunkswap` → none live; `kcdx-live-open|read|metadata|enum` → only named; no marker → `kFamAll` full swap). This is a discriminating, theory-independent probe: the mask that RENDERS (backdrop transition fires) vs the mask that goes BLACK (transition never fires) names the culprit family directly, with no theory about which file op matters.

Outcome→meaning map (bisection, one variable = which families are live):
- **`kFamNone` (kcdx-thunkswap) RENDERS** → the swap MECHANISM (object overwrite / seat timing / index build) is innocent; some kcdx slot LOGIC causes it → proceed to per-family build-up. **`kFamNone` still BLACK** → the mechanism itself is the differentiator, not the slot logic (the object-identity/timing/index axis, a separate investigation) → the confound self-check below decides which.
- **`kFamAll` (no marker) must reproduce BLACK** — the confound self-check. If a full swap RENDERS, the premise is contaminated (something about THIS build differs from the black runs) and the bisection is invalid until reconciled.
- **Per-family (one `kcdx-live-*` at a time) — which single family live flips to BLACK**, or (tear-down) which single family thunked flips to RENDER — names the culprit family by construction.

Pak mount is init/load-time (mount-once at startup, driven by kcdx's own loader at `enabled_list_builder.cpp:57` — `_research/fs-takeover-pak-mount-recon/FINDINGS.md`), so the perturbation is plausibly in the OPEN or ENUM family that init-time pak/asset resolution walks — but the bisection OBSERVES which, it does not assume it.

**Ordering (per incremental-delivery + the confound self-check):** run `kFamAll` (confirm black reproduces — the self-check) and `kFamNone` (mechanism-vs-logic split) FIRST, before any per-family run — a per-family result is meaningless if the endpoints don't bracket it correctly.

## Reframe 8 (2026-07-02) — PROBE X (CResourceList::Load) is a RED HERRING; "level never loads" is NOT established

PROBE X (levelload_probe.cpp) after-hooked `CResourceList::Load @ 0x4dcb60` armed before the swap decision, A/B over 3 launches:

| | swap-ON (black) ×2 | swap-OFF control (menu) |
|---|---|---|
| hook armed | ok=1 | ok=1 |
| swap state | live (`vtable_swapped`, FOpen `./system.cfg`) | suppressed (`probe_f_swap_suppressed`) |
| reached | black screen | MENU ✓ |
| **`load_calls`** | **0** | **0** |

`CResourceList::Load` fires ZERO times even on the working menu run. So `load_calls=0` swap-ON proves NOTHING about the swap — this function is not on the menu boot path at all. Identical trap to PROBE R2 (a swap-ON zero that is also zero on the path that works). **PROBE X is falsified as a probe target.**

Two consequences:
1. **Reframe 7's "the level-load never fires swap-ON" claim is NOT established.** It rested on FS-trace ABSENCE (`mmrm=0`, `.cgf=0`), never on catching the load trigger. `CResourceList::Load` was the candidate trigger; it is not the trigger (fires on neither path). The "level never loads" theory is unconfirmed, not proven.
2. **New symptom undercuts it further:** this session's swap-ON runs showed a LONG load time before the black screen (user-observed). A level that never begins loading would reach black FAST. A long load means the engine IS doing substantial work — closer to "content loads but never renders" than "level-load never triggers."

**Status: both prior framings (render-routing, level-load-never-fires) are now suspect.** Two independent leads each dead-ended one function deep. User-directed pivot (2026-07-02): STEP BACK — stop probing individual functions, reassess what the swap provably perturbs before the next probe. The reassessment supersedes Reframe 7's "next probe = level-load-entry hook" direction.

Probe retirement owed (no-residue): capture PROBE X finding + wiring to `_research/probe-archive/`, remove `levelload_probe.{h,cpp}` from source + CMake + seating_hook arm. (Not yet done — the several stale render-side probe arms K/P/R/S/U/W in seating_hook.cpp are ALSO owed retirement; batch with the reassessment outcome.)

---

## Reframe 7 (2026-06-23) — SUPERSEDED by Reframe 8: level-load-entry lead did not pan out

The 06-22 render-side investigation (`_research/ki0028-cshaderman-pso-consumer-recon/`) exonerated the entire shader/PSO axis by measurement (present succeeds, cache accepts, precache/PSO-create identical both paths) and pinned a terminal fact: swap-ON the frame records `draw_instanced=9500 draw_indexed=0 om_null_rt=0` vs swap-OFF `1383 / 96 / 0`. That investigation read `draw_indexed=0` as a render-target-routing question and proposed a heavier render-graph instrument.

**That read is SUPERSEDED (user-approved pivot 2026-06-23).** The next-day zero-plugin FS baseline (`_research/ki0028-fsr2-poll-loop-recon/CLEAN-ZEROPLUGIN-BASELINE-2026-06-23.md`) trace-diffed the FS logs and proved `draw_indexed=0` is a DOWNSTREAM SYMPTOM of "the level never loaded," not a routing bug:

```
mmrm_used_meshes      black=0       crash=7
merged_meshes_sectors black=0       crash=7
levels/kutnohorsko    black=6       crash=118,847   <-- the whole difference
leveldata.xml         black=2       crash=17
```

Swap-ON, the engine reads base + UI assets (high-water mark = `cursor_green.dds`, `pros_qr_frame.dds`, `autoexec.cfg`, `config/config.dat` — all menu/UI), then STALLS before requesting ANY level geometry — zero `.cgf` in 100,097 trace lines. The frame is black because there is no loaded world geometry to draw; the 9500 non-indexed draws are the menu/UI compositor over a world that never loaded. The render pipeline is ALIVE (present succeeds, PSOs build) — it just has nothing to draw.

**False lead already killed (do NOT re-chase):** level-EXISTENCE is not the gate. Both black and level-entering runs get identical enum (`level.pak`=1, loose `kutnohorsko.xml`=0, which is CORRECT — no loose xml exists, engine falls back to `level.pak`). Both agree through existence, then diverge at the level-LOAD the black run never starts.

**The pivot:** drop render-graph/resource-routing; pursue the LEVEL-LOAD TRIGGER — where the FS swap actually bites. A level-load that silently never starts is far more plausibly a file-serving/enumeration divergence (in kcdx's blast radius) than a D3D12 render bug (not).

**Next probe — level-load-entry after-hook (armed before the swap decision, A/B).** Entry points already RE'd + body-read in `_research/ki0028-vanilla-init-fs-map/` (reuse-first, no fresh Ghidra needed): `CResourceList::Load @ 0x4dcb60` (first level-resource read) → pathbuilder `0x4dd384` → slot-4 reader `0x4dd5e4` → open-helper `0x4605bc` → `CCryPak::FOpen` slot 36 `0x4614A0`; orchestrator `C_Game::CreateInstance`; record reader `0x66bbf0`. NOT on disk (fresh Ghidra if needed): `mmrm_used_meshes.lst` has zero hits (never traced to a function), and the `CLevelSystem::LoadLevel`/`SetCurrentLevel` WRITER is unpinned.

> Probe asks: *does the engine BEGIN loading the level swap-ON?* After-hook `CResourceList::Load @ 0x4dcb60`, armed before the swap decision.
> - **Outcome A** — fires swap-OFF, NOT swap-ON → an UPSTREAM gate stops level-load before it begins; the divergence is in what the engine checks to DECIDE to load (a file/enum kcdx serves differently). **← the bet.** Next: hook the `C_Game::CreateInstance` caller region to find the gate.
> - **Outcome B** — fires swap-ON, then early-returns/finds-nothing → level-load starts but a served manifest (`leveldata.xml`) parses empty under the swap. Next: dump the bytes it reads swap-ON vs swap-OFF.
> - **Outcome C** — fires identically both paths, identical bytes → the gate is DOWNSTREAM of the resource-list read. Next: hook the next stage (needs the unpinned writer, fresh Ghidra).

Full cross-document reconciliation: `_research/ki0028-fsr2-poll-loop-recon/RECONCILE-render-vs-levelload-2026-06-23.md`. Bind-root caveat: the `83a9279` bind-root-prefix fix cleared the `0xD2` abort and is IN; the black screen persists after it, so this level-load gate is a SEPARATE mechanism downstream of that fix — confirm the fix is still live in the swap-ON run before concluding Outcome A.

---

## Open questions (for /debug — after P-C)

- **NEW (Reframe 5, log-proven): is the `~20:29:43` NGX wedge PERMANENT, or does it eventually wake?**
  The dump proves three threads parked in `NVSDK_NGX_UpdateFeature` at `20:30:20` (37s after the log
  went silent) — but a single snapshot cannot prove "never wakes." Decisive cheap observation: launch
  swap-ON and WAIT 2–3 min at the audio/no-menu state. Menu eventually renders → the NGX wait
  resolves and the bug is boot LATENCY (something the takeover does makes the NGX/FSR2 init take
  minutes); the next probe times the NGX-init window swap-on vs swap-off. Menu NEVER renders →
  confirmed permanent NGX-condvar wedge at `~20:29:43`, and the next probe instruments WHAT the
  RenderThread's NGX worker is waiting on (the condvar's signaller) — kcdx perturbed a state NGX's
  `UpdateFeature` depends on. Either branch keeps the fix inside kcdx ownership (no thunk-back). Run
  this single observation before any instrumentation — it splits latency-vs-deadlock, the one fork
  the captured logs cannot resolve.


The wedge is a deadlock INSIDE NGX's `UpdateFeature` (main waits a condvar; the NGX worker that
should signal it spins forever), with kcdx on no stack and no thread blocked on a file read. P-B
already proved it is kcdx-INTRODUCED (vanilla boots). So kcdx perturbs NGX init WITHOUT being on
the stack at hang time — it changed some STATE that NGX reads, earlier in boot. Two live causes:

- **H3 (takeover-served NGX input is wrong/incomplete):** NGX/FSR2 init read a file kcdx now
  serves (an NGX config / model snippet / pipeline-or-shader cache / the upscaler's own data)
  and got wrong-or-empty bytes — succeeded the read (so no I/O block now) but on bad content NGX
  enters a state where its worker spins forever. The FS takeover's serve for SOME NGX-needed path
  is subtly wrong (a length, an alias, a `%engine%`-class path, a bad enumeration result).
- **H4 (takeover changed init TIMING/threading):** the takeover reordered or re-threaded boot
  enough that NGX's `UpdateFeature` runs before a dependency it needs, or its job is dispatched
  onto a worker pool whose state kcdx altered — a lost-wakeup the takeover's reordering introduced,
  not a content problem.

Decisive next probe (the cheapest-most-falsifying — read-only, no relaunch needed YET):

- **P-D (read-only, on the captured dump + the dev log): enumerate every file kcdx served during
  this boot (the dev-log `read_entry` / `FindFirst` lines) and intersect with NGX/FSR2-needed
  paths (anything under an `engine`/`shaders`/`fsr`/`ngx`/`dlss`/`upscal` name, a `*.bin` model
  snippet, a pipeline cache).** A served NGX-class path with a wrong `usize`/`matched`/alias is
  the H3 smoking gun; none found → H4 (timing/threading) and the next probe bisects WHEN the
  takeover installs vs when NGX inits. This reads the existing `kcdx-dev_<ts>.log` from THIS run
  — no relaunch; the hung process + its log are the ground truth in hand.
- The H1/H2 split + P-B + the superseded DispatchPre-bypass premise, preserved below for the trail:

P-A localized the wedge to a `SleepEx` on the main thread inside WHGame's
`C_Game::CreateInstance`/FSR2 path, reached via our `HookedUpdate`. P-B confirmed **H1
(kcdx-introduced)**; P-C narrowed it to an NGX-internal deadlock with kcdx's per-frame body
innocent (above). [Historical: the remaining question was WHICH kcdx effect wedges the path —
the per-frame body (ruled out by P-C) or the boot-state the FS takeover changed (now H3/H4).]

- **P-C (next — bypass kcdx's per-frame body): for one launch, make `HookedUpdate` call ONLY
  `g_orig_update(p1,p2,p3)` — skip the ApplyZone drain, DrainQueue, and the test-report
  blocks (guard them behind a `// === DIAGNOSTIC (PROBE C)` early-jump).** One variable: kcdx's
  per-frame work, present vs absent. Wedge CLEARS (game boots past menu) → kcdx's per-frame
  body is the cause (next: bisect ApplyZone vs DrainQueue vs the report blocks). Wedge PERSISTS
  → `HookedUpdate`'s body is innocent; the cause is the boot-STATE the FS takeover changed
  (what `CreateInstance`/FSR2 reads), and the probe moves to the takeover's effect on init
  state, not the per-frame path. Cheap, one-site, falsifiable both ways.
- The original H1/H2 split + the (now-superseded) DispatchPre-bypass premise, preserved for
  the trail:

- **H1 (kcdx-caused):** something `HookedUpdate` does each frame — the original-`update`
  trampoline call OR the `hook_chain::DispatchPre` per-frame pump it drives — makes the
  game's `CreateInstance`/FSR2 path spin/sleep forever. The FS takeover changed boot enough
  that the game reaches this path in a state where it busy-waits.
- **H2 (engine/environment):** the game's own FSR2/DLSS upscaler init at first-menu spins
  here regardless of kcdx — `HookedUpdate` is just the innocent per-frame call path, and a
  vanilla boot stalls identically in `C_Game::CreateInstance`/FSR2 in this environment.

- **P-B (decisive control): does a VANILLA (no-kcdx) boot reach AND get past the main menu in
  this same environment?** Vanilla ALSO hangs in `C_Game::CreateInstance`/FSR2 -> H2 (engine/
  environment; kcdx innocent, the takeover merely exposed it). Vanilla boots to a working,
  interactive menu -> H1 (kcdx-introduced; probe `HookedUpdate`/`DispatchPre` + what the FS
  takeover changed about the state `CreateInstance` reads). Cheap, no instrumentation — boot
  unmodded.
- If H1: the next probe bypasses `hook_chain::DispatchPre` in `HookedUpdate` for one launch
  (does the wedge clear with the per-frame chain pump disabled?) — isolates trampoline-call
  vs. chain-dispatch as the cause.
- The suite's `pending=21` is the in-game/manual rows that need menu interaction the hang
  prevents — not a separate signal. (Resolved — not a probe.)
