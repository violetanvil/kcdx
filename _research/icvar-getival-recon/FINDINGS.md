# Finding — `ICVar::GetIVal` is vtable slot 2 (+0x10), `i32 (ptr self)` __thiscall

Verified 2026-06-03 against `WHGame.dll` (release_1_5_1164953_841), fresh Ghidra +
RTTI walk + an independent gated body-read verifier (PROCEED). This is the accessor
that reads a CVar's stored 32-bit integer value off the `ICVar*` that
`IConsole::GetCVar` (seed id 16, vtable[23], 0x009DF818) returns.

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

## Status — verified, AWAITING AP18 user approval before any seed row

This finding is the provenance for a NEW Address Library entity `ICVar_GetIVal`. Per
AP18 the row is NOT written until the user signs off on the specific entity. The seed
entry, ready to paste on approval:

- `address_names_seed.csv`: a new id (highest+1, append-only) `ICVar_GetIVal`, notes:
  *"ICVar::GetIVal() -> int. __thiscall (rcx=ICVar*). vtable[2] / +0x10. Reads the CVar's
  stored 32-bit int value. Verified against the binary: CXConsoleVariableInt vtable
  @0x183a7e638 slot-2 body returns *(int*)(this+0x48); slot-3 GetI64Val reads the same
  field as int64. The ICVar* comes from IConsole::GetCVar (id 16). Canonical CryEngine
  slot — NOT shifted in this build (unlike AddCommand). Gated body-read verified."*
- `address_versions_seed.csv`: version `1.5.1164953`, `signature = "i32 (ptr self)"`.
  RVA is NOT applicable as a static row — GetIVal is a vtable-slot fact (the concrete
  body @ 0x181a72450 is one class's impl; the SLOT is the stable contract). Record per
  the vtable-ID convention (ids 3000+ are vtable INDEX constants) OR as a slot-fact in
  notes — the user's call on the exact row shape at approval.
