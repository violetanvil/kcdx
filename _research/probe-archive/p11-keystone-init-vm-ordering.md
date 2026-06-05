# PROBE P11 — keystone: CScriptSystem::Init / lua_newstate observation + boot-open ordering

**Trust:** primary evidence (live observation). Two runs: v1 (crashed, partial) +
v2 (clean, complete). **The headline correction is in the v2 section: the
VM-build→boot-open ordering is a CROSS-THREAD dependency that is currently UNGATED —
the design §5 ordering guard was a timing aspiration (a defect) and is now a
mandatory event gate.** Read the v2 section for the settled answers.

**Run v1:** `kcdx-dev_2026-06-05_13-23-59.log` (live, dev-mode). Crashed at boot (the
probe's Init detour faulted — see "Probe defect" below), but the §4.3 boot-open
observation logged BEFORE the fault and is valid.

## Questions (design §4) + observed answers

### §4.3 — boot-asset swap reachability (KI-0005, the user-required capability) — **HALT**

Observed, flat:
- `[13:24:01.858] P11_PROBE boot_open.first init_seq_at_first_boot_open=0 init_preceded_boot_open=0 wall_ms=440175014`
- `[13:24:07.542] P11_PROBE ss_init.entry init_seq=1 self=0x278EEB6C250 boot_open_seen_before_init=1 wall_ms=440180698`

**Ground truth: the engine opens its first boot asset ~5.7 s BEFORE `CScriptSystem::Init`
creates the Lua VM** (`init_preceded_boot_open=0` at the first boot open; then
`boot_open_seen_before_init=1` at Init entry — 440180698 − 440175014 ≈ 5684 ms). The
boot opens are real engine `CCryPak::FOpen`/`AdjustFileName` opens (the production
overlay hooks `engine.ccrypak_fopen` @ 0x7FF8AE0214A0 + `engine.ccrypak_adjustfilename`
@ 0x7FF8ADC2205C were live and observing real opens — L2217/L2158).

**Meaning (design §4.3 outcome 2):** the early-slot register would fire AFTER the
boot open — the ordering is wrong for an early Lua slot keyed to the VM-up point
(`CScriptSystem::Init`). The mechanism's VM-up point does not precede the boot-asset
open it must beat. This is the HALT the phase-01 gate pre-committed: the
mechanism as designed does NOT deliver the KI-0005 boot-asset Lua swap, because the
VM the early slot needs comes up ~5.7 s too late for the boot assets opened at
`CSystem::Init`.

### §4.4 — early-slot timing window

The VM-up point (`CScriptSystem::Init`, init_seq=1) is ~5.7 s after the first boot
asset open. An early Lua slot tied to the VM-up point has NO window before the boot
opens. (Raw timing observation; the candidate-A-vs-B slot decision is moot for boot
assets until the VM-up-before-boot-open problem is resolved — surfaced to the user.)

### §4.1 / §4.2 — intercept-point safety + single-VM — NOT cleanly observed this run

`ss_init.entry` fired (init_seq=1, self=0x278EEB6C250), but the probe's Init detour
faulted at `+0x3F` into Init before `ss_init.post` / the `lua_newstate.return`
single-VM read could log cleanly. So §4.1 (does Init read a virgin-state field) and
§4.2 (mainthread self-pointer / single allocation) were NOT answered this run — they
re-run after the probe-detour defect is fixed. (§4.1's static half — seed row 121's
documented Init sequence with no read-branch — still stands as static evidence.)

## Probe defect (instrumentation, not mechanism — dies when the probe is removed)

`FAULTED code=ACCESS_VIOLATION rip=0x7FF8AF008F77 module=WHGame.DLL` —
`0x7FF8AF008F77 − CScriptSystem::Init(0x7FF8AF008F38) = +0x3F`. The AV is INSIDE Init,
~63 bytes in, ~0.5 s after the probe's `ss_init.entry` post-hook logged. The probe's
detour on `CScriptSystem::Init` (a non-trivial `__fastcall` member function)
corrupted Init's frame on return — a calling-convention / trampoline-shape defect in
the Init detour, NOT a finding about the kcdx-owns-VM mechanism. The downstream
`ccrypak_fopen FAULTED_FIRE` flood is the unhandled-exception filter unwinding, not
the cause. Fix: the Init detour must preserve Init's exact frame (mirror how the
production `hook_chain::AddCEngine` Around-hook trampolines a member function, rather
than a bare MinHook `__fastcall` detour) — OR observe Init via a non-detour method
(a one-shot breakpoint-style read, or hook only `lua_newstate` which is a cleaner C
function, and infer Init's behavior from the lua_newstate return + the static
disassembly).

## Reusable wiring (reconstruct the probe from here, not from source)

- Resolve targets by canonical name: `refdb::ResolveAddrByName("CScriptSystem_Init")`
  (id 121) + `"lua_newstate"` (id 114). NEVER a hardcoded RVA.
- Arm from `hooks::Install()` (worker thread, post-WHGame-map, MinHook up).
- Boot-open observation: call a `P11_NoteBootOpen()` from the asset-overlay hooks
  (HOOK 1 `AdjustFileNameResolver` + HOOK 2 `FOpenLooseOverlay`) right after each
  `RecordBootOpen(key)` — the ki0005-resolver-dds-observer recipe.
- Log under category `P11_PROBE` with `LOG_DEBUG_KV` (qualified `log::KV(...)`).
- Outcome maps pre-committed at the site, flat, per design §4.
- **The Init detour is the defect — do NOT reuse its bare-MinHook shape; use a
  frame-preserving trampoline or a non-detour read.**

## Run v2 — clean, complete (the settled keystone answers)

**Run:** `kcdx-dev_2026-06-05_13-39-31.log` (live, dev-mode). No fault (the v2 probe
hooks `lua_newstate` — a clean C function — instead of detouring the member function
`CScriptSystem::Init`; the v1 defect is gone). Reached the main menu, suite ran
normally. The four `P11_PROBE` lines:

```
[13:39:31.554] dllmain_vm_point wall_ms=441098406 whgame_already_loaded=1 lua_newstate_resolved=1   (worker thread, tid 53192)
[13:39:33.465] boot_open.first  wall_ms=441100312 tid=46320                                          (GAME MAIN thread)
[13:39:38.145] newstate.return  newstate_call_seq=1 L=0x223ADE5C6C0 l_G=0x223ADE5C778 storedebug_g22=1 mainthread_self=1 tid=46320
```

### §4.1 intercept-point safety — SETTLED: the narrow hook (the lean) is SAFE

`storedebug_g22=1` at the `lua_newstate` return — the state IS virgin (storedebug=1)
when created. Combined with the static evidence (seed id 121: `CScriptSystem::Init`
*overwrites* storedebug→0 with NO read-branch on a virgin field), §4.1 outcome 1
holds: hooking `lua_newstate` (callee) and letting the engine run its own
`storedebug=0`/openlibs/registrars on kcdx's state is safe. The intercept-point lean
is confirmed.

### §4.2 single-VM — SETTLED: one VM, self-pointer holds

`newstate_call_seq=1` (exactly one `lua_newstate` call) + `mainthread_self=1`
(`[L->l_G+0xB0]==L`). Outcome 1.

### §4.3 boot-swap reachability — CORRECTED FINDING: a cross-thread UNGATED dependency (the design defect)

`dllmain_vm_point` is on the **worker thread (tid 53192)**; `boot_open.first` and the
engine's `lua_newstate` are on the **game main thread (tid 46320)**. **Two threads.**
The wall-clock "VM-point precedes boot-open by ~1.9s" is a CROSS-THREAD timing
comparison — NOT a guarantee, and leaning on it is the race the user flagged.

Verified against source (not just the log): the boot-open path
(`asset_overlay.cpp` HOOK 1 `AdjustFileNameResolver` + HOOK 2 `FOpenLooseOverlay`)
calls `RecordBootOpen(key)` and proceeds with "the Lua VM not yet up" — it does NOT
`WaitForSingleObject` on any readiness event. The existing events gate OTHER handoffs:
`g_whgameLoadedEvent` (worker waits for WHGame mapped), `g_kcdxReadyEvent` (game-thread
`HookedCtor` waits for the worker's enabled-list build). **NEITHER gates the
boot-asset open relative to the VM-build.** So the VM-build→boot-open ordering is
currently UNGATED — a cross-thread race.

**This made the design §5 "ordering guard" a DEFECT** (it stated the ordering as
timing — "must complete BEFORE" — and gestured at `g_kcdxReadyEvent` by analogy, with
no labeled mandatory gate). Per the user's rule (multiple threads allowed ONLY when a
gate clearly stops the dependent thread until the other finishes; if not clearly
labeled in the design, it is a defect to FIX, not defer), the design was corrected
2026-06-05: §5 now mandates an explicit happens-before EVENT GATE (the early slot
signals a new readiness event; the boot-open path waits-and-BLOCKS on it; timing-based
ordering FORBIDDEN; a new edge — `g_kcdxReadyEvent`/`g_whgameLoadedEvent` do not cover
it — added in P3/P4; an order-inversion regression). §4.3's outcome map + the P4
step-1 step doc were swept to match.

### §4.4 early-slot window — moot as a timing window; it is a gate

There is sequence-room between the worker's VM-build point and the game thread's boot
open, but the early-slot correctness is the GATE (above), not the window size. The
candidate-A-vs-B slot shape decision (reuse `plugin.lua`-early vs new `lua_before`)
is unaffected by this correction and remains the §4.4/§5 probe-gated decision for P4.

## Reusable wiring (reconstruct the probe from here, not from source)

- Resolve targets by canonical name: `refdb::ResolveAddrByName("CScriptSystem_Init")`
  (id 121) + `"lua_newstate"` (id 114). NEVER a hardcoded RVA.
- Arm from `hooks::Install()` (worker thread, post-WHGame-map, MinHook up).
- Boot-open observation: call a `P11_NoteBootOpen()` from the asset-overlay hooks
  (HOOK 1 `AdjustFileNameResolver` + HOOK 2 `FOpenLooseOverlay`) right after each
  `RecordBootOpen(key)` — the ki0005-resolver-dds-observer recipe.
- `dllmain_vm_point`: place at the earliest LOADER-SAFE pre-`CSystem::Init` point —
  the worker thread right after `refdb::Open()` (NOT kcdx.dll DllMain: WHGame is not
  mapped there under `CREATE_SUSPENDED` injection, `LoadLibraryW` is loader-lock-
  forbidden, refdb is not open). Observe `whgame_already_loaded` + resolve
  `lua_newstate`; do NOT call it (a 2nd state = the dual-Lua hazard).
- **§4.1/§4.2: hook `lua_newstate` (clean C fn) via the `ArmFreallocProbe` MinHook
  shape — NEVER a bare-MinHook detour on `CScriptSystem::Init` (the v1 crash, AV at
  Init+0x3F). §4.1's read-branch half is static evidence (seed id 121).**
- **ALWAYS record the thread id (`tid`) on every observation — the cross-thread fact
  is the whole point; a same-thread assumption is what hid the race in v1's framing.**
- Log under category `P11_PROBE` with `LOG_DEBUG_KV` (qualified `log::KV(...)`).

## Settled — the keystone is resolved (the §4 answers are folded into the design)

- §4.1: narrow hook (`lua_newstate` callee) safe — the lean holds.
- §4.2: one VM, self-pointer invariant holds.
- §4.3: the VM-build→boot-open dependency is cross-thread + was ungated → the design
  §5 ordering guard is fixed to a mandatory event gate (P3/P4 builds the new edge).
- §4.4: the slot-shape decision is unaffected; it stays a P4 probe-gated call.

P2–P6 build against the CORRECTED design. P3 (force-load + adopt) builds the narrow
`lua_newstate`-callee intercept (§4.1 safe); P4 (early slot + boot swap) builds the
mandatory event gate (§5) + the order-inversion regression.
