# KI-0028 — LOADER-TRACE: which FS op populates the level record the abort tests

**Date:** 2026-06-22 · **Scope:** the single unverified edge #1/#3 from `VANILLA-MAP.md` — the loader that
fills `Game->[0x88]->[0x58]` (current-level record) / name@`[+0xc8]` from a file read. Image base
`0x180000000`; RVA = VA − base. Verdict grounded in OWNING-FUNCTION BODIES read this pass (AP19).

## 0. The abort getter — re-confirmed (body-read)

`0x66bbf0` (the getter `CET_PrepareLevel` calls at `0x183eb62`):
```
0x66bbf0: mov rdx,[rcx+0x88]   ; rcx = CryAction/Game singleton; rdx = ILevelSystem
0x66bbf9: test rdx,rdx; je ret(rax=0)
0x66bbfe: mov rax,[rdx+0x58]   ; rax = current-level RECORD
0x66bc02: ret
```
So the abort fires when `[ILevelSystem+0x58]` is null OR (downstream) the name CryString @`[record+0xc8]`
has length 0. The writer I traced is whatever fills `[ILevelSystem+0x58]` and that name — populated from
the level's metadata read.

## 1. The resource-list loader located — `CResourceList::Load` @ `0x4dcb60` (body-read)

Located by xref: `0x4dcb60` is the ONLY function that references all three adjacent .rdata strings the live
failing-run trace observed kcdx miss — `Levels/` (`0x183a3bd40`, ref @ `0x4dcbb3`),
`auto_resourcelist.txt` (`0x183a3bd00`, ref @ `0x4dcd1f`), `resourcelist.txt` (`0x183a3bd18`, ref @ `0x4dcd45`).
It also opens `engine.pak` (`0x4dcc66` → pCryPak vcall `[rax+0x88]`) and builds a `CResourceList` object
(`0x4dcc96` alloc, vtable `0x3a3bcc0` installed @ `0x4dcccd`).

Body (the FS read, verbatim):
```
0x4dcd1f: lea r8,"auto_resourcelist.txt"
0x4dcd29: call 0x4dd384            ; PATHBUILDER -> "<root>/Levels/<lvl>/auto_resourcelist.txt"
0x4dcd33: mov rax,[rbx]            ; rbx = CResourceList obj; rax = its vtable (0x3a3bcc0)
0x4dcd3e: call [rax+0x20]          ; vtable SLOT 4 = the resourcelist READER (0x4dd5e4)
0x4dcd41: test al,al; jne ...      ; success? else fall through to the second name:
0x4dcd45: lea r8,"resourcelist.txt"
0x4dcd54: call 0x4dd384            ; build "<root>/Levels/<lvl>/resourcelist.txt"
0x4dcd7f: call [rax+0x20]          ; same reader on the fallback name
```
Two candidate names tried in order (`auto_resourcelist.txt`, then `resourcelist.txt`) — EXACTLY the two
files the `20:49` full-swap trace logged kcdx return `loose_open_failed errno=2` / `how=miss-original` for.

## 2. The FS operation — vtable SLOT 36 FOpen (body-confirmed, D5)

The reader `CResourceList::Load`-slot4 `0x4dd5e4` opens the resourcelist file through the CCryPak open helper
`0x4605bc`, body-read:
```
0x4dd61a: mov rax,[0x492b850]      ; gEnv+0x50 = pCryPak global
0x4dd642: call 0x4605bc            ; OPEN HELPER (rdx=path, r9d=4 open-mode)
0x4dd647: test al,al; je fail
...
0x4dd687: call [0x549b480]         ; TLS read-buffer alloc (same pool 0x4dcb60 uses @0x4dcc96)
0x4dd6c3: call 0x460b08 ; 0x4dd705: call [0x3a03e88]  ; line-by-line read of the opened handle
```
Open helper `0x4605bc` body — the open is a CCryPak vtable call:
```
0x460636: mov rcx,[rdi+0x110]      ; the CryPak-family object
0x46064e: mov rax,[rcx]            ; its vtable
0x460654: call [rax+0x120]         ; slot 36 (0x120/8) = CCryPak::FOpen (0x4614A0) — mints a tagged handle
0x46065a: mov [rbx],rax            ; store the handle
```
**This is `FOpen` (CCryPak vtable slot 36, `0x4614A0`), the file-read open the prompt's D5 hypothesis names.**
The resourcelist is opened by name through slot 36 and read line-by-line. A kcdx miss on that open (the
observed `errno=2` / `miss-original`) leaves the CResourceList empty.

## 3. VERDICT — D5 (resourcelist read via FOpen), NOT D1

- **D5 evidence (FOUND, body-confirmed):** the loader reads `Levels/<lvl>/auto_resourcelist.txt` →
  `resourcelist.txt` via CCryPak slot-36 FOpen + a line-reader. This is the precise file + FS-op the failing
  full-swap run logged kcdx miss. The D5-discriminating observable ("an FOpen/FRead on a resourcelist path")
  is present and read from the body.
- **D1 evidence (ABSENT on this path):** the resourcelist reader `0x4dd5e4` and helper `0x4605bc` walk NO
  `+0x268`-populated registered-open-handle structure. The open is a direct slot-36 FOpen returning a tagged
  handle consumed locally; there is no enumeration of an engine open-handle registry to discover level files.
  The D1-discriminating observable ("a walk of the +0x268-populated structure feeding the record") is NOT
  present in the traced bodies.

The falsifying design held: D5 and D1 predicted DIFFERENT observables; the body shows the D5 observable
(FOpen on a resourcelist path) and not the D1 one (open-handle-registry walk). **Verdict: D5.**

## 4. The one edge still unverified — the record-WRITE link (carried forward, narrowed)

`CResourceList::Load` (`0x4dcb60`) BUILDS a `CResourceList` object; it does NOT itself write
`[ILevelSystem+0x58]` / name@`[+0xc8]`. It has ZERO direct callers (it is reached by virtual dispatch — it
is itself a vtable method), so the call-edge from "resourcelist read" → "level record populated" is not
attributable through a direct CALL in this corpus. What IS established: the resourcelist file kcdx misses is
read via slot-36 FOpen at level-load time (D5 mechanism confirmed); what remains unproven is that THIS read's
result is the value written into `[ILevelSystem+0x58]`/the name — vs. the resourcelist being one of several
level-metadata reads, with the record's name coming from a sibling read (e.g. the `/LevelInfo.xml` path at
`0x178baa9`, or a `SetCurrentLevel` writer not located).

**Exact next read to close it:** find the writer of `[ILevelSystem+0x58]` and `[record+0xc8]` (the
`CLevelSystem::SetEditorLoadedLevel`/`LoadLevel`/`SetCurrentLevel` family) and confirm whether its name/record
value derives from the `0x4dcb60` resourcelist read or from `/LevelInfo.xml` (`0x178b86c`/`0x178baa9`). The
record-writer was not pinned this pass; tracing it requires a `mov [reg+0x58]` / `[reg+0xc8]` write scan inside
the ILevelSystem-handling functions (started in `_recordwriter.txt`). Until that lands, D5 is the FS-OP verdict
for the resourcelist read (body-confirmed) but the resourcelist→record edge remains inferred (corpus FS trace +
this body), not body-proven.

## Provenance
- Worker scripts: `disasm_loader_trace.py` (string-xref), `disasm_xref.py` (rip-rel xref), `disasm_bodies.py`,
  `disasm_callers.py`, `disasm_recordwriter.py`. Raw dumps: `_loader_trace.txt`, `_xref.txt`, `_bodies.txt`,
  `_callers.txt`, `_recordwriter.txt`. Trust: body-read (primary), AP19-clean.
