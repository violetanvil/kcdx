# Phase 8.5a - CryEngine pak/file resolver in WHGame.dll (KCD2 release_1_5_1164953_841)

> **Note (2026-05-28):** Paths referenced below have moved.
> `data/address-library/seed.csv` is now split into `data/seeds/address_names_seed.csv`
> + `data/seeds/address_versions_seed.csv` + `data/seeds/module_seed.csv`
> + `data/seeds/policy.md`. The address-library shape was also restructured —
> see those files and the schema docs at `data/reference.md` + `data/reference-dev.md`
> for the current authoring law. This file is a historical record and is not
> maintained against the new layout.

Goal: identify the function the game calls to open/resolve a game asset by virtual
path, so kcdx can hook it, consult a virtual-path->plugin-file overlay map, return a
replacement on a hit / fall through on a miss. Need the function (name + RVA), its ABI,
and how the ICryPak instance is reached at runtime.

Method: tiers 1-4 of the reuse ladder were exhausted by the dispatching brief (no
seed/_research/predecessor/wiki coverage of a pak resolver). This is tier-5 fresh
disassembly against the pre-analyzed Ghidra project
(third-party-ghidra/ghidra_project/KCD2.gpr) + reproducible pefile/capstone scripts.
Image base 0x180000000.

---

## THE RESOLVER FACT (VERIFIED)

The open-by-path resolver is CCryPak::FOpen, the ICryPak vtable method at slot 36
(vtable offset +0x120).

- Function RVA: 0x004614A0  (VA 0x1804614A0)  -- FUN_1804614a0
- ICryPak (== CCryPak) vtable: VA 0x183A95FA8 (RVA 0x03A95FA8), reached via the MSVC
  RTTI Complete-Object-Locator 0x1841758C0 whose type descriptor is .?AVCCryPak@@
  (name @ 0x184A40150, descriptor struct @ 0x184A40140).
- Slot index PROBED against the binary, NOT a canonical header (AP3): WriteCachePak
  0x182440D80 (CrySystem PlatformOS_PC.cpp) derefs the live pCryPak global and calls
  *(*pCryPak + 0x120) with a path + mode "wb" + flags 0x10004, checking the return for
  null = a file handle. 0x120/8 = slot 36; slot 36's pointer in the dumped vtable IS
  0x1804614A0.

### ABI

    ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)

- Convention: __fastcall member -- RCX=this, RDX=pName, R8=szMode, R9D=nFlags.
- Return: a CryEngine file handle (FILE*-like opaque ptr); 0/null on miss. Callers
  consume it as a handle and null-check it.
- Canonical CryEngine prototype matched:
  ICryPak::FOpen(const char* pName, const char* szMode, unsigned nFlags = 0).

### Evidence

1. Ghidra decompile of 0x1804614A0 (_fopen_confirm.txt, PakFOpenConfirm.java):
   ulonglong FUN_1804614a0(longlong *param_1, longlong param_2, char *param_3, uint param_4).
   Body: strlen loop over path param_2, then if (0x7ff < len) return 0; (CryEngine
   2048-byte path cap), then char-by-char parse of mode param_3 (matches a, +, sets
   0x8000/0x800000 open flags) -- canonical FOpen mode->flags parsing.
2. abi_walker prologue (_abi_1804614a0.txt, phase6_abi_walker.py): the prologue moves
   all four integer-class arg registers out before use -- mov r12,rcx (this), mov
   rdi,rdx + mov [rsp+0x30],rdx (path, walked by the strlen cmp byte [rdx+rax],sil),
   mov rbx,r8 + mov [rsp+0x28],r8 (mode), mov r15d,r9d (flags, 32-bit = u32). No
   5th/stack arg read. Corroborates the 4-arg fastcall and that flags is 32-bit.
3. Vtable family cross-check (_fopen_confirm.txt): WriteCachePak uses sibling slots on
   the same pCryPak -- +0x148 (slot 41) = FWrite (fwrite -> size_t), +0x1B8 (slot 55) =
   FClose (fclose -> int). Confirms +0x120 sits in the FOpen/FWrite/FClose family.
4. Vtable RTTI provenance (_probe_out.txt, find_pak_xrefs.py): the .?AVCCryPak@@ type
   descriptor has exactly one valid COL (0x1841758C0) whose meta-pointer (0x183A95FA0)
   precedes a 64+-entry table of all-.text pointers = the real vtable @ 0x183A95FA8.
   (A second COL candidate landed in string data -- rejected, not a real vtable.)

---

## gEnv -> pCryPak REACH (VERIFIED)

- gEnv already in the Address Library: seed id 1010, RVA 0x0492B800 (VA 0x18492B800),
  verified via muyuanjin's "exec autoexec.cfg" anchor.
- pCryPak offset = +0x50, CONFIRMED against the real KCD2 build (not just the
  predecessor env.h): the global ICryPak pointer that WriteCachePak and 350 other
  functions deref is DAT_18492b850 = 0x18492B850 = gEnv (0x18492B800) + 0x50. The same
  struct's other offsets are binary-verified in seed id 1010 (pGame +0x90, pConsole
  +0xA8), and +0x50 matches the env.h field order (pCryPak is field #11 = 10*8 = 0x50).
- Reach at runtime:
      ICryPak* pak = *(void**)(gEnv + 0x50);
      void* vtable = *(void**)pak;
      FOpenFn fn = *(FOpenFn*)((char*)vtable + 0x120);
  Hook either the vtable slot at vtable+0x120 (per-instance) or the function body at
  RVA 0x004614A0 (process-wide detour).

---

## CORROBORATION -- slot 36 is THE engine-wide open-by-path (read included)

PakFOpenCallers.java (_fopen_callers.txt): the pCryPak global has 680 xrefs across 350
distinct functions. At least 12 independent functions invoke
(*(*pCryPak + 0x120))(pCryPak, path, modeStr, flags). The mode strings at those sites
are real fopen modes: "rb" (0x183A53718), "ab" (0x1846D0984), "wt" (0x183B26650),
"w+b" (0x183DD0D58), "wb" (0x183DB31D4). The "rb" read-open call sites confirm +0x120
is the path the engine uses for asset reads, i.e. the right hook point for a loose-file
overlay.

---

## CONFIDENCE MAP

VERIFIED (static evidence above):
- The resolver is CCryPak::FOpen at RVA 0x004614A0, vtable slot 36 (+0x120).
- ABI ptr (ptr this, cstr path, cstr mode, u32 flags), __fastcall member, returns a
  handle / null.
- pCryPak = *(gEnv + 0x50); gEnv = seed id 1010 (RVA 0x0492B800); vtable = *pCryPak,
  FOpen at *pCryPak + 0x120.
- +0x120 is used for read opens ("rb"), not just writes. (12 call sites.)

NEEDS A LIVE PROBE before kcdx hooks it (runtime, not static):
1. Slot fires for a real asset load. Static proof shows the engine calls +0x120 with
   "rb" paths; a runtime probe (hook the slot, log pName on first fire while loading a
   known asset -- a .gfx/Lib/Scripts/**.lua) closes that the slot is on the live load
   path for the asset class kcdx wants to overlay. HIGH expected to pass; unconfirmed
   only because it is a runtime fact.
2. Loose-file vs pak-only resolution semantics. FOpen is the entry the engine routes
   opens through, but whether a hook returning a substitute handle (or rewriting pName)
   actually overrides a pak-resident asset depends on FOpen's internal precedence (the
   body consults param_1[0x45] pak structures + an open-mode table). The overlay
   strategy (rewrite path arg + call original vs return our own handle) is a DESIGN
   question for the hook step (out of scope); path-rewrite in particular needs a
   runtime probe confirming a rewritten pName resolves.
3. Thread-safety / call frequency. 680 xrefs implies a hot, possibly multi-threaded
   path. A runtime probe should confirm which threads hit it (AP6 relevance if a kcdx
   callback is involved) and the call volume.

Nothing here is invented (AP2): every field is backed by a decompile line, prologue,
vtable dump, or resolved string cited above. The slot is binary-probed (AP3), never
taken from a canonical header.

---

## PROPOSED Address Library rows (for the manager to land -- NOT written here)

Verify the current max id in data/address-library/seed.csv before assigning.
Append-only. gEnv reuses id 1010. A kEntries[] mirror entry must be added for each row
per address-library.md.

2006,1.5.1164953,0x004614A0,verified,CCryPak_FOpen,kcdx@phase8.5a,"ICryPak::FOpen(const char* pName, const char* szMode, unsigned nFlags) -> FILE*-like handle (null on miss). Engine-wide open-by-path resolver; all file opens (read+write) route through it. __fastcall member: rcx=this(ICryPak*), rdx=pName, r8=szMode, r9d=nFlags(u32). this = *(gEnv+0x50) (gEnv id 1010); also reachable as ICryPak vtable slot 36 (offset +0x120). Vtable RVA 0x03A95FA8 via RTTI .?AVCCryPak@@ COL 0x1841758C0. Verified tier-5 Ghidra+capstone: body does path strlen + 0x800 cap + mode-string parse; abi_walker confirms 4-arg fastcall, flags 32-bit; 12+ call sites pass (path, mode rb/wb/ab/wt/w+b, flags); sibling slots +0x148 FWrite, +0x1B8 FClose. See _research/phase8.5-pak-resolver/FINDINGS.md.","ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)"
2007,1.5.1164953,0x0492B850,verified,gEnv_pCryPak,kcdx@phase8.5a,"Static pointer slot gEnv->pCryPak (ICryPak*). RVA = gEnv (id 1010, 0x0492B800) + 0x50. Deref for the ICryPak instance; *pCryPak is the CCryPak vtable (RVA 0x03A95FA8); FOpen at vtable+0x120 (id 2006). Offset 0x50 confirmed against the real build: DAT_18492b850 is the global 350+ functions deref to call FOpen, == gEnv+0x50; consistent with seed-1010 verified offsets pGame+0x90 / pConsole+0xA8 and env.h field order. See _research/phase8.5-pak-resolver/FINDINGS.md.",""

---

## Artifacts in this directory

- find_pak_strings.py       step 1 -- pak/CryPak/RTTI string anchor scan
- find_pak_xrefs.py         step 2 -- LEA xrefs + RTTI COL->vtable walk (finds vtable 0x183A95FA8)
- dump_vtable_and_fopen.py  full 64-slot vtable dump (capstone)
- fopen_owner.py            .pdata-bounded owner of the FOpen error string (showed it is the WriteCachePak consumer, not FOpen itself -- a useful negative)
- PakResolverProbe.java     Ghidra: vtable dump + WriteCachePak decompile that revealed *(pCryPak+0x120)
- PakFOpenConfirm.java      Ghidra: decompile slot36/41/55 + mode string + gEnv offset check
- PakFOpenCallers.java      Ghidra: 12 real +0x120 call sites across the engine
- _probe_out.txt, _fopen_confirm.txt, _fopen_callers.txt, _abi_1804614a0.txt   raw outputs

(The three Ghidra scripts also live in third-party-ghidra/ghidra_scripts/.)
