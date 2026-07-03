# KI-0028 fix scope — route the Loose-arm CRT ops onto the engine's ucrtbase (settled fix a′)

**Date:** 2026-07-02. **Status:** fix NOT built. This is the scope map for the settled fix (Reframe 10 + Z3): kcdx opens loose files on the ENGINE's ucrtbase so the returned `FILE*` is a ucrtbase stream the engine's raw-CRT reader at `0x460b64` can operate. Pak stays kcdx-CRT in-memory (unchanged). The disassembler test does not bear (engine seam).

## The mechanism (new)

A **ucrtbase stdio function-pointer table**, resolved ONCE at fs-takeover init:
`HMODULE u = GetModuleHandleW(L"ucrtbase.dll");` then `GetProcAddress(u, "_wfopen")`, `"fread"`, `"_fseeki64"`, `"_ftelli64"`, `"feof"`, `"fwrite"`, `"fflush"`, `"ferror"`, `"fgetc"`, `"ungetc"`, `"fgets"`, `"_fileno"`, `"fclose"`, `"_get_osfhandle"`.
- **CHECKABLE UNKNOWN (probe = FIRST build step, incremental-delivery):** does `GetProcAddress(GetModuleHandleW(L"ucrtbase.dll"), "_wfopen")` resolve in-process? WHGame imports these via the apiset `api-ms-win-crt-stdio-l1-1-0.dll` (which forwards to `ucrtbase.dll`), and the game runs, so `ucrtbase.dll` IS loaded — but confirm the export names are on `ucrtbase.dll` directly (a `GetProcAddress` probe, agent-built/deployed, one launch). Outcome→meaning: all resolve → build the fn-table; any null → fall back to `LoadLibrary("api-ms-win-crt-stdio-l1-1-0.dll")` per-apiset resolution.
- Store the table behind an atomic-published pointer (concurrency.md: release on init-store, acquire on read).

## The OPEN change — `src/fs_takeover/open_slots.cpp`

- `OpenLooseAndMint` (line ~99): replace kcdx's static `_wfopen_s(&fp, wpath, wmode)` with the ucrtbase table's `_wfopen(wpath, wmode)` → `fp` is now a ucrtbase stream. (`_wfopen_s` may not be exported by name from ucrtbase; use `_wfopen` — the non-`_s` form IS imported by WHGame. Confirm in the probe.)
- `MintLoose(fp, vpath)` stores the ucrtbase `fp` unchanged — the pool slot is representation-agnostic; only WHICH CRT owns `fp` changes.
- The kcdx handle-id return is UNCHANGED for kcdx's own read slots. BUT the engine's raw-CRT reader reads FOpen's return directly — so FOpen must now return the **ucrtbase `FILE* fp`** for a LOOSE hit (not the handle-int), while kcdx's read slots still need to find the slot. DESIGN POINT for Gate A: the loose return is now a real ucrtbase `FILE*` (the engine operates it via ucrtbase); kcdx's read slots must map a `FILE*`→slot for loose (a `fp`→OpenFile side table), OR the read slots become thin wrappers over ucrtbase ops on `fp` directly. Pak still returns the handle-int (no `FILE*`). => loose and pak returns DIVERGE (loose = ucrtbase `FILE*`, pak = kcdx handle-int). The engine's tag test (`handle-1 < pakEntryCount` → pak arm, else OS/`FILE*` arm) must route a loose `FILE*` to the OS arm and a pak handle to... — RE the engine's FRead dispatch to confirm a pak handle-int lands on a kcdx-owned slot, since pak has no ucrtbase stream. THIS is the core Gate-A design question.

## The READ-family change — `src/fs_takeover/file_handle.cpp` (Loose arm only; Pak arm untouched)

Every `s->kind == OpenFile::Kind::Loose` op currently calls kcdx's STATIC CRT on `s->fp`; each must call the ucrtbase table instead (so a ucrtbase-opened `fp` is operated by ucrtbase):

| Op | Line | kcdx static call → ucrtbase table call |
|---|---|---|
| Read | 232 | `std::fread` → `u.fread` |
| (ferror check) | 233 | `std::ferror` → `u.ferror` |
| Seek | 263 | `_fseeki64` → `u._fseeki64` |
| Tell | 304 | `_ftelli64` → `u._ftelli64` |
| Eof | 320 | `std::feof` → `u.feof` |
| Write | 344 | `std::fwrite` → `u.fwrite` |
| Flush | 360 | `std::fflush` → `u.fflush` |
| Error | 373 | `std::ferror` → `u.ferror` |
| Getc | 393 | `std::fgetc` → `u.fgetc` |
| Ungetc | 404 | `std::ungetc` → `u.ungetc` |
| Gets | 420 | `std::fgets` → `u.fgets` |
| Fileno | 439 | `_fileno` → `u._fileno` |
| FileSize (loose) | 466-474 | `_ftelli64`/`_fseeki64` pair → `u.*` |
| GetModificationTime | 498 | `_fileno` → `u._fileno` (then `_get_osfhandle`→`GetFileTime` — `_get_osfhandle` also from ucrtbase) |
| GetCachedFileData (loose read) | 530-549 | `_fseeki64`/`_ftelli64`/`std::fread` → `u.*` |
| Close | 585 | `std::fclose` → `u.fclose` |

**Pak arm (in-memory buffer/cursor) is entirely untouched** — no CRT `FILE*`, so no cross-CRT concern; it keeps returning the handle-int.

## Header comment to correct (deletion-hygiene / the falsified premise)

`file_handle.h:262-267` (`Fileno`) claims "fileno is reached internally by FGetSize/FGetModificationTime via `_fileno`, never as a dispatched slot" — the Z2.3-open crash FALSIFIED this (the engine called `_fileno` on FOpen's return directly, not via a kcdx slot). Correct this comment in the fix (it asserts the very premise P3 got wrong). The `open_slots.cpp:22-26` "no engine-CRT handle is ever minted or returned" comment also needs correcting — the fix DELIBERATELY returns a ucrtbase `FILE*` for loose.

## Build order (incremental-delivery, each step independently verifiable)

1. **Probe** — ucrtbase `GetProcAddress` resolves in-process (one launch, outcome map above).
2. **Fn-table** — resolve + atomic-publish the ucrtbase stdio table at init (unit-testable: assert non-null after init).
3. **Loose open** — `OpenLooseAndMint` opens via `u._wfopen`; FOpen returns the ucrtbase `FILE*` for loose. (test: a loose override opens + the engine reads it — the cap-113 fs-takeover-lifecycle plugin + a live launch.)
4. **Loose read family** — reroute the table above; kcdx read slots map `fp`→slot (or wrap ucrtbase directly).
5. **Verify** — the KI-0028 black screen is gone on the full-swap arm (the DRAW_PROBE `draw_indexed` climbs past 0), Gate B root-cause-verifier on the Resolution.

## Gates owed

- **Gate A (architect-review)** on the concrete shape BEFORE build — the core question is the loose/pak return divergence + the engine's FRead dispatch routing (does a pak handle-int still land on a kcdx-owned slot once loose returns a real ucrtbase `FILE*`?). This needs an RE read of the engine's FRead/FSeek dispatch (the `handle-1 < pakEntryCount` tag test) to confirm pak handles still route to kcdx.
- **Gate B (root-cause-verifier)** on the Resolution paragraph before close.
- **`/design`** — re-open the FS-takeover TRD P3/§4.4 (falsified) + §9 (the "no engine-CRT handle returned" invariant is deliberately relaxed for loose): present-tense body edit + changelog.
