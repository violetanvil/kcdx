# Phase 1 step 2 — does DirectStorage bypass the asset-resolution seam?

Captured 2026-06-03. Trust: PRIMARY EVIDENCE — every claim below is READ in a
WHGame.dll `release_1_5_1164953_841` decompile body (image base 0x180000000),
cited to the dump + line. NO live probe — this is a static call-graph question,
statically resolved. Reuse-first: this finding consolidates already-captured
reads (F1, F5, the phase8.5 CVar dump); no fresh Ghidra run was needed.

## The question (step-2 doc)

KCD2 has an OPTIONAL DirectStorage stream engine (gated by
`wh_sys_streaming_directstorage_enabled`). Does a texture served via the
DirectStorage arm open its own file handle (the DStorage API / a Win32
`CreateFile`) WITHOUT calling `CCryPak::FOpen` — i.e. does it bypass the
Around-FOpen seam (HOOK 1 resolver / HOOK 2 own-FILE*)? Confirm BEFORE the seam
ships, so "the seam serves all textures" is grounded, not assumed.

## (a) DS default — OFF, READ in the CVar registration

`_research/phase8.5-pak-resolver/_pakpriority_cvar_reg.txt:570`:

```
FUN_1823d7678("wh_sys_streaming_directstorage_enabled",&DAT_1849272d4,0,0,
              "Use direct storage stream engine.",0);
```

The registrar `FUN_1823d7678(name, &storage, defaultValue, flags, help, onChange)`
binds the cvar to int storage `DAT_1849272d4` with **default value `0`** (the 3rd
arg). DS is **OFF by default** in the verified build. (Re-confirms the F1 flag and
the gate-verifier note; cited to the registration line, not inferred.)

## (b) The bypass question — READ in the DS-selector body `FUN_180d2ad38`

The DS-vs-normal selector body, READ in
`_research/asset-loadpath-map-recon/_streamengine_readprim_raw.txt:60-102`
(`FUN_180d2ad38`, the "Fallbacking to normal stream engine instead." owner):

```
if (DAT_1849272d4 != 0) {                              // :73 — DS gate, SAME storage as the cvar
    lVar2 = (**(code **)(*DAT_18492b908 + 0xa28))();   // :74 — create DirectStorage StreamEngine
    *(longlong *)(param_1 + 0x6a8) = lVar2;
    if (lVar2 == 0) {
        FUN_180a607a4(8,0,
          "Error creating DirectStorage StreamEngine. Fallbacking to normal stream engine instead.");
    }
}
if (*(longlong *)(param_1 + 0x6a8) == 0) {             // :82 — DS slot empty → build normal engine
    ...                                                // :84-99
    uVar3 = FUN_180d2a2b8(lVar2);                       // :97 — CStreamEngine ctor (the normal engine)
    *(undefined8 *)(param_1 + 0x6a8) = uVar3;
}
```

Two READ facts settle the bypass question:

1. **The DS gate `DAT_1849272d4` is the SAME storage the cvar's default-0 binds**
   (the cvar registration above passes `&DAT_1849272d4`). Default 0 → the `:73`
   `if` is NOT taken → the DS StreamEngine is never created → control falls to
   `:82`, which builds the normal `CStreamEngine` via `FUN_180d2a2b8`. The
   DirectStorage arm is dead code at the default cvar value.

2. **The normal engine `FUN_180d2a2b8` (CStreamEngine ctor) is the SAME engine F5
   read end-to-end as routing through CCryPak.** F5
   (`F5-streaming-engine-bypass.md`) VERIFIED that the CStreamEngine read path
   (`ZipDir::ReadFileStreaming` `FUN_180464b88`, caller `FUN_1804647fc`) reads
   only from a pak the CCryPak resolver/mount already chose, and its fallback
   `FUN_180461a5c` is the ZipDir FRead/FSeek family — it has NO independent
   asset-file search/open. So when DS is OFF (the default), textures stream
   through the CCryPak-resolved path F1 mapped (CCryFile::Open → FOpen slot 36 at
   open; the FRead-family handle dispatch at read).

### What the DS arm would do IF enabled (not the common path; default-off)

When `DAT_1849272d4 != 0`, `:74` calls `(*DAT_18492b908 + 0xa28)()` to construct a
DirectStorage StreamEngine object stored at `param_1+0x6a8`. The DStorage API
opens its own file/queue handles via `dstorage.dll` (an EXTERNAL module),
NOT through `CCryPak::FOpen` slot 36. The interior of the DStorage engine's
per-asset open is NOT read this session — it lives in `dstorage.dll` /
the `DAT_18492b908`-vtable factory, outside the decompiled WHGame.dll bodies.
Marked **unverified — not read**: the exact DStorage open API and whether the DS
engine still resolves the pak via CCryPak before queuing. This is the residual DS
arm and is moot for v1 because the arm is default-off (see verdict).

## (c) Outcome → meaning verdict (against the step-2 pre-committed map)

**Branch 1 holds: "DS default-OFF AND a texture's normal load goes through FOpen."**

- DS default-OFF: READ, cited (`_pakpriority_cvar_reg.txt:570`, default arg 0;
  same storage `DAT_1849272d4` gates DS init at `FUN_180d2ad38:73`).
- Normal texture load goes through FOpen: READ by F1 (CCryFile::Open → ICryPak
  FOpen slot 36) + F5 (the streaming engine reads only the CCryPak-resolved pak;
  no independent open).

→ The seam covers textures on the common (default) path. DirectStorage is a
**non-default arm** → a documented v1 limitation, NOT a common-path gap.
**No seam change. Step 2 = DONE, DS scoped as a documented default-off
limitation.** No design fork is surfaced (Branch 2 does not hold — the bypass
arm is default-dead, not on the common path; Branch 3 does not hold — DS is
default-OFF, re-confirmed against the binary).

### One nuance carried forward (from F5, not new)

Even on the default path, the streaming engine's uncached fast-path mints its pak
HANDLE via `CreateFileA` at MOUNT time (archive factory slot 72,
`FUN_1804d6910`), NOT via FOpen slot 36 — so a streamed-from-pak texture's bytes
are reached through the pak the resolver chose, not through an Around-FOpen
kcdx FILE*. That is a RESOLUTION-lane property (own the pak the streamer mounts),
already mapped by F5, and is independent of DirectStorage. It does not change the
step-2 verdict: no texture class reaches its bytes through a file the CCryPak
resolver did not pick. The DS-specific question (this step) is closed default-off.

## Live launch needed?

**No — static-resolved.** The bypass question is a static call-graph fact: the DS
gate reads the same cvar storage the default-0 binding sets, so the DS arm is dead
at the default; the normal arm is the CCryPak path F1/F5 already read in-body. No
runtime observation is required to confirm DS is default-off or that the default
path goes through CCryPak. (A future runtime probe could confirm the DS arm's
interior IF a user ever enables the cvar — but that is out of v1 scope and not
needed to ship the seam.)

## AP18 — no seed rows written

No Address Library row added. The DS-arm functions (`FUN_180d2ad38` selector;
the `DAT_18492b908`-vtable DStorage factory) are FLAGGED only — kcdx does not own
the DS open/read for v1 (DS is default-off; owning resolver+mount already controls
the default path). Any future seed row needs user sign-off per AP18.
