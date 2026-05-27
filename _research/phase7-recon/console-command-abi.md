# Console command ABI dossier — `IConsole::AddCommand` for kcdx `[[command]]`

Phase 7 reconnaissance, 2026-05-19. Game version: `release_1_5_1164953_841`.
Companion to `address-library-seed.csv` ids 2000–2003.

## TL;DR

- **Function name (CryEngine canonical):** `IConsole::AddCommand`. The
  design doc says `pConsole.RegisterCommand` but that's a colloquial
  naming. KCD2 ships CryEngine 5.2.3 which calls it `AddCommand`.
- **Calling convention:** `__thiscall` on x64-Windows — `rcx = IConsole*`,
  `rdx = sCommand`, `r8 = func`, `r9d = nFlags`, `[rsp+0x28] = sHelp`.
  (4-args-then-spill standard MS x64.)
- **Arity / overloads:** TWO overloads in CryEngine 5.2.3:
  1. `AddCommand(const char* sCommand, ConsoleCommandFunc func, int nFlags = 0, const char* sHelp = NULL)` ← **this one. The function-pointer form.**
  2. `AddCommand(const char* sName, const char* sScriptFunc, int nFlags = 0, const char* sHelp = NULL)` ← script-string form (e.g. `"Game.Connect(%1)"`). NOT what we want for kcdx.
- **Callback ABI:** `void __fastcall(IConsoleCmdArgs*)`. Single `rcx`
  argument, no `rdx/r8/r9`, no XMM. Returns void.
- **Argument access via `IConsoleCmdArgs` vtable:**
  - `slot 0: dtor`
  - `slot 1: int GetArgCount() const`
  - `slot 2: const char* GetArg(int nIndex) const`
  - `slot 3: const char* GetCommandLine() const`
- **Resolution path at runtime (RVAs not in seed CSV yet — see Open questions):**
  ```
  gEnv                  = addr-library id 1010
  pConsole_ptr          = addr-library id 1009   (gEnv + 0xA8)
  *pConsole_ptr         = IConsole*
  IConsole*->vtable[N]  = &AddCommand            (N likely 32, 33, or 34)
  ```
- **Confidence:** HIGH on the callback ABI and IConsoleCmdArgs layout
  (CryEngine 5.2.3 is open-source-leaked; muyuanjin's headers reproduce
  the canonical definitions and KCD2 ships these unmodified for
  IConsole). MEDIUM on the AddCommand vtable slot (canonical CryEngine
  slot is 32; muyuanjin documents +1 slot inserts on adjacent interfaces
  IScriptSystem and IScriptTable, so KCD2's may be 33). LOW on the
  AddCommand RVA — depends on the vtable being instantiated in a static
  IConsole implementation whose vtable lives at a known address (almost
  certainly true but uncomputed without Ghidra).

## Source — CryEngine 5.2.3 IConsole header

The full `struct IConsole` definition is in
`_research/predecessor-sigs/muyuanjin-kcd2db/external/cryengine/include/cryengine/IConsole.h:155-453`.
muyuanjin's CLAUDE.md notes the file is "extracted from
ValtoGameEngines/CryEngine, only declarations, no business changes."
Treat as canonical CryEngine 5.2.3 surface.

Relevant declarations:

```cpp
//! This a definition of the console command function that can be added to console with AddCommand.
typedef void (* ConsoleCommandFunc)(IConsoleCmdArgs*);

struct IConsoleCmdArgs {
    virtual ~IConsoleCmdArgs() {}
    virtual int          GetArgCount() const = 0;
    virtual const char*  GetArg(int nIndex) const = 0;
    virtual const char*  GetCommandLine() const = 0;
};

struct IConsole {
    // ... 32 virtual methods preceding AddCommand ...

    //! Register a new console command.
    virtual void AddCommand(const char* sCommand,
                            ConsoleCommandFunc func,
                            int nFlags = 0,
                            const char* sHelp = NULL) = 0;

    //! Register a new console command that execute script function.
    virtual void AddCommand(const char* sName,
                            const char* sScriptFunc,
                            int nFlags = 0,
                            const char* sHelp = NULL) = 0;

    //! Removes a console command which was previously registered with AddCommand.
    virtual void RemoveCommand(const char* sName) = 0;

    //! Execute a string in the console.
    virtual void ExecuteString(const char* command,
                               const bool bSilentMode = false,
                               const bool bDeferExecution = false) = 0;
    // ...
};
```

## Calling convention details

### `IConsole::AddCommand(ConsoleCommandFunc)` — caller side

x64 Windows `__thiscall` (which is the same calling convention as
`__fastcall` for member functions: the `this` pointer in rcx is just the
first arg). Pseudocode for invoking it:

```cpp
// Resolve IConsole* via the gEnv path:
SSystemGlobalEnvironment* gEnv =
    reinterpret_cast<SSystemGlobalEnvironment*>(
        kcdx::ResolveAddress(1010));    // gEnv base RVA
IConsole* console = gEnv->pConsole;     // ptr at gEnv + 0xA8

// Call AddCommand. The vtable slot is the open question; assuming
// slot 32 for now:
using AddCommandFn = void (__thiscall *)(IConsole*,
                                         const char*,
                                         ConsoleCommandFunc,
                                         int,
                                         const char*);
void** vtable = *reinterpret_cast<void***>(console);
AddCommandFn pAddCommand = reinterpret_cast<AddCommandFn>(vtable[32]);

pAddCommand(console,
            "kcdx_dump_state",          // sCommand
            &kcdx_dump_state_cb,        // func
            0,                          // nFlags (VF_NULL)
            "Dump kcdx state to JSON.");// sHelp
```

Argument registers per MS x64:
- `rcx` ← `console` (this)
- `rdx` ← `sCommand` (const char*)
- `r8`  ← `func` (function pointer)
- `r9d` ← `nFlags` (int, sign-extended)
- `[rsp+0x28]` ← `sHelp` (const char*) — the 5th arg goes to stack

Return value: `void` (no `rax` write expected on return).

### `ConsoleCommandFunc` — callee side (the registered callback)

x64 Windows `__fastcall`. When the player types the command in the
in-game `-console`:

```cpp
void __fastcall kcdx_dump_state_cb(IConsoleCmdArgs* args) {
    // args in rcx
    int argc = args->GetArgCount();   // vtable[1]
    for (int i = 0; i < argc; ++i) {
        const char* arg = args->GetArg(i);   // vtable[2]
        // arg[0] is the command name itself ("kcdx_dump_state")
        // arg[1..argc-1] are the player-typed arguments
    }
    const char* full = args->GetCommandLine();   // vtable[3]
    // Do work...
}
```

Single argument in `rcx`. No `rdx/r8/r9` used. Return is `void` — no
restriction on whether the callback writes to `rax` (caller-saved).

CryEngine does NOT pass argc/argv directly — it passes an interface
pointer that gives the callback typed access to its arguments. This
matches the design.md's `[[command]]` schema sketch closely.

## Sample `[[command]]` schema given this ABI

The design.md skeleton:

```toml
[[command]]
name        = "kcdx_dump_state"
description = "Dump kcdx-loaded plugin list and conflicts to a JSON file."
lua_callback = "MyMod.OnDumpCommand"
# Callback signature: function(args: string) -> nil
# args: the raw command tail (everything after the command name)
```

Given the actual ABI, a concrete `[[command]]` schema with full
flexibility looks like:

```toml
[[command]]
name         = "kcdx_dump_state"
description  = "Dump kcdx-loaded plugin list and conflicts to a JSON file."
# Lua callback name. Engine dispatcher calls
# IConsoleCmdArgs->GetArgCount() / GetArg(i) and constructs a string
# array, then calls the Lua callback with the array as a single
# argument. Same dispatch model as [[hook]] + lua_callback.
lua_callback = "MyMod.OnDumpCommand"

# Optional: VF_* flags for the console var system. Default 0 (VF_NULL).
# Common values:
#   0x00000002 VF_CHEAT
#   0x00080000 VF_RESTRICTEDMODE
#   0x00400000 VF_BLOCKFRAME
flags        = 0
```

The corresponding Lua callback:

```lua
function MyMod.OnDumpCommand(argv)
    -- argv is an array (1-indexed in Lua) of strings, including
    -- argv[1] = "kcdx_dump_state" (the command name itself).
    --
    -- Example: player types "kcdx_dump_state foo bar"
    --   argv[1] = "kcdx_dump_state"
    --   argv[2] = "foo"
    --   argv[3] = "bar"
    --   #argv   = 3

    kcdx.log_info("dump command received: " .. tostring(#argv) .. " args")
    -- ...
end
```

### Why Lua receives an array, not the raw `args: string` in design.md

The design.md sketch passed "the raw command tail (everything after the
command name)" as a single string. The ABI gives us a proper
arg-list interface. Passing it through as an array is:

1. **More idiomatic Lua.** Lua arrays are normal.
2. **Matches the engine's actual model.** No information loss.
3. **Backwards-compatible.** If we want a single-string fallback,
   `table.concat(argv, " ", 2)` does it client-side.

If the design.md sketch's contract matters, we ship a `command_tail`
helper field on the dispatched args; cheap.

### Why no `param_types`/`return_type` (unlike `[[hook]]`)

The callback ABI is fixed by CryEngine: it ALWAYS receives a single
`IConsoleCmdArgs*` and ALWAYS returns void. There's nothing for the
plugin author to vary. Removing `param_types`/`return_type` from
`[[command]]` (vs. `[[hook]]`) is correct.

## Open questions for engine implementer

These all live in the C++ dispatcher, not in TOML or the Address
Library:

### 1. Exact `AddCommand` vtable slot — 32, 33, or 34?

The canonical CryEngine 5.2.3 vtable slot for the
`(const char*, ConsoleCommandFunc, int, const char*)` overload is 32
(counting from 0, includes the destructor as slot 0).

muyuanjin documents that adjacent CryEngine interfaces in KCD2 have +1
slot inserts:

- `IScriptSystem::CreateTable` is at slot 13, canonical 12 ("there is
  an unknown virtual function inserted").
- `IScriptTable::SetValueAny` is at slot 7, canonical 6 (same reason).

This strongly suggests `IConsole` MAY also have inserted slots, putting
`AddCommand` at 33 or 34. **Confirmation requires Ghidra**:

1. Open `WHGame.dll` in the Ghidra project.
2. Find the IConsole vtable. The most reliable way is to start from
   `gEnv` at RVA 0x492b800 (id 1010 in the seed CSV), follow `+0xA8`
   to get the pConsole pointer (id 1009 at RVA 0x492b8a8), dereference
   to get the IConsole* address, then jump to that address and look at
   the first qword (the vtable pointer).
3. Walk the vtable looking for the function whose body calls
   `RegisterAutoCompleteImpl` or whose first prologue instructions
   match a `(const char*, void*, int, const char*)` signature. The
   string overload's body should be visibly different (it stores the
   script string rather than a function pointer).

**ETA:** ~½ day of focused Ghidra session by the engine implementer,
budgeted in design.md's "Open questions" section #1.

### 2. Where does the IConsole vtable live, and can we ship an RVA for AddCommand?

If the vtable is a static const in `.rdata` (the usual C++ ABI choice),
its address is known and stable at link time. Then we can ship a row
`(id=2000, rva=0x?????, name=iconsole-addcommand, status=verified)`
that resolves directly without a runtime vtable walk.

If the vtable is constructed dynamically (some CryEngine implementations
synthesize vtables from a struct of function pointers), the address is
only valid post-init and the seed CSV must mark the row `(rva blank,
status=unverified, resolution_kind=vtable_walk)`.

Almost certainly the former. Confirm in Ghidra.

### 3. Thread safety of the callback

CryEngine console commands are invoked synchronously on the thread that
calls `IConsole::ExecuteString` (typically the main thread, from the
in-game console input handler). kcdx hard rule #16 already says Lua
callbacks fire on whatever thread the host called us from; this is the
main thread for console commands. **Should be safe** but document the
guarantee explicitly in the `[[command]]` spec.

### 4. RemoveCommand on plugin unload — supported in v0.1?

v0.1 has no plugin-unload story (`kcdx_unload` is v0.2 per the design
doc's Out-of-scope section). Without unload, RemoveCommand is never
called from kcdx itself. Plugins that want to unregister at runtime can
look up the IConsole* and call RemoveCommand manually via the gEnv path.

Recommend documenting:
> "v0.1: commands registered via `[[command]]` persist for the process
> lifetime. There is no unload path. v0.2 may add RemoveCommand on
> plugin unload."

### 5. Flag interaction — VF_CHEAT in particular

The CryEngine `VF_CHEAT` flag (0x00000002) marks a command/cvar as
unavailable in release-builds-with-cheats-disabled. KCD2 is a release
build. Need to confirm: does `VF_CHEAT` make the command unavailable
from the player-facing `-console` window, or just from in-game
gameplay? If the former, kcdx plugins that want their command callable
without the `-console` flag MUST omit VF_CHEAT.

Default for `flags = 0` (VF_NULL) is safe.

### 6. UTF-8 encoding of command strings

CryEngine console strings are ASCII in the interface — `const char*`.
Player-typed arguments through the `-console` may include UTF-8.
Lua-side: kcdx's existing string marshaling pushes `const char*` as
Lua strings unconditionally (Lua strings are byte-buffers, no
encoding). Document that the callback receives raw bytes; if a plugin
wants UTF-8 validation it does so itself.

## Reproducibility — how to derive the RVAs at game-update time

When KCD2 ships a new build and Phase 7 ID 2000's `rva` needs
refreshing:

1. Run `find_genv.py` against the new `WHGame.dll`. Confirm
   `gEnv (id 1010)` and `pConsole_ptr (id 1009)` resolve.
2. **Live game required** (or static analysis with Ghidra) to walk the
   IConsole vtable: kcdx itself, post-engine-init, can read
   `*(*pConsole_ptr_VA)` to get the vtable address, then read 8 bytes
   from `vtable + 32*8` (or whatever the confirmed slot is) to get the
   AddCommand RVA. Optionally `kcdx-dev.log` can emit this every
   startup for verification.
3. Edit the CSV: add a row with the new `(id=2000,
   game_version=NEW, rva=0x?????, status=verified)` and submit a PR.

## Confidence summary

| Aspect | Confidence | Why |
|---|---|---|
| `IConsole` is the correct interface | HIGH | CryEngine 5.2.3 ships exactly this; muyuanjin uses it directly. |
| `ConsoleCommandFunc` signature is `void(IConsoleCmdArgs*)` | HIGH | Canonical, locked into CryEngine ABI since 5.0. |
| `IConsoleCmdArgs` vtable is `{dtor, GetArgCount, GetArg, GetCommandLine}` | HIGH | Same source. |
| KCD2 implements this interface unmodified | HIGH | muyuanjin's tested gEnv->pConsole path treats it as canonical IConsole. |
| `AddCommand` vtable slot is 32 | MEDIUM | Canonical CryEngine, but muyuanjin's +1 slot inserts on adjacent interfaces suggest possibly 33. |
| `AddCommand` lives in a static vtable (and thus has an RVA we can ship) | MEDIUM-HIGH | Standard C++ ABI for static interfaces; CryEngine implementations do this for all comparable interfaces. |
| `flags = 0` default works for in-`-console` player invocation | MEDIUM | VF_CHEAT exclusion has not been live-tested. |
| Callback fires on main thread | HIGH | ExecuteString comes from input handler; input handler is main-thread per CryEngine convention. |

## Blockers / what would advance this from "design dossier" to "engine code"

1. **Ghidra session: confirm the IConsole vtable slot 32 vs 33 vs 34.**
   ½-day per the design.md budget. Output: the verified slot number, and
   a confirmed RVA for the AddCommand function from a known-stable
   vtable. Updates seed CSV id 2000.

2. **Live-runtime confirmation: have the kcdx engine, post-init, dump
   the AddCommand RVA + slot index + IConsole vtable base to
   `kcdx-dev.log`.** A one-line patch in `hooks.cpp`'s `HookedUpdate`
   that runs once and logs `IConsole.AddCommand vtable[N] = 0x?????`.
   Then play KCD2 once; the log has the answer. **This is the cheapest
   path** — combined with #1 (which is the cross-check).

3. **Once the slot is confirmed, ship the dispatcher.** ~50 LOC in
   `command_engine.cpp` (new file): registration loop over the
   `[[command]]` entries, raw Lua C API marshaling for the argv array,
   dispatch via `lua_pcall`. Pattern matches `scripting.cpp`'s existing
   hook-dispatch.
