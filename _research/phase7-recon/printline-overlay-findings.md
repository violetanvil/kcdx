# IConsole console-overlay print method — verified findings

Research task (2026-05-29). Game build `release_1_5_1164953_841`. Question:
does KCD2's `IConsole` (in `WHGame.dll`) expose a method that writes a
caller-supplied line of text directly to the in-game `~` console's on-screen
line buffer (visible overlay), as opposed to the log file or a CVar-gated sink?

Static analysis only (Ghidra-project bytes via `pefile + capstone`). The
maintainer confirms true overlay-paint with one live probe (§5).

---

## 1. ANSWER

**YES — `IConsole::PrintLine(const char* s)` exists and writes to the console's
on-screen line ring-buffer.** Confidence **HIGH** that it is PrintLine and that
its sink is the console line buffer (not a log file, not the ILog path).
Confidence **MEDIUM-HIGH** that the line buffer it appends to is the same buffer
the `~` overlay renders — the binary shows it is the console object's own
line/scrollback buffer with a scrollback-max trim, which is the overlay's
backing store; the one residual uncertainty (does the overlay *render* this
buffer, vs. it being a dedicated-server/headless scrollback) is what the live
probe in §5 settles.

A sibling, `PrintLinePlus(const char* s)`, appends to the *last* line instead of
starting a new one. Both are the CryEngine console "print" family.

## 2. THE FACTS

### Slot index (empirically anchored — not from the canonical header)

The IConsole vtable base is at **`.rdata` RVA `0x03DCE840`** (VA `0x183DCE840`).
Found by scanning `.rdata` qword-aligned for the unique window where all five
already-verified IConsole method RVAs land at their known slot offsets
simultaneously (GetCVar@23, AddCommand-script@32, AddCommand-func@33,
RemoveCommand@34, ExecuteString@35). **Exactly one** location in `.rdata`
matches all five — that uniqueness is what makes the slot assignment empirical
rather than a canonical-header assumption. Script + full dump:
`find_console_printline.py` / `_console_vtable_dump.txt`.

| Method | vtable slot | byte offset | RVA |
|---|---|---|---|
| GetCVar (anchor) | 23 | 0xB8 | 0x009DF818 |
| GetVariable(...,char*) | 24 | 0xC0 | 0x01A72DA0 (`xorps xmm0,xmm0; ret` — stub) |
| GetVariable(...,float) | 25 | 0xC8 | 0x0066CF70 (`xor eax,eax; ret` — stub) |
| **PrintLine** | **26** | **0xD0** | **0x008DFF08** |
| PrintLinePlus | 27 | 0xD8 | 0x0247C878 |
| GetStatus | 28 | 0xE0 | 0x00863DD0 (`movzx eax,[rcx+0x1DC]; ret` → bool) |
| Clear | 29 | 0xE8 | 0x024754A4 |
| Update | 30 | 0xF0 | 0x0052F65C |
| Draw | 31 | 0xF8 | 0x009AEC44 |
| AddCommand-script (anchor) | 32 | 0x100 | 0x0100A3D4 |
| AddCommand-func (anchor) | 33 | 0x108 | 0x00B9A2B0 |
| RemoveCommand (anchor) | 34 | 0x110 | 0x0100955C |
| ExecuteString (anchor) | 35 | 0x118 | 0x007A5818 |

Slot 26 sits between the two verified anchors GetCVar(23) and the AddCommand
pair(32/33). The canonical CryEngine `IConsole.h` order in that exact span is
`GetCVar, GetVariable×2, PrintLine, PrintLinePlus, GetStatus, Clear, Update,
Draw, AddCommand×2` — the binary reproduces that order slot-for-slot here, and
every adjacent slot's body matches its canonical role (the two GetVariable
stubs, GetStatus's `bool` getter), so the alignment is corroborated on both
sides, not just counted. (Note: the +1 AddCommand divergence that put the
func-overload at 33 instead of the canonical 32 originates earlier in the
vtable — near Release/dtor — and does **not** perturb this region; 23 and 32/33
both land exactly where the fingerprint requires.)

### ABI (read from the body + arg-walk, not prologue shape)

```
void __fastcall IConsole::PrintLine(IConsole* this /*rcx*/, const char* s /*rdx*/)
```

kcdx signature-DSL: **`void (ptr self, cstr s)`** — 2 args, `void` return,
`__thiscall`/MS-x64-fastcall.

Body evidence (`_printline_candidates.txt`, walker output on `0x1808DFF08`):
- `mov rdi, rcx` — arg1 = `this` (IConsole*).
- `mov rbx, rdx` — arg2 is the only other incoming register consumed; it is
  passed straight in as the **source pointer** to a CryStringT string-assign:
  `mov rdx, rbx; lea rcx,[rbp+0x10]; call 0x1804F6AC8` (the same string-assign
  helper the script-AddCommand overload uses) → arg2 is a `const char*`.
- `r8`/`r9` are never read as inputs (only written as scratch: `xor r8d,r8d`,
  `mov r8b,0x20`); no incoming stack args are read. → exactly 2 args.
- Return path `add rsp,0x30; pop rbp; ret` sets no `eax`/`rax` as a return
  value → **`void`**.

### SINK assessment — on-screen console line buffer, NOT log/CVar-gated

The body, after assembling the CryStringT from `s`, does:
1. **Trailing-newline strip**: reads `nLength` (`movsxd rdx,[rax-8]`), checks the
   last byte `== 0x0A` (`\n`), and char-finds `\n`(0x0A)/`\r`(0x0D) — newline
   normalization, matching "print a string and go to the new line."
2. **Appends the assembled line to a buffer at `this+0x18`**:
   `lea rcx,[rdi+0x18]; lea rdx,[rbp+0x10]; call 0x1808E02F4`.
3. **Scrollback trim**: `mov esi,[rip+...]` (a CVar value), `cmp [rdi+0x38], esi`
   (current line count vs. max), `jg <trim>` — the bounded line ring-buffer that
   a console overlay scrolls.

There is **no `ILog`/file-write call and no varargs** on this path. That
distinguishes it from `IConsole::Exit(const char* fmt, ...)` (varargs → log +
abort) and from the engine's ILog file sink. The buffer at `this+0x18` with a
line-count cap at `this+0x38` is the console's own scrollback store — the
overlay's backing data. This is the line buffer, not a log file.

**Predecessor corroboration (independent of our disassembly):** the shipping
KCD2 1.5 plugin `muyuanjin/kcd2db` calls `gEnv->pConsole->PrintLine(...)` as its
chosen path for `-console`-visible debug output (`src/db/LuaDB.cpp:517/524/531`,
`src/log/log.cpp:82`; its docs state the output "显示在独立控制台窗口" / shows in
the console window). A working predecessor using PrintLine for exactly this
purpose is strong support that PrintLine reaches the visible console. (Caveat:
muyuanjin calls it through the *literal canonical header struct*, i.e. it trusts
the canonical slot — our independent empirical vtable walk is what removes that
assumption and confirms the slot is 26 in THIS binary.)

### Proposed seed rows (recording is the working flow's job — §6, not this skill)

Next unused entity id in `address_names_seed.csv` is **144** (max is 143
`CryString_init_from_string`). Console-method ids in the names seed run 13–18;
the version rows mirror at 13–18. Proposed (append-only):

`address_names_seed.csv`:
```
144,IConsole_PrintLine,,,,,,"IConsole::PrintLine(const char* s) -> void. __thiscall (rcx=IConsole*, rdx=const char* s). vtable[26]. Prints one line to the console's on-screen line ring-buffer (the ~ overlay's backing store): assembles a CryStringT from s, strips a trailing \n/\r, appends to the line buffer at this+0x18, trims to a scrollback-max CVar checked at this+0x38. NOT a log-file or ILog write and NOT varargs (distinct from IConsole::Exit). Sibling PrintLinePlus (vtable[27], RVA 0x0247C878) appends to the LAST line instead of a new one. Slot verified empirically: the IConsole vtable base is .rdata RVA 0x03DCE840, located by the unique 5-slot fingerprint of the already-verified slots 23/32/33/34/35; slot 26 sits between GetCVar(23) and AddCommand(32/33) with every adjacent slot's body matching its canonical role. ABI from body-wide analysis, not prologue shape. Visible-overlay paint to be confirmed by a live probe. Predecessor muyuanjin/kcd2db ships gEnv->pConsole->PrintLine for -console output."
```

`address_versions_seed.csv`:
```
144,1.5.1164953,WHGame.dll,0x008DFF08,"void (ptr self, cstr s)",1.5.1164953,VioletAnvil,2026-05-29,maintainer_ghidra
```

(Optional second pair for PrintLinePlus, id 145, RVA `0x0247C878`, sig
`void (ptr self, cstr s)` — append if the line-append variant is wanted.)

AOB for re-derivation across builds (PrintLine prologue, RVA 0x008DFF08):
`E9 03 00 00 00 CC CC CC 48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 48 8B EC 48 83 EC 30 48 8B F9 48 8B DA` (the `jmp +3 / int3 pad / save rbx,rsi,rdi / push rbp / sub rsp,0x30 / mov rdi,rcx / mov rbx,rdx` head). More robust: re-run the 5-slot vtable fingerprint (`find_console_printline.py`) and read slot 26.

## 3. EVIDENCE TRAIL — which ladder tier answered

- **Tier 1 (seeds):** had the four anchored IConsole slots (GetCVar 23,
  AddCommand 32/33, RemoveCommand 34, ExecuteString 35) — no PrintLine row.
  Provided the slot anchors the fingerprint needs.
- **Tier 2 (`_research/phase7-recon/`):** `console-command-abi.md`,
  `DISPATCH-INVESTIGATION.md`, `_abi_console_return_types.txt` covered
  AddCommand/RemoveCommand/ExecuteString only. The IConsole vtable BASE was
  never resolved by prior recon (it located methods by string/caller anchors,
  not a contiguous vtable). So tier 2 did **not** carry the PrintLine slot.
- **Tier 3 (predecessor sigs):** `muyuanjin/kcd2db` ships
  `gEnv->pConsole->PrintLine(...)` (canonical-header call) for visible
  `-console` output — strong sink corroboration, but slot-trusts the header.
  The canonical `IConsole.h` gave the vtable ORDER used as the lead for which
  slots to inspect.
- **Tier 4 (wiki):** not consulted — an internal ABI/slot fact, out of scope.
- **Tier 5 (fresh disassembly):** required for the empirical slot. New scripts
  dropped under `_research/phase7-recon/` for reuse:
  - `find_console_printline.py` → `_console_vtable_dump.txt` (vtable base via
    5-slot fingerprint + slots 0..63 dump).
  - `disasm_printline_candidates.py` → `_printline_candidates.txt` (slots 24–31
    bodies — PrintLine identification).
  - `_printline_callsites.txt` (engine `call [reg+0xD0]` scan — noisy at 620
    hits because offset 0xD0 collides across unrelated vtables; treated as
    supportive only, not load-bearing).
  - ABI via `_research/phase6-save-load/phase6_abi_walker.py` on `0x1808DFF08`.

## 4. WHAT WAS RULED OUT

- **Not a log-only / CVar-gated sink:** the slot-26 body has no ILog/file-write
  call and no varargs; it appends to the console object's own line buffer
  (`this+0x18`) with a scrollback cap (`this+0x38`). This is the structural
  difference from a log path, which is why this method is the candidate and
  `Exit`/ILog are not.
- **Not the wrong overload:** slot 25 and 24 are getter stubs; slot 28 is the
  `bool GetStatus` getter — none take a `const char*` to print.
- **Adjacent mechanisms found:** `PrintLinePlus` (slot 27) for last-line append;
  `ShowConsole`/`ExecuteCommand` UI entry points seen in `_console_ui_xrefs.txt`
  are the Scaleform-side console *input* path, not the print API. ExecuteString
  (slot 35) with `bSilentMode=false` also echoes the command to the console, but
  that routes through command dispatch (heavier, side-effecting) — PrintLine is
  the direct text-to-buffer path.

## 5. THE SINGLE LIVE PROBE TO CONFIRM OVERLAY-PAINT

Wire a one-shot call (e.g. from a dev probe or the `kcdx.console.print` backing
once it targets slot 26) resolving `gEnv->pConsole`, reading `vtable[26]`, and
calling it `__fastcall(pConsole, "KCDX_PRINTLINE_PROBE_OK")`.

- **Command line / gesture:** launch with the `-console` flag, reach main menu,
  open the `~` console.
- **Expected on-screen observable (PASS):** the literal line
  `KCDX_PRINTLINE_PROBE_OK` appears in the console overlay's scrollback.
- **FAIL / ambiguous:** line appears only in `kcdx-dev.log`/the game log but NOT
  on the overlay → the buffer at `this+0x18` is a non-rendered scrollback and a
  different render path is needed; re-observe (do not theory-hop).

One command, one on-screen string — that is the whole confirmation.

## 6. HANDOFF

This skill verified the fact + produced provenance. Writing id 144 (+ optional
145) into `data/seeds/address_names_seed.csv`, `data/seeds/address_versions_seed.csv`,
and the `src/address_library.cpp::kEntries[]` mirror is a code edit for the
working flow (append-only IDs; edit both seeds and the mirror). The exact prose
+ field values are in §2, ready to paste.
