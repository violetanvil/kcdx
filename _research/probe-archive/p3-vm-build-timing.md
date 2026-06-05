# PROBE P3 — VM-build timing: force-load vs worker-thread build

**Run:** `kcdx-dev_2026-06-05_15-38-19.log` (live, dev-mode). Clean boot, reached
menu (suite 179/213, normal). The worker force-load arm was gated OFF (observation
only — a blind force-load could perturb the CREATE_SUSPENDED boot).

**Trust:** primary evidence (live observation). Settles whether the design §6.2
"force-load WHGame from kcdx DllMain" is achievable, against ground truth (not
inferred from code comments).

## Question

Design §6.2 says kcdx force-loads WHGame via `LoadLibraryW` in its DllMain to build
the Lua VM early. The verified boot architecture (src/dllmain.cpp) is: kcdx is
injected into a CREATE_SUSPENDED game; at kcdx's DllMain WHGame is not mapped and
`LoadLibraryW` is loader-lock-forbidden; kcdx's WORKER thread waits for the game to
map WHGame (`WaitForGameDll`). §6.2 was never reconciled with this (it predates the
P1 v2 finding). Probe observes the real constraint.

## Observed (P3_PROBE lines + cross-referenced engine timestamps)

```
[15:38:19.174] P3_PROBE worker_entry whgame_already_mapped=0 forceload_enabled=0 wall_ms=448226171 tid=5716
[15:38:19.301] P3_PROBE vm_buildable whgame_mapped=1 lua_newstate_resolved=1 wall_ms=448226296 tid=5716
```
Cross-referenced (same log):
```
[15:38:21.484] MOD_ABSORB ctor_bracket_complete ... tid=34288   (game-main thread; ModManager ctor, precedes CScriptSystem::Init + boot opens)
```

## Answers (against the pre-committed outcome map)

- **WHGame mapped at kcdx DllMain? NO — force-load there is impossible.** `worker_entry
  whgame_already_mapped=0`: WHGame is not mapped even when the WORKER thread starts
  (which runs AFTER DllMain). So it is definitely not mapped at DllMain. The
  loader-lock-forbidden `LoadLibraryW`-in-DllMain of §6.2 cannot work. CONFIRMED.
- **Is a force-load needed? NO.** The worker's `WaitForGameDll` waits for the game's
  own load; WHGame maps ~125 ms after worker entry (vm_buildable at 448226296 vs
  worker_entry 448226171). The game maps WHGame itself; the worker just waits. A
  force-load buys nothing (and the worker arm was correctly not run — it could
  perturb the suspended boot).
- **Does the worker VM-build precede engine init / the boot opens? YES, by ~2.2 s,
  cross-thread.** Worker vm_buildable = 15:38:19.301 (tid 5716, the worker); the
  game-main-thread engine init (`ctor_bracket_complete`, upstream of
  `CScriptSystem::Init` and the boot-asset opens) = 15:38:21.484 (tid 34288). The VM
  is buildable on the worker well before the engine's Init runs. Matches P1 v2.

## Caveat (honest scope)

The probe's own `boot_open.first` P3_PROBE marker did NOT fire this run (its latch in
`AdjustFileNameResolver` did not catch a boot open — the boot opens this run likely
went through the HOOK 2 `FOpenLooseOverlay` lane, or the latch placement missed
them). So the VM-vs-boot-open ordering is established from the ENGINE-INIT timestamp
(`ctor_bracket_complete`, which is upstream of `CScriptSystem::Init` and the boot
opens) rather than the boot-open marker directly. The inference is sound (engine init
precedes the boot opens; the VM point is 2.2 s earlier and on the worker) and
corroborates P1 v2's direct boot-open finding — but it is an inference from the
upstream init timestamp, not a direct boot-open observation this run. If a direct
boot-open-vs-VM datum is wanted, the P4 early-slot work re-instruments the boot-open
path anyway (the gate's order-inversion regression IS that direct observation).

## Conclusion — the §6.2 correction

The VM-build mechanism is: **kcdx does NOT force-load WHGame (impossible — loader
lock + CREATE_SUSPENDED). The worker thread waits for the game to map WHGame
(`WaitForGameDll`), then — at the post-`WaitForGameDll` / post-`refdb::Open` point
where WHGame is mapped and `lua_newstate` resolves — builds the one VM + installs the
`lua_newstate`-callee intercept. This worker point precedes the game-main-thread
engine Init by ~2 s, so the VM is built in time; the cross-thread gate (design §5)
orders worker-builds-VM → game-thread-adopts.** §6.2's "LoadLibraryW in DllMain" is
dropped. P3 step 2 becomes "wire the worker-thread VM-build point + confirm the
existing LDR before_game apply", NOT "force-load from DllMain".

The existing LDR before_game apply (`ldr_notify.cpp` `ApplyEntriesForModule` per
mapped module + the WHGame-loaded `SetEvent` gate) ALREADY works — it is not new P3
work; P3 step 2 partly mis-described building what exists.

## Reusable wiring

- `whgame_already_mapped` = `GetModuleHandleW(L"WHGame.dll") != nullptr` at worker
  entry (ctx B, no loader lock).
- vm_buildable point = worker, post-`refdb::Open()` success, where
  `refdb::ResolveAddrByName("lua_newstate") != 0`.
- The worker force-load arm (`kP3WorkerForceLoad`) stays OFF — a force-load is not
  needed and is unsafe blind.
- Cross-thread fact: worker = a worker tid (5716 this run); engine Init + boot opens
  = the game-main tid (34288 this run). ALWAYS log tid.
