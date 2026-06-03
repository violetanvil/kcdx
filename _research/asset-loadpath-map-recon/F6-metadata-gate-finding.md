# FRONT 6 (F6) — the METADATA-CHECK gate (size/existence pre-read)

Captured 2026-06-03. Trust level: PRIMARY EVIDENCE for the slot-45/slot-67 bodies
(fresh-Ghidra decompiles, reused from the front-1 roles dump + the subresolver leaves
dump — NO new Ghidra run was needed; the reuse ladder answered every node at tier 2).
Slots binary-read from the CCryPak vtable @ VA 0x183A95FA8 (AP3). No prologue-shape
guessing (AP2). The ONE part that is NOT read — whether a real asset loader pre-sizes via
slot 45 before its FOpen read — is flagged "unverified — not read" (no consumer call
sequence was located this session).

THE QUESTION (front brief): does the engine size-check or existence-check an asset
BEFORE reading it, in a way that would REJECT or MIS-READ a kcdx FOpen-only substitute of
a DIFFERENT size? And if it size-checks, does the size come from the SAME resolution the
FOpen override serves, or from the original pak entry (a mismatch)?

Sources reused (do NOT re-walk):
- `../phase8.5-pak-resolver/_front1_roles_raw.txt` 265-378 — the decompiled bodies of
  slot 45 (GetFileSize `FUN_182418b48`) and slot 67 (IsFileExist `FUN_180463ec4`), plus
  slot 68/70 siblings.
- `../phase8.5-pak-resolver/_subresolver_leaves.txt` 867-879 — the DISK leaf
  `FUN_1819c9cb4` body (proves vtable+0x228 is the OS attr/size query).
- `../phase8.5-pak-resolver/front4_resolution_decision_tree.md` — slot-1 resolver tree +
  the pak/disk leaves + pakPriority gating.
- `../phase8.5-pak-resolver/front3-handle-consume-read-path.md` — the verified seam: the
  loose-vs-pak decision bites at FOpen→AdjustFileName (handle-minting, slot 36 / slot 1).
- `../phase8.5-pak-resolver/seamA-probe-timing-finding.md` — the two live seams being
  explored (Around-FOpen slot 36; Around-AdjustFileName slot 1 id 152).

---

## CLAIM 1 — slot 45 GetFileSize gets its size from BOTH arms; the source is whichever arm `sys_pakPriority` selects (pak-dir entry OR an OS stat of the resolved file). READ.

`FUN_182418b48(longlong *this, undefined8 name, char bDiskOnly)` — `_front1_roles_raw.txt`
269-303, read verbatim:

```c
ulonglong FUN_182418b48(longlong *param_1,undefined8 param_2,char param_3) {
  ...
  lVar2 = (**(code **)(*param_1 + 8))(param_1,param_2,local_828,2);   // (A) slot-1 AdjustFileName, flag 2
  if (lVar2 != 0) {
    if (((*(int *)(param_1[0x45] + 0x20) == 0) ||                      // pakPriority 0 (disk-first) ...
        ((*(int *)(param_1[0x45] + 0x20) == 3 &&                       // ... or mode 3 + mods/-gate
          (cVar1 = FUN_18241ad60(lVar2), cVar1 != '\0')))) &&
       (uVar3 = (**(code **)(*DAT_18492b850 + 0x228))(DAT_18492b850,lVar2),  // (B) OS query vtable+0x228
        uVar3 != 0xffffffffffffffff)) {
      return uVar3;                                                    //   -> OS size of the resolved path
    }
    lVar4 = FUN_1804631f0(param_1,lVar2,local_838,0);                  // (C) PAK directory binary-search
    if (lVar4 != 0) {
      return (ulonglong)*(uint *)(lVar4 + 8);                          //   -> size field of the PAK-DIR entry (+8)
    }
    if ((param_3 != '\0') || (*(int *)(param_1[0x45] + 0x20) == 1)) {  // bDiskOnly, or mode 1 (pak-first)
      uVar3 = (**(code **)(*DAT_18492b850 + 0x228))(DAT_18492b850,lVar2);  // (B') OS query again
      if (uVar3 == 0xffffffffffffffff) return 0;
      return uVar3;
    }
  }
  return 0;
}
```

The size source is arm-dependent, NOT a single fixed source:
- **(B)/(B') OS arm** — `(*(*pCryPak+0x228))(pCryPak, resolvedPath)`. The DISK leaf
  `FUN_1819c9cb4` (`_subresolver_leaves.txt` 872-879) is the SAME call shape used purely as
  `!= -1` existence; here slot 45 uses the RETURN VALUE as the size. `*pCryPak` is the CCryPak
  vtable; `+0x228` = slot 69 = GetFileStat (`_stat64`-class, front-1 slot table). So the OS arm
  returns the **actual on-disk size of the resolved file**.
- **(C) PAK arm** — `FUN_1804631f0` binary-searches the loaded-pak directory index (front-4:
  walks `[this+0x120..0x128]`, returns the pak file-entry ptr) and reads `*(uint*)(entry+8)` =
  **the size recorded in the pak's directory entry** (NOT a disk stat).
- Arm ORDER is `*(this[0x45]+0x20)` = the `sys_pakPriority` int (front-4 names this field as
  `this+0x228 -> +0x20`; `0x45*8 = 0x228`, so `param_1[0x45]` IS `this+0x228` — same cvar):
  mode 0 → OS first then pak; mode 1 → pak (C) then OS (B'); mode 2 (published default) → pak
  (C) only, no OS fallback for size; mode 3 → mods/-gated OS.

> This is the SAME resolution + same per-mode arm order the FOpen resolver uses (front 4),
> because both call slot 1 first. The size and the open agree ON WHICH FILE — IF nothing
> intercepts between them. The catch is CLAIM 3.

## CLAIM 2 — slot 67 IsFileExist is pure existence (pak-membership and/or OS attr), returns bool, allocates nothing on the asset's size. READ.

`FUN_180463ec4(longlong *this, undefined8 name, int location)` — `_front1_roles_raw.txt`
335-378. Calls slot 1 AdjustFileName (flag 2), then per `location`/`pakPriority`: `param_3==2`
→ pak-membership (`FUN_1804631f0`) only; else OS-exists (`FUN_1819c9cb4`) and/or pak-membership
in pakPriority order. Returns `true`/`false`. It does NOT return or compute a size, and nothing
in its body sizes a buffer. An existence MISMATCH (overlay exists where the pak entry does not,
or vice versa) would only flip a branch — it cannot mis-size a read. The size-mismatch risk lives
entirely with slot 45 (CLAIM 1), not slot 67.

## CLAIM 3 — the verified Around-FOpen seam (slot 36) does NOT intercept slot 45/67; both call slot 1 AdjustFileName DIRECTLY, so an FOpen-only override is invisible to a pre-read size query. READ (architecture), corroborated across fronts 1/3/4.

- The verified seam (commit e6e8e27 / front 3) hooks **FOpen = slot 36 (+0x120)** Around,
  returning a real `FILE*`; the loose-vs-pak decision "bites at FOpen→AdjustFileName
  (handle-minting), NEVER at FRead/CCryFile" (front-3 headline, decompiled).
- Slot 45 (line 282) and slot 67 (line 349) each call `(**(code**)(*param_1 + 8))` = **slot 1
  AdjustFileName** themselves — they do NOT route through slot 36 FOpen. (front-1 finding: every
  by-name method calls slot 1 first; the 9 non-open by-name surfaces — 13/14/45/67/68/70/92/93 —
  resolve independently of FOpen.)
- Therefore a hook that ONLY owns FOpen (slot 36) changes WHICH BYTES an open returns, but does
  NOT change what slot 45 reports as the size: slot 45 re-resolves via slot 1 and reads the
  size from the engine's pak-dir entry (mode 2 default) or an OS stat of the path slot 1
  resolved — neither of which the FOpen override touched.

### The concrete mismatch this produces (the "any asset" gap)

A load path of the shape `size = GetFileSize(vpath); buf = malloc(size); h = FOpen(vpath); FRead(h, buf, size)`:
- At pakPriority 2 (default): `GetFileSize` returns the ORIGINAL pak entry's size (arm C). A
  kcdx FOpen-only override returns a real FILE* to a DIFFERENT-sized loose file. `FRead` then
  copies `min(size, ...)` — a LARGER overlay is **truncated to the original pak size**; a SMALLER
  overlay is **over-read** (FRead's OS arm `fread` returns short, but the caller sized its buffer
  to the pak size and may treat the tail as valid). Either way the overlay is mis-read.
- The seam that does NOT have this gap is the **Around-AdjustFileName seam (slot 1, id 152)**
  from `seamA-probe-timing-finding.md`: owning slot 1 makes slot 45 AND FOpen resolve to the
  kcdx path uniformly (front-1 Q3: owning slot 1 owns all by-name consumers), so the size query
  and the read agree on the overlay. This is the front-1 finding restated for the metadata axis:
  FOpen-only is a surgical subset that misses the size/exist surfaces; slot-1 is the full seam.

## CLAIM 4 (the brief's "at least one real size-check-then-read consumer") — UNVERIFIED — NOT READ.

No real asset-loader call sequence of the form "slot 45 GetFileSize → malloc → FOpen → FRead on
the same path" was located and read this session. Front 3 found CCryFile::Open (`FUN_1804605bc`,
~24 callers) opens-then-reads WITHOUT a pre-FOpen GetFileSize call in the body it decompiled
(it forwards straight to FOpen and stores the handle). Whether any specific texture/model/XML
loader calls slot 45 (or `IResourceManager`-class size query) before its FOpen is **not read** —
it requires walking a concrete consumer's body (a fresh-Ghidra caller trace of slot 45 /
`FUN_182418b48` xrefs), which this front did not do. So the EXISTENCE of the mismatch MECHANISM
is verified (CLAIM 1+3); whether a COMMON asset load actually exercises it is unverified.

What WAS read about consumers: CCryFile::Open (front 3) does NOT pre-size — it opens and reads
on the handle, so the CCryFile-mediated handle-consumed classes (`.lua`/`.xml`/mod-loader)
do NOT hit the slot-45 gate. The size-gate risk, if real, is in a DIFFERENT (non-CCryFile)
loader that calls slot 45 first — unconfirmed it exists.

---

## VERDICT (for the ledger)

**MIXED / partially-verified — a SIZE-MISMATCH MECHANISM exists for an FOpen-only override, but no common size-gated consumer was read to confirm it fires.**

- VERIFIED (decompiled): slot 45 GetFileSize sizes from the pak-dir entry (mode 2 default) or
  an OS stat — via slot 1, independent of FOpen. An FOpen-only seam (slot 36) does NOT change
  what slot 45 reports → a pre-read `GetFileSize→malloc→FOpen→FRead` path WOULD mis-size a
  different-sized kcdx loose substitute (truncate larger / over-read smaller).
- VERIFIED (decompiled): slot 67 IsFileExist is existence-only; it cannot mis-size, only flip a
  branch. The CCryFile open-then-read path (front 3) does NOT pre-size, so handle-consumed
  classes are not exposed to the slot-45 gate.
- UNVERIFIED — NOT READ: whether any COMMON asset loader actually calls slot 45 before its FOpen
  (no consumer call sequence located). The mechanism is proven; a real consumer of it is not.
- DESIGN-LEVEL CONSEQUENCE (front-1 corroboration, not a new claim): the slot-1 AdjustFileName
  seam (id 152) closes this gap by construction (size query + open resolve uniformly); the
  FOpen-only seam is the surgical subset that leaves it open. This is the SAME FOpen-only-vs-slot-1
  tradeoff fronts 1/3/4 already surface, now confirmed on the metadata axis.

Confidence: HIGH on the mechanism (slot-45/67 bodies + the FOpen-doesn't-touch-slot-1 architecture,
all READ). LOW on whether it bites in practice (no consumer READ — "unverified, not read").

## Seed-row candidates (AP18 — FLAGGED, NOT written; need user sign-off)

No NEW candidates beyond those the prior fronts already flagged. If a kcdx slot-1 replacement is
chosen (the gap-closing seam), the relevant rows are already flagged by front 4
(`CCryPak::AdjustFileName` `FUN_18046205c` 0x6205C). Slot 45 GetFileSize (`FUN_182418b48`
0x2418b48) and slot 67 IsFileExist (`FUN_180463ec4` 0x463ec4) are the surfaces a FOpen-only
seam would MISS — flag them ONLY if kcdx decides to hook the metadata surfaces directly
(front-1 already listed both as candidates).
