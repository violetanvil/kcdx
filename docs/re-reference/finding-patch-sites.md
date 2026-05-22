# Finding patch sites in WHGame.dll

> **Vendored RE methodology reference.** Originally written for the
> predecessor declarative-patch engine; read every "mempatch" mention
> below as the generic byte-patch tool — the patch-site-finding
> technique is engine-agnostic and applies directly to kcdx's
> `[[patch]]` schema. Schema and `../examples/` links are from the
> original doc and may not resolve in this repo.

You want to ship a kcdx patch, but you don't yet know *what
bytes to patch*. This guide is the **general methodology** for finding
a patch site in `WHGame.dll` — applicable to any popup, gameplay
restriction, action gate, or hard-coded behavior the game presents.

The methodology comes in 15 steps, ordered as you'd execute them. Most
of them apply regardless of what specifically you're trying to patch.
Where it helps to be concrete, a single worked example runs through the
guide so you can see what each step looks like with real bytes —
*outfit-swap-in-combat* (the patch that removes the "You can't switch
outfits in combat" popup). Read the outfit-swap content as illustration
of the technique, not as the only thing this guide is for.

Companion doc: [`writing-safe-patches.md`](writing-safe-patches.md)
covers what to do *after* you've found a patch site (locator tiers,
verification, schema). This doc covers everything *before* that.

---

## 0. Shortcuts — what other people have already mapped

Before you fire up Ghidra, check whether the work you're about to do
has been done by someone else. KCD2 modding has a small but active
ecosystem of reverse-engineering references.

### [muyuanjin/kcd2-mod-docs](https://github.com/muyuanjin/kcd2-mod-docs)

A community reference repo with **verified function offsets** for
WHGame.dll: `gEnv`, `pScriptSystem`, `pGame`, `pConsole`, and the recipe
for finding each via byte-pattern search even after game updates. See
their [`DISASSEMBLY.md`](https://github.com/muyuanjin/kcd2-mod-docs/blob/main/DISASSEMBLY.md).
If your patch involves CryEngine internals (the console, the script
system, the game object), this is your first stop.

### [yobson1/kcd2lua](https://github.com/yobson1/kcd2lua)

A bootstrap ASI for KCD2 Lua access. Its `cpp/src/dllmain.cpp`
contains the verified AOB signatures for `lua_pcall` and the engine's
`update` tick function. If you need to hook into the Lua VM (or the
game's tick), start there.

### The official Warhorse modding wiki

[warhorse.youtrack.cloud/articles/KM-A-1](https://warhorse.youtrack.cloud/articles/KM-A-1)
documents the **data-driven** modding surface (pak mods, table patches,
quest concept graphs, Lua bindings). **Read this first to confirm your
target actually requires a memory patch** — if there's a CVar, table
column, or XML attribute that controls the behavior, you don't need an
ASI at all. The pak path is simpler, Workshop-compatible, and survives
game updates automatically.

### The worked example here

The [`examples/outfit-swap-in-combat/`](../examples/outfit-swap-in-combat/)
folder in this repo is a complete, verified mempatch plugin. If a patch
you're investigating happens to touch code near the outfit-swap
function, its AOB and surrounding context may save you a Ghidra session.

### What you still have to do yourself

- **Find the specific function** that contains your gate. Community
  references cover engine infrastructure (gEnv, Lua, console). Anything
  specific to gameplay logic — the function that decides whether you
  can equip an item, talk to an NPC, open a chest, drop a weapon —
  almost certainly hasn't been mapped yet.
- **Verify the bytes** at runtime via x64dbg — even patterns published
  by someone else need re-verification against your installed game
  version.
- **Pick a unique AOB pattern** for your TOML. Any pattern shorter than
  ~12 bytes is rarely unique; longer is safer.

---

## 1. What you need installed

You will be doing both **static analysis** (reading the compiled binary
to find candidate sites) and **dynamic analysis** (attaching a debugger
to a running game to verify the patch works). The two are complementary
and trying to use only one will burn time.

| Tool | What it does | Get it from |
|---|---|---|
| **Ghidra** 12.1+ | Disassembler + decompiler. Free, NSA-released, Java-based. ~870 MB. | https://github.com/NationalSecurityAgency/ghidra/releases |
| **OpenJDK 21+** | Ghidra's runtime. | `winget install Microsoft.OpenJDK.21` or any JDK 21+ |
| **x64dbg** | Live debugger. Free. ~30 MB. | https://github.com/x64dbg/x64dbg/releases |
| The game | Required for live verification. Anti-cheat: KCD2 ships without any, you're safe to attach. | Steam |

You do *not* need:

- A C++ compiler
- Cheat Engine (x64dbg is sufficient and clearer for this kind of work)
- IDA Pro (Ghidra's decompiler is competitive for KCD2-sized binaries)

---

## 2. Frame the problem precisely

Before any tools, write down two things:

1. **The exact in-game text or behavior you want to suppress or alter.**
   Get the wording character-for-character if it's a popup, or describe
   the specific behavior change you want if it's not. (Worked example:
   the popup *"You can't switch outfits in combat."*)
2. **The trigger.** When does the message appear, or when does the
   behavior fire? What player action causes it? (Worked example:
   pressing the outfit-swap key while the inventory is open in combat.)

This sounds trivial but it controls every later decision. The
investigation that produced the outfit-swap patch eventually landed on
the function that registers the *action binding* for `next_outfit` —
not the function that shows the popup, and not the function that
checks `IsInCombat` globally. Getting specific about the trigger meant
the search narrowed to the **specific action**, not generic
combat-state code that fires from dozens of callers.

The same principle applies regardless of what you're patching:

- If your target fires on a player input (a keypress, a menu click),
  the trigger frames the search as "find the registration for that
  input."
- If your target fires on game state (entering combat, leaving an
  area, interacting with an NPC type), the trigger frames the search
  as "find the state-transition handler for that event."
- If your target is a UI hard-coded constraint (a cap, a threshold, a
  hidden minimum), the trigger frames the search as "find the
  comparison against that constraint."

### Is mempatch even the right tool?

mempatch handles **same-length byte rewrites**: change `mov r14b, al`
into `xor r14d, r14d` (both 3 bytes), or flip a constant from `0x14`
to `0xFF`. If what you actually need is to **add code** to the binary
— a new branch, a function detour, code that didn't exist before —
mempatch is the wrong tool. The same-length restriction is fundamental
to how it works; you can't insert bytes into a loaded PE in place
without breaking every relative address after the insertion point.

For code-injection work the right tools are MinHook (function-level
detours) or hand-written code caves, packaged as your own ASI.
mempatch may grow `[[inject]]` support in a future major version, but
not today. See [`writing-safe-patches.md`](writing-safe-patches.md) §
"Code injection is not supported" for more.

If your goal still fits inside same-length byte rewrites — most
gameplay-restriction patches do — keep reading.

---

## 3. Inventory the game

```
<install>/Bin/Win64MasterMasterSteamPGO/
```

Two DLLs are interesting:

| File | Size | Role |
|---|---|---|
| `KingdomCome.exe` | ~5 MB | The launcher / process host. Calls into WHGame.dll. |
| `WHGame.dll` | ~86 MB | All game logic. **This is what you patch.** |
| `WHGameArm.dll` | ~107 MB | Themida/Arxan-wrapped variant. Not loaded by default. Static analysis will be misleading — ignore unless you confirm it's the loaded module. |

Confirm which DLL the running game loads by reading `kcd_launcher.log`
right after launching the game. You should see a line like:

```
Loading Game DLL 'WHGame'...
```

(The `Arm` variant is for distribution channels that require code
protection. Your analysis target is always `WHGame.dll` for a Steam
build.)

### Get a static copy

Copy `WHGame.dll` somewhere safe to analyze without disturbing the live
install. The Ghidra project you're about to create binds to a file path,
so put it somewhere stable.

### Pull the vanilla paks too

The XML configuration files in `Data/Tables.pak` and `Data/Scripts.pak`
are extremely valuable for cross-referencing — they contain
human-readable strings (localization keys, action names, FlowGraph node
names) that point you at the C++ code that consumes them. Unzip both
paks somewhere; KCD2 paks are just renamed zips.

```
vanilla/
  Scripts_extracted/          # Lua scripts + AI behavior trees
  Tables_extracted/           # All XML tables, including PackageString.xml
  IPL_extracted/              # defaultProfile.xml (input maps)
```

---

## 4. Find the string

This is the only step where the localization XML is directly useful.
Open `vanilla/Tables_extracted/Libs/Tables/text/PackageString.xml` and
search for keywords from your target text:

```xml
<PackageString Name="cant_change_outfit_in_combat" Type="Apse" Comment="UIApseCharacter.cpp">
    <Text StringName="cant_change_outfit_in_combat" Text="You cannot switch outfits in combat." LoadedOn="..." />
</PackageString>
```

**Three pieces of information are now yours:**

1. **The localization key**: `cant_change_outfit_in_combat`. This is
   what the C++ code uses to look up the displayable string. Code
   references this key as a literal string.
2. **The Type**: `Apse`. Tells you the UI subsystem (`Apse` = the
   inventory/character menu UI in KCD2).
3. **The Comment**: `UIApseCharacter.cpp`. The dev who added the entry
   noted which source file uses it. **This is an author hint, not a
   binding** — but it's still useful as a starting compass.

Sometimes the displayed text and the localization key text are slightly
different. Check the **actual loaded text** in `Localization/English_xml.pak`
under `text_ui_menus.xml`:

```xml
<Row><Cell>cant_change_outfit_in_combat</Cell><Cell>You cannot switch outfits in combat.</Cell><Cell>You can't switch outfits in combat.</Cell></Row>
```

(The two display columns are "old wording" and "current wording" — the
game uses the third.)

---

## 5. The trap: searching for the string in the DLL

This is where most investigations dead-end if you don't know the pattern.

Localization keys (like `cant_change_outfit_in_combat` in the worked
example) exist in `WHGame.dll`'s `.rdata` section as plain C strings.
You will find them trivially. You will then assume the code references
them via an LEA instruction, set a memory breakpoint, trigger the
event, and watch your breakpoint never fire.

**This was the biggest time-waster in the original investigation, and
it's a foot-gun any time the string you found is a localization key.**

What actually happens at runtime:

1. At game startup, the localization XML parser loads `PackageString.xml`
   into a runtime hash table. Each entry gets an **integer ID**.
2. From then on, *no code references the string by pointer*. The C++
   side only references the integer ID.
3. The first time a given localized message is displayed, the engine
   resolves the ID to a heap-allocated `std::string` and caches it.
   Subsequent displays read the cached heap copy, not the `.rdata` copy.

**Practical consequences:**

- A static LEA xref scan for the localization-key string will return
  **zero hits**. Don't spend hours on this.
- A hardware memory breakpoint on the `.rdata` string fires only on the
  FIRST display of that message in a session. By the time you trigger
  the event you care about, the message has usually been displayed once
  during some unrelated UI init and is now cached. Your BP misses the
  actual gate.

So: **the localization key is the trail's starting point, not its
endpoint.** It tells you what the gate's blocker-message is, which is
useful for confirming you found the right function later. It does not,
by itself, lead you to the gate.

---

## 6. The right anchor

To trace from "I know what string the popup uses" to "I know what
function fires the popup," you need a different anchor — a string the
**code actually references** by LEA pointer rather than int ID. What
serves as such an anchor depends on what your target is.

| Target type | Anchor candidates that ARE LEA-referenced |
|---|---|
| Player action gate (keypress / button) | The action's name in `IPL_GameData.pak`'s `Libs/Config/defaultProfile.xml` |
| Action map | The action-map name in the same file |
| UI button / Flash event | The button or event name string in `Scripts_extracted/` or referenced directly from compiled UI handlers |
| Item / weapon-class restriction | The item or class GUID string from `Tables_extracted/Libs/Tables/item/*.xml` |
| NPC interaction | The interaction name in entity XML or AI behavior trees |
| Quest condition | The quest XML node name or its associated script function name |
| Console command (cvar) | The CVar name string — these are always LEA-referenced because they're registered against the console at startup |

The unifying property: **strings that get matched against XML/data at
startup are referenced by C++ code via LEA** (because the engine has to
string-compare them). Strings that are *only* used as keys into a
runtime hash table (localization, some lookup tables) are not — they
get interned as int IDs and the LEA reference disappears.

### Worked example: action-map anchor

For a player-action gate, action maps live in `IPL_GameData.pak` at
`Libs/Config/defaultProfile.xml`. Grep for keywords related to your
target. Continuing the outfit-swap example:

```bash
grep -i "outfit\|change_outfit\|switch_outfit" vanilla/IPL_extracted/Libs/Config/defaultProfile.xml
```

```xml
<actionmap name="apse_change_outfit" priority="apse" exclusivity="0">
    <action name="next_outfit" onPress="1" onRelease="1" keyboard="_keybinds_ref_" xboxpad="xi_y" pspad="pad_triangle"/>
</actionmap>
```

Two new search terms: **`apse_change_outfit`** and **`next_outfit`**.
These are passed as **C string literals** to the action-registration
code in `WHGame.dll`. Unlike localization keys, **these strings ARE
referenced by code via LEA**, because they're matched against the
action-map XML at startup by string-comparison.

This is the actual entry point.

---

## 7. Run Ghidra (once, properly)

You will use Ghidra in two modes:

- **Headless analysis** to do the long up-front work of decompiling the
  whole binary. ~30 min on an 86 MB PGO-optimized DLL.
- **GUI** to browse the analyzed project and decompile functions.

### Headless first

```powershell
$base = 'C:\path\to\ghidra_12.1_PUBLIC'
& "$base\support\analyzeHeadless.bat" `
    "C:\YourWorkspace\ghidra_project" KCD2 `
    -import "C:\YourWorkspace\WHGame.dll" `
    -overwrite
```

Symptoms of progress:

- The log will sit on `ANALYZING all memory and code` for ~20 min with
  no per-line output. That's normal.
- Java will hold ~2 GB of RAM and burn one full CPU core.
- It eventually emits `Analysis succeeded` and `Save succeeded`.
- The project file is now ~1 GB on disk.

Common pitfalls:

- **Don't run a custom post-script for your first pass.** Get the
  analysis saved. Then explore in the GUI.
- **Don't kill it because it looks idle.** It's not idle. The
  decompiler phase has no per-line logging.
- If you genuinely need to abort, the partial analysis is unusable —
  start over.

### Then open the GUI

```powershell
& "$base\ghidraRun.bat"
```

Open the project, double-click the `WHGame.dll` entry. The CodeBrowser
opens with disassembly on the left and decompiler on the right.

---

## 8. Find code that references your anchor string

In the CodeBrowser:

1. **Search → For Strings → Search**. Wait for the Defined Strings
   window. Filter for the anchor string you picked in §6.
2. Find the row for your string. In the outfit-swap example:
   ```
   183a88790    s_next_outfit_183a88790      "next_outfit"
   183a3cea8    s_apse_change_outfit_183a3cea8   "apse_change_outfit"
   ```
3. **Double-click** the row. The listing jumps to that address. Look at
   the line just above the string declaration for `XREF[N]:` annotation:
   ```
   XREF[1]:    FUN_1805616e8:1805618fa (*)
   ```
   That tells you the string is referenced by exactly one function
   (`FUN_1805616e8`) at one instruction (`1805618fa`).

What the xref count tells you:

- **Exactly one xref** — easy case. That function is your target.
- **Multiple xrefs** — all of them are candidate sites. Usually one
  will be the "register / setup" function (the one you want) and
  others might be "reset", "save config", "tear down", etc. Inspect
  the decompile of each to identify which one contains the
  conditional check you're trying to patch.
- **Zero xrefs** — your search target is data-only (interned as an int
  ID, like the localization keys in §5). Pick a different anchor from
  the table in §6.

---

## 9. Read the decompile

Navigate to the function Ghidra pointed you at (`G` → address → Enter).
Look at the right-side decompiler pane.

You're looking for **the gate**: the conditional that decides whether
the restricted thing happens. Gates almost always show up as one of a
few common shapes:

- **A boolean stored from a function-call result** that's then used as
  an argument to some other call. This is the most common shape for
  popup-and-block gates: `bool blocked = SomeStateCheck(); register(...,
  !blocked, ...)`. Easy to patch — force the boolean to your desired
  value.
- **An `if` statement that branches to an error path on a state check.**
  `if (in_combat) { show_popup(); return; }`. Patchable by flipping the
  conditional, NOPing it, or zeroing the state-check result before it.
- **A numeric comparison against a constant** (a cap, a threshold).
  `if (player_level < required) { ... }`. Patch the constant, the
  result of the comparison, or the conditional jump.
- **A switch-statement dispatch** keyed on an enum. Less common for
  binary gates; more common for "this NPC type can do X."

### Worked example: outfit-swap

The function the action-name xref pointed at:

```c
void FUN_1805616e8(longlong *param_1) {
    // ... setup ...
    cVar2 = (**(code **)(*param_1 + 0x130))(param_1);  // outer check
    if (cVar2 == '\0') {
        // OutfitDeactivate branch
        ...
    } else {
        (**(code **)(*plVar4 + 0xe8))(plVar4, "apse_change_outfit", 1);

        // THE GATE — a 3-level vtable dispatch returning a bool
        cVar2 = (**(code **)(*(longlong *)(*(longlong *)(param_1[1] + 0x90) + 0xb60) + 8))();

        // The action is registered as "enabled" only when cVar2 == 0
        FUN_1804f692c(local_res10, "next_outfit");
        FUN_1804f692c(&local_res8, "apse_change_outfit");
        (*pcVar1)(plVar4, &local_res8, local_res10, cVar2 == '\0', 1);

        // Then the localization key is bound as the blocker message
        uVar5 = FUN_1804f692c(local_res18, "cant_change_outfit_in_combat");
        (*pcVar1)(plVar4, &local_res8, local_res10, uVar5, 1);
        ...
    }
}
```

Three landmarks identify this as the gate:

- A boolean `cVar2` computed by calling a virtual method through a long
  chain of pointer dereferences (`*(*(*(param_1[1] + 0x90) + 0xb60) + 8)
  ()`). The chain depth and the vtable dispatch are characteristic of a
  game-state query — here, `IsInCombat()` or a sibling. Long pointer
  chains in CryEngine code are nearly always virtual method calls
  through a global-state object.
- That boolean is passed as the `enabled` argument
  (`cVar2 == '\0'` — note the inversion) of an action-registration call.
- The localization key from §4 appears right after as the
  blocker-message argument of a sibling call.

The gate, then, is the boolean computed by the virtual call. Forcing
that boolean to a chosen value — usually `false` (i.e., "not blocked")
— permanently enables the action. Don't patch the virtual call itself
(many callers); patch the **store of its result** at this one call
site. §10 covers how.

---

## 10. Find the corresponding assembly

Click the gate line you identified in §9 in the decompiler pane. The
listing view on the left highlights the matching machine instructions —
this is how you cross from the decompiler's C-style view back to
patchable bytes. In the outfit-swap example, the gate line maps to
lines `180561741` through `180561759`:

```
180561741  49 8B 47 08              MOV  RAX, [R15+0x8]      ; param_1[1]
180561745  48 8B 88 90 00 00 00     MOV  RCX, [RAX+0x90]     ; +0x90
18056174c  48 81 C1 60 0B 00 00     ADD  RCX, 0xB60          ; +0xB60
180561753  48 8B 01                 MOV  RAX, [RCX]          ; vtable
180561756  FF 50 08                 CALL [RAX+0x8]           ; IsInCombat()
180561759  44 8A F0                 MOV  R14B, AL            ; <-- save bool result
```

**The patch target is the result store** at `180561759`:
`44 8A F0` (`mov r14b, al`).

This is the general principle: **patch the result-store at the gate
site, not the function that produces the result.** Don't patch the
virtual call itself, don't patch the function it dispatches to, don't
patch the vtable. Patch only the **store of the result** at this one
specific call site. That makes the change local to this one gate.
Other callers of the same query function (in the outfit-swap example,
combat AI / music / hit detection / detection — all of which also call
`IsInCombat()`) are completely unaffected. This locality is what makes
a result-store patch safe to ship.

### Choosing replacement bytes

You need an instruction that:

1. Is **the same length** as the original (`44 8A F0` is 3 bytes; your
   replacement must also be 3 bytes to avoid shifting subsequent
   instructions).
2. Sets the destination register to your desired forced value (usually
   0).

For the outfit-swap example, `mov r14b, al` → `xor r14d, r14d`:

- `xor r14d, r14d` = `45 31 F6` (3 bytes, identical length)
- Zeros the full r14 register (the byte view r14b will read 0 from now
  on — i.e., always "not in combat" for this one binding's gate)

For other registers / forced values:

| Forced value | Bytes | Notes |
|---|---|---|
| `xor reg32, reg32` (zero) | varies (typically 2-3) | Cleanest. The `xor` zeros a 32-bit register, which on x86-64 also zeros the upper 32 bits. |
| `mov reg, 0` (zero, explicit) | longer | Same effect but emits more bytes; usually doesn't fit the 3-byte slot. |
| `nop` filler | `90` each | If your original is `n` bytes and your replacement instruction is shorter, NOP-pad the rest. |

Use a tool like https://defuse.ca/online-x86-assembler.htm or
`xed`/`nasm` locally to assemble the candidate instruction and confirm
its byte length.

---

## 11. Verify the patch in x64dbg before shipping

Static analysis is necessary but never sufficient. Verify in the live
game before shipping a mempatch plugin.

### Setup

1. Launch the game normally (Steam → Play). Get to a state where you
   can reliably trigger the gate (for the outfit-swap example: load a
   save in or near a combat encounter).
2. Open x64dbg.
3. **File → Attach** → select `KingdomCome.exe`.

Note that anti-cheat is not present in KCD2, so attaching is safe.

### Find the live patch address

Ghidra showed the patch site at preferred-base VA `0x180561759`. The
DLL is loaded at a different base at runtime due to ASLR. Find the
runtime base:

In x64dbg's command bar (bottom of window):

```
mod.base(WHGame.dll)
```

You get back something like `0x7FFCCBE10000`. Add your RVA:

```
0x7FFCCBE10000 + 0x561759 = 0x7FFCCC371759
```

That's the live patch address.

### Confirm the bytes

```
Ctrl+G   → 7FFCCC371759 → OK
```

The CPU view jumps there. The line should read `44 8A F0  mov r14b, al`.

**If it doesn't:** your AOB matched somewhere else, OR the game updated
since you analyzed and the function moved. Search by AOB instead:

In the Command bar:

```
findall 7FFCCBE10000, 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0, 0x4000000
```

The References tab will show matches. If you get exactly one, that's
the new address; rerun your math. If you get zero, the surrounding
pattern changed too and you need a more flexible AOB.

### Apply the patch live

1. Click the line `44 8A F0  mov r14b, al` so it's highlighted.
2. Press **Space** (Assemble dialog opens).
3. Type the replacement instruction (for the outfit-swap example,
   `xor r14d, r14d`; substitute the replacement you chose in §10).
4. Click OK.
5. Verify the line now reads `45 31 F6  xor r14d, r14d`.

### Verify in-game

Press **F9** in x64dbg to resume the game. Trigger the gate scenario
you framed in §2. Expected outcome:

- The blocking behavior (popup, denial, etc.) does not happen
- The action succeeds (or the new behavior takes effect)

If the action succeeds but the game crashes a few seconds later: your
patch had a side effect. Revisit §9 — you might have patched something
that has other consumers. A real example from the outfit-swap
investigation: an early attempt flipped a `je` instruction that *also*
gated all localized text display, not just the targeted popup. After
patching, every UI string in the game showed `SAMPLE TEXT`. That `je`
was a downstream join point with many consumers, not the upstream gate
specific to the action. Result-store patches (§10) are much less prone
to this than conditional-jump patches.

If the blocking behavior still happens: your patch site isn't the
actual gate. Either there's a *different* function fielding the same
trigger that you didn't find, or the gate is upstream and what you
patched is downstream cosmetic. Look further. Sometimes the symptom
helps narrow which one: if the popup still appears but the action
*would* now succeed (you can hear sound effects, see a brief animation
flash), you patched downstream of the popup but upstream of the
action — keep tracing back.

### Restore before exiting

Always restore the original bytes before detaching the debugger if you
want the game to keep running cleanly. Re-assemble at the patch line
with the original instruction (`mov r14b, al`). Or just close the game.
Patches via x64dbg are not persistent across game restarts; they're for
verification only.

---

## 12. Package as a mempatch plugin

You now have:

- A confirmed 3-byte patch at a known RVA
- An AOB pattern around it (16+ bytes — verify in Ghidra that it's
  unique by searching the binary for that byte sequence and getting 1
  hit)
- The "before" bytes (`44 8A F0`) and the "after" bytes (`45 31 F6`)

Write `mempatch.toml` — schema reference in
[`config-schema.md`](config-schema.md). The outfit-swap example
declaration:

```toml
[[patch]]
name        = "outfit_swap_in_combat"
module      = "WHGame.dll"
pattern     = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
offset      = 13
original    = "44 8A F0"
replacement = "45 31 F6"
```

Substitute your own values: a `name` that's unique among all installed
plugins, the AOB pattern you verified is uniquely 1 match in the
binary, the `offset` from pattern start to the byte you're patching,
the original bytes at that offset, and your same-length replacement.

See
[`../examples/template-fully-commented/mempatch.toml`](../examples/template-fully-commented/mempatch.toml)
for an annotated schema reference covering every field, and
[`../examples/outfit-swap-in-combat/mempatch.toml`](../examples/outfit-swap-in-combat/mempatch.toml)
for the worked example's complete TOML including `context` and
`description`.

Before publishing, read
[`writing-safe-patches.md`](writing-safe-patches.md) for the safety-check
fields (`context`, `anchor_string`) you should add. They make your
patch resilient to future game updates and refuse to apply rather than
risk a wrong-site write.

The same guide also covers **conflict semantics** when your patch is
installed alongside other plugins: incidental byte overlaps are
silent and harmless, write-on-original aborts cleanly with a clear
log line, write-on-write follows priority order. If your patch
targets a popular function (e.g., something related to combat,
inventory, or movement), assume other plugins may eventually want to
touch nearby bytes and pick a pattern that doesn't overlap their
likely write sites.

---

## 13. Things that will trip you up

Common failure modes encountered during real KCD2 reverse-engineering
work; assume any of these can happen on your investigation too.

### "The string isn't xref'd"

Already covered in §5. Localization keys are int IDs at runtime, not
pointers. Don't anchor on localization keys; anchor on action-map XML
names or other code-referenced literals.

### "The address Ghidra shows isn't where the bytes are"

PE sections have file-offset vs RVA vs VA mappings. Ghidra shows VA
relative to preferred base (`0x180000000` for WHGame). At runtime the
DLL relocates due to ASLR. Convert with:

```
runtime_VA = runtime_base + (ghidra_VA - 0x180000000)
```

### "My breakpoint fires before I even trigger the action"

Memory breakpoints in x64dbg cover an entire page by default (`bpm`).
Use hardware breakpoints (`bph`) with a 1-byte width to break only on
your exact target byte. There are only 4 hardware breakpoint slots
across all threads — they're a scarce resource. Disable when not in
use.

### "My breakpoint never fires"

Probably the localization cache problem from §5. Or your breakpoint
target was wrong (you targeted the wrong DLL, the wrong section, or
your address math is off — see previous bullet).

### "The patch worked but the game crashed later"

You patched a value that has other consumers. The chain you traced
forward to the popup also branches elsewhere. Three possibilities:

1. You patched the IsInCombat function directly — many callers,
   patching it breaks them all. Patch the *result store* at the
   specific call site, not the function itself.
2. You patched a switch fall-through or a flag that gates more than
   the one action.
3. You picked a `je` or `jne` to flip and accidentally bypassed setup
   code. Conditional-jump patches are more dangerous than result-store
   patches; prefer the latter when possible.

### "Ghidra's saved analysis is large and I want to start over"

The project folder is ~1 GB. Just delete it and re-run headless. The
WHGame.dll itself doesn't need to be re-copied.

### "I can't tell if my Ghidra address is RVA or VA"

Ghidra always displays VAs computed against the PE's preferred
ImageBase (for WHGame, `0x180000000`). To get the RVA, subtract
`0x180000000`. To get the runtime VA, add the actual loaded base
(`mod.base(WHGame.dll)` in x64dbg).

---

## 14. Useful Ghidra search shortcuts

- **G** — Go to address
- **Ctrl+T** — Symbol table (mostly empty for KCD2 — RTTI is mostly
  classes, not functions)
- **Ctrl+B** — Find pattern (in current memory region)
- **Search → For Strings** — Find any string literal in any section
- **Right-click on function name → References → Show References to** —
  who calls this function

In disassembly view, the `XREF[N]:` annotation above any definition
shows incoming references. Bookmark this. It's how you trace from a
known anchor (a string) backward to the calling function (the
registration code) to its callers (the higher-level UI handler).

---

## 15. Where to anchor for different gameplay restrictions

The outfit-swap example was a clean case because the action has a
unique name (`next_outfit`) referenced from exactly one function. Not
every gate works out this neatly. The general heuristic by target
type:

| If the restriction is on... | Try anchoring on... |
|---|---|
| A specific player action (keypress) | The action's name string in `defaultProfile.xml`, then string-xref in Ghidra |
| Using a specific item type | The item GUID string from `vanilla/Tables_extracted/Libs/Tables/item/*.xml`, or `OnItemUse` / similar handler |
| An interaction (NPC, container, etc.) | The interaction name in entity/AI XML, often referenced by C string |
| A UI button | The button's flash event name (e.g. `OnOutfitClicked`), found in `vanilla/Scripts_extracted/` and `WHGame.dll` strings |
| A quest condition | The quest XML node + its associated script function names |

The pattern is always the same:

1. Find a human-readable string in the game's data files that names the
   restricted thing.
2. Confirm the string is referenced by code (look at xrefs in Ghidra).
3. Read the function that contains the reference. Find the conditional
   check that decides whether to allow the action.
4. Identify the byte sequence that stores/computes the deciding bool.
5. Pick a same-length replacement that forces the bool to the value you
   want.
6. Verify in x64dbg.
7. Package as a mempatch plugin with a unique AOB pattern.

---

## See also

- [`writing-safe-patches.md`](writing-safe-patches.md) — once you have a
  patch, this is how you make it safe enough to ship to players.
- [`config-schema.md`](config-schema.md) — full TOML schema for
  `mempatch.toml`.
- [`../examples/outfit-swap-in-combat/`](../examples/outfit-swap-in-combat/) —
  the reference plugin built from this guide's worked example.
- [muyuanjin/kcd2-mod-docs](https://github.com/muyuanjin/kcd2-mod-docs) —
  community reference repo with verified function offsets and the recipe
  for finding `gEnv` / `pScriptSystem` / etc.
- [yobson1/kcd2lua](https://github.com/yobson1/kcd2lua) — bootstrap ASI
  with verified `lua_pcall` / `update` signatures.
- [Warhorse modding wiki](https://warhorse.youtrack.cloud/articles/KM-A-1) —
  data-driven modding surface (pak, table patches, Lua bindings). Check
  here first to confirm your target needs a memory patch at all.
