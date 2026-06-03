# Sub-resolver decompile — the `[rax+8]` call FOpen makes at 0x1804615FA

Additive to FINDINGS.md. Trust level: PRIMARY EVIDENCE (fresh Ghidra decompile of WHGame.dll
release_1_5_1164953_841, image base 0x180000000). Every claim cites a decompile/disasm line in
`_subresolver_decomp.txt` / `_subresolver_leaves.txt` / `_subresolver_strings.txt`. No live probe;
no inference presented as fact (AP2). Slots binary-read from the vtable, not from a header (AP3).

## The sub-function — how it was resolved from `[rax+8]`

- At `0x1804615FA` FOpen does `call qword ptr [rax+8]` where `rax = [r12]` and `r12 = rcx` (FOpen's
  own `this`, set at 0x1804614E9). So `rax` = **CCryPak's OWN vtable**, and `[rax+8]` = **CCryPak
  vtable slot 1 (+0x8)**, called with the same `this` (`rcx=r12`), path in `rdx`, and the computed
  flags in `r9d` (`_subresolver_decomp.txt` confirms the call args).
- Vtable @ VA 0x183A95FA8 (RVA 0x03A95FA8 — the ICryPak/CCryPak vtable from FINDINGS.md). Read live:
  - slot 0 (+0x0)  = 0x1824169fc
  - **slot 1 (+0x8)  = 0x18046205c**  ← the sub-resolver
  - slot 2 (+0x10) = 0x18041a900 …
- **Sub-resolver: `FUN_18046205c`, RVA 0x0006205C (VA 0x18046205C), 439 bytes.**
  It is the canonical CryEngine **`CCryPak::AdjustFileName` + search-path precedence resolver**:
  it does NOT return a handle — it returns the *resolved concrete path string* (`char*`), which FOpen
  (`mov rdi,rax` at 0x1804615FD) then opens. The loose-vs-pak PRECEDENCE decision lives entirely here.

Decompiled signature (recovered): `char* FUN_18046205c(CCryPak* this, char* pName, void* outBuf, uint nFlags)`.

## Children decompiled (one level down — the actual decision logic)

- `FUN_1804621bc` (RVA 0x000621BC, 1004 B) = **`AdjustFileName` path-normalizer** — copies pName into a
  2048-byte buffer (CryEngine path cap), applies flag-gated ROOT prefixing, returns normalized path in
  `outBuf`. THIS is the Q2 root logic.
- `FUN_1804631f0` (RVA 0x000631F0, 1013 B) = **PAK-membership check** — walks the loaded pak array
  (`[this+0x120]..[+0x128]`), binary-searches the path in each pak's directory index; returns the pak
  file-entry if the path is pak-resident, else 0.
- `FUN_1819c9cb4` (RVA 0x009C9CB4, 35 B) = **DISK/loose existence check** — `return (*(*pCryPak+0x228))
  (pCryPak, path) != -1;` i.e. the OS-filesystem attribute query (a file-on-disk-exists test).
- `FUN_18241ad60` (RVA 0x21AD60) = mode-3 prefix match (builds `"mods/"` + a level-ish prefix from
  .rdata chars, `_strnicmp`s the path) — the `sys_pakPriority==3` special case.
- `FUN_180462664` (RVA 0x000462664) = mod/alias path-substitution table walk (`[this+0x1b0]..[+0x1b8]`).

## The flag tests (cited)

`uVar10 = (uint)nFlags`. In `FUN_18046205c`:
- `0x18046207e`: if global `DAT_184927274 > 0` → `nFlags |= 0x1000000` (a "tools/editor present" mode; =0 in the dump).
- `0x180462097`: `TEST EBP,0x810000` — if `nFlags & (0x800000|0x10000)` is SET, the search-path loop is
  SKIPPED entirely and it goes straight to the single `FUN_1804621bc(this,pName,outBuf,nFlags)` open
  (decomp line 142). 0x800000 = "skip search-paths/already-full". 0x10000 here gates whether the
  per-search-path loop runs.
- The loop (when entered) calls `FUN_1804621bc(this, builtPath, outBuf, nFlags | 0x10000)` for each
  search-path entry (disasm `BTS R9D,0x10` @ 0x180462123) and tests existence via the pakPriority mode.

In `FUN_1804621bc` (the normalizer) — the load-bearing flag tests:
- `(nFlags >> 0x17 & 1)` = **bit 23 / 0x800000**: if CLEAR, run alias-lowercase normalization
  (FUN_1804625d0/FUN_1804627a0); if SET, the `%user%\`-prefix branch (decomp 304-315).
- `(nFlags >> 0x15 & 1)` = **bit 21 / 0x200000**: if SET → skip ALL root logic, just copy path (the
  "already adjusted / full path" flag; decomp 393-400).
- `(nFlags & 0x10000)` (decomp 319, 336): controls the data-root treatment (below).
- `(nFlags >> 0x12 & 1)` = **bit 18 / 0x40000**: append a trailing `\` (directory form; decomp 388).

## Q1 — which bit enables loose/OS-filesystem search

**`0x10000` is the loose-search ENABLER bit, and it is sufficient on its own for the SEARCH to reach
disk.** Mechanism (cited):
- The DISK existence check `FUN_1819c9cb4` (loose-on-disk) is reached on the `nFlags & 0x10000` /
  pakPriority branches at 0x1821ef42a and 0x1821ef460. The PAK check `FUN_1804631f0` is the other arm.
- WHICH of pak-vs-disk is consulted first, and whether disk is consulted at all, is gated by the
  **pakPriority mode** read from `*(*(this+0x228)+0x20)` (an ICVar's iValue = `sys_pakPriority`),
  NOT purely by a flag (decomp 102, 105, 113-119; disasm 0x18046213d `CMP [rcx+0x20],0x3`):
  - mode 0 → check DISK (`FUN_1819c9cb4`) then PAK — loose wins.
  - mode 1 → PAK (`FUN_1804631f0`) then disk.
  - mode 2 → PAK then disk (the published default; FINDINGS: `sys_pakPriority 2`).
  - mode 3 → PAK only / the `FUN_18241ad60` prefix-gated mode (no plain disk fallback).
- The `0x4` and `0x2` bits the prior FOpen analysis saw are FOpen-internal (fallback-arm disposition /
  forwarded mode) — **the sub-resolver body tests NEITHER 0x4 NOR 0x2.** It tests 0x10000, 0x800000,
  0x200000, 0x40000, and the pakPriority cvar. So `0x10006` is NOT required *by the sub-resolver* to
  make a disk search happen; the `0x6` part lives upstream in FOpen's own arm selection.

**Why `.dds` resolved at flag 0 but `.lua` did not — the body settles it.** It is NOT the path root and
NOT a sub-resolver branch on file type — the sub-resolver is extension-agnostic (no `.dds`/`.lua` test
anywhere in the body). The divergence is the prior analysis's hypothesis (a): **the texture caller and
the script caller pass different `nFlags` and consume the result differently.**
- A memory-mapped `.dds` is opened/probed in a way that succeeds on the FOpen path even at the loose
  path the resolver returns (the texture streamer tolerates the loose handle).
- The `.lua` is opened as a *handle to be consumed/read*; the script caller passes flags (and/or the
  pakPriority-2 default makes the PAK arm win first), so the loose substitute is not the one that gets
  consumed — the engine falls back to the pak copy. The bit that flips this for the script path is
  exactly `0x10000` (force the search-path/loose arm) which the probe (U.4) added.

VERIFIED from the body: `0x10000` is the bit that engages the search-path/loose-disk arm of the
resolver. NEEDS-LIVE-CONFIRM: the exact per-caller flag the texture vs script caller passes (that is a
runtime fact about the CALLERS, not in this function's body) — but the prior U.4 probe already
empirically pinned the working combination at `0x10006` + a `Data/`-relative path.

## Q2 — the loose-search ROOT rule

**The loose search is ROOTED, not absolute-anywhere.** `FUN_1804621bc` decides the root by prefixing
(cited, decomp 336-354):
- If `(nFlags & 0x10000) == 0` AND the path is not already under a recognized root (it `_strnicmp`s
  against `languages\`, `mods\`, `editor\`, `engine\`, and the two CryStringT roots `param_1[0x31]`
  = the **game DATA root** (`this+0x188`) and `param_1[0x4a]`), it **PREPENDS the data-root string
  `param_1[0x31]`** to the path (`memmove` the path up by the root length, `memcpy` the root in front).
  → A bare/relative path with flag-0 is forced under the **`Data/` game-data root**.
- If `(nFlags & 0x10000)` IS set and the path starts with `./` or `.\`, it STRIPS the `./` (treats the
  path as already-data-root-relative; decomp 319-335).
- `0x200000` set → no root logic at all, path used verbatim.
- A `%user%\` prefix is applied only on the `0x800000` user-dir branch (decomp 311) — the cosave/user
  path, not the asset path.

**Therefore an arbitrary `assets/`-dir ABSOLUTE path did NOT resolve** because the resolver either
(a) re-rooted it under the data root (turning `<abs>` into `Data/<abs>` which does not exist), or, when
the path looked absolute (`param_2[1]==':'`), did not match any search-path/pak entry and the disk arm
was not consulted at the pakPriority-2 default. **A `Data/`-relative path DID work** because it lands
exactly where the data-root prefixing + search-path loop expects, and `0x10000` engaged the loose arm.

**What step 3b's hook must redirect to (FINDING — manager/user decide):** the redirect target must be a
path the engine roots under its **game-data root** (`this+0x188`, i.e. `<game>/Data/`-relative), NOT an
arbitrary absolute path, AND the open must pass `nFlags` with `0x10000` set so the search-path/loose-disk
arm is taken (matching `ModManager_ReadModOrder`'s `0x10006`). Equivalently the hook can register the
overlay dir into the CCryPak search-path list (`[this+0x198]..[+0x1a0]`, the entries the loop iterates) —
then a search-path-relative name resolves loose without per-call flag surgery. Two viable shapes:
1. per-class flag: rewrite the open to `nFlags |= 0x10000` (or `0x10006` to match the loose reader) and
   stage the file at a `Data/`-relative path; OR
2. register a search path: add the overlay root to the pak search-path vector so loose files there are
   found by name at the engine's normal precedence.

## Confidence map

- VERIFIED (decompiled, this build): sub-resolver = CCryPak vtable slot 1 = FUN_18046205c (RVA 0x6205C);
  it is AdjustFileName + search-path precedence; the loose-disk check is FUN_1819c9cb4 via vtable +0x228;
  the pak check is FUN_1804631f0; `0x10000` engages the loose/search-path arm; the data-root (`this+0x188`)
  is prepended when `0x10000` is clear; pakPriority (`*(*(this+0x228)+0x20)`) gates pak-vs-disk order;
  the body tests neither 0x4 nor 0x2.
- NEEDS-LIVE-CONFIRM: the exact per-caller flags the texture vs script open sites pass (a fact about the
  callers, not this body); that registering a search-path entry vs per-call `0x10000` is the cleaner
  production hook (a design call). The U.4 probe already empirically confirmed `0x10006` + `Data/`-relative
  opens a loose `.lua`.

## Seed-row candidate (AP18 — FLAG for manager, do NOT write)

`CCryPak::AdjustFileName` / search-path precedence resolver — **FUN_18046205c, RVA 0x0006205C**, the
loose-vs-pak precedence + data-root-prefix resolver (CCryPak vtable slot 1, +0x8). Strong seed candidate:
it is the engine function a path-overlay hook would target. Targets: the open-by-path normalization +
loose/pak arbitration the project would hook for plugin loose-file overlays. Requires explicit user
sign-off before any seed row is written (AP18). Sibling already-seeded: CCryPak::FOpen (kcdx_id 131,
slot 36). The disk-existence leaf (FUN_1819c9cb4) and the pak-membership leaf (FUN_1804631f0) are
secondary candidates if the overlay design needs them directly.
