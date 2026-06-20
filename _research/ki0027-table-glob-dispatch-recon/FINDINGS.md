# KI-0027 — which CCryPak slot the table-DB override-glob dispatches through

**Question:** the engine's table-database loader discovers per-table override-patch
files by globbing `Libs\Tables\<base>__*.<ext>` (a `*`-containing pattern). Which
CCryPak vtable slot serves that glob? (kcdx owns slot 14 `ForEachFile`, thunks slot
101 `FindFirst`/`CCryPakFindData`; the table-DB load fails to see pak-resident
`__*.xml` overrides — KI-0027.)

**Verdict — FALSIFIED the slot-101 hypothesis (and slot 14): the glob dispatches
through the `FindFirst`/`FindNext`/`FindClose` handle triplet at vtable
+0x1F8 / +0x200 / +0x208 (slots 63 / 64 / 65)** — a THIRD CCryPak enumeration API,
distinct from slot 14 (callback, disk-only) and slot 101 (CCryPakFindData iterator
object). Read directly from the loader body; HIGH confidence.

## The dispatch — read in the loader's own body

`FUN_180974484` (entry `0x180974484`) — the table override-glob enumerator. Found by
LEA-xref of the `__` string (`0x183A93C20`) into the `0x180974xxx` CCryPak code
cluster, then Ghidra decompile. Verified call sites (raw dump: `_ghidra_globconsumer.txt`):

```c
uVar4 = FUN_1804f03dc(&local_1d0, param_1, &DAT_183a93c20);   // append "__"  (0x183a93c20)
uVar4 = FUN_1804f03dc(local_1c8, uVar4, &DAT_183e3ca78);      // append "*."  (0x183e3ca78)
FUN_1804fdc7c(&local_1c0, uVar4, &local_1d8);                 // pattern = <base>__*.<ext>
plVar7 = DAT_18492b850;                                       // the CCryPak singleton
lVar5 = (**(code **)(*DAT_18492b850 + 0x1f8))(DAT_18492b850, local_1c0, local_158, 0); // FindFirst (+0x1F8 / slot 63) → find-handle
if (-1 < lVar5) {
  do {
    ... // filter '.'/'..', prefix-match, emit each matched name to the merge-list (param_2)
    iVar3 = (**(code **)(*plVar7 + 0x200))(plVar7, lVar5, local_158);  // FindNext (+0x200 / slot 64)
  } while (-1 < iVar3);
  (**(code **)(*plVar7 + 0x208))(plVar7, lVar5);              // FindClose (+0x208 / slot 65)
}
```

The iterator is a **handle** (`lVar5`, an int-like find handle returned by FindFirst),
NOT an object with its own vftable — distinct from slot 101's `CCryPakFindData`
factory. `local_158` is the caller-provided find-data scratch buffer (36+ bytes);
`local_134`/`local_133`/`local_132` are the matched entry's name bytes (the `.`/`..`
skip test).

## `DAT_18492b850` IS the CCryPak singleton

A direct global to the same object normally reached via `*(gEnv+0x50)`. Proven by two
independent decompiled consumers using the IDENTICAL +0x1F8/+0x200/+0x208 triplet
(`_ghidra_confirmglobal.txt`):
- `FUN_18041d238` — a generic directory listing on `DAT_18492b850` (the triplet is the
  engine's GENERAL by-name directory enumeration, not table-specific).
- CCryPak vtable @ `0x183A95FA8`: `+0x1F8 → 0x180973058`, `+0x200 → 0x18041d640`,
  `+0x208 → 0x18097383c` — `0x180973058` sits in the same `0x180973xxx` cluster as
  slot 101's factory `0x180973294`.

## Loader chain (the fatal path)

`FUN_180ef803c` (raises "Database system error - tables can't be loaded" at
`0x180ef81e6` when the worker returns false) → `FUN_180ef83dc` → `FUN_180ef8588`;
merge-key build from `__` in `FUN_181e39b30`; the directory glob in `FUN_180974484`.
A separate site at `0x180D1D58E` opens a single constructed table filename via slot 36
FOpen (+0x120) — the by-name open, NOT the glob.

## Correction to the front1 vtable map

`_research/phase8.5-pak-resolver/front1-full-vtable-surface.md` mislabeled these three
as low-confidence (`i`): slot 63 (+0x1F8) "path-build op", slot 64 (+0x200)
"IsFileExist-by-handle", slot 65 (+0x208) "pak/alias op". The decompiled consumers
prove they are the **FindFirst(handle) / FindNext / FindClose** triplet.

## Implication for the kcdx takeover (stated AFTER the read)

kcdx owns slot 14 (+0x70) and thunks slot 101 (+0x328). **Neither is the table glob's
slot.** To serve pak-resident `__*.xml` table overrides, kcdx must own the
**+0x1F8 / +0x200 / +0x208 FindFirst/FindNext/FindClose triplet (slots 63/64/65)** —
the engine's GENERAL by-name directory enumeration, so owning it serves ALL such
enumerations, not just tables (consistent with the §1 totalizing invariant).

## Caveats / unread edges

- The engine impl of the +0x1F8 triplet (`0x180973058` / `0x18041d640` / `0x18097383c`)
  was NOT decompiled — WHICH slot the loader calls is read+verified; whether the engine
  FindFirst walks pak dirs + disk vs disk-only is a SEPARATE unread fact. Do not assume
  it shares slot 101's pak+disk behavior. (kcdx's own impl will walk the unified set
  regardless, so this gates the engine-behavior description, not the kcdx design.)
- The glob surface is broader than `Libs\Tables` — `FUN_18041d238` uses the same triplet
  for generic directory listing.
- `DAT_18492b850`'s write/assignment site was not located (no Ghidra write-ref); its
  CCryPak identity rests on behavior + the vtable-offset match, not the store.

## Reuse-ladder tiers

Slot offsets: tier-2 (`front1-full-vtable-surface.md`, corrected). Loader bodies +
dispatch slot + global identity: tier-5 fresh Ghidra decompile (this dir).
