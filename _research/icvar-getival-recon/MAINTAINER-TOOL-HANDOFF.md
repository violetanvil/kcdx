# Maintainer-tool handoff — Address Library entities for `kcdx.cvar.*`

Everything the maintainer-tool agent needs to record the CVar-read surface's
game-binary facts into the reference DB, so the `kcdx.cvar.*` feature can resolve
them by name at runtime without issue. Authored by the RE side (verified +
gated); the DB write + CSV export is the maintainer-tool's lane.

Verified 2026-06-03 against `WHGame.dll` (release_1_5_1164953_841). Provenance +
the gate verdicts: [`FINDINGS.md`](FINDINGS.md) in this dir. Next free id at
authoring time: **156** (highest existing = 155; append-only, claim the next
free integer — re-check before writing in case a parallel change consumed it).

## The surface this backs

`kcdx.cvar.get_int(name)` / `get_bool(name)` / `get_float(name)` (Lua) +
`kcdxConsoleInterface::GetCVarInt/GetCVarBool/GetCVarFloat` (C++ mirror). Runtime
resolution chain: `GetCVar(name) -> ICVar*`, then call the ICVar accessor slot
(GetIVal for int/bool, GetFVal for float). `get_bool` = `get_int != 0` (no separate
entity). `name` is the engine CVar string the author supplies.

## What is ALREADY in the DB (reuse — do NOT re-add)

- **id 16 `IConsole_GetCVar`** — `GetCVar(name) -> ICVar*`, vtable[23], rva
  0x009DF818, kind=function, sig `ptr (ptr self, cstr name)`. The surface resolves
  this by name (`refdb::ResolveAddrByName("IConsole_GetCVar")`). No change to its
  resolve facts.
- **id 10 `gEnv_pConsole`** — the IConsole* slot the surface reads to get the
  console pointer (already used by `src/console.cpp`). No change.

## What MUST BE ADDED — two new entities (both AP18-APPROVED 2026-06-03)

Both record as **`kind=function`** — the repo convention for an IConsole/ICVar
vtable METHOD (matches the GetCVar/AddCommand/ExecuteString/PrintLine siblings:
concrete slot-target RVA + signature, vtable slot stated in `notes`). NOT
`vtable_index` (that kind is for pure slot-INDEX constants with no callable
target — the ids-3000+ convention).

### Entity 1 — `ICVar_GetIVal` (claim next free id, e.g. 156)

`address_names_seed.csv` row
(`id,name,superseded_by,superseded_at_version,is_deprecated,deprecated_at_version,deprecation_replacement,notes`):

```
<id>,ICVar_GetIVal,,,,,,"ICVar::GetIVal() -> int. __thiscall (rcx=ICVar*). vtable[2] / +0x10. Reads the CVar's stored 32-bit int value. The ICVar* comes from IConsole::GetCVar (id 16). Verified against the binary: CXConsoleVariableInt vtable @0x183a7e638, slot-2 body @0x181a72450 returns *(int*)(this+0x48); slot-3 GetI64Val reads the same field as int64. Canonical CryEngine slot — NOT shifted in this build (unlike IConsole::AddCommand id 13). Gated body-read verified. Underpins kcdx.cvar.get_int / get_bool."
```

`address_versions_seed.csv` row
(`kcdx_id,valid_from_version,module,rva,kind,signature,last_verified_at_version,verified_by,verified_date,evidence_kind,survival_aob,survival_anchor_string,survival_derives_from,survival_rule,survival_slot_count,survival_expect_unique,value,offset,vtable_slot,struct_offset`):

```
<id>,1.5.1164953,WHGame.dll,0x181A72450,function,"i32 (ptr self)",1.5.1164953,VioletAnvil,2026-06-03,maintainer_ghidra,,,,,,,,,,
```

NOTE on the rva: `0x181A72450` is the absolute VA of the CXConsoleVariableInt
slot-2 body; as a module-relative RVA (subtract image base 0x180000000) that is
**`0x01A72450`**. Use whichever form the repo's existing rows use — the GetCVar
sibling stores a small RVA (`0x009DF818` = module-relative), so the
**module-relative `0x01A72450`** is the consistent value. (Confirm against how
the importer's `parse_int` + the existing rows treat rva; FINDINGS.md records the
absolute VA `0x181a72450`, RVA = VA − 0x180000000.)

### Entity 2 — `ICVar_GetFVal` (claim next free id, e.g. 157)

`address_names_seed.csv` row:

```
<id>,ICVar_GetFVal,,,,,,"ICVar::GetFVal() -> float. __thiscall (rcx=ICVar*). vtable[4] / +0x20. Reads the CVar's stored float value. The ICVar* comes from IConsole::GetCVar (id 16). Verified against the binary: CXConsoleVariableFloat vtable @0x183ab7858, slot-4 body @0x181a73680 returns the float field at this+0x48 (slots 2/3 read it via *(float*), proving the field type). Canonical CryEngine slot. Gated body-read verified. Underpins kcdx.cvar.get_float."
```

`address_versions_seed.csv` row:

```
<id>,1.5.1164953,WHGame.dll,0x01A73680,function,"f32 (ptr self)",1.5.1164953,VioletAnvil,2026-06-03,maintainer_ghidra,,,,,,,,,,
```

(rva: absolute VA `0x181a73680` → module-relative `0x01A73680`.)

## What MUST BE FIXED — stale prose on the EXISTING id-16 GetCVar row

The id-16 `notes` currently reads: *"...Underpins kcdx.get_cvar_bool /
get_cvar_int / get_cvar_float (Lua surface)..."* — that flat `kcdx.get_cvar_*`
surface was NEVER built; the actual surface is `kcdx.cvar.get_int / get_bool /
get_float`. This is an UPDATE to an existing row (not a new entity — no AP18
gate), correcting documentation prose. Replace that clause with:

> *"...Underpins kcdx.cvar.get_int / get_bool / get_float (Lua surface,
> kcdx.cvar.* domain) via the ICVar accessors ICVar_GetIVal / ICVar_GetFVal, and
> the C-side CVar lookup."*

## Comprehensiveness check — every fact the surface needs has a home

| Surface call | Needs | DB entity | State |
|---|---|---|---|
| resolve console ptr | gEnv->pConsole | id 10 `gEnv_pConsole` | present |
| name -> ICVar* | GetCVar | id 16 `IConsole_GetCVar` | present (prose fix owed) |
| int value | GetIVal slot 2 | `ICVar_GetIVal` (new) | ADD (approved) |
| bool value | GetIVal != 0 | `ICVar_GetIVal` (reused) | (same entity) |
| float value | GetFVal slot 4 | `ICVar_GetFVal` (new) | ADD (approved) |

No fact the `kcdx.cvar.*` surface needs is missing after these two adds + the
prose fix. The test plugin (`cap-NN`) exercises both new entities, upgrading their
`evidence_kind` from `maintainer_ghidra` to `live_test_plugin` at the verification
checkpoint (per `data/seeds/policy.md` §"Test plugin requirement", which is why a
new entity lands in the same unit of work as its test plugin).
