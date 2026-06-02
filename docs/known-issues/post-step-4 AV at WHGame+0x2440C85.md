# post-step-4 AV at WHGame+0x2440C85 — virtual call on a vtable VA mistaken for a modMgr object

**Status:** RESOLVED 2026-05-28. The kcdx ctor bracket returned the heap
`obj` instead of `outResult`; the engine's install helper dereferenced the
heap ptr and installed the C_ModManager **vtable VA** into `csys[+0x2B30]`
as if it were the modMgr pointer. Frame-4 later dispatched on that VA and
walked `[vtable+0x30, vtable+0x38)` as if it were a `vector<I_Mod*>`,
AVing at WHGame+0x2440C85 trying to read `[code_bytes+0x60]` inside the
predicate `FUN_2440C6C`. Fix in one line in
[`src/mod_absorb/ctor_bracket.cpp`](../../src/mod_absorb/ctor_bracket.cpp):
`return obj` → `return outResult`.

## Symptom

Boot 2 post-step-4 (the kcdx ctor-bracket build, with crash #1 — the
I_Mod-vtable-null AV at `WHGame+0x244D085` — closed by commit `498934c`).
BugSplat MFA reports `WHGame!0243fc85`; the actual `.ecxr` RIP from the
minidump is `0x2440C85` (a 1-page delta — the MFA truncates the high
digit). Crash class is an access violation reading `[rcx+0x60]` where
`rcx` holds x86 instruction bytes, not a pointer.

```
.ecxr:
rip=00007ff8`f1250c85   (RVA 0x02440C85, NOT 0x0243FC85)
rcx=6c894808 245c8948   ← x86 instruction bytes "mov [rsp+8], rbx; mov [rsp+..],rbp"
rsi=00007ff8`f12452a0   (RVA 0x024352A0 — IN WHGame.dll's .text section)
rbx=00007ff8`f124e3f8   (RVA 0x0243E3F8 — also in WHGame.dll's .text/.rdata)
rbp=00007ff8`f44728c8   (RVA 0x036628C8 — matches the C_ModManager vtable VA logged at `ctor_bracket_complete`)

WHGame!NVSDK_NGX_UpdateFeature+0x858d95:
00007ff8`f1250c85 488b6960  mov rbp, qword ptr [rcx+60h]    ; <-- CRASH
```

Stack walk (game main thread):

```
WHGame+0x2440C85   FUN_2440C6C    ← crash: mov rbp,[rcx+0x60] with rcx=code bytes
WHGame+0x1DBC26B   FUN_1DBC230    ← iterates [begin,end) 8-byte stride, calls FUN_2440C6C per element
WHGame+0x1DBBE84   FUN_1DBBE20    ← reads `[this+0]` = begin, `[this+8]` = end, calls FUN_1DBC230
WHGame+0x6C662F0   C_Game::CreateInstance+0xC1FD0C — does `lea rcx,[rbx+0x30]; call FUN_1DBBE20`
WHGame+0x7A7A24    ...
KingdomCome.exe+0x3637 → 0x4AD5 → 0x898A → kernel32!BaseThreadInitThunk
```

Watchdog: exit `0xC0000005`, "SEH handler didn't run — crash class probably
bypassed it: fast-fail, kernel kill, etc." BugSplat's UnhandledExceptionFilter
wins the race against the kcdx SEH guard.

## The existing writeup's framing was empirically wrong

Three independent errors in the initial writeup, all caught by cdb on the
minidump:

| Writeup said | Reality (from `.ecxr` on the dump) |
|---|---|
| Crash RVA `0x0243FC85` | RVA is **`0x02440C85`**. BugSplat's MFA text-form `WHGame!0243fc85` truncated the high digit. cdb is authoritative. |
| "+0x14CD inside `ModManager_ParseManifest`" | The function at `0x02440C6C` is a small `find_if`-style predicate, ~0x20 bytes from prologue to crash. **`ModManager_ParseManifest` never appears on the call stack.** |
| "Something native is calling ParseManifest on a synthesized record" | ParseManifest is unreachable in the step-4 architecture (the bracket replaces ctor + SELECT entirely). The crash is in a completely different post-MOUNT dispatch chain. |

## What the dispatch chain actually is

From the minidump's stack walk + targeted static disasm (cdb on
`crash2.dmp` / `crash3.dmp`, and the dumps under `_research/init-cycle-recon/`):

1. `C_Game::CreateInstance+0xC1FCBA` calls a `CSystem`-method at RVA
   `0x019C6268`.
2. That method loads a `gEnv`-style global at RVA **`0x0492B8A8`**
   (`.data`, bss-zero at compile time, 2470 readers in `.text`, no direct
   `mov`-writers — written once at boot via an indirect store).
3. Virtual slot 23 (offset `0xB8`) on that singleton returns
   "the C_ModManager."
4. The result is treated as `C_ModManager*`; the code does
   `lea rcx, [this+0x30]; call FUN_1DBBE20` — a "lookup-by-name in the
   enabled-list `vector<I_Mod*>` at `this+0x30`."
5. `FUN_1DBBE20` → `FUN_1DBC230` runs a `std::find_if` over `[begin, end)`
   8-byte stride.
6. The predicate `FUN_2440C6C` reads `[I_Mod*+0x60]` of each element.
   **AV: rcx holds `0x6C894808245C8948` — x86 instruction bytes, not a
   pointer.**

## Facts (captured before resolution)

All facts captured from `cdb.exe -z C:/Users/Michael/AppData/Local/Temp/crash2.dmp .ecxr;r;k;u;ub;dps`.

- **The crash RVA in the initial writeup (`0x243FC85`) is wrong by one page.**
  Verified RVA is `0x2440C85` (`0xf1250c85 - 0xeee10000`). BugSplat's MFA
  text-form `WHGame!0243fc85` is the misleading source — the high digit is
  truncated by BugSplat's reporter.
- **`0x2440C85` is NOT inside `ModManager_ParseManifest`** (id 137, RVA
  `0x243E7B8`). The function starts at `0x2440C6C` — a fresh prologue
  (`mov rax,rsp; mov [rax+8],rbx; ...; sub rsp,0x30`) — so the crash is at
  function start + 0x19, not deep inside ParseManifest.
- **The crashing instruction is a virtual dispatch on the `this` arg.**
  `mov rbp, [rcx+0x60]` reads field +0x60 of `*rcx`. `rcx` was loaded from
  `*rsi` two frames up.
- **`rsi = 0x7ff8f12452a0` lives inside `WHGame.dll`'s `.text` section.**
  `dps` at that address dumps sequential function prologues
  (`48 89 5c 24 08 48 89 6c ...`). The first qword AT `rsi` is exactly
  `rcx`'s crash value `0x6c894808245c8948` — the iterator's first deref
  reads code bytes from .text. `rsi` is NOT a heap pointer.
- **The iteration is over `[rbx+0x30, rbx+0x38)` in 8-byte stride.**
- **The "this" passed to frame 4 is `rbx = 0x7ff8f1245270`, also inside
  `WHGame.dll`'s `.text`.** Reverse: `rsi = rbx + 0x30 = 0x7ff8f12452a0`
  confirms `rbx = 0x7ff8f1245270`. The engine called this method on what
  looked like an in-image struct, not a heap object.
- **kcdx's heap-allocated `C_ModManager` is at `0x2AE2FAAC820`** (per
  `ctor_bracket_complete obj=2947147197248`). That is NOT what the engine
  is dispatching on at frame 4.
- **kcdx built 79 enabled records, all via `BuildRecord`.**
- **rbp at the crash exactly matches the C_ModManager vtable VA the
  bracket logged.** `rbp = 0x7ff8f44728c8`, RVA `0x036628C8` — the
  C_ModManager vtable RVA. The engine had dispatched on the vtable, not
  on a modMgr object.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-05-28 | First read of the writeup framing (RVA → ParseManifest+0x14CD) | Treated as the leading theory. |
| 2026-05-28 | Pulled the minidump out of the NTFS ADS at `C:\Users\Michael\AppData\Local\Temp\Kingdom Come:<stream>`. Ran cdb `.ecxr; r; k 30; ub; dps`. | RVA is `0x2440C85` not `0x243FC85`; crash is at a function-start + 0x19, not deep inside ParseManifest; rcx is code bytes; rsi points into WHGame's .text section; rbx = `0x7ff8f1245270` also in .text. **The "ParseManifest" framing is falsified — the crash is in a different engine pass.** |
| 2026-05-28 | PROBE A (static): disasm frame-2/3/4 around the crash; identify GLOBAL_PTR, the dispatched-to virtual, and trace rbx provenance (`_research/init-cycle-recon/probe_a_static_singleton.py`). | Frame 4 (`FUN_19C6268`) is a "look up name in container at `rbx+0x30`" routine: it loads `global @ RVA 0x0492B8A8`, calls virtual+0xB8 → object, virtual+0x28 → name string, builds a local CryString from it, then `lea rcx, [rbx+0x30]; call FUN_1DBBE20` (find-by-name in vector at this+0). Frame 3 reads `[arg+0]=begin, [arg+8]=end`. The crash callee `FUN_2440C6C` reads `[rcx+0x58/+0x60]` of each I_Mod — a search predicate. **Interim: the iteration WAS over the I_Mod enabled-list vector at `&modMgr+0x30`. Static cannot tell whether `rbx` was kcdx's obj or a different modMgr; PROBE B (live) must disambiguate.** |
| 2026-05-28 | PROBE B (live, observe-only MinHook on frame-4 at RVA `0x019C6268`): log `rcx_arg`, `kcdx_obj`, every field at +0x00..+0x60, and the first 3 enabled-list slot derefs. | **Outcome 3 — DECISIVE.** `rcx_arg = 0x7FF8F28B2E60 = C_ModManager_vtable VA (RVA 0x03AA2E60)`, NOT kcdx's `obj = 0x1A6FCFCC000`. Every field read from `rcx_arg` is in WHGame.dll's `.rdata`/`.text`. `enabled[0]` slot value = `0x6C894808245C8948` (exactly the AV's rcx). The dispatch returned the VTABLE ADDRESS as if it were a C_ModManager object. The engine then "iterated `[vtable+0x30, vtable+0x38)`," reading bytes of the 7th vtable slot's CODE as if they were `I_Mod*` pointers. **The bracket built a modMgr the engine never read through this path.** |
| 2026-05-28 | PROBE B follow-up (static): identify `GLOBAL_RVA 0x0492B8A8` provenance. | Lives in `.data` (runtime-mutable), initial value 0 (bss-zero), **0 direct `mov [rip+rel32], reg` writers in `.text` despite 2470 readers**. The init writer is not a simple direct mov — likely an indirect store via `mov [rax], rbx` after `lea rax, [global]`, or via a thunk/relocation. The global is a CryEngine `gEnv`-pattern singleton pointer. |
| 2026-05-28 | Static disasm of the engine's post-ctor install helper at RVA `0x019DDCA4` (`_research/init-cycle-recon/disasm_install_helper.py`). | The helper body opens with `mov rax, [rdx]; mov [rdx], 0; mov rdi, [rcx]; mov [rcx], rax` — a unique_ptr-style move-assign that **dereferences rdx as a slot pointer** and stores its content into `*rcx`. The caller in `CSystem::Init` is `mov rdx, rax; mov rcx, r13; call FUN_1819DDCA4` — passing the ctor return as rdx. If the ctor returned a heap pointer instead of the outResult slot pointer, the helper would dereference the heap pointer and install `*heap[0]` = the vtable VA at +0x00. |
| 2026-05-28 | Re-read the native ctor disasm at `_research/init-cycle-recon/_disasm_full_bodies.txt:53-59`. | The native epilogue is `mov rbx, [rsp+0x48]; mov rax, rsi; mov rbp, [rsp+0x50]; ...; ret`. **`rsi` was captured at the top of the function from `rcx` (= arg1 = `outResult`).** The native return is `outResult` — the slot pointer — NOT the heap block (which lived in `rbx`). The bracket's own `return obj` at line 298 of ctor_bracket.cpp is one indirection off from the native ABI. **Root cause confirmed.** |

## Active diagnostic instrumentation

| File | What | Status |
|---|---|---|
| `src/mod_absorb/post_bracket_probe.{h,cpp}` | PROBE B: MinHook on `WHGame+0x019C6268` (frame-4 of the AV stack); logs once on first entry — `rcx_arg`, kcdx's `obj`, every `modMgr` field +0x00..+0x60, first 3 enabled-list slot derefs, first 3 `g_enabledList[]` derefs, and the singleton chain at `*0x0492B8A8` / vtable / `[vtable+0xB8]` getter pointer; tails to original. SEH-guarded on every deref. | **archived** (`#if 0` in place; the install call in `src/dllmain.cpp` is removed so the dormant probe does not install). Root cause: the bracket built a modMgr the engine never read through this path because the ctor's return ABI was wrong. |

## Resolution

**Root cause.** The kcdx ctor bracket returned the heap block pointer
(`obj`) at the end of `HookedCtor` instead of the `outResult` slot
pointer the caller passed in. The native
`wh::C_ModManager::C_ModManager` ends with `mov [rsi], rbx;
mov rax, rsi; ret` — where `rsi` was captured from arg1 (`rcx`, =
`outResult`) at the top of the function, and `rbx` held the allocated
heap block. The native ABI therefore writes the heap block into
`*outResult` AND returns `outResult` (the slot pointer), not the heap
block. The engine's call site in `CSystem::Init` immediately after the
ctor does

```
mov rdx, rax              ; rdx = ctor return value
mov rcx, r13              ; rcx = &csys[+0x2B30] (the install slot)
call FUN_1819DDCA4        ; unique_ptr move-assign install helper
```

and `FUN_1819DDCA4`'s first effective op is `mov rax, [rdx]` — it
**dereferences rdx**, expecting a slot pointer. With the bracket
returning the heap block pointer instead, the helper read
`*heap_block` = the heap modMgr's first qword = the **C_ModManager
vtable VA** that `HookedCtor` had just written at +0x00 of the heap
block. The helper then installed that vtable VA into `csys[+0x2B30]`
as if it were the modMgr pointer. Every later "get modMgr" path
(`*global @ RVA 0x0492B8A8` → virtual +0xB8 getter) returned the
vtable VA in place of a modMgr `this`; frame-4 of `CSystem::Init`
dispatched against it, did `lea rcx, [vtable + 0x30]`, called
`FUN_1DBBE20` which read `[vtable+0x30]` = begin and `[vtable+0x38]`
= end as an I_Mod** range, walked into the 7th vtable slot's CODE
bytes as if they were pointers, and AVed at `WHGame+0x2440C85` inside
`FUN_2440C6C` trying to read `[code_bytes+0x60]` for the predicate
compare. The bracket's own comment at lines 281-285 of
`ctor_bracket.cpp` had misread the native epilogue as "return obj" —
`rsi` was `outResult`, not the heap block.

**Fix.** One line in
[`src/mod_absorb/ctor_bracket.cpp`](../../src/mod_absorb/ctor_bracket.cpp)
(`HookedCtor`'s final return):

```cpp
return obj;        // previous — heap ptr, wrong ABI
return outResult;  // fixed — slot ptr, matches native `mov rax, rsi; ret`
```

The existing `std::memcpy(outResult, &obj, sizeof(obj))` immediately
before stays — it fills the slot the engine's install helper then
dereferences to retrieve the heap pointer. The misleading comment
block at lines 281-285 was rewritten to describe the real ABI and
name the crash mechanism so the next reader does not repeat the
misread.

**Verification.** The native ABI is verified against the binary:
`_research/init-cycle-recon/_disasm_full_bodies.txt:53-59` shows the
native ctor's `mov rax, rsi; ret` epilogue (rsi captured from rcx at
function entry); `_research/init-cycle-recon/disasm_install_helper.py`
shows `FUN_1819DDCA4`'s first effective op is `mov rax, [rdx]`. The
cap-61 test plugin
([`test-plugins/cap-61-init-cycle-ownership/plugin.lua`](../../test-plugins/cap-61-init-cycle-ownership/plugin.lua))
pins the regression: at `kcdx.on("ready")` it asserts the kcdx loader
pipeline reached the first update tick end-to-end past C_ModManager
construction. FALSIFIABLE on the current bug (the ctor never returns
cleanly past the install helper crash, so no first update tick fires
and no `suite: X/Y passing` line is emitted). Re-PASSes post-fix.

**Diagnostic archive.** PROBE B's runtime body was extended by one
SEH-guarded log line that walks the singleton chain at
`*0x0492B8A8 → *that[0] → *(vt+0xB8)` to settle the open question
about who writes the global and what the virtual+0xB8 thunk resolves
to. The probe's finding + full instrumentation wiring were then
captured to `_research/probe-archive/` (per the no-residue rule —
`working-artifacts.md`) and the `post_bracket_probe.{h,cpp}` source
pair removed from `src/` (the `InstallPostBracketProbe()` call site
was already gone). The archived wiring at
`_research/probe-archive/post_bracket_probe.cpp.txt` is the cheapest
jumping-off point for the next investigation into the same call site.

**Doc updates.** `docs/outstanding-work/init-cycle-ownership.md`
§"Crash #2" closed and pointed back here.
`docs/mod-loader-absorb.md`'s I_Mod field map gained the +0x58/+0x60
finding from the predicate disasm (a CryString-data substring range
pair the predicate reads — kcdx records zero this slot via the
existing memset, the predicate takes the equal-empty error-log branch
rather than the substring search). Cap-61's matrix row in
`test-plugins/README.md` notes the ABI fix landed.

## Files produced this investigation (preserved)

- `_research/init-cycle-recon/probe_a_static_singleton.py` — static
  disasm of the frame-2/3/4 dispatch chain.
- `_research/init-cycle-recon/probe_b_followup.py` — static walk of
  the gEnv-style global at RVA `0x0492B8A8` (writers + readers +
  relocation entries).
- `_research/init-cycle-recon/disasm_crash_site.py` — disasm around
  the crash function.
- `_research/init-cycle-recon/disasm_csys_init_around_ctor.py` —
  disasm of `CSystem::Init`'s call to the ctor + the post-call
  install helper invocation.
- `_research/init-cycle-recon/disasm_install_helper.py` — disasm of
  `FUN_1819DDCA4`, the unique_ptr-style install helper whose
  `mov rax, [rdx]` exposed the ABI defect.
- `_research/init-cycle-recon/disasm_validator.py` — disasm of
  `FUN_2440C6C`, the predicate that AVed (confirms it reads
  `[rcx+0x58]` then `[rcx+0x60]` then `cmp rbx, rbp`).
- `_research/probe-archive/post_bracket_probe.{cpp,h}.txt` — PROBE B
  wiring + finding (extracted from `src/` to the probe archive; the
  4-line header points back to this Resolution section).
