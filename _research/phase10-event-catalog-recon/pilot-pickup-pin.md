# Pilot — item_picked_up, entity-script DISPATCH pin (pure static)

Front 2 found the `OnPickup`/`OnUse`/`OnEnterArea` entity-script-event family
exists as literals and named the dispatch as `ScriptBind_Entity` /
`Entity:CallScriptFunction`, but did NOT pin the dispatch RVA and flagged the
load-bearing build-unknown: are these callbacks **globally subscribable ONCE** or
must they be **wrapped per-entity script table**? This pass pins the dispatch and
answers it, pure-static (no game launch). Every claim is READ in the owning body.

**VERDICT: STATIC PIN of the dispatch CHOKEPOINT.** The engine→Lua entity-script
call funnels through a single concrete `CScriptTable::CallFunction` slot, shared by
every entity's script table. A hook there observes every `On<X>` for every entity.

```
entity_script_dispatch (CScriptTable::CallFunction, vtable slot 22 / +0xb0)
  -> undefined8 FUN_180b9ceb4(CScriptTable* this /*rcx*/, SScriptCallDesc* desc /*rdx*/)
  @ RVA 0xb9ceb4   (file VA 0x180b9ceb4, image base 0x180000000)
  desc->funcName == *rdx == "OnPickup" / "OnUse" / ... (the callback-name filter)
```

GLOBAL-vs-PER-ENTITY verdict: **functionally GLOBAL via one shared concrete class.**
The PER-FIRE-SITE call is per-entity-script-table (`call [scriptTable_vtable+0xb0]`,
the table resolved from each entity), BUT there is ONE concrete `CScriptTable`
class — one `CScriptTable::vftable` (RVA `0x3a49c70`), installed by the ONE script-
table ctor that the ONE `IScriptSystem::CreateTable` API calls. So slot 22 is a
single global chokepoint: hooking `FUN_180b9ceb4` (or detouring vtable slot 22 of
`CScriptTable::vftable`) intercepts the dispatch for ALL entities at once — kcdx
does NOT have to wrap per-entity script tables. Filter on `desc->funcName`.

## The evidence chain (every edge read in a body)

### 1. The fire site — OnUse (the readable template for OnPickup)
`FUN_180efb548` @ RVA `0xefb548` (`_abi_180efb548.txt`, decompile in
`_scripttable_call.txt`) is a CryEngine entity-script-event fire site. Body, read:
```c
plVar1 = *(longlong **)(param_1 + 0x48);     // entity -> its script table (+0x48)
if (plVar1 != 0) {                            // PER-ENTITY guard: no table => no fire
  local_88 = "OnUse";  local_80 = "userId";   // the call descriptor on the stack:
  local_48 = FUN_180ac8fb0;                    //   funcName, paramSig, marshaller cb,
  local_50 = *(undefined4 *)(param_1 + 0x58);  //   entity id, flags=0x10, args…
  local_38 = 0x10; local_28 = param_4;
  (**(code **)(*plVar1 + 0xb0))(plVar1, &local_88);  // CALL slot +0xb0 on the table
}
```
Disasm confirms (`_abi_180efb548.txt`): `mov r8,[rcx+0x48]` ; `test r8,r8 ; je` ;
build descriptor ; `mov rcx,r8 ; mov rax,[r8] ; call qword [rax+0xb0]`. So the
dispatch is **vtable slot +0xb0 on the entity's script table**, with the callback
NAME carried in the descriptor (`*desc`). This is the mechanism that carries
`OnPickup` — same shape, different name string.

### 2. The descriptor marshaller `FUN_180ac8fb0` (RVA 0xac8fb0)
Read (`_scripttable_call.txt`): `pcVar1 = param_2[1]; ... (*pcVar1)(uVar2, param_1,
arg)` on the args-present branch, else `[*scriptTable+0x58]` (the no-arg/error
path). Confirms `&local_88` is an IScriptTable call-descriptor `{funcName,
paramSig, callbackData, callback, …}`, not an ad-hoc struct.

### 3. The dispatch slot resolves to ONE function via ONE shared class
- `IScriptSystem::CreateTable` = CScriptSystem vtable (RVA `0x3b8af70`, DB id 119)
  slot 13 -> `FUN_18071a204` @ RVA `0x71a204`. Body (`_scripttable_vtable.txt`):
  allocates via `FUN_18071ed18()`.
- The ctor `FUN_18071ed18` @ RVA `0x71ed18`. Body (`_scripttable_ctor.txt`):
  `*puVar1 = CScriptTable::vftable;` — installs the ONE concrete vtable. (Ghidra
  resolved the store to the named label; no per-instance variant.)
- `CScriptTable::vftable` @ RVA `0x3a49c70` (`_cscripttable_vtbl.txt`), slot[22]
  (off `+0xb0`) -> `FUN_180b9ceb4` @ RVA `0xb9ceb4`. **The same `+0xb0` the fire
  site calls.**

### 4. The dispatcher body — confirms it IS the Lua call dispatch
`FUN_180b9ceb4` @ RVA `0xb9ceb4` (decompile `_cscripttable_vtbl.txt`, ABI
`_abi_180b9ceb4.txt`), read:
```c
undefined8 FUN_180b9ceb4(this /*rcx*/, desc /*rdx = r14*/) {
  FUN_180b9d4b8(buf, "%s.%s(%s)", desc[2], *desc);  // formats Table.Func(params)
  FUN_18071cbf0(this);                              // begin call on the table
  FUN_18071ef54(g_scriptSys, *desc);                // push the function name (*desc)
  ... copies the param signature, sets the marshalling callback, invokes ...
}
```
The first arg read off `desc` (`*rdx`) is the function NAME; `desc[1]`=paramSig,
`desc[2]`=tableName, `desc[3..6]`=arg block, `desc[0x38]`=flags byte — exactly the
descriptor the fire site (#1) built. abi_walker prologue (`_abi_180b9ceb4.txt`):
`mov rbx,rcx` (this), `mov r14,rdx` (desc), standard `__fastcall`, returns `rax`.

## ABI (abi_walker, not prologue-guess)
`FUN_180b9ceb4` — `__fastcall undefined8 (CScriptTable* this /*rcx*/,
SScriptCallDesc* desc /*rdx*/)`. `desc` layout (read in #1 + #4):
`+0x00` funcName (const char*), `+0x08` paramSig (const char*), `+0x10` tableName
(const char*), `+0x18..` arg block, `+0x38` flags byte. Returns a result code in
`rax` (`undefined8`). Two args, both register-homed; no stack args
(`_abi_180b9ceb4.txt` shows only an `entry_rsp+0x180` access = the saved-rbx spill,
not an incoming arg).

## What the OnPickup name itself resolved to (honest scoping)
The `OnPickup` literal @ `183fb7d30` is referenced ONLY by `FUN_18026cc10`, which
is an **interned-string-constant initializer** (`FUN_1804f692c(&DAT,"OnPickup")` +
`atexit` — the same shape front 3 found for `QuestStateChanged`): it interns
`OnPickup`/`OnDelivery`/`OnProcessed` as an event-key enum group (values 1/0/2),
NOT a fire site. The OnPickup-specific fire site (the item-pickup C++ path that
calls the dispatcher with `funcName="OnPickup"`) was NOT located this pass. What
IS proven: the dispatch MECHANISM (`OnUse` fire site -> slot 22 -> `FUN_180b9ceb4`)
is the shared path every entity-script `On<X>` callback takes. `Entity:CallScript
Function` @ `183d1f690` is a FlowGraph node registration
(`CAutoRegFlowNode<CFlowNode_CallScriptFunction>`), NOT the engine dispatch — a
red herring as a hook target.

## The build-unknown — answered, with the residual live check
- **Static answer: a single global chokepoint.** One `CScriptTable` concrete class
  => one `CScriptTable::CallFunction` (slot 22, `0xb9ceb4`). Hook it once; filter
  `desc->funcName`. kcdx need NOT wrap per-entity tables.
- **What a live probe MUST still confirm (2 items, both checkable in ONE launch):**
  1. That a player **item pickup actually drives an entity-script callback through
     this slot** (i.e. the game fires `funcName="OnPickup"` — or whatever the real
     item-pickup callback name is — into `FUN_180b9ceb4`). Static located the
     mechanism for `OnUse`; whether pickup uses this entity-script path vs the C++
     `CItemSystem` path (front 2's NEEDS-LIVE-CORRELATION fallback) is runtime.
  2. **Volume/filter sanity** — `CScriptTable::CallFunction` is the dispatcher for
     EVERY script call (not just events): a global hook will see heavy traffic and
     MUST filter on `desc->funcName` (and likely an entity-class check). The probe
     logs `desc->funcName` for each fire to confirm `OnPickup`/`OnUse`/`OnEnterArea`
     appear and to size the dispatch rate (hot-path concern, `memory.md`/`logging.md`).
  Probe shape: hook `0xb9ceb4` entry, log `*rdx` (funcName) under one category tag,
  pick up an item in-game, grep the log for `OnPickup`. (Ghidra xref-DB showed 0
  refs to `CScriptTable::vftable` and only 2 to the dispatcher — the SAME xref-DB
  blind spot noted for ModManager_ctor (DB id 134); the single-class conclusion
  rests on the CreateTable->ctor->one-vftable body chain, not the xref count.)

## AP18 seed-row candidate (PENDING live-confirm — not proposed yet)
On the live confirm (item pickup fires `funcName="OnPickup"` through `0xb9ceb4`):
- `entity_script_call_dispatch -> u64 FUN_180b9ceb4(CScriptTable* this, SScriptCallDesc* desc)` @ RVA `0xb9ceb4` — the per-script-table CallFunction; `desc->funcName` (`*desc`) is the `On<X>` callback name. The global entity-script-event chokepoint.
- Supporting (vtable anchor, if a slot-detour surface is wanted instead of a fn hook): `CScriptTable::vftable` @ RVA `0x3a49c70`, slot 22 (+0xb0) is this dispatcher.

NEEDS A LIVE LAUNCH to pin (not seed-ready as STATIC-FINDABLE yet): the dispatch
FUNCTION + ABI are statically pinned (this pass), but the **anchor selection for
item_picked_up** — that a player pickup actually routes through this entity-script
slot with a knowable funcName — is runtime-bound (RTTR/item-system layer). Recording
the seed row before that correlation would assert an unverified pickup->dispatch
edge (AP2/AP18). This is STATIC-FINDABLE for the *dispatch mechanism*, NEEDS-LIVE
for the *item_picked_up anchor binding*.

## Artifacts (this dir, reuse-first / producer co-located)
- `FindEntityScriptDispatch.java` -> `_dispatch_recon.txt` / `_dispatch_clean.txt`
  — anchor literals -> referencing fns + bodies (OnUse fire site, OnPickup interner,
  Entity:CallScriptFunction flownode, ActivateOutput command).
- `ResolveScriptTableCall.java` -> `_scripttable_call.txt` — marshaller + fire-site bodies.
- `FindScriptTableVtable.java` -> `_scripttable_vtable.txt` — CreateTable (slot 13) body.
- `DumpScriptTableCtor.java` -> `_scripttable_ctor.txt` — the ctor's `*obj = CScriptTable::vftable`.
- `DumpCScriptTableVtbl.java` -> `_cscripttable_vtbl.txt` — vtable slots 0..30 + the +0xb0 dispatcher body.
- `CheckCScriptTableShared.java` -> `_cscripttable_shared.txt` — vftable/dispatcher ref counts (xref-DB blind spot noted).
- `_abi_180efb548.txt` (OnUse fire site ABI), `_abi_180b9ceb4.txt` (dispatcher ABI) — abi_walker raw.
