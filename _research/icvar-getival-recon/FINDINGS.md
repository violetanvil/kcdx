# Finding — ICVar accessor slots: `GetIVal` slot 2 (+0x10), `GetFVal` slot 4 (+0x20)

Verified 2026-06-03 against `WHGame.dll` (release_1_5_1164953_841), fresh Ghidra +
RTTI walk + independent gated body-read verifiers (both PROCEED). These are the
accessors that read a CVar's stored value off the `ICVar*` that `IConsole::GetCVar`
(seed id 16, vtable[23], 0x009DF818) returns. Two new entities back `kcdx.cvar.*`:
`ICVar_GetIVal` (slot 2) and `ICVar_GetFVal` (slot 4). The int section is below; the
float section follows it (§"GetFVal").

## The verified fact (ready for the `ICVar_GetIVal` seed entity, AP18-gated)

- **Slot:** 2 · **Offset:** +0x10 · **Signature:** `i32 (ptr self)` __thiscall (returns
  `int`, takes only `this`).
- **Concrete class:** `CXConsoleVariableInt` (RTTI `.?AVCXConsoleVariableInt@@`,
  name string @ 0x184a48128). Vtable @ **0x183a7e638** (reached via the COL at
  0x184173660 whose `+0xC` disp32 == TD_start−imageBase).
- **GetIVal body** (slot 2 target @ **0x181a72450**, size 4 bytes):
  ```c
  undefined4 FUN_181a72450(longlong this) { return *(undefined4 *)(this + 0x48); }
  ```
  A pure 32-bit field read of `this+0x48` — the stored CVar int. NOT GetType/GetFlags.
- **Sibling contrast** (slot 3 = `GetI64Val`, +0x18, @ 0x181a736b0):
  ```c
  longlong FUN_181a736b0(longlong this) { return (longlong)*(int *)(this + 0x48); }
  ```
  Reads the SAME backing field `this+0x48`, sign-extended to int64 — confirms slot 2
  is specifically the int-width getter and slot 3 its int64 sibling (the canonical
  GetIVal / GetI64Val pair).

## The evidence chain (each link read in a body, not inferred — AP3, §3.5/§4.5)

1. **Canonical order** (predecessor sig `muyuanjin-kcd2db/.../IConsole.h`): `struct ICVar`
   declares `~ICVar`(0), `Release`(1), `GetIVal()→int`(2), `GetI64Val()→int64`(3),
   `GetFVal()→float`(4), `GetString()`(5). GetIVal at slot 2. *(tier-3 LEAD only — AP3.)*
2. **Two independent callers** call the `ICVar*` at vtable+0x10 with only `this` and use
   the int result (`_caller_bodies_raw.txt`):
   - `FUN_180d1b7d8` @ 0x180d1b7d8 (the "pure GetIVal reader", file-search mode):
     `GetCVar("sys_PakPriority")` → `[0x10]()` → `int iVar3` → `iVar3 == 0` branch.
     Disasm: `CALL [RAX+0xb8]`(GetCVar) → `MOV RDX,[RCX+0x10]` → `MOV RCX,RAX` (this-only)
     → `CALL RDX` → `TEST EAX,EAX` / `JZ` (consumes EAX as int).
   - `FUN_180e384d8` @ 0x180e384d8 (REGISTER_CVAR2-by-ref helper): `GetCVar(name)` →
     `param_5 = [0x10]()` (capture int) → later `[0x38](param_5)` (`SetIVal` restore).
     A get/set int pair.
3. **The accessor body itself** — the gate read (`_cvarint_vtable_slot2_raw.txt`): the
   CXConsoleVariableInt slot-2 body returns `*(int*)(this+0x48)` (above). This is what
   makes the slot GetIVal, not merely *positioned* where GetIVal should be.

`DAT_18492b8a8` (the IConsole* slot used at the GetCVar callsite) is `gEnv->pConsole`
— cross-confirmed: the static AddCommand wrapper @ 0x180b99098 uses the same slot, and
`[0xb8]` = GetCVar matches seed id-16 vtable[23] (23×8 = 0xB8).

## Binary vs canonical — no divergence here

Unlike `IConsole::AddCommand` (seed id 13: slot 33, NOT canonical 32), KCD2's **ICVar
accessor block is NOT shifted** — GetIVal sits at the canonical slot 2. Verified against
the binary, not assumed from the header (AP3).

## Gate (research-disassembly §4.5 — this becomes a seed entity)

An independent fresh-frame verifier re-read the owning bodies cold (the synthesizer's
leaning WITHHELD), via its own RTTI route, and returned **PROCEED**: slot 2, +0x10,
`i32 (ptr self)` __thiscall, all three facts reproduced at the disasm level. Concerns:
none. The `this+0x48` field offset is NOT part of the claim (it can shift per build) —
the verified facts are the slot index, the offset, the signature, and the getter shape.

## GetFVal — ICVar slot 4 (+0x20), `f32 (ptr self)` __thiscall

Same method, same day, gated PROCEED. The float-read accessor for `kcdx.cvar.get_float`.

- **Slot:** 4 · **Offset:** +0x20 · **Signature:** `f32 (ptr self)` __thiscall (returns
  the stored float).
- **Concrete class:** `CXConsoleVariableFloat` (RTTI `.?AVCXConsoleVariableFloat@@`,
  name @ 0x184a48058 → COL @ 0x184173688 → vtable @ **0x183ab7858**).
- **GetFVal body** (slot 4 target @ **0x181a73680**, size 6):
  ```c
  undefined4 FUN_181a73680(longlong this) { return *(undefined4 *)(this + 0x48); }
  ```
  Returns the raw 4-byte field at `this+0x48` — the stored float bit pattern.
- **Field-type proof** (why `f32`, not Ghidra's default int): on the SAME float class,
  slot 2 (GetIVal @ 0x181a736d0) = `return (int)*(float*)(this+0x48)` and slot 3
  (GetI64Val @ 0x181a73690) = `return (longlong)*(float*)(this+0x48)` — BOTH read
  `this+0x48` AS a float. So the backing field is a float; slot 4 returns it natively.
  The slot semantics are stable across concrete classes: slot 2 = int-typed read, slot
  4 = float-typed read; the FIELD type differs per class (int class stores int, float
  class stores float), the SLOT contract does not.
- Gate (§4.5): independent verifier re-read cold via its own RTTI route → **PROCEED**,
  concerns none, the `f32` return justified by the field being float (slots 2/3 read it
  via `*(float*)`), not by auto-typing.

## Status — both verified + gated; AP18 approval: GetIVal APPROVED, GetFVal PENDING

Provenance for TWO new Address Library entities backing `kcdx.cvar.*`. The recording is
the maintainer-tool flow's job (DB-owner lane); per `data/seeds/policy.md` §"Test plugin
requirement" both land in the SAME unit of work as the `cap-NN` test plugin. The
maintainer-tool handoff spec (exact fields, the repo's IConsole-method recording
convention) is the canonical source — see the feature's handoff summary.

**Recording shape (repo convention — `kind=function`, NOT `vtable_index`):** every
existing IConsole/ICVar vtable METHOD (GetCVar id16, AddCommand id13, ExecuteString
id15, PrintLine id150/151) is recorded `kind=function` with the concrete slot-target
RVA + signature, the vtable slot in `notes`. `vtable_index` is reserved for pure
slot-INDEX constants with no callable target (the ids-3000+ convention). So both new
entities record `kind=function`:

- **`ICVar_GetIVal`** (AP18 APPROVED 2026-06-03) — rva `0x181a72450`, sig `i32 (ptr self)`,
  notes: *"ICVar::GetIVal() -> int. __thiscall (rcx=ICVar*). vtable[2] / +0x10. Reads the
  CVar's stored int. CXConsoleVariableInt vtable @0x183a7e638 slot-2 returns
  *(int*)(this+0x48); slot-3 GetI64Val reads same field as int64. ICVar* from
  IConsole::GetCVar (id 16). Canonical slot, NOT shifted (unlike AddCommand). Gated."*
- **`ICVar_GetFVal`** (AP18 PENDING — surfaced for the user with this float verification)
  — rva `0x181a73680`, sig `f32 (ptr self)`, notes: *"ICVar::GetFVal() -> float.
  __thiscall (rcx=ICVar*). vtable[4] / +0x20. Reads the CVar's stored float.
  CXConsoleVariableFloat vtable @0x183ab7858 slot-4 returns the float field at
  this+0x48 (slots 2/3 read it via *(float*), proving the field type). ICVar* from
  IConsole::GetCVar (id 16). Canonical slot. Gated."*

Per `address-library.md` New-game-version: each entity also needs `valid_from_version =
1.5.1164953`, `last_verified_at_version = 1.5.1164953`, `verified_by = VioletAnvil`,
`verified_date = 2026-06-03`, `evidence_kind = maintainer_ghidra` (upgrades to
`live_test_plugin` once the cap-NN plugin exercises it).
