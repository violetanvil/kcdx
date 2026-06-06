# PROBE FIXC — does kcdx.* table registration on the adopted WHGame state trip the dual-Lua write hazard?

**Run:** `kcdx-dev_2026-06-05_17-28-46.log` (live, dev-mode, boot + save-load into
world). Clean boot, no FAULTED. THE KEYSTONE RAN LIVE for the first time
(kcdx-builds-VM, engine-adopts) — this run also proves the keystone end-to-end.

**Trust:** primary evidence (live observation + the mechanism read from PROBE Q's own
dummynode diagnostic). Settles a checkable hazard claim that a design deviation had
rested on REASONING (per results-driven.md §"a design clause asserting a runtime
mechanism is a probe target").

## The claim

The keystone registers kcdx.* tables via the STATIC-LINKED vendored Lua binder
(`RegisterKcdxTable` → `lua_newtable`/`lua_setfield`) on the adopted WHGame-built
state (game-thread first-tick path, hooks.cpp). A deviation from design §6.4
("the worker registers kcdx.* tables") was made on the reasoned fear that this
static-Lua write on a WHGame state trips the kcdx→WHGame dual-Lua sentinel WRITE
hazard FIX C guards. The probe tests whether the hazard is REAL.

## Verdict — SILENT: the hazard does NOT fire (FIX C already neutralized it)

Zero `MID_HOOK frealloc.kcdx_image_ptr` lines across the full exercise:
- `P_FIXC register_on_adopted_state` — the keystone's boot `RegisterKcdxTable(adopted L)` (static-Lua) already ran on the adopted state.
- `P_FIXC synthetic_write_done` — a deliberate static-Lua `lua_newtable`+`lua_setfield` on the adopted state, AFTER PROBE Q armed.
- `P_FIXC forced_gc_done kb_before=42434 kb_after=32261` — a full GC ran (so any embedded kcdx-image sentinel would reach frealloc).
- Plus the save-load cycle (PROBE Q's canonical pass).
- **Result: no `frealloc.kcdx_image_ptr` anywhere.** PROBE Q stayed silent through all of it.

**Mechanism (why it's silent — read from PROBE Q's own arm-time diagnostic):**
`probe_q.dummynode addr=0x161EF49FD10 ... in_kcdx_image=0` — kcdx's dummynode
sentinel is a HEAP allocation (`MEM_PRIVATE`, `module=""`), NOT a kcdx-image
(`.rdata`) pointer. That is FIX C's `setnodevector` patch working exactly as designed:
kcdx's static Lua allocates a real 1-node array for an empty table instead of writing
the static-const `.rdata` dummynode. **There is no kcdx-image sentinel to embed in the
first place** — so a static-Lua table write on the adopted state cannot trip the
hazard, regardless of which thread does the registration.

## What this settles

- **The table-registration deviation is MOOT.** The hazard does not exist for this
  operation (FIX C neutralized it). Game-thread registration (the keystone as-built)
  is safe; worker registration would be equally safe — the thread is immaterial to
  this hazard. Design §6.4's "worker registers kcdx.* tables" is corrected: the
  registration thread is a free choice (the keystone keeps the existing game-thread
  first-tick path), because FIX C makes the write sentinel-free either way.
- **Ratified on the probe, not on reasoning** — exactly the methodology
  (results-driven.md): the hazard was a checkable runtime mechanism, so it was probed,
  not argued.

## Bonus — the keystone is PROVEN live (this run also confirms P3 step 2)

- `LUA_VM_BUILD vm_built_and_intercept_armed L=0x1616D3B0990` — kcdx built the VM on
  the worker, published g_L (release), armed the engine-stamped lua_newstate Replace.
- `LUA_VM_BUILD engine_adopted_kcdx_state L=0x1616D3B0990` (game thread) — Init called
  lua_newstate; the intercept returned kcdx's state; the original never ran; NO second
  VM. The cross-thread adoption (worker release → game acquire) fired as designed.
- cap-81 all PASS: single-state (built==live==0x1616D3B0990), mainthread invariant on
  the adopted state, kcdx.* present (`_G.kcdx`), CryEngine scripts run (`_G.System`).
- cap-79 flipped to PASS (all 5 shim rows live — the pinned contract resolved).
- Clean boot + save-load, no FAULTED.

## Reusable wiring

- The hazard instrument IS PROBE Q (permanent canary): `frealloc.kcdx_image_ptr` =
  a kcdx-image sentinel reached the allocator. Silent = no such write.
- The mechanism check is PROBE Q's own `probe_q.dummynode in_kcdx_image=0` —
  confirms FIX C keeps kcdx's dummynode off the kcdx image.
- To re-test on a future change: register a static-Lua table on the adopted state
  after PROBE Q arms + force a full GC; watch for `frealloc.kcdx_image_ptr`.
- The probe site (boot first-tick, after ArmFreallocProbe) is removed post-run; the
  finding is here. PROBE Q stays (permanent).
