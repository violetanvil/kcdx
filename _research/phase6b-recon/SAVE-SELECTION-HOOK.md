# Phase 6b hook-target — "user selected this save to load"

Survey date: 2026-05-19. Game version: `release_1_5_1164953_841`.
Target binary: `WHGame.dll`, ImageBase `0x180000000`.

## Task

Find a function-entry hook target in `WHGame.dll` that fires when the
user confirms "Load" on a specific save row in the main-menu /
pause-menu "Load Game" screen, and that exposes either the savegame
filename or a slot/index we can resolve to a filename.

## Source survey

Methodology: starting from the `_research/phase6-save-load/_phase6_callers2.txt` finding
that `LoadGame_wrapper @ 0x1825BCEEC` has only two callers (the
orchestrator `0x180FBEE78` plus a second `0x181F8F440`), I walked the
"other path" backwards through the std::function lambda machinery,
then forwards through C_UISaveLoad's vtable methods and the Flash UI
event-binding table at `0x1807D5FC8` (the screen-init function that
calls `register_flash_event_handler("OnLoadButton", &handler)`).

The Flash UI binder uniquely identified `0x182BA7CE0` as the
"OnLoadButton" handler (single LEA xref to the string `"OnLoadButton"`
at `.rdata 0x183A53D78` lives at `0x1807D6097`, walked back to
`0x1807D5FC8`'s body, and the binding immediately preceding that LEA
loads the handler VA `0x182BA7CE0` via `lea rax, [rip + 0x23D1C50]`
at `0x1807D6089`).

A second viable candidate is the std::function-target lambda at
`0x181F8F440` — it's the ONLY non-orchestrator caller of
`LoadGame_wrapper`, meaning every menu-driven load flow funnels
through it before deserialization begins.

Raw probe outputs:
[`_abi_181F8F440_full.txt`](_abi_181F8F440_full.txt),
[`_abi_182BA7CE0.txt`](_abi_182BA7CE0.txt),
[`_abi_182BA712C.txt`](_abi_182BA712C.txt),
[`_abi_182BA9C54.txt`](_abi_182BA9C54.txt),
[`_abi_182BA99D8.txt`](_abi_182BA99D8.txt),
[`_uisaveload_vtable.txt`](_uisaveload_vtable.txt),
[`_callers_181F8F440.txt`](_callers_181F8F440.txt),
[`_callers_top.txt`](_callers_top.txt).

## Candidates

### #1 — TOP RECOMMENDATION: `C_UISaveLoad::OnLoadButton_FlashHandler` @ `0x182BA7CE0`

| | |
|---|---|
| Source | Discovered 2026-05-19 by Flash-binder string anchor + walk. `0x1807D5FC8` is `C_UISaveLoad::BindFlashEventHandlers` (in COL2 vtable slot 1 of the class, see [`_uisaveload_vtable.txt`](_uisaveload_vtable.txt)). It calls a generic Flash-binder helper at `0x180564C78` with three args: an `lea rax, [rip + handler_VA]` materialized just before the call, an `lea rdx, [rip + name_string]`, and the binder. For "OnLoadButton" (`.rdata 0x183A53D78`), the handler immediately above the LEA is `0x1807D6089: lea rax, [rip + 0x23D1C50]` → VA `0x182BA7CE0`. |
| Function name | `wh::guimodule::C_UISaveLoad::OnLoadButton_FlashHandler` (inferred — direct binding target for the Flash event named `"OnLoadButton"`) |
| Function entry VA | `0x182BA7CE0` |
| Function entry RVA | `0x2BA7CE0` |
| Calling convention | `__fastcall` — `rcx = C_UISaveLoad*`. Frame: `push rbp; lea rbp, [rsp-0x57]; sub rsp, 0xB0`. Reads `[rcx+0x120]` early (the screen-state object); reads `[stateobj+0xA1]` to branch on the menu mode (8 = one path, 9 = a different one with an extra string-decode step via `0x180561B4C`). Both paths converge on `call 0x182BA5584` with `r10=this, dl=opcode (6 or 0x13), r8=&load_request_struct`. |
| Inferred signature | `void __fastcall C_UISaveLoad::OnLoadButton_FlashHandler(this, int32_t playline_or_kind, int32_t save_slot)` — both `edx` and `r8d` are read as 32-bit values and stored into the stack-allocated load-request struct (`mov [rbp-0x15], edx` @ `0x182BA7D1A`, `mov [rbp-0x19], r8d` @ `0x182BA7D0A`). `r9` is not read. No stack args. |
| Tier-1 AOB (24 B) | `40 55 48 8D 6C 24 A9 48 81 EC B0 00 00 00 48 8B 81 20 01 00 00 4C 8B D1` — **1 hit**, unique |
| Tier-2 AOB (40 B) | `40 55 48 8D 6C 24 A9 48 81 EC B0 00 00 00 48 8B 81 20 01 00 00 4C 8B D1 8A 88 A1 00 00 00 80 F9 08 75 32 48 8D 05 CE BF` — **1 hit**, unique |
| Distinctive constants | `48 8D 6C 24 A9` (`lea rbp,[rsp-0x57]` — exact `-0x57` offset), `48 8B 81 20 01 00 00` (read [this+0x120] = screen-state object), `8A 88 A1 00 00 00` (read [stateobj+0xA1] = menu-mode byte), `80 F9 08 75 32` (compare to 8, branch). The `[this+0x120]` indirection makes this method uniquely identifiable across the binary. |
| String-anchor evidence | "OnLoadButton" string at `.rdata 0x183A53D78` has exactly ONE LEA xref in `.text` (at `0x1807D6097` inside `BindFlashEventHandlers`). The matching handler VA is loaded at `0x1807D6089: lea rax, [rip + 0x23D1C50]` → `0x182BA7CE0`. No other "OnLoad*" string in the binary binds to this handler. |
| Caller-chain evidence | Reached **only** through Flash UI dispatch (the binder registers the function pointer with the Scaleform/Flash runtime; no direct `E8 ... ?? rel32` callers in `.text` to `0x182BA7CE0`). This guarantees the function fires on the user's button click — there is no game-loop code path. |
| Thread context | **Main thread (static evidence)**. Flash event dispatch runs synchronously on the main thread (CryEngine's `IFlashPlayer::Advance` is called from the main game loop). No thread-spawn primitives in the body. |
| Firing-time analysis | Fires the instant the user releases the "Load" button on a save row, **before** any LoadGame_wrapper or PostLoadGame work. This is the canonical SKSE `kPreLoadGame` analogue with attached payload. The dispatch path goes: this handler → `0x182BA5584` (inner enqueuer) → eventually arrives at `LoadGame_wrapper` via the std::function lambda `0x181F8F440` (which is candidate #2 below). |
| Confidence | **HIGH** on function identity (single Flash-binder xref). **MEDIUM-HIGH** on argument semantics — `edx` and `r8d` are confirmed read as int32, but whether `edx` is `playline` (signed) and `r8d` is `save_slot` (matching `LoadLastSave`'s argument order: `mov edx, [rsp+0x28]; mov r8d, [rsp+0x2C]; call 0x182BA712C(this, playline, slot)` at `0x182BA7104..0x182BA7108`) is a strong inference but should be confirmed by dumping both values on first live fire. |
| Recommended pick | **Yes**. This is the cleanest function-entry hook for "user picked Load on a specific save row." Two int32 args are easy to forward to plugin callbacks. The slot can be resolved to a filename via the engine's existing accessor at `0x182BA6BE8` (calls `0x180B486BC` to enumerate saves of a playline and returns the per-slot metadata pointer). |

**TOML sketch:**

```toml
[[hook]]
name        = "phase6b_menu_load_button"
priority    = 100
module      = "WHGame.dll"
pattern     = "40 55 48 8D 6C 24 A9 48 81 EC B0 00 00 00 48 8B 81 20 01 00 00 4C 8B D1 8A 88 A1 00 00 00 80 F9 08 75 32 48 8D 05 CE BF"
offset      = 0

# void __fastcall C_UISaveLoad::OnLoadButton_FlashHandler(this, int32_t arg2, int32_t save_slot)
return_type = "void"
param_types = ["ptr", "int32", "int32"]

lifecycle_message = "kMenuLoadGameSelected"
```

**Drop-in C++ typedef:**

```cpp
// returns void (true tail flows through epilogue to ret; al unused)
using OnLoadButton_FlashHandler_t = void (__fastcall*)(
    void*   this_,                  // arg1 rcx — C_UISaveLoad*
    int32_t playline_or_kind,       // arg2 rdx — first Flash int arg (likely playline)
    int32_t save_slot);             // arg3 r8  — second Flash int arg (the save row)
```

---

### #2 — BACKUP: `lambda_LoadGame_dispatch` @ `0x181F8F440`

| | |
|---|---|
| Source | Identified in `_research/phase6-save-load/_phase6_callers2.txt` as the second (non-orchestrator) caller of `LoadGame_wrapper @ 0x1825BCEEC`. The pointer to this function lives at `.rdata 0x183EF1F00` (offset +0x20 inside a `std::function` instance — slot 4, the "invoke" slot). The `std::function`'s vtable record at `0x183EF1EE0` has shape `{copy, type_info_blob, copy_ctor, copy_ctor, invoke, target_type, deleter}` and resolves via the linker-generated `_Func_impl_no_alloc` machinery (the `.data 0x184CD23F0` blob is mangled `.?AV?$_Func_impl_no_alloc@V<lambda_b9cb0c0_da53b20ddd...`). |
| Function name | `wh::guimodule::lambda_load_savegame_dispatch` (compiler-generated lambda body; the closure captures a C_UISaveLoad* in `[rcx+0x08]` and the slot/reason as 32-bit fields in `[rcx+0x10]` / `[rcx+0x14]`) |
| Function entry VA | `0x181F8F440` |
| Function entry RVA | `0x1F8F440` |
| Calling convention | `__fastcall` — `rcx = lambda_closure_t*`. No standard MSVC prologue beyond `push rbx; sub rsp, 0x20`. Body is 55 bytes (0x37). |
| Inferred signature | `void __fastcall lambda_load_savegame_dispatch(void* closure)` where `closure` layout is `{ void* /*+0x00*/; void* context /*+0x08*/; int32_t slot /*+0x10*/; int32_t reason /*+0x14*/ }`. Disassembly: `mov rax, [rcx+8]; mov rbx, rcx; mov byte [rax+0x72], 0; call 0x18091C138 (env-get); mov r8d, [rbx+0x14] (reason); mov edx, [rbx+0x10] (slot); mov rcx, [rax+0x50] (SaveGameManager*); call 0x1825BCEEC (LoadGame_wrapper); mov rcx, [rbx+8]; movzx edx, al; pop rbx; jmp 0x182BA79F8 (UI result dialog).` Returns `void` via tail-jump. |
| Tier-1 AOB (24 B) | `40 53 48 83 EC 20 48 8B 41 08 48 8B D9 C6 40 72 00 E8 E2 CC 98 FE 44 8B` — **1 hit**, unique |
| Tier-2 AOB (40 B) | `40 53 48 83 EC 20 48 8B 41 08 48 8B D9 C6 40 72 00 E8 E2 CC 98 FE 44 8B 43 14 8B 53 10 48 8B 48 50 E8 86 DA 62 00 48 8B` — **1 hit**, unique |
| Distinctive constants | `C6 40 72 00` (`mov byte [rax+0x72], 0` — clear a pending-load flag), `44 8B 43 14 8B 53 10` (load reason into r8d then slot into edx), `48 8B 48 50` (fetch SaveGameManager via `[env+0x50]`), `E9 81 85 C1 00` (tail-jmp to `0x182BA79F8`, the UI result-display helper). |
| String-anchor evidence | None directly. Identified by pointer-presence in the `.rdata` std::function vtable at `0x183EF1F00`, and confirmed by being the ONLY non-orchestrator caller of `LoadGame_wrapper` (per `_phase6_callers2.txt`). |
| Caller-chain evidence | Zero direct `E8`-rel32 callers; reached exclusively through Scaleform/Flash UI's std::function dispatcher. Tail-jumps to `0x182BA79F8` (a `C_UISaveLoad`-area helper that decodes the bool result and shows a localized "load failed" dialog when applicable). |
| Thread context | **Main thread**. Lambda is dispatched from Flash UI which runs on the main thread; the env-get call at `0x18091C138` is the standard CryEngine main-thread TLS env accessor. |
| Firing-time analysis | Fires AFTER the menu confirm-dialog UI has run and IMMEDIATELY BEFORE `LoadGame_wrapper`. This is one frame downstream of candidate #1. The slot and reason are already resolved (the lambda's capture object was populated by the calling code at construction time). |
| Confidence | **HIGH** on identity and uniqueness (Tier-1 AOB is unique; ABI walker output confirms `[rcx+0x10]` and `[rcx+0x14]` are the only reads off the closure beyond `[rcx+0x08]`). **MEDIUM** on always-fires-for-menu-loads — needs live confirmation that this lambda is what the menu's "Load this save" button construction actually targets (vs. some other binding constructed at runtime). |
| Recommended pick | **Use as the v0.1 backup** if #1's argument-shape inference (playline vs. slot) doesn't pan out in live testing. The closure's `[rcx+0x10]` is unambiguously the slot value being passed to `LoadGame_wrapper`'s `edx` arg2. |

**TOML sketch:**

```toml
[[hook]]
name        = "phase6b_menu_load_lambda"
priority    = 100
module      = "WHGame.dll"
pattern     = "40 53 48 83 EC 20 48 8B 41 08 48 8B D9 C6 40 72 00 E8 E2 CC 98 FE 44 8B 43 14 8B 53 10 48 8B 48 50 E8 86 DA 62 00 48 8B"
offset      = 0

# void __fastcall lambda_load_savegame_dispatch(closure*)
# closure layout: { void*, void* ctx, int32_t slot @ +0x10, int32_t reason @ +0x14 }
return_type = "void"
param_types = ["ptr"]

lifecycle_message = "kMenuLoadGameDispatch"
```

**Drop-in C++ typedef:**

```cpp
struct LoadGameLambdaClosure {
    void*   _unused;     // +0x00 (likely vtable for std::function target)
    void*   context;     // +0x08 — env or C_UISaveLoad* surrogate
    int32_t slot;        // +0x10 — the save slot being loaded
    int32_t reason;      // +0x14 — load reason (passed as r8d to LoadGame_wrapper)
};

using lambda_load_savegame_dispatch_t = void (__fastcall*)(
    LoadGameLambdaClosure* closure);
```

---

## Worst-case fallback

If both candidates above fire at wrong moments or carry wrong semantics
in live testing, **the engine-level slot→filename accessor exists** at
`0x182BA6BE8` (called by `LoadLastSave` to convert a playline index
into a slot record). Signature observed via static walk:

```cpp
// returns int32_t (eax) — the slot ID for the first valid save of the given playline,
// or -1 if none found. Populates stack-local out-buffers at [rsp+0x30..0x40] which
// can be inspected to retrieve metadata. Internally calls 0x180B486BC to enumerate.
using GetFirstValidSaveSlotForPlayline_t = int32_t (__fastcall*)(
    void*    this_,    // C_UISaveLoad*
    int32_t  playline);
```

In the worst case, a plugin author could:

1. Hook `LoadGame_wrapper @ 0x1825BCEEC` (Phase 6a already does this — receives `edx = slot`, currently 0 in tests because Round-2 only triggered QuickLoad).
2. When `edx != 0` on entry, conclude this is a menu load and pass `edx` to plugin callbacks as the slot. Plugins resolve to filename via `GetFirstValidSaveSlotForPlayline` + sibling accessors that read `[savegame_record_ptr+offsetN]` for the filename string.
3. When `edx == 0`, fall back to caching SaveGame's last-seen filename (the existing Phase 6a mechanism).

This is uglier than hooks #1 or #2 because it ties slot semantics to a
single argument that the engine sometimes zeros, but it requires zero
additional hook surface and stays inside the Phase 6a `[[hook]]`
schema.

---

## Decision summary

| Candidate | Hook target VA | Args | Strength | Notes |
|---|---|---|---|---|
| **#1 (recommended)** | `0x182BA7CE0` | `(this, int32, int32)` | HIGH | Flash event-binder anchor; unique single LEA xref to "OnLoadButton" string |
| #2 (backup) | `0x181F8F440` | `(closure*)` w/ slot @ +0x10, reason @ +0x14 | HIGH | The only non-orchestrator caller of `LoadGame_wrapper`; fires one frame later than #1 |
| #3 (worst-case fallback) | reuse `0x1825BCEEC` from Phase 6a | `(this, slot, reason)` | MEDIUM | Conditional on `edx != 0`; risks false negatives on edge cases |

The two hooks together also give nice **pre-vs-immediate-pre** symmetry: #1 fires the moment the Flash button confirms (good for plugin pre-fly checks and sidecar staging), and #2 fires immediately before deserialization (good for plugins that need the resolved slot value with absolute certainty that the load will commence).

## Files in this folder

- [`SAVE-SELECTION-HOOK.md`](SAVE-SELECTION-HOOK.md) — this dossier
- [`_abi_*.txt`](.) — ABI walker outputs for each candidate
- [`_callers_*.txt`](.) — caller-chain audit outputs
- [`_uisaveload_vtable.txt`](.) — C_UISaveLoad RTTI vtable dump
- [`_find_uisaveload_vtable.py`](.) — vtable resolver script

## Open questions for live verification

1. **#1's arg2 semantics.** Is `edx` the playline (signed) or some other Flash-passed integer? The `[stateobj+0xA1] == 8/9` branch suggests `8 = main-menu load` vs `9 = pause-menu load`. The branch-9 path calls `0x180561B4C` to convert something then passes the result through `0x181F7FBE0` — this might be the "save row identifier from Flash" conversion. Confirm by dumping both `edx` and `r8d` on first fire and comparing to a known savegame slot.

2. **#1 fires on Continue too?** If "Continue" in the main menu re-uses the same Flash button binding, #1 will fire on Continue clicks as well. Live test should confirm whether Continue uses a separate `OnContinueButton` binding (search for `"OnContinue"` strings if not).

3. **#2 closure construction site.** Where is the `std::function` instance at `0x183EF1F00`'s offset constructed/populated? The closure's `[+0x10]` (slot) and `[+0x14]` (reason) must be written somewhere before invocation. If the menu code constructs a fresh lambda per click rather than reusing this static one, candidate #2 won't fire for menu loads at all. (Static analysis suggests it IS the menu path because LoadGame_wrapper has only TWO callers and the other is the game-loop orchestrator — but live test is the only authoritative answer.)

---

## ROUND 2 LIVE FINDINGS + FILENAME RESOLUTION

Live capture 2026-05-19 21:00 confirmed Candidates #1 and #2 fire on
every menu-driven load, but **neither one exposes a savegame
filename directly through its arguments**. The Flash UI's
"OnLoadButton" only carries two ActionScript ints; the SWF-side
selection state stays in ActionScript memory and is not handed to C++
at the moment the button fires.

### What the static recon turned up that the dossier missed

Re-walking the dispatcher chain `MenuLoadButton → 0x182BA5584
(inner_enqueuer) → 0x182BA1164 (deferred_dispatch) → lambda
@ 0x181F8F440 → LoadGame_wrapper @ 0x1825BCEEC → 0x1825BD1BC
(inner_load) → 0x1819DDE78 (slot_resolver) → 0x180703C0C (vector_get)`
showed three new facts:

1. **`LoadGame_wrapper`'s `(this, edx, r8d)` is NOT `(SaveGameMgr,
   slot, reason)`.** It is `(SaveGameMgr, playline_index, slot_index)`.
   `0x1819DDE78` does `lea rcx, [rcx + edx*72 + 8]` (playline stride
   72) and then jumps to `0x180703C0C` (vector indexer using
   `r8d` as the element index). The 100/125 values arriving as
   `r8d` are real **slot indices within playline 0**, not load reasons.
   The "reason" label in the live log is misleading — it's a slot
   index. (The user happens to have 100..125+ saves in playline 0,
   which is consistent with a regular playthrough.)

2. **The SaveGameRecord pointer comes back in `rax` from
   `0x1819DDE78`.** Its layout (observed from how `0x1825BD1BC`
   consumes it after the resolver returns):
   - `[record + 0x08]` — byte flag (type/state)
   - `[record + 0x40]` — qword pointer (path/filename buffer; passed
     as arg5 to a vtable call that emits log lines like
     `filename="%USER%/saves/playline0/exit.whs"`)
   - `[record + 0x50]` — int32 sub-count
   - `[record + 0x80]` — second string field (read by `0x1825BCC18`,
     likely the human-readable label)
   - `[record + 0x88]..[record + 0x90]` — `std::vector<...>` of
     64-byte sub-records (`(end - begin) >> 6` is the count)

3. **The MenuLoadButton handler never touches per-save data.** Its
   `edx` and `r8d` are written into a local `LoadRequest` struct
   (`[req+0x10]` and `[req+0x14]`) and forwarded to `0x182BA5584`.
   The inner dispatcher then *only* uses those values to populate a
   deferred-action record; the static template at
   `0x182B36010(opcode=6|0x13)` supplies all the other defaults. So
   the **filename is resolved exclusively inside `0x1819DDE78`** when
   the deferred action eventually executes — too late to capture from
   inside `0x182BA7CE0`.

### Revised typedef for MenuLoadButton

The dossier's `(playline_or_kind, save_slot)` guess was wrong on
direction. Empirically the args are *opaque* ActionScript ints — their
semantic interpretation depends on the menu mode at
`[stateobj+0xA1]`. The most defensible documented shape is:

```cpp
// rcx = C_UISaveLoad* (always same singleton: 0x1C618AC6340 in tests).
// Args 2 and 3 are Flash-supplied int32s; their meaning depends on
// the menu mode read from [this->screen_state @ +0x120][+0xA1]:
//   mode 8 → main-menu load   (path-8 of the if-cascade)
//   mode 9 → pause-menu load  (path-9, runs an extra string-decode step)
// Both modes treat arg2/arg3 as opaque selectors that the deferred
// dispatcher resolves against SaveGameManager state.
using OnLoadButton_FlashHandler_t = void (__fastcall*)(
    void*   this_,            // arg1 rcx — C_UISaveLoad*
    int32_t flash_arg2,       // arg2 rdx — observed: 100 main / 125 pause
    int32_t flash_arg3);      // arg3 r8  — observed: 0 in both cases
```

### Filename-extraction recipe

The filename cannot be reliably read from MenuLoadButton's `rcx` at
function entry. Two viable paths:

**Preferred — hook `0x1819DDE78` (the slot resolver):**

```cpp
// Hook 0x1819DDE78 with both pre + post (need rax post-call).
//
// At entry:
//   rcx = SaveGameManager::sub_object (= [SaveGameMgr + 0x48])
//   edx = playline_index   (multiplied by 72 to stride the playline array)
//   r8d = slot_index       (vector index within that playline's saves)
//
// At exit (rax = SaveGameRecord*):
//   filename_buf_va = *(uintptr_t*)((uint8_t*)record + 0x40);
//   // filename_buf is an std::string-like buffer; the actual char*
//   // is at [filename_buf + 0x00] for SSO-or-heap layouts seen
//   // elsewhere in this binary. Worst case dereference once more:
//   const char* filename = (const char*)filename_buf_va;
//   // For the human-readable label use [record + 0x80] instead.
```

`0x1819DDE78` is 24 bytes long (single LEA chain + tail jump). AOB:
`48 63 C2 48 8D 14 C0 48 8D 0C D1 41 8B D0 48 83 C1 08 E9 7D 5D D2 FE`
— **1 hit, unique**. The function fires once per load (immediately
before `0x180703C0C` returns the record), so a post-hook can capture
the filename in flight.

**Fallback — at MenuLoadButton entry, hold the args; resolve later:**

Cache `(C_UISaveLoad*, flash_arg2, flash_arg3)` from the MenuLoadButton
hook, then in a second hook on `0x1825BCEEC` (LoadGame_wrapper, already
hooked in Phase 6a) compute:

```cpp
// In LoadGame_wrapper(SaveGameMgr* sm, int32_t playline, int32_t slot):
auto* sub = *(void**)((uint8_t*)sm + 0x48);
auto* records_array = *(void**)((uint8_t*)sub + 0x48);
auto* playline_rec = (uint8_t*)records_array + playline * 72;
auto** vec_begin = (void**)(playline_rec + 8);
auto** vec_end   = (void**)(playline_rec + 16);
auto count       = vec_end - vec_begin;
if ((size_t)slot < (size_t)count) {
    auto* record = (uint8_t*)vec_begin[slot];
    auto filename_buf_va = *(uintptr_t*)(record + 0x40);
    // filename = (const char*)filename_buf_va (or deref once more for std::string SSO)
}
```

This avoids adding a third hook target but requires probing the std::
string layout (the existing Phase 6a SaveGame hook already does this
for `%USER%/saves/...` paths, so the indirection is known).

### Confidence + fallback

- **Confidence on `0x1819DDE78` being the right anchor**: HIGH. It is
  the single chokepoint where `(playline, slot)` resolve to a
  SaveGameRecord pointer. Its 24-byte body and uniqueness make the
  AOB stable across rebuilds.
- **Confidence on filename being at `[record + 0x40]`**: MEDIUM. Static
  evidence shows `[record + 0x40]` is consumed as `rbp` and passed as
  a stack arg to a vtable call that the Phase 6a SaveGame hook also
  observes emitting `%USER%/saves/...` strings. A single live probe
  on the new hook will confirm whether it's the filename or the
  full path. `[record + 0x80]` is the secondary candidate (passed
  through a string-copy helper at `0x1825BCC18`).
- **Confidence on the MenuLoadButton args being opaque**: HIGH. The
  100/125 values are inconsistent with any of the four documented
  modes (4/7/8/9) and consistent with raw save-slot ordinals from a
  long playthrough. The mismatch between dossier's "reason" and live
  capture's behavior is fully explained: the live log mislabels
  `r8d` of LoadGame_wrapper as "reason" when it is actually the slot
  index passed into the vector indexer.

### Recommended deployment

1. Keep the MenuLoadButton hook (#1) for the **lifecycle signal**
   "user confirmed Load on a save row" — it fires reliably and gives
   pre-deserialization timing. Don't trust its args for filename.
2. Add `0x1819DDE78` as the **filename-extraction hook**. It fires
   once per load, exposes `(playline, slot)`, and post-call exposes
   the SaveGameRecord pointer.
3. Plumb both into `kcdxMessage_LoadGameSelected` — MenuLoadButton
   provides the timing/intent edge; the slot-resolver provides the
   filename payload.

---

## ROUND 3 SHIP NOTES (2026-05-19, what actually shipped + engine behavior)

The Phase 6b production hook surface is `0x1819DDE78` (slot resolver)
ONLY. We dropped the MenuLoadButton and MenuLoadLambda probe hooks
after this round of live testing: the slot resolver fires reliably
on every load path including Continue (which DOES NOT trigger
MenuLoadButton), and reading `[record + 0x80]` is the
single-indirection const char* basename we want for plugin messaging.

### Confirmed by live probe (2026-05-19 ~22:52)

- `[record + 0x80]` is `const char**` pointing to the savegame
  **basename**. Live results from explicit menu loads of three
  distinct saves: `"exit.whs"`, `"save561.whs"`, `"autosave565.whs"`.
- `[record + 0x40]` is `const char**` pointing to the game version
  string the save was created with (`"1.5.5-15315-release_1_5"`).
  NOT the filename — the agent's MEDIUM-confidence guess was wrong.
- `[record + 0x00]` is the C++ vtable pointer
  (always `0x7FFCF5C525D0` after ASLR for this build). Same record
  type as PostLoadGame's arg3, confirming both reference the same
  C++ class.

### Engine load-path topology (matters for dedup)

The engine has **two distinct load paths** depending on whether
the player is currently in-world or at the main menu:

**Cold load** (main-menu Continue, main-menu Load Game):
1. `LoadGame_wrapper` fires (call it "bootstrap")
2. `SlotResolver` fires inside the wrapper, returns SaveGameRecord*
3. The engine reads the savegame off disk, hydrates the world
   (this takes 20-30 seconds for a non-trivial save)
4. `LoadGame_wrapper` fires AGAIN (the "actual deserialization"
   wrapper that walks the deserialized world state)
5. `SlotResolver` fires AGAIN — **same SaveGameRecord pointer
   as step 2** (the engine reuses the record)
6. `PostLoadGame` fires once

**Warm load** (ESC → Load Game while already in-world):
1. `LoadGame_wrapper` fires (just one — the engine already has a
   world loaded and is swapping state)
2. `SlotResolver` fires, returns SaveGameRecord*
3. `PostLoadGame` fires (~10ms after step 2)

**Cross-load isolation:** when the user loads the same savegame
file twice in a row (rare but legal), the engine allocates a
**new SaveGameRecord pointer** for the second load. Different
pointer, same basename. The dedup logic must use the pointer
identity, not the basename, to correctly fire one
`kcdxMessage_LoadGameSelected` per user-visible load.

### Dedup contract (shipping)

The Phase 6b hook keeps an `atomic<void*> g_last_resolved_record`
that's:
- Compared-and-set to the record pointer on each `SlotResolver`
  fire. If the CAS swaps `nullptr → record`, that's the FIRST
  fire for this load and we emit `kcdxMessage_LoadGameSelected`.
  If the CAS fails (already holding this record), it's a
  duplicate — skip.
- Cleared (`store(nullptr)`) on `PostLoadGame`. The NEXT user
  load gets a fresh window. **Critically: NOT cleared on
  `LoadGame_wrapper` entry** — clearing there would re-fire on
  the cold-load's second wrapper pass.

This gives exactly one `kcdxMessage_LoadGameSelected` per user-
visible load regardless of cold-vs-warm path, with the correct
filename in the message data.

### What did NOT make the ship

- `MenuLoadButton @ 0x182BA7CE0` — works for Load Game menu but
  doesn't fire for Continue. The slot resolver covers both paths.
- `lambda_load_savegame_dispatch @ 0x181F8F440` — fires on every
  load too, but doesn't carry the filename (just the closure with
  `slot=0` always observed). Slot resolver is strictly better.
- Args of MenuLoadButton (`edx`/`r8d`) — the agent guessed
  playline/slot but live data showed they're Flash UI-binding
  ints that don't correspond directly to (playline, slot). The
  load chain re-derives those values from the screen-state
  object via Flash event template tables. Not useful for our
  purposes.
