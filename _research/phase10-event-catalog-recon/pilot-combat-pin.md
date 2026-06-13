# Pilot — combat-state TRANSITION site, pure-static pin attempt

Task: pin a verified address+ABI for the combat-state TRANSITION site (the function
that WRITES/changes the combat-state value `IsInCombat` reads, and/or fires the
`C_CombatSignalWithNewValueTrait` change-signal) — the hook target for
`kcdx.on("combat_started"/"combat_ended")`. Pure static, no game launch.

**VERDICT: NO STATIC PIN.** The combat-state value is read by a verified getter
(`FUN_181a7dac0`, `return *(byte*)(prop+8)`), but its WRITER is an inlined templated
`C_ModelProperty::SetValue` with **no dedicated function or vtable slot** — the value
write is one of 2,523 indistinguishable `mov byte [reg+8], r8` sites, and the change-
signal is dispatched through a **runtime-bound listener list**, not a fixed fire fn.
A live probe must hook the getter and edge-detect, OR runtime-trace the listener.
This CONFIRMS and HARDENS front-1's F5 suspicion with the actual property layout read.

Artifacts (this dir): `CombatPropVtable.java` / `_combat_propvtable_0x183b091e0.txt`
(the property vtable + slots, decompiled), `CombatPropCtor.java` / `_combat_propctor.txt`
(the combat-actor model ctor — the whole property array), `CombatStateWrite.java` /
`_combat_statewrite.txt` (getter-consumers + model alloc/dtor), `_combat_trace_setter_FUN18245e7c0.txt`
(the prior TraceSetterAt0xB60 run — disproves its writer candidate). Scripts also in
`third-party-ghidra/ghidra_scripts/`.

---

## What was VERIFIED this pass (each `<fact> — <evidence>`)

### V1 — The prior writer candidate `FUN_18245e7c0` is NOT the combat writer; it is CryEngine `System::Shutdown`
The un-run `TraceSetterAt0xB60.java` (front-1 handoff) named `FUN_18245e7c0` as the
`mov [rdi+0xB60], rsi` writer. RAN it (`_combat_trace_setter_FUN18245e7c0.txt` L1-21):
the function's first call is `FUN_1804d4510("...CryEngine\\CrySystem\\System.cpp", 0x2f2,
"System Shutdown")` — it is the engine **shutdown teardown** nulling subsystem pointers;
its `0xB60` is an unrelated field in a different object's destructor cleanup. The raw
`mov [obj+0xB60]` byte-scan that produced this candidate hits a common offset; it is a
false positive. **The `[+0xB60]` writer was never actually located by front-1's script.**

### V2 — The three `add rcx,0x0B60 ... call [rax+8]` sites are all CONSUMERS (getter callers), none a writer
`TraceSetterAt0xB60` decompiled all three (`_combat_trace_setter_FUN18245e7c0.txt`):
- `FUN_1805605b8` (id-7 site, RVA 0x5605BC): `return (getter()) == '\x02'` — read+compare.
- `FUN_180566040` (id-8 site, RVA 0x566040): `return (getter()) == '\x01'` — read+compare.
- `FUN_1805616e8` (id-5/6 site): calls the getter inside the **outfit-swap** handler to
  gate `"cant_change_outfit_in_combat"` (L22-42) — a read, then a UI block. Not a write.

Confirms front-1 F2/F3: every `+0xB60` site READS via the getter vtable slot; none writes.

### V3 — The combat-state property's vtable is RVA 0x3b091e0; its getter is slot[1] = `FUN_181a7dac0`
Located the bool `C_ModelProperty` vtable via RTTI COL search (type descriptor base
`0x184AFFAF0` = the rtti-dump name VA `0x184AFFB00` minus the 0x10 TypeDescriptor header;
COL `0x18415ef30` sig==1, pTypeDescriptor RVA 0x4affaf0; vtable = COL-ptr+8). Vtable VA
**`0x183b091e0`** (RVA `0x3b091e0`), 4 slots (`_combat_propvtable_0x183b091e0.txt`):
- slot[0] `FUN_18275bdf8` — scalar-deleting dtor (its body literally prints the type:
  `C_ModelProperty<bool, C_StandardDefaultValueTrait<bool>, C_CombatSignalWithNewValueTrait<bool, I_CombatActor*>, C_CombatModelNoTrace, C_NoSaveLoad, C_CombatActorModelOwnership>::vftable`).
- slot[1] `FUN_181a7dac0` = **the getter**: `undefined1 FUN_181a7dac0(longlong param_1){ return *(undefined1*)(param_1 + 8); }` — **192 direct refs**. This IS the `call [rax+8]` the IsInCombat chain hits. **The combat-state VALUE byte is at `[prop+8]`.**
- slot[2] `FUN_18275f83c` — a `ToString`: returns `&DAT_183b773ec` or `&DAT_183db32a4` based on `[prop+8]` (debug-name accessor, NOT a setter).
- slot[3] `FUN_181a72b60` — `return param_1 + 0x18` (accessor returning the trait/signal sub-object pointer; NOT a setter).

**There is NO SetValue slot in this vtable.** The property is small (get / tostring /
sub-object accessor / dtor). Setting the value is a NON-virtual templated member.

### V4 — The combat-actor MODEL is a flat array of `C_ModelProperty` members; the getter reads the value, the graded thresholds imply the state property is an ENUM
The model ctor `FUN_1810eea6c` (`_combat_propctor.txt` L1-90) constructs a sequence of
properties. `param_1[0]` = `C_ModelProperty<bool, ..., C_CombatSignalWithNewValueTrait<bool>, ...>`;
`param_1[6]` = `C_ModelProperty<E_CombatActorStateId::Type, ..., C_CombatSignalWithOldValueTrait<E_CombatActorStateId>, ...>`
— **the graded combat-state enum** (matches the getter's `cmp al,1` / `cmp al,2`
thresholds, which are 3-state, not bool). Model size = `0xd10` bytes (alloc in
`FUN_18091da3c` L22). The `[component+0x90]+0xB60` getter chain points at the enum
state property's slot within the model; the getter `FUN_181a7dac0` reads its `[+8]`
value byte regardless of which property (bool vs enum) — the read function is shared.

### V5 — The change-signal is dispatched through a runtime-bound listener, not a fixed fire fn
The `C_CombatSignalWithNewValueTrait` / `WithOldValueTrait` is the signal-on-change
mechanism. slot[3] of the property returns `[prop+0x18]` — the trait/signal sub-object.
A signal fire walks that sub-object's listener collection and invokes each listener's
virtual callback (same shape front-3/pilot-perk found for the perk on-changed notify:
`call [[listener]+slot]` on a runtime-registered delegate). There is no statically-named
"combat signal dispatcher" function — the dispatch is `call [vtable+N]` on objects bound
at runtime. (Front-1 F4/F5; re-grounded here against the actual property vtable.)

### V6 — No static writer is distinguishable: 2,523 `mov byte [reg+8], r8` sites
Scanned all of `.text` for the value-write shape `mov byte [reg+8], <r8>` (the inlined
SetValue writes `[prop+8]`): **2,523 sites**. `[+8]` is far too common an offset to
identify THIS property's writer statically — the writer's object identity (which `prop`
pointer flows in) is only resolvable by observing the runtime value. No single-variable
static discriminator exists.

---

## Why NO STATIC PIN (the exact failure, falsifiably)

1. **The value has no setter function/slot to hook.** It is written by an inlined
   templated `C_ModelProperty::SetValue` (V3: not in the 4-slot vtable; V6: one of 2,523
   `[+8]` writes). A `kcdx.hook` needs a fixed function entry or vtable slot; SetValue is
   neither — it is inlined at each transition call site, and those sites are not
   statically distinguishable from any other `[obj+8]` byte write.
2. **The change-signal is a runtime-bound listener dispatch** (V5), not a flat fire fn.
   Hooking "the combat signal" would require the dispatcher address, which does not exist
   as a static symbol — the dispatch is `call [delegate-vtable+N]` on runtime-registered
   objects.

This is NOT honest-uncertainty masking a missed read — every load-bearing claim was read
in the owning body THIS pass (V1 shutdown string, V2 three consumer bodies, V3 four vtable
slot bodies, V4 the model ctor array, V6 the exhaustive write-shape scan). The static
surface genuinely terminates at "read the value" with no "write the value" counterpart.

## What a LIVE probe must hook instead (the closing step)

The combat-state surface is fully a GETTER. Two live paths, both needing one launch:

- **Hook the getter `FUN_181a7dac0` (RVA `0x1a7dac0`) and edge-detect.** It returns the
  combat-state value byte; a `kcdx.hook` on it that remembers the prior value per actor
  fires `combat_started` on the false→true (0→≥1) edge and `combat_ended` on the ≥1→0
  edge. The `cmp al,1` vs `cmp al,2` thresholds (V2) say the state is graded — edge-detect
  on the boolean "in combat at all" (≥1) for start/end. **Caveat:** this getter is on the
  read path (192 callers), called frequently — the hook is an edge-detector over polled
  reads, not a true transition event; acceptable for a v1 `combat_started`/`combat_ended`
  but it fires on read cadence, not on the actual SetValue moment.
- **OR runtime-trace the writer.** Set a hardware/data write-watch on a live combat-actor's
  `[prop+8]` (prop resolved at runtime via `[combatComponent+0x90]+0xB60`), trigger combat
  in-game, capture the return address that wrote it → THAT is the real transition site /
  the inlined SetValue's call site, pinnable as a hook target only after this observation.

## AP18 seed-row readiness

**No seed row proposed.** Per the no-invented-address bar (AP2/AP18): the getter is
already covered by the IsInCombat call-site rows (ids 5/6/7/8); the transition WRITER has
no static address to record. The getter function entry `FUN_181a7dac0 @ RVA 0x1a7dac0`
IS a verified fact (V3) and could become a row IF the design elects the getter-hook +
edge-detect path — its ABI is trivial (`__fastcall undefined1 GetValue(void* prop /*rcx*/)`,
returns the state byte in `al`; arg1 only, read in the body V3, no abi_walker needed for a
one-arg `mov al,[rcx+8]; ret`). That is a **getter** row, not a transition-site row; whether
it is the right `combat_started` anchor is the design+live-correlation call, not a static fact.

**Still needs a live launch to even pin the transition site.** The getter address is
static-ready; the actual write/transition site is not pinnable without the runtime
write-watch trace above.
