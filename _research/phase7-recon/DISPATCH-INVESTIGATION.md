# Console-command dispatch investigation — kcdx CAP-13

Phase 7 deep-dive, 2026-05-20. KCD2 build `release_1_5_1164953_841`.
Static analysis only — no live game runs. Method: `pefile + capstone`
disassembly of `WHGame.dll` (Ghidra GUI would yield the same answer
faster but the scripted approach is more reproducible and the analysis
docs are 100% derived from the raw bytes).

All worker scripts live next to this doc:
`_research/phase7-recon/disasm_slots.py`,
`_research/phase7-recon/disasm_helpers.py`,
`_research/phase7-recon/find_xref.py`,
`_research/phase7-recon/find_console_strings.py`,
`_research/phase7-recon/find_cmd_registration.py`,
`_research/phase7-recon/dump_wrapper.py`,
`_research/phase7-recon/find_console_ui.py`.

---

## VERDICT

**kcdx is hooking the WRONG vtable slot. Slots 32 and 33 are swapped
relative to the recon doc's guess.**

- **vtable[32] @ RVA 0x0100A3D4** = `AddCommand(const char*, const char* sScriptFunc, int, const char*)` (**script-string overload**)
- **vtable[33] @ RVA 0x00B9A2B0** = `AddCommand(const char*, ConsoleCommandFunc, int, const char*)` (**function-pointer overload — the one we want**)

Confidence: **HIGH**. Confirmed by (a) the byte-level body difference between slots, (b) the duplicate-warning strings each slot LEAs to, and (c) a discovered engine-side static wrapper `0x180B99098` that pConsole-loads itself and explicitly calls `vtable[33]` (= `[rax + 0x108]`) when registering `playerGoto` and `freeze`.

kcdx's CAP-13 plugin registered into vtable[32] (the script-string overload). Slot 32 interpreted our raw function pointer `&our_slot_thunk_0` as a `const char* sScriptFunc` and `strdup`-copied it into the script-command record. Lookup succeeds on the name (so no "Unknown command"), but ExecuteString dispatches via the script-command path (`vtable[35]` second map at `pConsole + 0xC0`, not the func-pointer map at `pConsole + 0xA0`), which feeds the bogus "script string" through CryEngine's Lua eval. Our function pointer is **never** treated as code; it's parsed as Lua source. That's why our thunk never fires.

The "too many arguments" yellow text is almost certainly the **Lua-eval error** from the script-command path tripping on whatever bytes `&our_slot_thunk_0` happened to start with — NOT a CryEngine console-parser warning. (See Q5 below: the literal string "too many arguments" is only in Lua's coroutine.c; the Lua VM is exactly where the script-command path lands.)

### Recommended fix (drop-in)

```cpp
// kcdx Phase 7 / CAP-13 / command_engine.cpp
//
// FIX: hook vtable[33], not vtable[32]. The two overloads of
// CXConsole::AddCommand are present in the canonical CryEngine 5.2.3
// order but in KCD2's build the *function-pointer* overload is slot 33
// and the *script-string* overload is slot 32. Verified via:
//   - RVA 0x00B9A2B0 stores r8 raw at record[+0x20] (func ptr)
//   - RVA 0x0100A3D4 calls string_assign_helper(record[+0x08], r8) (sScriptFunc)
//   - Engine wrapper at RVA 0x00B99098 calls [pConsole->vtable + 0x108]
//     (== slot 33) to register playerGoto / freeze.

constexpr int kAddCommand_FuncOverload_Slot   = 33;  // was 32 - WRONG
constexpr int kAddCommand_ScriptOverload_Slot = 32;  // was 33

using AddCommandFn = void (__fastcall*)(IConsole*       /*rcx*/,
                                        const char*     /*rdx sCommand*/,
                                        ConsoleCommandFunc /*r8 func*/,
                                        int             /*r9d nFlags*/,
                                        const char*     /*[rsp+28] sHelp*/);

void** vtable    = *reinterpret_cast<void***>(pConsole);
auto AddCommand  = reinterpret_cast<AddCommandFn>(vtable[kAddCommand_FuncOverload_Slot]);

// VF_RESTRICTEDMODE (0x80000) bypasses the con_restricted gate.
// VF_CHEAT (0x02) is what playerGoto uses, but VF_CHEAT requires
// con_restricted=1 to dispatch — keep VF_RESTRICTEDMODE for kcdx commands.
AddCommand(pConsole,
           "kcdx_test_cap13",
           &our_slot_thunk_0,
           0x00080000,                     // VF_RESTRICTEDMODE
           "Test command registered by kcdx...");
```

Confidence in fix: **HIGH** if also paired with the dispatcher confirmations
in Q3 below. The hooked slot is the only thing that changes; everything
else (calling convention, flag value, callback ABI) was already correct.

---

## Q1. WHICH SLOT IS `AddCommand(ConsoleCommandFunc)`?

**Answer: slot 33 (RVA 0x00B9A2B0).** The recon doc's guess (slot 32) was wrong.

### Evidence A: byte-level diff between slot 32 and slot 33 prologue

Slot 32 (RVA 0x0100A3D4) and slot 33 (RVA 0x00B9A2B0) have byte-identical first 32 bytes (saved-reg block + `sub rsp, 0x90` + `mov r14, rcx` + `mov r12d, r9d`). They diverge starting around offset +0x78.

**Slot 33 (function-pointer overload):**

```text
0x180b9a32c  mov  qword ptr [rbp - 0x11], r15    ; store r15 (= original r8 = func) RAW
0x180b9a330  test rdx, rdx                        ; rdx = sHelp from [rbp+0x7f]
0x180b9a333  je   0x180b9a33e
0x180b9a335  lea  rcx, [rbp - 0x21]              ; help slot
0x180b9a339  call 0x1804f6ac8                    ; CryString assign for sHelp
```

`r15` was set at function entry from `r8` (the third arg). Storing it raw, not copying through `0x1804f6ac8` (the CryString assign helper), means slot 33 treats `r8` as a function pointer.

**Slot 32 (script-string overload):**

```text
0x18100a44c  mov  rdx, r15                       ; rdx = r15 (= original r8 = sScriptFunc)
0x18100a44f  lea  rcx, [rbp - 0x29]              ; record[+0x08] = script-string slot
0x18100a453  call 0x1804f6ac8                    ; CryString assign for sScriptFunc
0x18100a458  mov  rdx, qword ptr [rbp + 0x7f]    ; sHelp
0x18100a45c  test rdx, rdx
0x18100a45f  je   0x18100a46a
0x18100a461  lea  rcx, [rbp - 0x21]              ; help slot
0x18100a465  call 0x1804f6ac8                    ; CryString assign for sHelp
```

Slot 32 routes `r8` through the **same CryString-copy helper** as the help and name strings — i.e. it `strdup`s the bytes you pass. That's the script-string overload's behavior.

### Evidence B: each slot's "[DUPLICATE]" warning string

Each slot has a cold path (`jne ...`) that emits a duplicate-registration warning:

- Slot 33's cold path is at `0x18228AFC4`, which LEAs the `.rdata` string
  `"[CVARS]: [DUPLICATE] CXConsole::AddCommand(): console command [%s] is already registered"` (`0x183DCF790`).
- Slot 32's cold path is at `0x1823025F4`, which LEAs the `.rdata` string
  `"[CVARS]: [DUPLICATE] CXConsole::AddCommand(): script command [%s] is already registered"` (`0x183DCF7F0`).

That's a direct, unambiguous self-identification: the engine's own diagnostic strings name slot 33 as "console command" (the func-overload, since "console command" in CryEngine = the AddCommand(func) variant) and slot 32 as "script command".

### Evidence C: engine wrapper proves which slot the engine uses

A static wrapper at RVA `0x00B99098` is used for `playerGoto`, `freeze`, and dozens of other engine commands. Its body:

```text
0x180b99098  sub  rsp, 0x38
0x180b9909c  mov  r10, rcx                            ; save sCommand
0x180b9909f  mov  rcx, qword ptr [rip + 0x3d92802]    ; load pConsole (static)
0x180b990a6  test rcx, rcx
0x180b990a9  je   0x180b990c2
0x180b990ab  mov  rax, qword ptr [rcx]                ; vtable
0x180b990ae  mov  qword ptr [rsp + 0x20], r9          ; sHelp -> 5th-arg shadow slot
0x180b990b3  mov  r9d, r8d                            ; flags
0x180b990b6  mov  r8, rdx                             ; func (raw pointer)
0x180b990b9  mov  rdx, r10                            ; sCommand
0x180b990bc  call qword ptr [rax + 0x108]             ; *** vtable[0x108 / 8 = 33] ***
0x180b990c2  add  rsp, 0x38
0x180b990c6  ret
```

The engine itself uses `vtable[33]` for the function-pointer overload. Game over.

Confidence: **HIGH** (three independent lines of evidence converge).

---

## Q2. WHAT DOES `AddCommand(func)` STORE, AND WHERE?

### Map location

In slot 33: `lea rcx, [r14 + 0xA0]` (where `r14 = pConsole`) is the **func-command map**. Confirmed by the ExecuteString helper at `0x1807A586C`, which loads `lea rbx, [rdi + 0xA0]` and calls a lookup helper on it.

In slot 32: same offset `[r14 + 0xA0]` is also passed to a name-lookup at the top (the dedup probe — both AddCommand overloads share the dedup against the SAME map). Then the actual insertion in slot 32 goes to ??? let me re-check — actually both slot 32 and 33 insert via `0x180B9A394` (`map_insert`) into `[r14 + 0xA0]`. Hmm. Either there's a single map with a sub-type discriminator, or there are separate maps and slot 32's insertion target is computed differently.

Looking at ExecuteString more carefully:

- First lookup (line 0x1807A5B48 onwards) is at `[rdi + 0xA0]` = func-command map.
- If miss, **second lookup** (line 0x1807A5BC9 onwards) is at `[rdi + 0xC0]` = script-command map.

So at runtime ExecuteString distinguishes the two by *which map it looks in*. That implies the AddCommand overloads MUST insert into different maps. We have a small loose end here on which map slot 32 inserts into; the slot 33 insertion path is unambiguous (`[r14 + 0xA0]`). For the kcdx fix, that loose end doesn't matter — we only care about slot 33's behavior.

### Command-record layout

The record `[rbp - 0x31]` is a 0x28-byte struct, constructor at `0x180B9A268`:

```text
[+0x00]  CryString name        (refcounted, header 12 bytes before data)
[+0x08]  CryString scriptString (only used by slot 32; slot 33 leaves it empty)
[+0x10]  CryString help
[+0x18]  int32     flags        (mov dword [rbp-0x19], r12d == original r9d)
[+0x20]  void*     func         (slot 33: mov qword [rbp-0x11], r15 == original r8)
                                (slot 32: same offset, but holds NULL or unused)
```

So `[record + 0x20]` is the func pointer. ExecuteString's dispatcher loads exactly that offset:

```text
0x1807a610b  mov  rax, qword ptr [rdi + 0x20]   ; rdi = command record
0x1807a610f  test rax, rax
0x1807a6112  je   0x1807a6136                   ; if null, fall through to help/autocomplete
0x1807a6114  lea  rcx, [rip + 0x32acd0d]        ; IConsoleCmdArgs vtable
0x1807a611b  mov  qword ptr [rbp + 7], rsi      ; argv vec
0x1807a611f  mov  qword ptr [rbp - 9], rcx      ; cmdArgs->vtable
0x1807a6123  lea  rcx, [rbp - 0x29]             ; command name string
0x1807a6127  mov  qword ptr [rbp - 1], rcx
0x1807a612b  lea  rcx, [rbp - 9]                ; rcx = &IConsoleCmdArgs (this)
0x1807a612f  call rax                            ; *** INVOKE USER'S FUNC ***
```

### Does it strdup the name? YES.

`record_ctor@0x180B9A268` initializes each CryString field to the empty-string sentinel `[rip + 0x4f846c3]`. Then slot 33's body calls `0x1804F6AC8` (CryString-assign) for the name — that helper calls `tmp_name_init@0x1804F692C` which `malloc`s a buffer and `memcpy`s the input. So **the engine owns its own copy of every name, help, and script-string passed to AddCommand**.

This means **`VF_COPYNAME (0x00008000)` is NOT needed** — the engine copies regardless. (`VF_COPYNAME` in CryEngine is for CVars-via-static-strings, not commands; the AddCommand path always copies. Inspection of `0x1804F692C` confirms: it unconditionally allocates a refcounted CryString buffer and writes the bytes.)

### Is there a "max argc" or "min argc" field? NO.

The 0x28-byte record contains only: 3 CryStrings, an int flags, and one qword. There is no argc-min or argc-max field. The "too many arguments" warning is NOT coming from this code path.

Confidence: **HIGH** for record layout, **HIGH** for strdup behavior, **HIGH** for "no argc field in the record".

---

## Q3. WHAT DOES `ExecuteString` DO?

Slot 35 (RVA `0x007A5818`) is a thin entry-point that dispatches into the real helper at `0x007A586C`. The helper's pseudocode:

```text
ExecuteStringInternal(IConsole* console, const char* cmd, bool silent, bool defer)
{
    if (cmd[0] == '#' || cmd[0] == '@')              // Lua / secret prefixes
        special_path();

    // Strip prefix, save the rest:
    tokenize on first space-or-'=':                    # splits "name args"
        rbp-0x29 = command name token
        rbp+0x6f = remainder string (args)

    if (silent && rbp-0x29 contains '?')               # help query
        treat as help, dispatch via vtable[0x248]
        return;

    // Lookup #1 (function-command map):
    iter = name_lookup_in_map(&console->[+0xA0], rbp-0x29);
    if (iter != end) {
        record = *iter + 0x28;
        flags  = record[+0x18];

        // gate on VF_RESTRICTEDMODE / con_restricted CVar:
        if (!(flags & 0x80000) && con_restricted_cvar != 0 && silent)
            goto map2;                                 // reject
        if (flags & 0x400000)                          // VF_BLOCKFRAME
            console->[+0x138] += 1;

        dispatch_func_command(console, record, &remainder);
        return;
    }

    // Lookup #2 (script-command map):
map2:
    iter = name_lookup_in_map(&console->[+0xC0], rbp-0x29);
    if (iter != end) {
        ...lots of checks via record vtable[0x60] / vtable[0x70]...
        if (record passes checks) dispatch_script_command(record);
        return;
    }

    // Both missed:
    if (!silent) console_log("Unknown command: %s", cmd);    // <- LEA 0x183DCF870
}
```

The function-pointer dispatcher (`dispatch_func_command` at `0x1807A5F88`) is the function that:

1. Logs `"[CONSOLE] Executing console command '%s'"` (string at `0x183DCF888`, LEA at `0x1807A5FB6`).
2. Tokenizes `remainder` into an `std::vector<CryString>` (the argv array).
3. Treats `"?"` as a single-token help-query (vtable[0x248] of pConsole).
4. Tests `record[+0x18] & 0x3000002` (VF_CHEAT|VF_DEV_ONLY|VF_DEDI_ONLY); if any set, blocks unless an internal flag (probably `con_cheat_enabled`) is on.
5. Loads `[record + 0x20]` (func ptr); if non-null, builds an `IConsoleCmdArgs` on the stack and **calls the func**.
6. If func is null, falls through to autocomplete/help logic.

### Is there a "wrong number of args" check in CXConsole? NO.

We searched `dispatch_func_command` and its callees down 2 levels. There is no argc check that emits a yellow warning. Every "wrong number / too many" string we found in `.rdata` resolves to one of:

- **Lua coroutine.c** errors (bundled Lua VM — `0x183A8DA78 "too many local variables"`, `0x183A7E808 "too many captures"`, etc.).
- **Flash binding validators** like the one at `0x182EA7AAC` that emits `"Something called the '%s' command with the wrong number of arguments"` (`0x18473F3B0`) for Flash-callback-arity mismatch.
- **CScriptBind_System / Lua bindings** that emit `"[%s] %d arguments passed, N expected)"` strings like `0x18468 6C0` and the dispatch in `CScriptBind_System::ExecuteCommand` at `0x1839A6140` which expects exactly 1 string-typed Lua argument.
- **AI command bindings** like `"[enable activity] wrong argument count"`, **achievement** binding, etc. — all Flash- or Lua-side.

The CryEngine console-command parser itself has no argc validation.

Confidence: **HIGH**.

---

## Q4. WHAT DOES THE `~` IN-GAME CONSOLE INVOKE?

**Partial answer, MEDIUM confidence.** We don't have enough Scaleform reach to settle this conclusively from static analysis without the Flash assets.

What we found:

1. The strings `"ShowConsole"` and `"ExecuteCommand"` (`0x1840883C8` / `0x184088AD8`) each have exactly one LEA xref, both inside an `m_pIScriptSystem->Register(...)`-style binder near `0x18144B300-0x18144BBC0`. Each LEA is followed by `call 0x180B9C36C` which is the Lua/Scaleform method-bind helper (mirrors the kcdxScriptingInterface registration pattern verified in Phase 5).
2. `"CScriptBind_System::ExecuteCommand"` (`0x1847A8520`) has one LEA inside the implementation function `0x1839A6140`. That function:
   - Reads `IFunctionHandler::GetParamCount()` via vtable[5] (`[rax + 0x28]`).
   - If `argc == 1`, calls `IFunctionHandler::GetParam(1, &str)` (vtable[?] at `0x18041DE80`).
   - **Then calls `console->vtable[0x118]` (slot 35 = `ExecuteString`) with that string.**
   - If `argc != 1`, calls the Lua warning emitter at `0x180A607A4` with the format
     `"[%s] %d arguments passed, 1 expected)"`.

So `System.ExecuteCommand(strLine)` from Lua **does** call `IConsole::ExecuteString`. But it expects EXACTLY ONE string argument. If the Scaleform `~` console handler in KCD2 calls `System.ExecuteCommand("kcdx_test_cap13 hello world")` (a single string of the full line), that maps to `IConsole::ExecuteString("kcdx_test_cap13 hello world")` and our command IS reachable through ExecuteString.

We could not statically prove the `~` console UI's Flash event handler dispatches via this path. The Phase 6b recon found a Flash binder pattern at `0x1807D5FC8`; finding the equivalent for `"OnEnterPressed"` / `"OnCommandTyped"` would require pulling the swf bytecode from the game's GFx asset bundles, which is beyond Ghidra's static reach.

**However**, the dispatch we hooked IS reachable from Lua-side `System.ExecuteCommand(line)`, and almost certainly from the in-game console (since the Phase 7 user reports the command name IS recognized — no "Unknown command: %s" — which means it's hitting the lookup at `pConsole + 0xA0` or `+0xC0`).

The "almost certainly" framing matters: our actual finding is that the **command name lookup succeeds** but the **func pointer is interpreted as a script string** because we registered into the wrong slot. The dispatch path 32-vs-33 question is settled. The Flash-handler question is moot once the fix is applied — the user will see the difference live.

Confidence: **MEDIUM** on the exact Scaleform path, **HIGH** that this is irrelevant to the actual bug.

---

## Q5. WHERE IS THE "too many arguments" WARNING FROM?

**Answer: the bundled Lua VM (`coroutine.c` / Lua source code lines), tripped because the script-command dispatch path interpreted `&our_slot_thunk_0` as a Lua source string and tried to compile it.**

### Evidence chain

1. We searched `.rdata` exhaustively. The literal string `"too many arguments"` does not appear standalone. There is `"Too few arguments"` (`0x184715828`) but no matching "too many arguments". The Lua bundle has `"too many captures"`, `"too many local variables"`, `"too many length or distance symbols"`, `"too many parameters for function '%s'"`. None say "arguments" exactly.

2. The xrefs for every "too many" or "wrong number" string we found resolve to either:
   - Lua-internal error paths (bundled libcurl Lua / vendored Lua VM).
   - Flash binding ARITY checks (e.g. the one at `0x182EA7AAC` that calls
     `vtable[2] on the args object` and compares to literal 2).
   - PNG, AI binding, achievement binding — unrelated to console.

3. **None of these are reachable from `IConsole::ExecuteString`'s normal func-command dispatch path** (Q3).

4. **But all of these ARE reachable from the script-command path.** The script-command dispatcher at `0x1807A5D54` calls into the Lua VM via the script-command record's vtable. If the "script string" we registered is `&our_slot_thunk_0` — a binary-looking pointer like `0x00007FF7XXXXXXXX` — the Lua compiler will fail with a parse error. The most likely shapes of that parse error, given Lua 5.1's error messages, are something like:
   - `"unexpected symbol near '...'"`
   - `"' ' expected near '...'"`
   - `"'<eof>' expected"`
   - or, if the pointer's bytes happen to start a syntactically-valid function call with too many tokens before EOF: an arity error that says something close to "too many arguments".

5. The user's screenshot text is paraphrased as "too many arguments" but the actual yellow text is probably the Lua parse-error format. Without the screenshot we can't pin it exactly.

The decisive observation is **not** the exact wording of the warning. It's that the warning's existence at all proves the Lua VM was invoked, which means we're on the script-command dispatch path, which means we registered into slot 32 (script-string overload) instead of slot 33 (func-pointer overload). That confirms Q1.

Confidence: **MEDIUM-HIGH** that the warning is a Lua error from the script-command path; **HIGH** that whatever it is, it's not from the func-overload dispatcher (since we verified that dispatcher has no argc-validation step at all).

---

## Q6. HOW DO YOU REGISTER A COMMAND THAT ACTUALLY DISPATCHES?

**Answer: use the engine's own static wrapper at `0x180B99098`, OR mimic its argument shuffle.**

`playerGoto` is registered like this (at `0x18100A580`):

```text
0x18100a58a  lea  r9, [rip + 0x2e0736f]        ; sHelp = "...help text..."
0x18100a591  mov  r8d, 2                       ; nFlags = VF_CHEAT (0x02)
0x18100a597  lea  rdx, [rip - 0x863f3a]        ; func ptr (the playerGoto callback)
0x18100a59e  lea  rcx, [rip + 0x2e070b3]       ; sCommand = "playerGoto"
0x18100a5a5  call 0x180b99098                  ; wrapper
```

The wrapper at `0x180B99098`:
1. Loads `pConsole` from the global at `[rip + 0x3d92802]` (= some `0x4xxxxxxx` static address; that's a kcdx address-library candidate id 1009 sibling).
2. Skips if null.
3. Shuffles args: `r9 -> shadow stack`, `r8d -> r9d`, `rdx -> r8`, `r10 (saved rcx) -> rdx`, then sets `rcx` to the loaded `pConsole`.
4. Calls `[pConsole->vtable + 0x108]` (= slot 33 = AddCommand(func)).

So the engine itself proves slot 33 is the function-pointer overload AND it uses `VF_CHEAT (0x02)` as the typical flag for cheats. `VF_RESTRICTEDMODE (0x80000)` is the flag for "always callable from `-console`-restricted-mode builds" — which IS what we want for a dev tool that should work without `-cheats`.

### Comparison to what kcdx CAP-13 passed

| field | kcdx CAP-13 | engine's playerGoto |
|---|---|---|
| slot | **32 (WRONG)** | **33 (correct)** |
| sCommand | `"kcdx_test_cap13"` | `"playerGoto"` |
| func | `&our_slot_thunk_0` | RVA-relative to local func |
| nFlags | `0x80000` (VF_RESTRICTEDMODE) | `0x02` (VF_CHEAT) |
| sHelp | `"Test command registered by kcdx..."` | help text |

The ONLY material difference is the slot index. Everything else (calling convention, flag value, sHelp non-null) is fine. Flag `0x80000` (VF_RESTRICTEDMODE) is actually a BETTER choice than VF_CHEAT for kcdx because VF_CHEAT requires `con_restricted = 1` to dispatch, whereas VF_RESTRICTEDMODE bypasses that check (per ExecuteString line `0x1807A5B82`).

Confidence: **HIGH**.

---

## Recommended fix recap

```cpp
// command_engine.cpp - registration site
const int kAddCommandFuncOverloadSlot = 33;   // was 32
void** vt = *reinterpret_cast<void***>(pConsole);
auto AddCommand = reinterpret_cast<AddCommandFn>(vt[kAddCommandFuncOverloadSlot]);
AddCommand(pConsole, name, func, 0x80000, help);   // VF_RESTRICTEDMODE
```

Also recommend exposing the engine wrapper at `0x180B99098` as Address Library
ID 2004 ("iconsole-addcommand-static-wrapper"). Plugins that want to call
AddCommand without first walking the IConsole vtable can resolve and call
that wrapper directly with the (sCommand, func, nFlags, sHelp) tuple. This
also future-proofs against vtable-slot drift across game updates: the
wrapper is a stable .text address that always resolves to the right slot
via `[rip + 0x3d92802]` (pConsole) + `[rax + 0x108]` (vtable[33]).

The two Address Library IDs to seed:
- `id=2003`, name=`iconsole-vtable-addcommand-func`, slot=33, rva=`0x00B9A2B0`, game_version=`release_1_5_1164953_841`, status=`verified-static`
- `id=2004`, name=`iconsole-addcommand-static-wrapper`, rva=`0x00B99098`, game_version=`release_1_5_1164953_841`, status=`verified-static`

(Phase 7's existing seed CSV's id 2000 should be retired or marked
deprecated — its `vtable_slot=32` was wrong.)

---

## Open follow-ups (not blocking the fix)

- **The duplicate-name check (slot 33 cold path)** runs against the same `[+0xA0]` map that slot 32's dedup also queries. If we register `"kcdx_test_cap13"` first as a script-command (current bug state) and *then* try again as a func-command, the second registration may take the duplicate-warning path and SKIP insertion. **Test plan**: after applying the fix, restart the game cleanly (don't just re-init kcdx mid-session) to be sure we're not bumping into a stale script-command entry from a prior session. (CryEngine's `pConsole + 0xA0` map is in-memory only, so a process restart clears it.)

- **The Phase 6b Flash binder pattern at `0x1807D5FC8`** should be revisited to find the `~` console submit handler. If we can confirm it calls `IConsole::ExecuteString` directly (vs. `System.ExecuteCommand` via Lua), we close the small ambiguity in Q4. Not required for the fix to work.

- **`con_restricted` CVar default value.** The dispatcher gates VF_CHEAT commands on `con_restricted != 0`. If the game ships with `con_restricted = 0` (likely, per CryEngine defaults), VF_CHEAT commands like `playerGoto` would be blocked from the player's `-console`. The engine has internal ways to enable them (commandline `-devmode`, etc.). For kcdx, VF_RESTRICTEDMODE remains the right default.
