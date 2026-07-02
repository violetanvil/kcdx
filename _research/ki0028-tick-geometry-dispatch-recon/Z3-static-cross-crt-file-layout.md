# KI-0028 PROBE Z3 (static tier) — kcdx `/MT` static CRT ≠ WHGame dynamic ucrtbase: a raw kcdx `FILE*` is NOT safe to hand the engine

**Date:** 2026-07-02 (static — no launch; results-driven §4 "static evidence before a live probe").
**Question (Z3):** can the engine's CRT operate a `FILE*` that kcdx's CRT opened? (The precondition on the settled Q2 fix "FOpen returns a real FILE*".)
**Answer (static, decisive):** NO — kcdx and WHGame link DIFFERENT CRT INSTANCES; a `FILE*` is CRT-instance-private. Handing WHGame's ucrtbase a kcdx-static-CRT `FILE*` is the KI-0019 cross-CRT straddle. **The raw-`FILE*` fix (Q2 option a as literally stated) is unsafe.**

## The two facts (both read this session, not recalled)

1. **kcdx links its CRT STATICALLY (`/MT`).** `CMakeLists.txt:384` `target_compile_options(kcdx PRIVATE /W3 /MT)` + `:385` `MSVC_RUNTIME_LIBRARY "MultiThreaded"` (and every vendored lib forced to match, `:386-392`). `/MT` bakes a private copy of the CRT into `kcdx.dll` — its own `_iob`/`__acrt_stdio_stream` table, its own heap, its own `errno`.

2. **WHGame links the CRT DYNAMICALLY (ucrtbase via the apiset).** `pefile` import scan of `third-party-ghidra/WHGame.dll`: imports `api-ms-win-crt-stdio-l1-1-0.dll` (→ `ucrtbase.dll`) for `_wfopen`/`fopen`/`fread`/`fseek`/`_fseeki64`/`ftell`/`_ftelli64`/`_fileno`/`fclose`, and `api-ms-win-crt-filesystem-l1-1-0.dll` for `_fstat64i32`/`_fstat64`. WHGame's `FILE*` values live in ucrtbase's stream table.

**A `FILE*` is a handle INTO the CRT instance that opened it.** On modern UCRT a `FILE*` is a pointer to that CRT's internal `__crt_stdio_stream_data`; `fileno`/`fread`/`fseek` dereference CRT-instance-private state (the fd table, the buffer, the lock). A `FILE*` from CRT-A passed to CRT-B's stdio functions is undefined — CRT-B never registered that stream. kcdx-static-CRT `FILE*` → WHGame-ucrtbase `fread`/`fileno` is exactly that.

## Why this is the SAME class as KI-0019

KI-0019 (closed): kcdx's CRT/GC freeing a WHGame-owned object → `0xC0000374` (heap corruption), because the two CRTs have DIFFERENT HEAPS and one CRT cannot free the other's allocation. Z3 is the stdio-stream analogue: the two CRTs have DIFFERENT STREAM TABLES, and one CRT cannot operate the other's `FILE*`. The `/MT`-vs-dynamic split is the root of both. This is why the takeover's §9 "no cross-CRT straddle" was the right instinct — but the handle-id it chose to satisfy §9 is precisely what the engine's raw-CRT reader at `0x460b64` bypasses.

## What this does to the Q2 fix (the settled "FOpen returns a real FILE*")

The LITERAL form of Q2-option-a — return kcdx's `_wfopen_s` `FILE*` — is **unsafe**: it hands WHGame's ucrtbase a foreign-CRT `FILE*`. It would trade the Z2.3-open `fileno`-on-an-int crash for a `fileno`/`fread`-on-a-foreign-`FILE*` crash/corruption — same class, no fix.

The fix must give the engine a `FILE*` **the engine's OWN ucrtbase opened / can operate**. Options (all keep full takeover; the design fork the architect already flagged as the pak-`FILE*` residual now applies to the LOOSE case too):

- **a′ — kcdx opens on the ENGINE's CRT.** kcdx calls WHGame's imported `_wfopen`/`fopen` (resolved from ucrtbase, the engine's instance) instead of kcdx's static `_wfopen_s`, so the returned `FILE*` is a ucrtbase stream the engine can `fread`/`fileno`. kcdx's own read slots then must ALSO operate it via ucrtbase (not kcdx's static CRT). Closes the class; the whole read family moves onto the engine's CRT for loose files.
- **b — hand the engine an OS `HANDLE` adopted onto its CRT via `_open_osfhandle` + `_fdopen` on the ENGINE's ucrtbase.** kcdx opens the OS handle (CreateFile), the engine's CRT adopts it into a ucrtbase `FILE*`. Same "engine's-CRT-owns-the-stream" property.
- **c — force the abstract-stream `[+0x110]` path** (the architect's Q2 option b) so the engine NEVER calls raw CRT on `[+0x108]` — sidesteps the cross-CRT `FILE*` question entirely by never letting the engine's raw-CRT reader run. But it only fixes the wrappers that HAVE the abstract stream; a raw-CRT consumer with `[+0x110]==null` still straddles.

The pak case is unchanged and still needs its own shape (an in-memory inflate has no `FILE*` at all) — but note a′/b make the loose and pak cases DIVERGE (loose → a real ucrtbase `FILE*`; pak → still a synthesized stream), whereas the handle model unified them.

## What is PROVEN vs still open

**PROVEN (static, this session):** kcdx `/MT` static CRT and WHGame dynamic ucrtbase are DISTINCT CRT instances with distinct stream tables (`CMakeLists.txt:384` + the WHGame import table). A raw kcdx-CRT `FILE*` handed to the engine's ucrtbase stdio is the KI-0019 cross-CRT class — unsafe.

**Still open (feeds the fix design, NOT a launch):** which of a′/b/c is the right shape — a design fork (does kcdx resolve + call ucrtbase's `_wfopen` for opens? adopt via `_open_osfhandle`? drive the abstract stream?). This is Gate A / the user's call, re-opened by this static finding. NO live probe is owed to establish the cross-CRT hazard — the static facts settle it; the launch would only confirm what the CRT-instance split already proves.

**The literal Z3 live probe (return kcdx's static `FILE*` and see if the engine reads it) is NOT worth running** — it would test a form the static evidence already shows is unsafe, and a "crash" outcome tells us nothing new while a "works" outcome would be a platform accident (the two static CRTs happening to share ucrtbase's stream ABI on this build) that we must NOT depend on. Skip the launch; design the fix on a′/b/c.
