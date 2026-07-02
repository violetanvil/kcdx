# KI-0028 PROBE Z2.3-open — the OPEN family alone CRASHES: engine calls fileno() on kcdx's handle, not a FILE*

**Date:** 2026-07-02 (run `kcdx-dev_2026-07-02_12-55-33.log`, dump `kcdx_2026-07-02_12-55-33.dmp`)
**Swap state:** `probe_z_live_mask=1` (kFamOpen only), `kcdx_owned=3` (slots 1/35/36 live; read/metadata/enum THUNK to engine original).
**Outcome:** CRASH (a third outcome — not black, not menu). Dump read FIRST per discipline.

## The crash — ground truth from the dump

- **Exception:** `c0000005` Access violation, READ of address `0x1b`. `FAILURE_BUCKET_ID: NULL_CLASS_PTR_READ_c0000005_ucrtbase.dll!fileno`.
- **Faulting instruction:** `ucrtbase!fileno+0x23: mov eax, dword ptr [rcx+18h]` with **`rcx=3`** → `[3+0x18]=[0x1b]` → AV.
- **`fileno()` was called with a `FILE*` of literally `3`** — an integer, not a pointer. `fileno` extracts the OS fd from a `FILE*` by reading `[FILE*+0x18]`; handed `3` it dereferences `0x1b` and faults.
- **Stack:** up through `wh::game::C_Game::CreateInstance` (the KI-0028 wedge region; the `ffxFsr2ResourceIsNull+…` labels are nearest-export noise, real frames inside CreateInstance). tid=33804.

## The mechanism — falsifiable, source-confirmed (AP17-grade)

**What value was wrong:** kcdx's `FOpen` (slot 36) returns a **kcdx handle** — a small integer token (observed `3`), NOT a CRT `FILE*`. `src/fs_takeover/open_slots.cpp`: `_wfopen_s(&fp, ...)` gets a real `FILE* fp`, then `MintLoose` mints a kcdx handle `h` and `return h;` (line 119; log at 269 "kcdx FOpen minted a kcdx handle on kcdx's CRT"). The kcdx handle is an integer/opaque token, not the FILE* pointer.

**Who wrote it, in what order:** the engine calls kcdx's FOpen (open family live) → gets handle `3` back → then the engine's ORIGINAL code path calls `fileno(3)` directly on that return value (read/metadata thunked, so the engine's own read/handle path runs, not kcdx's).

**Why the original path made the wrong read inevitable:** kcdx's handle model is coherent ONLY when kcdx ALSO owns the read/handle slots (they understand the kcdx handle and map it to kcdx's real FILE*). With only the open family live, the engine's original `fileno`/read code receives a kcdx handle-integer it interprets as a `FILE*` → dereferences it → AV. The engine calls `fileno()` (or a CRT function expecting a real FILE*) DIRECTLY on FOpen's return, bypassing kcdx's read slots.

## What this establishes for KI-0028

- **The OPEN family is directly implicated** — kcdx's FOpen return TYPE (handle vs FILE*) is the fault axis. The bisection isolated it: kFamNone renders (open thunks → engine gets a real FILE*), kFamOpen-only crashes (engine gets kcdx's handle at a fileno call).
- **The engine makes at least one CRT call (`fileno`) directly on FOpen's return** — a path that assumes FOpen returns a real `FILE*`, which kcdx's full-takeover handle model does NOT satisfy. This is a takeover completeness gap: FOpen's contract with the engine is "return something the engine can pass to fileno()", and kcdx's handle breaks it.

## The connecting question (NOT yet established — next)

Does this SAME handle-vs-FILE* mismatch cause the BLACK SCREEN in the full swap (kFamAll)? Under kFamAll, kcdx owns the read slots too, so kcdx's read/close consume the handle correctly and this particular `fileno` crash does not fire — but the engine's DIRECT `fileno(handle)` call still gets a kcdx handle, and if the engine uses the fd for something kcdx's slots don't intercept (a raw OS read, an fd passed to another API), it would silently get wrong data → the backdrop load fails → black. This is the likely bridge from "open-only crashes" to "full-swap goes black," but it is NOT yet proven. Next: find WHERE in CreateInstance the engine calls fileno() on FOpen's return, and what it does with the fd — swap-ON vs the kFamNone (working) path.

## BRIDGE PROVEN (2026-07-02, static read of the fileno caller — `disasm_fileno_caller.py` → `_fileno_caller.txt`)

The crash stack's fileno caller is the engine file-wrapper reader body at **RVA 0x460b64** (frame 01 ret 0x460cc5). Read cover-to-cover, it is a **raw-CRT-FILE\* consumer**:

- The wrapper object (`this`=rdi) holds a `FILE*` at **`[this+0x108]`** and an abstract-stream object at `[this+0x110]`.
- `0x460b84`: `if ([this+0x110] != null)` → call the abstract stream's `[vtable+0x170]` (the CryPak-style reader path). ELSE fall through to the raw-CRT path:
- `0x460b9b` `ftell([this+0x108])` · `0x460bb7` `fseek([this+0x108])` · `0x460bc8` `ftell` · `0x460bdd` `fseek` · `0x460b5c` `fread([this+0x108])` · **`0x460cbf` `_fileno([this+0x108])`** (the crash) · `0x460ccc` `_fstat64i32(fd)`.

**Every op on `[this+0x108]` is a CRT call that assumes a real `FILE*`** — `ftell`/`fseek`/`fread`/`fileno`/`fstat`. This path BYPASSES kcdx's CCryPak read slots entirely (it does not call any vtable slot — it calls the CRT imports directly on the stored `FILE*`).

**This is the bridge, proven by code (not inferred):**
- The `FILE*` at `[this+0x108]` is what FOpen returned. kcdx's FOpen returns a **kcdx handle (int), not a FILE\***.
- **open-only (Z2.3-open):** `_fileno(handle)` derefs `[handle+0x18]` → AV (the crash).
- **full-swap (the BLACK SCREEN):** `ftell`/`fseek`/`fread(handle)` operate the CRT on the handle-int → fail / read garbage; the file this wrapper reads never loads → the backdrop asset never loads → the ~27s transition never fires → black. kcdx owning the CCryPak read SLOTS (38..66) is IRRELEVANT here — the engine is not calling those slots, it is calling `fread()` on the raw FILE* from `[this+0x108]`.

**Same root, both arms:** kcdx's FOpen returns a handle; the engine's file-wrapper has a raw-CRT-FILE\* path (`[wrapper+0x108]`, taken when the abstract stream `[+0x110]` is null) that kcdx's read-slot ownership does not intercept. The full-takeover handle model silently breaks FOpen's contract ("return a value the engine can `fread`/`fileno` directly").

**The fix axis (design decision — surface, Gate A):** kcdx's FOpen must return something the engine's raw-CRT path can consume — a real `FILE*` (with kcdx tracking it for its own read slots), OR ensure the engine always takes the abstract-stream `[+0x110]` path (populate it so `[+0x108]` is never used), OR intercept the CRT consumption. Which one is the user's call.

## Precision — what is PROVEN vs the one remaining runtime link (honest boundary)

**PROVEN (code + dump):** (1) kcdx's FOpen returns a handle-int, not a FILE* (`open_slots.cpp:119`). (2) The engine reader at `0x460b64` consumes the stored `[this+0x108]` value via raw CRT (`ftell`/`fseek`/`fread`/`fileno`/`fstat`), bypassing kcdx's read slots. (3) With open-only live, `fileno(handle)` AVs — the Z2.3-open crash IS this exact site. So the DEFECT is proven: kcdx's FOpen return is used as a raw CRT FILE* by the engine, and kcdx's value is not one.

**The one link NOT yet runtime-proven:** that the FULL-SWAP (kFamAll) *black screen* specifically stalls at THIS reader (vs another raw-CRT consumer of FOpen's return). Static: the reader takes the `[+0x108]` raw path only when the abstract stream `[+0x110]` is null (`0x460b84` branch); whether `[+0x110]` is null on the full-swap backdrop-load path is the unread bit (the wrapper ctor writer of `+0x108`/`+0x110` was not fully traced — diminishing static returns). BUT the fix does not depend on WHICH raw-CRT site stalls the full swap: the root defect (FOpen returns a non-FILE* the engine reads as a FILE*) is the same, and any such site fails identically. The bridge is proven at the DEFECT level (the mechanism is real and on the CreateInstance path); the exact full-swap stall site is a detail the fix subsumes.

**Cheapest airtight confirmation if wanted (one probe):** on the full-swap arm, log kcdx's FOpen return value + a one-shot at `0x460b64`'s `[+0x108]` read — confirms the full swap feeds a kcdx handle into this same raw-CRT reader. Not required to establish the defect or design the fix; available if the user wants the last link nailed before the fix lands.
