# C_ModManager init-cycle observation — findings

Source: the three-point probe in `src/mod_absorb/ctor_probe.{h,cpp}` (commit
`87a2a38`), run twice on 2026-05-27:

- Boot 1 — no-mods setup (mods/ moved aside, but Steam Workshop content for
  KCD2 appid 1771300 remained accessible to the engine).
  Log: `kcdx-dev_2026-05-27_23-20-04.log`.
- Boot 2 — full normal mods setup.
  Log: `kcdx-dev_2026-05-27_23-23-09.log`.

Both runs captured POINT A (ctor entry), POINT B (SELECT entry — inside the
ctor body, post-zero-init, pre-SELECT-body), and POINT C (ctor return).
Every memory deref outside the C_ModManager's own 0x68 bytes was SEH-guarded.

The probe is observe-only and forwards the ctor / SELECT to their originals
unchanged.

## TL;DR

- The `wh::C_ModManager` is a **0x68-byte object** with 10 used slots and 3
  zero-padding slots; the previous seed-prose claim that the ctor zero-inits
  +0x18..+0x58 is **literally true** but understates which of those slots SELECT
  populates by ctor return.
- The previous narrow probe (commit `bf21802`) was reading the **caller's
  stack frame** for POINT C, not the C_ModManager itself. The "+0x30 = mods
  ASCII" / "+0x48 = 15" / "+0x50 = 54" / "+0x58 = pointer" / "+0x60 =
  0x600000094" findings were ALL stack data from CSystem::Init's local
  scratch buffer, not C_ModManager state. The comprehensive probe fixes this
  by dereferencing `*outResult` first.
- The architecture's premise — that the absorb hook at id 3100 reads a
  valid `std::vector<I_Mod*>` triple at +0x30/+0x38/+0x40 — is **confirmed
  in BOTH boots**: first I_Mod's vtable matches Address Library id 3105.
- The scanned list at +0x18/+0x20 is a separate **inline-record vector**
  (0x70 stride), NOT a sub-object vtable. The +0x28 third slot is its
  end_of_storage.
- Three slots (+0x48, +0x50, +0x58) are **zero in both boots** — unused
  fields in the running build. Step 4's replacement ctor leaves them zero.

## The corrected C_ModManager (0x68 bytes) layout

| Offset | Width | Field | Boot 1 | Boot 2 | Static? |
|---|---|---|---|---|---|
| +0x00 | 8 | C_ModManager vtable | image_ptr (8/8 code) | image_ptr (8/8 code) | **Static address (image RVA 0x3AA2E60)** |
| +0x08 | 8 | `CSystem* sys` (ctor arg2) | heap_ptr | heap_ptr | Per-boot value |
| +0x10 | 8 | `CryStringT modsDir` (data ptr; in-place CryString constructed by ctor) | heap_ptr → "mods" | heap_ptr → "mods" | **Static content "mods"** (CryString header `{pad=0, nRefs=1, nLength=4, nAllocSize=4}`) |
| +0x18 | 8 | scanned-list begin (`std::vector<I_Mod>`, 0x70-stride INLINE records) | heap_ptr (7 records) | heap_ptr (15 records) | Per-boot value |
| +0x20 | 8 | scanned-list end | heap_ptr | heap_ptr | Per-boot |
| +0x28 | 8 | scanned-list end_of_storage | heap_ptr | heap_ptr | Per-boot |
| +0x30 | 8 | enabled-list begin (`std::vector<I_Mod*>`, 8-byte stride) | heap_ptr (64 ptrs) | heap_ptr (71 ptrs) | Per-boot |
| +0x38 | 8 | enabled-list end | heap_ptr | heap_ptr | Per-boot |
| +0x40 | 8 | enabled-list end_of_storage (= end; vector is at-capacity) | heap_ptr | heap_ptr | Per-boot |
| +0x48 | 8 | **unused — zero in both boots** | zero | zero | **Always zero** |
| +0x50 | 8 | **unused — zero in both boots** | zero | zero | **Always zero** |
| +0x58 | 8 | **unused — zero in both boots** | zero | zero | **Always zero** |
| +0x60 | 1 | initialized flag (byte=1, upper 7 bytes zero) | 1 | 1 | **Static byte=1** |

The seed row 3101 prose's "zero-inits +0x18..+0x58" describes the
**ctor's** own behavior (true — see POINT B which captures pre-SELECT
state, where +0x18..+0x58 ARE all zero and only +0x00/+0x08/+0x10/+0x60
are populated). The post-ctor state at POINT C reflects what SELECT
subsequently wrote into +0x18..+0x40 — building the scanned and enabled
vectors before returning.

## POINT B vs POINT C — separating ctor writes from SELECT writes

At POINT B (SELECT entry, post-ctor-zero-init), only four slots are
non-zero in both boots:

```
+0x00 = vtable           (ctor)
+0x08 = sys ptr          (ctor)
+0x10 = "mods" CryString (ctor)
+0x60 = byte 1           (ctor)
```

Everything else is zero at POINT B.

At POINT C (ctor return — i.e., SELECT has run inline), six additional
slots are non-zero:

```
+0x18 / +0x20 / +0x28 = scanned-list vector (SELECT's "scan mods/" pass)
+0x30 / +0x38 / +0x40 = enabled-list vector (SELECT's "read mod_order +
                        enable" pass)
```

This **separates the ctor's responsibility from SELECT's** — kcdx's
replacement ctor in step 4 writes only +0x00/+0x08/+0x10/+0x60 and leaves
+0x18..+0x40 zero for kcdx's own enabled-list builder to populate.

## The previous narrow probe was reading the wrong memory

The narrow step-1 probe (commit `bf21802`, file `ctor_probe.cpp` at that
SHA) read `outResult` directly as if `outResult` were the C_ModManager.

The ctor disassembly at `_disasm_full_bodies.txt:24-47`:

```
mov rsi, rcx           ; rsi captures arg1 = outResult
...
call 0x1804f7820       ; allocator returns rax = heap block
mov rbx, rax           ; rbx = the C_ModManager heap block
...
[all member writes go to [rbx+N], NOT to [rsi+N]]
...
mov [rsi], rbx         ; *outResult = the heap block (last write before ret)
mov rax, rsi           ; return value is outResult (the caller's slot)
ret
```

So the C_ModManager object lives at `*outResult`, **not at `outResult`**.
The narrow probe was dumping the caller's stack-frame scratch buffer, which
in both boots contained: the literal string "mods" (CSystem::Init's local
CryString temporary used to build the modsDir arg), three small integers
(4, 15, 54 — likely local-counter scratch), and some pointer-shaped values
(stack/heap addresses from earlier register spills). **Every "surprising"
value from the narrow probe was caller-stack data, NOT C_ModManager data.**

The comprehensive probe corrects this by dereferencing `*outResult` first
(`std::memcpy(&obj, outResult, 8)`) and only THEN walking the 0x68 bytes
of the actual heap object.

POINT A's `point_a_outresult_raw` dump captures the pre-ctor stack-buffer
state — useful as a diagnostic showing the caller's frame layout BEFORE
the ctor overwrites `*outResult` — and confirms the stack pattern matches
the narrow probe's misread values exactly.

## Vector layout details

### Enabled list at +0x30 / +0x38 / +0x40 — `std::vector<I_Mod*>`

- 8-byte stride per element (pointers to I_Mod records).
- Boot 1: count=64. Boot 2: count=71. Delta = 7 (matches the 7 mods the
  user had installed in `<game>/mods/` for boot 2).
- The remaining 64 enabled entries are **engine-built-in pseudo-mods**
  (locale packs, official DLC mods, etc. — the engine populates them
  unconditionally regardless of `<game>/mods/` contents).
- First entry's `*begin` deref points to an I_Mod record; reading qword
  at that record's +0x00 yields a value that **matches Address Library
  id 3105 (ImodVtable_primary) in both boots** — confirming Outcome 1
  of the probe's outcome map.

### Scanned list at +0x18 / +0x20 / +0x28 — `std::vector<I_Mod>` INLINE

- **0x70-byte stride per element (records stored inline, NOT pointers)**.
- Boot 1: count=7. Boot 2: count=15. Delta = 8 (matches the 7
  user-installed mods + 1 — likely the user's mods + a built-in scanned
  entry not in boot 1, or a Steam Workshop entry the engine scanned).
- First record's vtable at +0x00 also matches id 3105 — confirming
  these are I_Mod records stored inline.
- The +0x28 slot is end_of_storage (slightly larger heap address than
  end in both boots).
- The walk-2 dual interpretation in the probe output yields:
  - count_a (8-byte stride): 98 / 210 (sane numerically but the vtable
    check fails — NOT the right interpretation).
  - count_b (0x70 stride): 7 / 15 (sane AND vtable check passes — the
    correct interpretation).

### What MOUNT actually iterates

Per seed row 3102 (ModManager_Mount): *"iterates the ENABLED wh::I_Mod
list (modMgr+0x30)"*.

So **MOUNT reads only +0x30/+0x38/+0x40 (the enabled list of pointers)**.
The scanned list at +0x18/+0x20/+0x28 is an intermediate working set used
by SELECT (the mod_order.txt selection pass + manifest parse) and is not
required at MOUNT time.

**Step 4 implication:** kcdx's replacement ctor can leave the scanned
list empty — kcdx synthesizes I_Mod records directly into the enabled
list, and MOUNT iterates them verbatim.

## Outcome map — which outcomes triggered

From `ctor_probe.h`:

- **Outcome 1 — CONFIRMED.** Walk 1 sane in both boots, first I_Mod's
  vtable matches id 3105 → +0x30/+0x38/+0x40 IS the enabled-list vector.
- **Outcome 2** — did not trigger (Walk 1 was sane, not garbage).
- **Outcome 3** — did not trigger (POINT B showed +0x30/+0x38/+0x40 zero;
  SELECT populates the vector, not the ctor).
- **Outcome 4** — did not trigger (CryString header at +0x10 read sanely:
  `{pad=0, nRefs=1, nLength=4, nAllocSize=4}`, content "mods").
- **Outcome 5** — did not trigger (+0x00 vtable's 8 function pointers
  all classified as code in both boots, `code_flags=11111111`).
- **Outcome 6** — partially triggered. +0x48/+0x50/+0x58 ARE zero in
  both boots, so they are NOT per-boot data fields; they are unused.

## Open follow-ups (NOT blocking step 2)

These need resolution before step 4 (the kcdx-owned bracket) lands, but
do NOT block step 2 (Steam Workshop walk in `pak_mod_registry`):

1. **The C_ModManager vtable address.** RVA `0x3AA2E60` (computed:
   `lea rax, [rip + 0x2d01f67]` at instruction `0x180da0ef9` →
   `0x180da0ef9 + 7 + 0x2d01f67 = 0x183AA2E60`, image base
   `0x180000000` → RVA `0x03AA2E60`). Needs a new Address Library row.
   The 8 function pointers in the vtable should be probed for slot
   semantics if any are called during MOUNT or downstream passes.

2. **The WHGame allocator.** `FUN_1804f7820` (RVA `0x004F7820`). Called
   by the ctor to allocate the 0x68-byte block. Needs a new Address
   Library row. Kcdx's replacement ctor uses this to allocate the
   C_ModManager so WHGame's destructor (if it ever runs) can free it.

3. **The CryString placement-construct helper.** `FUN_1804fd468` (RVA
   `0x004FD468`). Called by the ctor to construct the in-place
   CryString at +0x10 from a stack-local CryString. Needs a new Address
   Library row. Kcdx's replacement ctor uses this to construct the
   "mods" CryString at +0x10.

4. **The CryString init-from-string helper.** `FUN_1804f692c` (RVA
   `0x004F692C`). Called by the ctor to init the stack-local CryString
   from the modsDir arg. Already named `tmp_name_init` in phase7-recon
   work — needs a seed row.

5. **The console-cmd register call.** `FUN_180B99098` (RVA `0x00B99098`).
   Called by the ctor to register `wh_mod_GenerateReport`. If kcdx skips
   the original ctor, this command will not exist unless kcdx
   re-registers it OR explicitly chooses to drop it (it's a
   developer-facing mod-debug command, not a user-facing feature).

6. **What writes the "+0x48 = 15" / "+0x50 = 54" stack pattern the
   narrow probe misread.** Not architecturally important since these
   bytes belong to the caller's frame, but worth a sentence in the
   doc for future readers: this is CSystem::Init's local CryString
   ctor scratch — the header layout `{pad=0, nRefs=1, nLength=4,
   nAllocSize=4}` for the modsDir literal "mods" string.

7. **The C_ModManager destructor.** Not yet analyzed. Will be needed at
   game shutdown / save-reload — kcdx's replacement ctor must allocate
   via WHGame's allocator (#2 above) so the dtor's `free` call lines up.

## Files

- `_research/init-cycle-recon/disasm_modmanager.py` — capstone filtered-write
  scanner for the ctor + SELECT.
- `_research/init-cycle-recon/_disasm_modmanager.txt` — its output.
- `_research/init-cycle-recon/disasm_full_bodies.py` — full disassembly of
  the ctor, SELECT, and helpers.
- `_research/init-cycle-recon/_disasm_full_bodies.txt` — its output.
- `src/mod_absorb/ctor_probe.{h,cpp}` (commit `87a2a38`) — the live probe.
- `kcdx-engine/logs/kcdx-dev_2026-05-27_23-20-04.log` (boot 1).
- `kcdx-engine/logs/kcdx-dev_2026-05-27_23-23-09.log` (boot 2).
