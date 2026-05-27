# Phase 6 hook-target candidates — save/load/delete lifecycle

Survey date: 2026-05-19. Game version: `release_1_5_1164953_841`.
Target binary: `WHGame.dll`, ImageBase `0x180000000`.

> **Live-update 2026-05-19 — Round 2 probe results corrected several
> static-analysis predictions in this document.** Function VAs and
> Tier-2 AOB sigs are confirmed correct against the live build. The
> *inferred function signatures* below were partly wrong; see the
> "ROUND 2 LIVE FINDINGS" section at the end of this document for
> the authoritative corrections. The original analysis is preserved
> in its as-written form below for traceability — do not rely on the
> "Inferred signature" rows when implementing; cross-reference with
> the live findings.

## Task

Find four function-entry hook targets in `WHGame.dll` suitable as `[[hook]]`
sites for kcdx Phase 6's SKSE-style save/load lifecycle messages. Requirements
(per Phase 6 design):

- One hook per lifecycle message: `kSaveGame`, `kPreLoadGame`,
  `kPostLoadGame`, `kDeleteGame`.
- Each target must be reachable as a unique AOB in `.text` (Tier-2 40-byte
  fallback when Tier-1 24-byte collides with class siblings).
- Main thread (per hard rule #16 in [kcdx/CLAUDE.md](../../../CLAUDE.md)).
- Standard MSVC `__thiscall` (`rcx = this`, additional args in `rdx`/`r8`/`r9`).
- One fire per operation, no flooding.
- No schema changes to kcdx's existing `[[hook]]` TOML block (function-entry
  detour only; no vtable hooks, no runtime listener registration).

## Source survey

Methodology mirrors [`predecessor-sigs/CAP-03-CANDIDATES.md`](../predecessor-sigs/CAP-03-CANDIDATES.md):
string anchor → LEA xref → function-entry walk → prologue sig → uniqueness
verify. Full methodology + alternatives in [`FINDINGS.md`](FINDINGS.md).
Audit outputs in
[`_phase6_final_verify.txt`](_phase6_final_verify.txt),
[`_phase6_xrefs_pass1.txt`](_phase6_xrefs_pass1.txt),
[`_phase6_xrefs_pass2.txt`](_phase6_xrefs_pass2.txt),
[`_phase6_xrefs_pass3.txt`](_phase6_xrefs_pass3.txt),
[`_phase6_xrefs_delete.txt`](_phase6_xrefs_delete.txt),
[`_phase6_callers.txt`](_phase6_callers.txt),
[`_phase6_callers2.txt`](_phase6_callers2.txt),
[`_phase6_strings_out.txt`](_phase6_strings_out.txt),
[`_phase6_rtti.txt`](_phase6_rtti.txt).

All four recommended candidates are methods of
`wh::framework::C_SaveGameManager` (RTTI type descriptor at `.data 0x184A66630`,
[`_phase6_rtti.txt`](_phase6_rtti.txt) line 59).

## Candidates

### #1 — `kSaveGame` → `C_SaveGameManager::SaveGame` (RECOMMENDED v0.1)

| | |
|---|---|
| Source | Discovered 2026-05-19 via [`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) on string anchors `"SaveGame: '%s' %s. [Duration=%.4f secs]"` (`.rdata 0x184779088`) and `"Saving failed : player is dead!"` (`.rdata 0x184779028`). |
| Function name | `wh::framework::C_SaveGameManager::SaveGame` (inferred from class RTTI + exclusive ownership of two save-specific log strings) |
| Function entry VA | `0x183581B04` |
| Function entry RVA | `0x3581B04` |
| Calling convention | `__thiscall` — `rcx = C_SaveGameManager*`. Prologue (`mov r11,rsp; mov [r11+8],rbx; mov [r11+0x18],rsi; mov [r11+0x20],rdi; push rbp; push r12; push r13; push r14; mov rbp,rsp; sub rsp,0x50; mov dil/r15b, [rbp+0x58]; lea rax, [rip+...]`) is the wide-frame MSVC `__thiscall` pattern, see [`_phase6_xrefs_pass3.txt`](_phase6_xrefs_pass3.txt) lines 16-24 and 358-372. |
| Inferred signature | `void __thiscall C_SaveGameManager::SaveGame(this, ISaveGame* pSaveGame, ESaveGameReason reason)` — `rdx` and `r8d` are touched by callee preserving setup before the prologue's `mov dil`/`r15b` reads a stack-passed argument. Plumbing exact arg list requires live debug-print verification (see [`FINDINGS.md`](FINDINGS.md) Open Question #2). |
| Tier-1 AOB (24 B) | `4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57` — **2 hits** in `.text` (one sibling collision; not unique) |
| Tier-2 AOB (40 B) | `4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 50 40 8A 7D 58 48 8D 05 B2 A6` — **1 hit**, verified unique in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 47 |
| Distinctive constants | `40 8A 7D 58` (`mov dil/r15b, [rbp+0x58]` — reads the stack-passed `ESaveGameReason` enum) and the trailing `48 8D 05 B2 A6 ?? ??` (LEA to a `C_SaveGameManager`-local string in `.rdata`). |
| String-anchor evidence | LEA at `0x183581BCF` references `"SaveGame: '%s' %s. [Duration=%.4f secs]"` (`.rdata 0x184779088`); LEA at `0x183581DC9` references `"Saving failed : player is dead!"` (`.rdata 0x184779028`). Both walk back to the same function entry `0x183581B04` ([`_phase6_xrefs_pass3.txt`](_phase6_xrefs_pass3.txt) lines 4-48). Single function owning both strings = exclusive identification. |
| Caller-chain evidence | LEA at `0x183581B7B` (inside the same function) references the description-table format string at `.rdata 0x183E19098`, which has only 3 LEA xrefs in the binary — two from unrelated paths and one from this function ([`_phase6_xrefs_pass3.txt`](_phase6_xrefs_pass3.txt) lines 322-373). Body is ~248 bytes, makes one `[rax+0x200]` vtable dispatch and tail-calls `0x180A61D00`. Thin wrapper structure. |
| Thread context | **Main thread (static evidence)**. No `CreateThread` / `QueueUserWorkItem` primitives in body. Reached via `CCryAction::PreUpdate`/`PostUpdate` chain. Confirm with `GetCurrentThreadId()` assert on first fire (Open Question #4 in [`FINDINGS.md`](FINDINGS.md)). |
| Firing-time analysis | Fires once per save operation, before the duration-log emission inside the body (the log captures a delta computed within `SaveGame`). Hooking at function entry means the kcdx callback runs **before** the engine begins serializing — this is the correct `kSaveGame` semantics matching SKSE's "save is about to happen" message. |
| Confidence | **HIGH** — exclusive ownership of two save-specific log strings (no other function references either); thin wrapper structure with clean prologue; Tier-2 sig is unique. |
| Recommended pick | **Yes.** Use this for `kSaveGame`. |

**TOML sketch:**

```toml
[[hook]]
name        = "phase6_save_game"
priority    = 100
module      = "WHGame.dll"
pattern     = "4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 50 40 8A 7D 58 48 8D 05 B2 A6"
offset      = 0

# void __thiscall C_SaveGameManager::SaveGame(this, ISaveGame*, ESaveGameReason)
return_type = "void"
param_types = ["ptr", "ptr", "int32"]

lifecycle_message = "kSaveGame"
```

---

### #2 — `kPreLoadGame` → `C_SaveGameManager::LoadGame` (RECOMMENDED v0.1)

| | |
|---|---|
| Source | Discovered 2026-05-19 via [`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) on string anchor `"Loading saved game '%s' %s, created by '%s', ver. %d ..."` (`.rdata 0x183E16C00`). |
| Function name | `wh::framework::C_SaveGameManager::LoadGame` (inferred from exclusive ownership of the load-opener log string and `C_SaveGameManager` RTTI scope) |
| Function entry VA | `0x1825BD1BC` |
| Function entry RVA | `0x25BD1BC` |
| Calling convention | `__thiscall` — `rcx = C_SaveGameManager*`. Prologue (`mov [rsp+8],rbx; mov [rsp+0x18],r8d; push rbp; push rsi; push rdi; push r12; push r13; push r14; push r15; sub rsp,0x50; xor r15d,r15d; mov ebx,r8d; mov [rsp+0xa8],r15d; ...`) saves `r8d` as a 32-bit value early — see [`_phase6_xrefs_pass1.txt`](_phase6_xrefs_pass1.txt) lines 10-25 and the caller-chain walk in [`_phase6_callers.txt`](_phase6_callers.txt) lines 3-17. |
| Inferred signature | `void __thiscall C_SaveGameManager::LoadGame(this, ILoadGame* pLoadGame, ELoadGameReason reason)` — `r8d` saved as 32-bit enum, `rdx` passed through as object pointer. Exact arg-list confirmation deferred to live debug (Open Question #2 in [`FINDINGS.md`](FINDINGS.md)). |
| Tier-1 AOB (24 B) | `48 89 5C 24 08 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC` — **4 hits** in `.text` (3 sibling collisions; not unique) |
| Tier-2 AOB (40 B) | `48 89 5C 24 08 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 33 FF 41 8B D8 44 89 BC 24 A8 00 00 00 44` — **1 hit**, verified unique in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 23 |
| Distinctive constants | `45 33 FF` (`xor r15d, r15d`) and `44 89 BC 24 A8 00 00 00` (`mov [rsp+0xA8], r15d` — clears a wide-stack slot at +0xA8). The 8-byte displacement makes this rare. |
| String-anchor evidence | Exactly one LEA xref in `.text` to `"Loading saved game '%s' %s, created by '%s', ver. %d ..."` (`.rdata 0x183E16C00`), at `0x1825BD2F2` inside this function ([`_phase6_xrefs_pass1.txt`](_phase6_xrefs_pass1.txt) lines 4-13). Single owner. |
| Caller-chain evidence | Body is ~125 bytes, terminates by calling `[rax+0x28]` then `C_SaveGameManager::UpdateSaveGameDescriptions` (`0x1806EBF34`, verified unique in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 41). Single caller — the LoadGame wrapper at `0x1825BCEEC` — at call site `0x1825BCF3F` ([`_phase6_callers.txt`](_phase6_callers.txt) lines 3-17). The wrapper in turn has one caller: the orchestrator at `0x180FBEE78` ([`_phase6_callers2.txt`](_phase6_callers2.txt) lines 6-17). Clean three-frame chain: orchestrator → wrapper → LoadGame. |
| Thread context | **Main thread (static evidence)**. No thread-spawn primitives in body. Reached via the orchestrator under `CCryAction::PreUpdate`/`PostUpdate`. Confirm with `GetCurrentThreadId()` assert on first fire (Open Question #4 in [`FINDINGS.md`](FINDINGS.md)). |
| Firing-time analysis | The log string reads syntactically like an opener (logs *that* a load is starting, not its outcome). Hooking at function entry should fire **before** the deserialization path runs — matching `kPreLoadGame` semantics. **CAVEAT**: if live testing shows the wrapper `0x1825BCEEC` actually drives deserialization *before* invoking `LoadGame`, the correct hook site would shift one frame up to inside the orchestrator `0x180FBEE78` (before its call to the wrapper). See [`FINDINGS.md`](FINDINGS.md) Open Question #1. |
| Confidence | **HIGH** on identity (exclusive owner of opener log string, exclusive caller chain). **MEDIUM-HIGH** on `kPreLoadGame` semantic matching pending Open Question #1. |
| Recommended pick | **Yes**, with caveat above. Use this for `kPreLoadGame`; if live test invalidates the pre-deserialization assumption, fall back to the orchestrator-internal hook site. |

**TOML sketch:**

```toml
[[hook]]
name        = "phase6_pre_load_game"
priority    = 100
module      = "WHGame.dll"
pattern     = "48 89 5C 24 08 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 45 33 FF 41 8B D8 44 89 BC 24 A8 00 00 00 44"
offset      = 0

# void __thiscall C_SaveGameManager::LoadGame(this, ILoadGame*, ELoadGameReason)
return_type = "void"
param_types = ["ptr", "ptr", "int32"]

lifecycle_message = "kPreLoadGame"
```

---

### #3 — `kPostLoadGame` → `C_SaveGameManager::PostLoadGame` (RECOMMENDED v0.1)

| | |
|---|---|
| Source | Discovered 2026-05-19 via [`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) on string anchor `"SaveGameManager::PostLoadGame"` (`.rdata 0x183E16B90`). |
| Function name | `wh::framework::C_SaveGameManager::PostLoadGame` (string literally names the method) |
| Function entry VA | `0x1825BCF94` |
| Function entry RVA | `0x25BCF94` |
| Calling convention | `__thiscall` — `rcx = C_SaveGameManager*`. Prologue (`mov [rsp+0x10],rbx; push rbp; push rsi; push rdi; push r14; push r15; lea rbp,[rsp-0x37]; sub rsp,0x90; mov rdi,r8; mov ebx,edx; mov rsi,rcx; call ...; mov r15, [rax+...]`) — see [`_phase6_xrefs_pass2.txt`](_phase6_xrefs_pass2.txt) lines 34-48. |
| Inferred signature | `void __thiscall C_SaveGameManager::PostLoadGame(this, int reason_or_flags, void* context)` — `rdx` (32-bit) and `r8` (pointer) are both consumed. Live debug-print confirmation deferred (Open Question #2). |
| Tier-1 AOB (24 B) | `48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00` — **3 hits** in `.text` (2 sibling collisions; not unique) |
| Tier-2 AOB (40 B) | `48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 49 8B F8 8B DA 48 8B F1 E8 7F F1 35 FE 4C 8B B8` — **1 hit**, verified unique in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 25 |
| Distinctive constants | `48 8D 6C 24 C9` (`lea rbp,[rsp-0x37]` — the `-0x37`/`0xC9` displacement is a fingerprint of this build's compiler choice for this stack layout) and the relative call `E8 7F F1 35 FE` to a specific helper. |
| String-anchor evidence | LEA at `0x1825BD01B` references `"SaveGameManager::PostLoadGame"` (`.rdata 0x183E16B90`); the string literally names the function ([`_phase6_xrefs_pass2.txt`](_phase6_xrefs_pass2.txt) lines 29-48). |
| Caller-chain evidence | ~570 bytes body, 14 outbound calls including action-event dispatcher at `0x180FBE628` (entry sig verified unique, [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 31). Single caller via LoadGame's tail path: `LoadGame` (`0x1825BD1BC`) calls into PostLoadGame at site `0x1825BD4AA` ([`_phase6_callers.txt`](_phase6_callers.txt) lines 22-34). |
| Thread context | **Main thread (static evidence)**. No thread-spawn primitives in body. Reached as a tail-stage callee of `LoadGame` which itself is main-thread. Confirm with `GetCurrentThreadId()` assert on first fire. |
| Firing-time analysis | The method name "PostLoadGame" and its position after `LoadGame`'s deserialization steps both indicate it fires **after** the savegame state has been applied to the world. This is the SKSE-equivalent `kPostLoadGame` moment — plugin callbacks here can safely read the rehydrated entity state. |
| Confidence | **HIGH** — string anchor is the method name itself; Tier-2 sig is unique; caller chain is clean. |
| Recommended pick | **Yes.** Use this for `kPostLoadGame`. |

**TOML sketch:**

```toml
[[hook]]
name        = "phase6_post_load_game"
priority    = 100
module      = "WHGame.dll"
pattern     = "48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 49 8B F8 8B DA 48 8B F1 E8 7F F1 35 FE 4C 8B B8"
offset      = 0

# void __thiscall C_SaveGameManager::PostLoadGame(this, int, void*)
return_type = "void"
param_types = ["ptr", "int32", "ptr"]

lifecycle_message = "kPostLoadGame"
```

---

### #4 — `kDeleteGame` → `C_SaveGameManager::DeleteSavegame` (RECOMMENDED v0.1)

| | |
|---|---|
| Source | Discovered 2026-05-19 via [`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) on string anchor `"Deleting savegame %d (%s) of playline %d"` (`.rdata 0x183E16B40`). |
| Function name | `wh::framework::C_SaveGameManager::DeleteSavegame` (inferred from log string + `C_SaveGameManager` RTTI scope) |
| Function entry VA | `0x1825BC510` |
| Function entry RVA | `0x25BC510` |
| Calling convention | `__thiscall` — `rcx = C_SaveGameManager*`. Prologue (`mov [rsp+8],rbx; mov [rsp+0x10],rbp; mov [rsp+0x18],rsi; push rdi; push r14; push r15; sub rsp,0x40; movsxd rbp,edx; mov r15d,r8d; mov edx,r13d (?); mov rsi,rcx`) — see [`_phase6_xrefs_delete.txt`](_phase6_xrefs_delete.txt) lines 16-24. |
| Inferred signature | `void __thiscall C_SaveGameManager::DeleteSavegame(this, int playline_or_slot, int flags)` — `rdx` sign-extended via `movsxd rbp, edx` (32→64-bit slot/playline index), `r8d` saved to `r15d` (second 32-bit arg). Exact semantics deferred to live debug-print. |
| Tier-1 AOB (24 B) | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40` — **400 hits** in `.text` (this is the MSVC `__thiscall` register-save-prologue pattern that's emitted all over the binary; not unique under any circumstances) |
| Tier-2 AOB (40 B) | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 48 63 EA 45 8B F8 8B D5 48 8B F1 E8 40 19 42 FF` — **1 hit**, verified unique in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 48 |
| Distinctive constants | `48 63 EA` (`movsxd rbp, edx`) and the immediate-relative call `E8 40 19 42 FF` to a fixed helper VA. The `42 FF` displacement is highly specific to this build. |
| String-anchor evidence | LEA at `0x1825BC5C0` references `"Deleting savegame %d (%s) of playline %d"` (`.rdata 0x183E16B40`); single owner ([`_phase6_xrefs_delete.txt`](_phase6_xrefs_delete.txt) lines 4-24). Sibling at `0x1825BC41C` handles bulk-delete (`"Deleting all %d savegames of playline %d"`, `.rdata 0x183E16EC8`), distinct function with unique Tier-1+Tier-2 sigs ([`_phase6_xrefs_delete.txt`](_phase6_xrefs_delete.txt) lines 28-48; verified unique in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 51). |
| Caller-chain evidence | Body is comparable in size to the other three; per-caller scan was not run separately for delete (the orchestrator path is more diffuse — typically UI-triggered through `C_UISaveLoad`-class methods rather than the game-loop save/load orchestrator). Single LEA xref to the log string suggests this is the lone delete dispatcher; the sibling at `0x1825BC41C` is the bulk-delete variant. |
| Thread context | **Main thread (static evidence)** — UI-triggered delete operations are dispatched from the menu thread which is the main thread in CryEngine. No thread-spawn primitives in body. Confirm with `GetCurrentThreadId()` assert on first fire. |
| Firing-time analysis | Fires once per single-savegame delete operation. Hooking at function entry runs the kcdx callback **before** the file is removed — matching SKSE's `kDeleteGame` "delete is about to happen" semantics, so plugins can clean up co-save sidecar files (`.kcdx` next to `.whs`) in the same operation. |
| Confidence | **MEDIUM-HIGH** — Tier-1 collision count (400) is large but that's the prologue pattern not the function-identifying suffix; Tier-2 is unique. Identity is strong (exclusive owner of the delete log string). Confidence is one notch below HIGH because caller-chain walk wasn't extended for delete and we haven't traced exactly which UI path triggers the function (vs. bulk-delete sibling), so there's a small chance the menu invokes both functions in some flows. Acceptable for v0.1; revisit if live testing shows double-fire on multi-save-delete UI actions. |
| Recommended pick | **Yes.** Use this for `kDeleteGame`. Optionally also hook the bulk-delete sibling at `0x1825BC41C` (Tier-1 already unique, see [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 51) if SKSE-parity wants per-savegame messages for bulk operations too — but that's a v0.2 enhancement, not required for v0.1. |

**TOML sketch:**

```toml
[[hook]]
name        = "phase6_delete_game"
priority    = 100
module      = "WHGame.dll"
pattern     = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 48 63 EA 45 8B F8 8B D5 48 8B F1 E8 40 19 42 FF"
offset      = 0

# void __thiscall C_SaveGameManager::DeleteSavegame(this, int, int)
return_type = "void"
param_types = ["ptr", "int32", "int32"]

lifecycle_message = "kDeleteGame"
```

---

## Decision summary

Wire all four candidates above into Phase 6's `[[hook]]` blocks for kcdx
v0.1. Each Tier-2 sig is verified unique against the full `.text` section
of `WHGame.dll` for build `release_1_5_1164953_841`
([`_phase6_final_verify.txt`](_phase6_final_verify.txt) — every entry
`[PASS] 1 hit(s) (expect 1)`).

All four are methods of `wh::framework::C_SaveGameManager`. All four are
`__thiscall`. All four are reached from `CCryAction::PreUpdate`/`PostUpdate`
without crossing thread-spawn primitives, so static evidence supports main-
thread invocation; live confirmation via debug `GetCurrentThreadId()` assert
on first fire is the recommended belt-and-suspenders (see Open Question #4
in [`FINDINGS.md`](FINDINGS.md)).

Strategy A (the muyuanjin-style `IGameFrameworkListener` registration via the
gEnv resolver and `IGame::CompleteInit` vtable hook) is documented in
[`FINDINGS.md`](FINDINGS.md) as a v0.2+ candidate, not adopted for v0.1 — it
requires schema work kcdx doesn't have yet AND fundamentally cannot deliver
`kPreLoadGame` or `kDeleteGame` because CryEngine's listener interface
doesn't expose those events.

## Files in this folder

- [`FINDINGS.md`](FINDINGS.md) — methodology, Strategy A vs B analysis,
  save-format hints, open questions, RTTI forward-looking notes
- [`SAVE-LOAD-CANDIDATES.md`](SAVE-LOAD-CANDIDATES.md) (this file) — formal
  candidate dossier, one block per lifecycle message

---

## ROUND 2 LIVE FINDINGS (2026-05-19, authoritative)

Two probe rounds against the deployed `kcdx.asi` on KCD2
`release_1_5_1164953_841` answered the four open questions and
revealed three corrections to the static-analysis predictions above.

### VAs and Tier-2 sigs — CONFIRMED

All four function VAs and Tier-2 AOB sigs predicted in the
candidates above resolved exactly as written in the live binary.
After ASLR they appear at WHGame.dll base + the documented RVAs.

### Open Question #1 — fire ordering: PRE-DESERIALIZATION

A hook on `LoadGame_wrapper` (`0x1825BCEEC`) fires before
`PostLoadGame` (`0x1825BCF94`) in every successful load. PostLoadGame
fires from inside LoadGame's tail path. Both occur within the same
millisecond on observed loads. So:

- Wrapper-entry hook = correct `kPreLoadGame` semantic (before
  deserialization)
- PostLoadGame-entry hook = correct `kPostLoadGame` semantic (after
  deserialization, world hydrated)

**No need to move the hook frame up to the orchestrator at
`0x180FBEE78`** — the function-entry frame is correct.

**Bonus finding:** `PostLoadGame` does NOT fire on aborted loads. A
LoadGame fire WITHOUT a matching PostLoadGame fire = the load was
internally rejected (corrupt save, missing content, etc.). **This
means PostLoadGame is the "world is ready" success signal**, not a
"load attempt finished" signal. Plugin authors who care about the
new game state should listen for `kcdxMessage_PostLoadGame`, not
`kcdxMessage_PreLoadGame`.

### Open Question #2 — filename payload: PARTIAL ANSWER

**SaveGame's rdx is the filename string directly**, not an
`ISaveGame*` as the dossier predicted. Round 2 probe dumped the
bytes at arg2 and decoded ASCII:

```
arg2 @ 0x201ECCFEA8C: 25 55 53 45 52 25 2F 73 61 76 65 73 2F 70 6C
                     61 79 6C 69 6E 65 30 2F 61 75 74 6F 73 61 76 65 35
                     = "%USER%/saves/playline0/autosave5"
```

The candidate-1 "Inferred signature" row for SaveGame above is
**wrong**: the actual ABI is
`void __thiscall C_SaveGameManager::SaveGame(this, const char* filename, int reason)`,
not `(this, ISaveGame*, ESaveGameReason)`. The `mov dil/r15b, [rbp+0x58]`
in the prologue is reading the *fourth* stack-passed arg (some
additional flags / source identifier), not the second.

**LoadGame_wrapper's rdx is always 0**, even though the wrapper is
the user-API entry frame. No filename available at the
`kcdxMessage_PreLoadGame` dispatch site. v0.2 task: read the
filename from a `C_SaveGameManager` member field populated by the
orchestrator before LoadGame is called.

**PostLoadGame's arg3 is an object pointer** (vtable
`0x7FFCF5C525D0`, consistent across all PostLoadGame fires; content
varies per load). The filename presumably lives at some member
offset, but the layout is unresolved as of Round 2. v0.2 task:
disassemble PostLoadGame body to find a `[r8+offsetN]` read that
loads a string pointer.

### Open Question #3 — multi-fire: TWO FIRES PER SAVE

`SaveGame` fires twice per user save action, ~500ms apart. Both
fires carry the same filename. Likely a game-internal pattern
(hardsave + rotation snapshot? autosave + manual save? primary +
backup?). Static caller-chain analysis missed this because both
fires probably go through the orchestrator at different times.

**Decision for v0.1**: emit `kcdxMessage_SaveGame` for **every**
fire, no deduplication. Plugins that care can dedupe by filename if
needed (a `unordered_set<string>` cleared between sessions, or a
"last fire timestamp + filename" check). Mechanical dedup at the
engine layer would risk silently dropping a real second save (e.g.,
user double-pressed F5 deliberately).

`LoadGame_wrapper` fires once per load attempt (some attempts
proceed to PostLoadGame, others abort silently).

`DeleteSavegame` never observed firing in the test run — no UI
delete option in the current KCD2 build. Hook stays installed in
case a future patch adds the UI.

### Open Question #4 — main thread: CONFIRMED

All Round 1 + Round 2 probe fires show the same `tid` (one
session-specific value). Static-evidence guess held. Plugin
callbacks dispatched from `kcdxMessage_*` for save/load are safe
to call into Lua via `g_lua_state` without thread-ID guards.

### Inner LoadGame vs wrapper — DROPPED inner from hook surface

Round 2 probed both `0x1825BD1BC` (inner LoadGame) and
`0x1825BCEEC` (wrapper) simultaneously and confirmed they have
identical args (both rdx=0, both r8d=reason). Wrapper fires first;
inner fires immediately after as the wrapper's tail call. **The
inner hook adds nothing**, so v0.1 ships only the wrapper hook
for `kPreLoadGame`. (The inner is still mentioned in dossier
candidate #2 above; ignore it.)

### One additional finding worth flagging

Wrapper's `this` (arg1) and inner's `this` are **different objects
with different vtables**:
- Wrapper `this` @ `0x2019BEC1FF0`, vtable `0x7FFCF60183B0`
- Inner `this`  @ `0x2022ED44D90`, vtable `0x7FFCF60180A8`

PostLoadGame and inner LoadGame share the same `this` and vtable —
they ARE the same `C_SaveGameManager` instance. The wrapper is a
**different class** that owns the SaveGameManager. RTTI lookup of
both vtables is a v0.2 task (knowing the wrapper's class will help
when extending Phase 6 with additional lifecycle messages).

### Implementation as-shipped

The four hooks live at [`kcdx/src/save_load_hooks.cpp`](../../kcdx/src/save_load_hooks.cpp).
Header doc at [`kcdx/src/save_load_hooks.h`](../../kcdx/src/save_load_hooks.h)
lists the four hook-to-message mappings and ABIs. Logging is
dev-mode-only (`KCDX_DEV("SAVE_LOAD", ...)`), no INFO spam in the
main kcdx.log once dev mode is off.

---

## ROUND 3 ABI RECON (corrected arg counts)

Survey date: 2026-05-19. Triggered by the discovery that the Round 2
typedefs were guesses from register-save prologue patterns rather than
body-wide stack-arg analysis. The previous 3-arg typedef for `SaveGame`
was silently corrupting save files because the function reads
`[rbp+0x58]` (arg6) deep into its body and our hook never forwarded
that argument.

Methodology: recursive disassembly via capstone (walker at
[`_research/phase6_abi_walker.py`](phase6_abi_walker.py)), following
every conditional + unconditional intra-function jump. For each
function, every memory operand whose base register is `rsp`, `rbp`, or
`r11` was back-mapped to its `entry_rsp + N` offset, with epilogue
register-restore detection (when `lea r11, [rsp+K]` or similar
redefines r11, subsequent `[r11+N]` reads are excluded as restores).

Detailed scratch notes per function with disassembly excerpts:
[`abi-disassembly.txt`](abi-disassembly.txt). Raw walker outputs:
[`_phase6_abi_savegame.txt`](_phase6_abi_savegame.txt),
[`_phase6_abi_loadgame.txt`](_phase6_abi_loadgame.txt),
[`_phase6_abi_postload.txt`](_phase6_abi_postload.txt),
[`_phase6_abi_delete.txt`](_phase6_abi_delete.txt).

### Corrected arg-count summary

| Function | Round 2 prediction | Round 3 actual | Delta |
|---|---|---|---|
| `SaveGame` | 3 args | **7 args** | **+4 stack args (the bug)** |
| `LoadGame_wrapper` | 3 args | 3 args | none |
| `PostLoadGame` | 3 args | 3 args | none |
| `DeleteSavegame` | 3 args | 3 args | none (arg2 is **signed** via `movsxd`) |

### #1 `C_SaveGameManager::SaveGame` — corrected typedef

Frame: `mov r11,rsp` then 5 pushes (rbp, r12, r13, r14, r15 = 40) +
`sub rsp, 0x50`. `rbp = entry_rsp - 0x28`, `rsp = entry_rsp - 0x78`.

Per-slot reads (all FIRST reads, before any clobber):

| entry_rsp+ | Insn | Width | Type |
|---|---|---|---|
| +0x08 (arg1) | `mov rbx, rcx` @ 0x183581B39 | 8 | `void* this` |
| +0x10 (arg2) | `mov r15, rdx` @ 0x183581B54 | 8 | `const char* filename` (live-confirmed Round 2) |
| +0x18 (arg3) | `movzx r12d, r8b` @ 0x183581B43 | 1 | `uint8_t` (ESaveGameReason, low byte only) |
| +0x20 (arg4) | `mov r14b, r9b` @ 0x183581B51, `test r9b, r9b` @ 0x183581B5B | 1 | `uint8_t` (bool flag) |
| +0x28 (arg5) | `mov r14d, dword ptr [rbp+0x50]` @ 0x183581C21 | 4 | `uint32_t` (written to `[this+0x744]`) |
| +0x30 (arg6) | `mov dil, byte ptr [rbp+0x58]` @ 0x183581B23 | 1 | `uint8_t` (**THE bug-trigger byte** — bool flag used throughout body) |
| +0x38 (arg7) | `mov rsi, qword ptr [rbp+0x60]` @ 0x183581B2E | 8 | `const char*` (saved to rsi; `test rsi,rsi; cmove rsi, [rip+const]` at 0x183581D31 falls back to a literal default string when NULL — strong evidence of `const char*` semantics. Passed as rdx to fmt-print at 0x1804f6ac8.) |

**Resolving the workspace `[rbp+0x58]` question:** With `rbp = entry_rsp - 0x28`, the byte at `[rbp+0x58]` lives at `entry_rsp + 0x30`. By the MSVC x64 slot map (arg1=+0x08, arg2=+0x10, arg3=+0x18, arg4=+0x20, arg5=+0x28, arg6=+0x30), this is **arg6** (the 6th argument, counting `this` as arg1 — or equivalently, the 2nd stack-passed arg). It is read as a single byte (`mov dil`) and used as a boolean guard. The Round 2 hook never forwarded args 5/6/7, so the function read garbage/old-stack from arg5+0x28..arg7+0x38, producing silently corrupted saves.

**Upper bound:** Highest positive rbp displacement in the body (excluding the rbp+0x60 scratch-buffer reuse) is `rbp + 0x64` = `entry_rsp + 0x3C` (second half of arg7's 8-byte slot). No reads at `rbp + 0x68` or beyond. The body uses `[rbp+0x60..0x68]` as a stack-local scratch buffer after consuming arg7 once. The `[r11+0x30], [r11+0x40], [r11+0x48]` reads at 0x183581BE2..0x183581BEA are register restores after `lea r11, [rsp+0x50]` at 0x183581BDD (r11 then equals `entry_rsp-0x28`, so those reads target the home save area at entry_rsp+0x08/+0x18/+0x20).

```cpp
// returns 1-byte bool in al (`mov al, dil` or `xor al, al` then ret)
using SaveGame_t = char (__fastcall*)(
    void*       this_,         // arg1 rcx — C_SaveGameManager*
    const char* filename,      // arg2 rdx — ASCII path
    uint8_t     reason,        // arg3 r8 (low byte only)
    uint8_t     flag_a,        // arg4 r9 (low byte only)
    uint32_t    arg5,          // arg5 [rsp+0x28]
    uint8_t     flag_b,        // arg6 [rsp+0x30] — the [rbp+0x58] byte
    const char* description);  // arg7 [rsp+0x38] — string with NULL-fallback
```

### #2 `C_SaveGameManager::LoadGame_wrapper` — confirmed typedef

Frame: 1 push + `sub rsp, 0x20`. No rbp frame pointer.

| entry_rsp+ | Insn | Width | Type |
|---|---|---|---|
| +0x08 (arg1) | `mov rsi, rcx` @ 0x1825BCF00 | 8 | `void* this` |
| +0x10 (arg2) | `mov edi, edx` @ 0x1825BCEFE | 4 | `uint32_t` (always 0 live; slot still read) |
| +0x18 (arg3) | `mov ebx, r8d` @ 0x1825BCEFB | 4 | `uint32_t` (ELoadGameReason) |

arg4 (r9) is never read. No stack-arg reads at entry_rsp+0x28+. Only 42 instructions in the function; epilogue reads at `[rsp+0x30]` and `[rsp+0x38]` are register restores of rbx/rsi from the home save area.

```cpp
using LoadGame_wrapper_t = char (__fastcall*)(
    void*    this_,    // arg1 rcx
    uint32_t arg2,     // arg2 rdx (always 0 live, but slot is read)
    uint32_t reason);  // arg3 r8d
```

### #3 `C_SaveGameManager::PostLoadGame` — confirmed typedef

Frame: 5 pushes + `lea rbp, [rsp-0x37]` + `sub rsp, 0x90`. `rbp = entry_rsp - 0x5F`, `rsp = entry_rsp - 0xB8`.

| entry_rsp+ | Insn | Width | Type |
|---|---|---|---|
| +0x08 (arg1) | `mov rsi, rcx` @ 0x1825BCFB1 | 8 | `void* this` |
| +0x10 (arg2) | `mov ebx, edx` @ 0x1825BCFAF | 4 | `uint32_t` (written to `[this+0x188]`) |
| +0x18 (arg3) | `mov rdi, r8` @ 0x1825BCFAC | 8 | `void*` (struct read at `+0x54`, `+0x78`) |

arg4 (r9) is never read. The `[rbp+0x67]` and `[rbp+0x77]` reads (at `entry_rsp+0x08`/`+0x18`) are *scratch-buffer use* of the home save area — the function passes `lea rcx, [rbp+0x67]` to a callee (`call 0x1804f692c`) that fills the buffer, then reads it back. Not incoming-arg reads.

```cpp
using PostLoadGame_t = char (__fastcall*)(
    void*    this_,   // arg1 rcx
    uint32_t arg2,    // arg2 rdx (32-bit; written to this+0x188)
    void*    arg3);   // arg3 r8 (object pointer)
```

### #4 `C_SaveGameManager::DeleteSavegame` — confirmed typedef (arg2 signed)

Frame: 3 pushes + `sub rsp, 0x40`. No rbp frame ptr; rbp gets repurposed.

| entry_rsp+ | Insn | Width | Type |
|---|---|---|---|
| +0x08 (arg1) | `mov rsi, rcx` @ 0x1825BC530 | 8 | `void* this` |
| +0x10 (arg2) | **`movsxd rbp, edx`** @ 0x1825BC528 | 4→8 | **`int32_t` (SIGNED)** — used as array index `[rsi + rbp*8]` patterns |
| +0x18 (arg3) | `mov r15d, r8d` @ 0x1825BC52B | 4 | `uint32_t` (flags/reason) |

The `movsxd` is significant: arg2 is a **signed** 32-bit slot/playline index. arg4 (r9) is never read. The `[rsp+0x78]` reads in the body are scratch use of the arg4 home slot. Epilogue reads at `[rsp+0x60/0x68/0x70]` are register restores.

```cpp
using DeleteSavegame_t = char (__fastcall*)(
    void*    this_,   // arg1 rcx
    int32_t  slot,    // arg2 edx — SIGNED (movsxd at 0x1825BC528)
    uint32_t flags);  // arg3 r8d
```

### Why these arg counts are *upper bounds*

For each function I (a) recursively visited every reachable instruction
from the entry (following all conditional + unconditional jumps within
the function extent), (b) enumerated every memory operand whose base
register is rsp/rbp/r11, (c) back-mapped each to an entry_rsp+N offset,
(d) filtered out epilogue register-restores by detecting r11/rbp
redefinition addresses. The resulting set of "first read in body of a
slot at entry_rsp+N" is exhaustive for the listed N range (0x08..0x100).

Any plausible arg8+ slot would manifest as a read at `[rsp+N]` where N
> 0x28 + 4*8 = 0x48 (post-prologue rsp-relative). No such reads exist
in any of the four functions outside register-restore patterns. The
walker also dumps the full sorted disassembly so this can be
independently verified.

Caveat: if a function takes args it never reads but only passes through
to a tail-call, we wouldn't see them. This matters only for forwarding
to nested calls that read further. None of the four functions tail-call
in a way that would shift argument indices (`SaveGame` makes vtable
dispatch calls that pass `rdx = r15` (saved arg2) explicitly; the
others either don't tail-call or rebuild arg lists from scratch).

### Drop-in C++ typedefs for kcdx

All four return `char` (1-byte bool in `al`). Calling convention is
MSVC x64 standard `__fastcall` (rcx/rdx/r8/r9 + stack args).

```cpp
// in kcdx/src/save_load_hooks.h or .cpp

using SaveGame_t = char (__fastcall*)(
    void*       this_,
    const char* filename,
    uint8_t     reason,
    uint8_t     flag_a,
    uint32_t    arg5,
    uint8_t     flag_b,         // this is the [rbp+0x58] byte
    const char* description);   // ptr (NULL-fallback to literal observed)

using LoadGame_wrapper_t = char (__fastcall*)(
    void*    this_,
    uint32_t arg2,
    uint32_t reason);

using PostLoadGame_t = char (__fastcall*)(
    void*    this_,
    uint32_t arg2,
    void*    arg3);

using DeleteSavegame_t = char (__fastcall*)(
    void*    this_,
    int32_t  slot,             // SIGNED
    uint32_t flags);
```

When the kcdx hook handler calls back into the original function via
its trampoline, it MUST forward ALL of these args (especially the
7 for SaveGame) — otherwise the function reads garbage from the
stack slots above arg4-home and silently corrupts the save.

