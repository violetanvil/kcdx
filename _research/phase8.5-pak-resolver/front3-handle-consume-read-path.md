# FRONT 3 — the HANDLE-CONSUME / read path (CCryFile + FRead-family dispatch)

Captured 2026-06-02. Trust level: PRIMARY EVIDENCE (fresh-Ghidra decompile + raw
disasm of WHGame.dll release_1_5_1164953_841, image base 0x180000000). Every claim
cites a decompile/disasm line. Slots binary-read from the CCryPak vtable @ VA
0x183A95FA8 (AP3), no canonical header. No prologue-shape guessing (AP2). No live
probe — runtime-effect items flagged NEEDS-LIVE-CONFIRM. No build/boot claim.

Producers (this dir + `third-party-ghidra/ghidra_scripts/`):
`PakReadPathDecomp.java` → `_readpath_decomp.txt` (read-family slot map + FRead/FSeek/FEof
decomp + disasm + the CCryFile helper FUN_1804605bc + the lock helpers).
`PakReadLeaves.java` → `_readpath_leaves.txt` (the OS-arm CRT fread leaf, the dispatch-bound
accessor, the three pak-arm read leaves).

Builds on (does NOT re-walk): `FINDINGS.md` (FOpen slot 36 RVA 0x4614A0; FWrite slot 41
+0x148; FClose slot 55 +0x1B8; vtable @ 0x183A95FA8), `subresolver-decompiled-mechanism.md`
+ `searchpath-registrar-mechanism2.md` (the slot-1 AdjustFileName resolver + pakPriority
gating), seed `address_names_seed.csv` (CCryPak_FOpen id 131, gEnv_pCryPak id 132,
ModManager_ReadModOrder id 136 naming the shared helper FUN_1804605bc).

---

## THE LOAD-BEARING FACT — loose-vs-pak is baked into the HANDLE at FOpen; FRead just dispatches on it

`CCryPak::FOpen` (slot 36) returns one opaque handle that is one of two things, and
**every read-family method (FRead/FSeek/FEof/FWrite/FClose) branches on the handle value
itself** — there is no per-read path resolution. The decision was already made when the
handle was minted.

### The handle is a tagged union: pak-pseudo-handle (small index+1) vs real FILE*

FRead = **CCryPak vtable slot 40 (+0x140) = FUN_18051cd00** (binary-read from the vtable,
sits directly below FWrite slot 41). Its dispatch (`_readpath_decomp.txt` 97-146, disasm
150-238):

```c
undefined8 FUN_18051cd00(longlong *this, FILE *handle, longlong *dst) {     // FRead
  FUN_1804613d0(local_28, this);                                // enter pak-mgr scope lock
  ...
  puVar6 = (FILE*)handle - 1;                                   // handleIdx = handle - 1
  puVar2 = FUN_180427e40(this + 8);                             // = count of pak-handle entries
  if (puVar6 < puVar2) {                                        // IN-BOUND -> PAK ARM
    uVar3 = FUN_18051ce40(this[8] + handleIdx*0x18, dst);       //   read from pak-handle struct
  } else {                                                      // OUT-OF-BOUND -> OS ARM
    ... (**(this+0x170))(this, handle);                         //   FGetSize via vtable, then
    FUN_1804d7ab4(buf, 1, size, handle);                        //   the REAL CRT fread (below)
  }
  FUN_1804613fc(local_28); ...
}
```

- `FUN_180427e40` (the dispatch bound, `_readpath_leaves.txt`): `(this[8].end - this[8].begin >> 3) * 0xAAAB...` — the COUNT of 0x18-stride entries in the pak-handle vector. (FRead reaches the vector via a `this+8` typed accessor; FSeek/FEof/FWrite/FClose reach the SAME vector inline as `[this+0x40]/[this+0x48]` — `this+8` is the typed member that aliases `this+0x40`.)
- `FUN_1804d7ab4` (the OS arm, `_readpath_leaves.txt`): literally `sVar1 = fread(param_1,param_2,param_3,param_4);` — the real CRT `fread` on a genuine `FILE*`, optionally under a global lock (`DAT_1856632e9`). **This is the confirmed proof the out-of-bound handle is a real OS FILE\*.**
- `FUN_18051ce40` (the pak arm): reads the uncompressed size from the pak-entry struct (`*(entry+0x28)+8`) and calls `FUN_1804607e4` (the pak extract/decompress). Bytes come from the loaded `.pak` zip directory, never the filesystem.

The SAME handleIdx-vs-count test appears verbatim in:
- **FRead** (slot 40, FUN_18051cd00) — `_readpath_decomp.txt` 111-118.
- **FSeek** (slot 38, FUN_180461304) — `_readpath_decomp.txt` 270-281 (`param_5-1 < count`; pak → FUN_1804618b4, OS → FUN_1804d7ab4).
- **FEof/FTell** (slot 39, FUN_18051e1f8) — `_readpath_decomp.txt` 304-313 (pak → FUN_18051e2bc, OS → fseek+FUN_1804d7ab4).
- **FWrite** (slot 41, FUN_180a700c8) — `_fopen_confirm.txt` 156-165 (pak → FUN_180506f94, OS → fwrite).
- **FClose** (slot 55, FUN_1804609d0) — `_fopen_confirm.txt` 194-203 (pak → FUN_180460958, OS → fclose).

The pak-arm read leaf `FUN_1804618b4` carries the literal CryPak string
`"FRead did not read expected number of byte from file, only %zu of %lld bytes read"`
and the compressed-vs-stored branch (`& 0x4000`) — confirming this is `CCryPak::FReadRaw`.

---

## CCryFile is a THIN WRAPPER over ICryPak — it does not resolve anything itself

`FUN_1804605bc` (seed id 136's "shared CCryFile open helper") IS `CCryFile::Open`
(`_readpath_decomp.txt` 328-355, disasm 359-416):

```c
bool FUN_1804605bc(CCryFile *this, const char *path, const char *mode) {
  ... copy path into this[0]..[0x104] ...
  if (*(this + 0x110) == NULL)                                  // no ICryPak bound
    fopen_s(&this[0x108], path, mode);                          //   -> raw OS open
  else                                                          // ICryPak bound (normal)
    this[0x108] = (**(*(this+0x110) + 0x120))();                //   -> ICryPak::FOpen (slot 36 = +0x120)
  return this[0x108] != NULL;
}
```

- `this+0x110` = the bound `ICryPak*`; `+0x120` off its vtable = **FOpen (slot 36)** — the
  exact slot/offset seeded as id 131. So CCryFile, when it has an ICryPak (the normal game
  case), opens through `ICryPak::FOpen` and stores the returned handle at `this+0x108`.
- The handle CCryFile stores is therefore the SAME tagged-union handle (pak-pseudo or real
  FILE*) FOpen mints; CCryFile's own reads forward to `ICryPak::FRead` (slot 40) on it.
- **CCryFile adds NO resolution logic** — no pak-vs-loose decision lives here. It is a
  buffering/path-holding shell. The decision is entirely in FOpen→AdjustFileName (front 1/2)
  and the handle-tag dispatch (this front).

CCryFile (FUN_1804605bc) has **~24 call sites** (`_readpath_decomp.txt` 419-443) — the
mod-loader (FUN_180da1294 = ModManager_ReadModOrder id 136, FUN_18243e7b8 = ParseManifest
id 137), config/XML readers, etc. It is the engine's standard "open a file for reading"
front door for handle-consumed assets.

---

## WHERE the loose-vs-pak decision ACTUALLY BITES for a handle-consumed read

**Answer: entirely at FOpen-time (in AdjustFileName, slot 1) — NOT at FRead, NOT in CCryFile.**

1. `CCryFile::Open` → `ICryPak::FOpen(path, mode, flags)`.
2. FOpen → `AdjustFileName` (slot 1, FUN_18046205c) runs the search-path/pakPriority
   precedence (front 1/2). It decides whether the bytes come from a pak or loose disk, and
   **mints the handle accordingly**: a pak hit → a pak-pseudo-handle (a small index+1 into
   `[this+0x40]`); a loose-disk hit → a real OS `FILE*` (from `fopen` on the resolved path,
   far above the pak-vector count).
3. FRead/FSeek/FEof read the bytes by dispatching on that already-decided handle tag. The
   read path is mechanical and extension-agnostic — it has no second chance to choose loose
   vs pak.

This is **why `.lua` (handle-consumed) diverged from `.dds` (memory-mapped) in the live
tests** — and it CORROBORATES the front-1/2 conclusion at the read-machinery level:

- The divergence was never in the read path or in CCryFile. Both classes get the same
  handle-tag dispatch.
- The divergence is upstream, at FOpen→AdjustFileName: at `sys_pakPriority 2` the
  search-path arm checks pak-membership ONLY (`searchpath-registrar-mechanism2.md` Q4), so a
  loose overlay for a handle-consumed class is never selected → FOpen mints a PAK handle →
  FRead reads pak bytes. The `.dds`/memory-mapped class reached the loose-disk check through
  a different consume path (a streamer / `FGetCachedFileData`) and so a loose handle was
  minted for it. AdjustFileName is extension-agnostic (front-1 finding); the per-class
  difference is which open flags/consume path the CALLER uses — confirmed here: the read
  machinery does not branch on extension, only on the handle tag FOpen already set.
- The `MECHANISM-CONFIRMED-pakpriority-loose.md` result (set `sys_pakPriority 0` → loose
  wins for BOTH classes) is fully consistent: at mode 0 AdjustFileName's per-entry test hits
  the loose-disk check first, so FOpen mints a real-FILE* handle for the handle-consumed
  class too, and FRead's OS arm (`fread`) serves the loose bytes.

---

## WHAT kcdx MUST CONTROL to serve a handle-consumed asset's BYTES regardless of pak/loose

A kcdx-owned resolver that replaces the engine's must guarantee one of these, because the
read path obeys only the handle:

- **Mint the right handle at open-time.** Whatever kcdx hooks/replaces at the OPEN seam must
  return a handle whose tag routes reads to the bytes kcdx wants:
  - To serve LOOSE override bytes through the engine's own FRead: return a real `FILE*` (the
    out-of-bound tag) opened on kcdx's chosen file — the engine's `fread` arm then serves it.
    (This is exactly what `sys_pakPriority 0` makes the engine do natively.)
  - To serve PAK bytes: return a pak-pseudo-handle (index into `[this+0x40]`) — requires the
    overlay be a mounted pak entry.
- **The deepest single point of full control = FOpen (slot 36) itself** (Around/Replace, not
  Before). A replacement FOpen that opens kcdx's chosen file with the OS and returns that
  real `FILE*` makes the engine's unmodified FRead/FSeek/FClose serve kcdx's loose bytes for
  EVERY handle-consumed asset, no per-read hook, no staging-to-Data/ required, no
  pakPriority change. The handle-tag dispatch is what makes this work: a real FILE* is
  always the OS arm.
- **kcdx does NOT need to hook FRead.** Hooking the read method is unnecessary and worse:
  the read path is already correct for whatever handle it is given. Control the handle, and
  the entire FRead/FSeek/FEof/FClose family follows for free. (This is the front's headline
  for the synthesis: the read machinery is reusable verbatim; only the handle-minting seam
  must be kcdx-owned.)
- If kcdx fully REPLACES the resolver (its own CCryPak vtable or its own FOpen body), it must
  preserve the handle-tag CONTRACT the rest of the engine relies on: small index+1 = pak
  entry in `[this+0x40]`; anything else = a real FILE*. Returning a kcdx-owned handle that is
  neither would crash FRead's dispatch (it would index the pak vector out of bounds or fread
  a non-FILE*). A kcdx handle for a loose override is safest as a genuine OS FILE* (the OS
  arm is a plain `fread`, no engine-private state).

---

## Contribution to the 4 synthesis questions (FRONT 3 = the read/consume path)

1. **Salvageable/reusable:** the ENTIRE read-consume family (FRead slot 40, FSeek slot 38,
   FEof/FTell slot 39, FWrite slot 41, FClose slot 55) + CCryFile (FUN_1804605bc) is reusable
   verbatim — it is pure mechanical handle-tag dispatch with no resolution logic. kcdx need
   not reimplement reading. The CRT `fread`/`fopen_s` OS arm means a loose override served as
   a real FILE* needs zero engine-private machinery.
2. **Full-replace vs partial:** PARTIAL is sufficient for the read/consume concern — the read
   path needs no replacement at all. The only seam kcdx must own is HANDLE-MINTING (FOpen /
   AdjustFileName, fronts 1/2). If kcdx goes full-replace on the resolver, it must honor the
   handle-tag contract so the (reused) read family still dispatches correctly.
3. **Exactly what to hook vs replace (read-path view):** hook/replace **FOpen (slot 36)** —
   that is the single point that mints the handle the whole read family obeys. Do NOT hook
   FRead/FSeek/FClose — redundant; the handle already encodes the decision. CCryFile is not a
   hook point (it just forwards to FOpen).
4. **How a kcdx resolver loads a STOCK pak mod unchanged:** a stock Nexus/Workshop `.pak` is
   mounted into the pak system, so the stock asset's FOpen mints a pak-pseudo-handle and
   FRead's pak arm (`FUN_18051ce40`/`FUN_1804618b4` → `FUN_1804607e4` decompress) serves it —
   UNCHANGED — as long as kcdx's resolver still produces a valid pak-pseudo-handle for a
   pak-resident path (i.e. kcdx must keep the `[this+0x40]` pak-handle minting + the pak
   directory binary-search front-2 mapped). kcdx-owned loose overrides return real FILE*s and
   coexist with stock-pak pak-handles in the same dispatch — the two tags never collide
   (index+1 small range vs heap FILE* pointer range).

---

## Confidence map

VERIFIED (decompiled/disasm, this build, cited above):
- FRead = CCryPak vtable slot 40 (+0x140) = FUN_18051cd00 (RVA 0x51CD00).
- FSeek slot 38 (+0x130) FUN_180461304; FEof/FTell slot 39 (+0x138) FUN_18051e1f8 (read-family RVAs).
- The handle-tag dispatch (handleIdx = handle-1; compared to the 0x18-stride pak-vector count at [this+0x40]/[this+0x48]) is IDENTICAL across FRead/FSeek/FEof/FWrite/FClose.
- OS arm FUN_1804d7ab4 = the real CRT `fread`; pak arm FUN_18051ce40/FUN_1804618b4 → FUN_1804607e4 (pak extract).
- CCryFile::Open = FUN_1804605bc: routes through ICryPak::FOpen (vtable+0x120 = slot 36) when an ICryPak is bound, else raw fopen_s; stores the handle at this+0x108; adds NO resolution logic.
- The loose-vs-pak decision bites at FOpen→AdjustFileName (handle-minting), never at FRead/CCryFile.

NEEDS-LIVE-CONFIRM (runtime, not static — for the chosen kcdx mechanism, not for this map):
- That a replacement/Around FOpen returning a kcdx-opened real FILE* makes the unmodified
  FRead family serve the loose bytes for a handle-consumed `.lua` (HIGH expected — it is the
  exact OS-arm path `sys_pakPriority 0` already exercises live per MECHANISM-CONFIRMED; the
  residual is whether a hook returning its own FILE* at the FOpen seam is accepted where a
  Before-hook could not). One probe: Around-mode FOpen on a boot `.lua`, return our fopen
  handle, confirm the substitute marker prints.

---

## Seed-row candidates (AP18 — FLAGGED, NOT written; need user approval)

The read family is the obvious add IF the synthesis chooses to own/wrap the read path. Most
likely NOT needed (the synthesis points at FOpen as the only seam), but flagged for completeness:

- **`CCryPak::FRead`** — FUN_18051cd00, RVA 0x51CD00, CCryPak vtable slot 40 (+0x140). ABI
  (from decomp): `void*(CCryPak* this /*rcx*/, FILE* handle /*rdx*/, longlong* outDst /*r8*/)`
  — 3-arg fastcall; dispatches handle-tag pak-vs-OS. Sibling already seeded: FOpen id 131
  (slot 36), FWrite slot 41 / FClose slot 55 (noted in id 131 prose, not separate rows).
- **`CCryFile::Open`** — FUN_1804605bc, RVA 0x4605BC. ABI `bool(CCryFile* this /*rcx*/,
  const char* path /*rdx*/, const char* mode /*r8*/)`. Thin wrapper → ICryPak::FOpen
  (vtable+0x120) or fopen_s; handle stored at this+0x108. Already REFERENCED (not seeded as
  its own entity) in id 136 ModManager_ReadModOrder's prose; promote to its own row only if
  kcdx needs to call/hook it directly.
- Secondary (only if directly wired): FSeek slot 38 (FUN_180461304, RVA 0x461304), FEof/FTell
  slot 39 (FUN_18051e1f8, RVA 0x51E1F8), the pak-arm read leaf FUN_1804618b4 (RVA 0x4618B4 =
  CCryPak::FReadRaw), the OS-arm leaf FUN_1804d7ab4 (RVA 0x4D7AB4).
