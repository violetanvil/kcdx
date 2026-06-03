# Finding — `sys_pakPriority` is cfg-SETTABLE by design (flags = VF_NULL, default = 2)

Captured 2026-06-02. Tier-5 fresh Ghidra (the registration was not in any prior
dump). Settles `step1-pakpriority-cfg-syntax.md` open question #1 DEFINITIVELY,
from the binary — not by repeated launches.

Trust level: PRIMARY EVIDENCE (decompiled + disassembled WHGame.dll; VF_ enum
from the in-repo vendored CryEngine `IConsole.h`).

Scripts (reproducible): `third-party-ghidra/ghidra_scripts/PakPriorityCvarReg.java`,
`PakPriorityRegHelper.java`, `PakPriorityGateProbe.java`.
Raw output: `_pakpriority_cvar_reg.txt`, `_pakpriority_reg_helper.txt`, `_pakpriority_gate_probe.txt`.

## How the registrar was reached

`"sys_PakPriority"` string @ `0x183a93c00` (prior dump `_u5_worker2.txt:231`)
→ 3 LEA xrefs (`getReferencesTo`):
- `0x180e36e87` in **`FUN_180e36ae0`** — the REGISTRAR (SystemInit pak-CVar block).
- `0x180da35bd` in `FUN_180da342c` — a `GetCVar`+conditional `SetIVal` (see §pins).
- `0x180d1b807` in `FUN_180d1b7d8` — a pure `GetIVal` reader (file-search mode).

## The registration call (decompiled, `_pakpriority_cvar_reg.txt:190`)

```c
// FUN_180e36ae0, SystemInit.cpp pak-CVar block
if (*(char *)(param_1 + 0x5b1) != '\0') {   // editor/tool-mode flag only
  DAT_184927298 = 0;
}
FUN_180e384d8(param_1, "sys_PakPriority", &DAT_184927298,
   "0: not-in pak file first, 1: pak file first, 2: pak file only, 3: not-in pak mod file first");
```

`FUN_180e384d8(this, name, &intSlot, help)` is the engine's
`REGISTER_CVAR2`-by-reference wrapper used for ALL `sys_Pak*` CVars. It does
NOT take a flags or default argument at the call site — both are derived inside:

```c
// FUN_180e384d8 (_pakpriority_reg_helper.txt:58)
plVar1 = console = (*this)[0x278]();              // GetIConsole
plVar2 = console->[0xb8](name);                   // GetCVar(name) (prior?)
if (plVar2) { param_5 = plVar2->[0x10](); console->[0x50](name, 1); } // capture GetIVal + Unregister
FUN_180b9aca0();                                  // the REAL RegisterInt-by-ref
plVar1 = console->[0xb8](name);                   // re-fetch
if (plVar2) plVar1->[0x38](param_5);              // SetIVal(prior) restore
```

The flags + default reach the real registrar `FUN_180b9aca0` in registers/stack
(disasm `_pakpriority_reg_helper.txt:104-111`):

| arg to `FUN_180b9aca0`    | value | meaning |
|---------------------------|-------|---------|
| `R8 = R15`                | `&DAT_184927298` | the int storage slot (register-by-reference) |
| `R9D = dword[R15]`        | `*intSlot`       | **default value = current slot contents** |
| `RDX`                     | name             | `"sys_PakPriority"` |
| `RBP / [RSP+0x28]`        | help             | the help string |
| **`dword[RSP+0x20] = 0`** | **0**            | **the FLAGS bitmask = `VF_NULL` (0x0)** |
| `qword[RSP+0x30] = 0`     | 0                | (extra/on-change callback slot) |

## Flags bitmask = 0 (`VF_NULL`) — decode

VF_ enum cited from `_research/predecessor-sigs/muyuanjin-kcd2db/external/cryengine/include/cryengine/IConsole.h`
(the matching CryEngine fork header, vendored primary source):

| flag | value | set? | cfg-settability impact |
|------|-------|------|------------------------|
| `VF_CONST_CVAR`          | `0x00800000` | NO | header comment: *"Set if it is a const cvar not to be set inside cfg-files"* — the BLOCKING flag; **absent** |
| `VF_READONLY`            | `0x00000800` | NO | *"Can not be changed by the user"*; absent |
| `VF_DEPRECATED`          | `0x40000000` | NO | default-only, immutable outside code; absent |
| `VF_CHEAT`               | `0x00000002` | NO | cheat-gated; absent |
| `VF_REQUIRE_APP_RESTART` | `0x00002000` | NO | (would be satisfiable pre-launch anyway); absent |
| (all others)             | —            | NO | bitmask is exactly `0x0` |

No flag is set. `sys_pakPriority` is a plain settable int CVar.

## Default value = 2 (`DAT_184927298` slot)

`_pakpriority_reg_helper.txt:52` — the value slot `DAT_184927298` reads **`0x2`**
in the static image (initialized data, not .bss). The editor-mode zeroing
(`+0x5b1`) does NOT run on the shipped player build, so the default the engine
registers is **2** ("pak file only"). This is the value the wiki described as
"the default for the published version" — confirmed it is a *default*, not a pin.

## VERDICT — settable from a pre-launch user.cfg: YES

`flags = VF_NULL` → no `VF_CONST_CVAR`, no `VF_READONLY`, no `VF_DEPRECATED`, no
`VF_CHEAT`. A correctly-syntaxed cfg line (`sys_pakPriority = 0`, the `=` form
the shipped `system.cfg` uses — `step1-pakpriority-cfg-syntax.md`) IS applied.
The value 2 is only the registered default; nothing in the registration prevents
a cfg override. The wiki's "only possible behavior for the published version"
claim is REFUTED at the binary level — it is the default, not an enforced pin.

## Does anything RE-WRITE the value to 2 after cfg load? — NO (no "pins it")

All 3 references to the value slot `DAT_184927298` (`_pakpriority_reg_helper.txt:172`):
- `[1]` WRITE `0x180e36e6f` — the editor-mode `=0` zeroing INSIDE the registrar, BEFORE registration (and to 0, not 2).
- `[2]` READ `0x1819aa762` — the value-log (`"CVar sys_PakPriority value is %d"`).
- `[3]` DATA `0x180e36e80` — the LEA for the register call.

No post-cfg-load store of 2 to the slot anywhere.

The ONE `SetIVal` on the CVar object (`FUN_180da342c`, `_pakpriority_cvar_reg.txt:697`)
sets it to **0** (loose-first), and is gated on `DAT_18556f2a0 != 0` — a global
that is `0x0` in the static image and only ever written by reference via
`FUN_18100c968` (`_pakpriority_gate_probe.txt`); the surrounding code builds
shader merge-caches (`Shaders/MergeCache/`, `%USER%/`), i.e. a dev/build path.
That path forces priority to 0, the OPPOSITE of pinning to 2. There is NO
mechanism that re-asserts 2 after cfg load.

## Confidence map

- VERIFIED (decompiled/disassembled WHGame.dll): registrar fn, the 4-arg
  register-by-ref wrapper ABI, flags arg = 0, default slot = 2, the 3 slot
  references, no post-cfg write-to-2, the Set-to-0 path is dev-gated.
- VERIFIED (in-repo vendored primary source `IConsole.h`): the `VF_*` numeric
  values used in the decode (canonical CryEngine fork header, not recall).
- NOT a live/boot claim: this is the registration's design; the corrected-syntax
  `user.cfg` launch (`step1-pakpriority-cfg-syntax.md` step 2) remains the
  end-to-end behavioural confirmation that an overlay then resolves.

## Seed-row candidate (AP18 — FLAG only, NOT written)

If kcdx ever reads/writes pakPriority from engine code, candidate DB targets:
- the value slot `DAT_184927298` (RVA `0x04927298`), or
- the registrar `FUN_180e36ae0` (RVA `0x00e36ae0`) / wrapper `FUN_180e384d8` (RVA `0x00e384d8`).
NOT added — surfacing only, per the user-approval gate.
