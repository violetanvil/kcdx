# kcdx v0.1 — design gaps

> Companion to [`design.md`](design.md). That doc is the *spec*; this
> doc is the *what the spec doesn't yet acknowledge or solve*. Items
> live here until they're either resolved in `design.md` itself or
> explicitly deferred to v0.2+ with a written rationale.

The audience this doc serves: an experienced SKSE / F4SE plugin author
sitting down to write their first kcdx mod, and a new modder who has
never opened a disassembler. The current `design.md` covers the SKSE
veteran well; this doc captures the remaining authoring-experience
gaps once you try to actually ship something against the v0.1 surface.

A prior version of this doc included items that have since landed.
Removed: dynamic Lua hook surface (shipped Phase 5c.7b–7d), TOML
`[[hook]] lua_callback` (5f), `[[mid_hook]]` (5g), three-stream
logging + per-plugin levels ([`127586a`](https://github.com/violetanvil/kcdx)),
`kcdxScriptingInterface` (5e), `kcdxMemoryInterface` with `ScanPattern`
for C++ plugins ([`86dcc04`](https://github.com/violetanvil/kcdx)),
`kcdxMessage_LuaReady` + `kcdx.dev.on_ready` for pak-Lua ordering
([`3e7403e`](https://github.com/violetanvil/kcdx)). Items below are
the remaining gaps.

---

## 1. Single-callsite redirection has no first-class entry

> **CLOSED (as-built note, 2026-05-22) — superseded by the restructure.**
> Shipped as `kcdx.hook{ mode = "callsite" }` — a per-call-site E8
> rel32 redirect, isolation-proven (Phase 2b sub-6, commit `f04edbe`;
> matrix rows CAP-22-before/after/around/replace + the two isolation
> rows in `test-plugins/README.md`). The `[[call_redirect]]` TOML
> sibling proposed below is obsolete (TOML is manifest-only post-
> restructure); the capability lives on the `kcdx.hook` verb. Original
> gap text retained below as historical context.

SKSE / CommonLibSSE's most-used hooking primitive is
`write_call<5>(callsite_addr, &MyFn)` — patch a *specific call site*
so one caller gets your behavior, every other caller still hits the
original. kcdx's `[[hook]]` is callee-side: it detours the function
entry, so every caller sees the override. They are not interchangeable.

The current workaround is a manual `[[trampoline]]` plus a `[[patch]]`
that writes the rel32. Doable, but it's three TOML entries and
hand-written assembly for what CommonLibSSE does in one line. For C++
plugins, `kcdxMemoryInterface::WriteBytes` makes the manual path
slightly less painful, but the schema-level primitive is still
missing.

**Suggested resolution:** add a sibling schema, e.g.

```toml
[[call_redirect]]
name      = "..."
# locator picks the call site
pattern   = "..."
offset    = N
# target is either a published symbol or another plugin's export
target    = "myplugin.my_replacement"   # symbol-table lookup
# (or)
target_bytes = "..."                    # inline raw bytes (small stubs only)
```

Engine work is small: resolve the callsite, snapshot the displacement
to capture the original target (publish as `<name>.original` in the
symbol table for chaining), then patch the rel32. Conflict semantics
drop out of the existing matrix because it's a 5-byte write.

If this is genuinely v0.2, `design.md`'s **Deferred to later** section
should say so explicitly. Right now an SKSE veteran reading the spec
won't notice it's missing until they try to port a real plugin.

---

## 2. "Wrap with mutate" — call original, inspect return, mutate it — is partially shipped

> **CLOSED (as-built note, 2026-05-22) — superseded by the restructure.**
> The full wrap-with-mutate pattern is now first-class via
> `kcdx.hook{ mode = "around" }`: the callback receives a
> `call_original` callable, invokes the original, inspects its return,
> and overwrites it by returning the mutated value (mutate-by-return)
> (Phase 2b sub-4, commit `27ee126`; matrix row CAP-20-around in
> `test-plugins/README.md`). The `lua_post_callback`-can't-return
> half-shipped TOML form below is obsolete. Original gap text retained
> below as historical context. (The worked-example follow-up #7 below
> is likewise mooted by the `mode=around` surface.)

Phase 5f's `lua_post_callback` field lets a Lua callback observe the
original's return value after it runs ([`src/scripting.cpp:327-366`](../src/scripting.cpp#L327-L366)).
That's half of wrap. The missing half: the post-callback is invoked
with `lua_pcall(..., n_args, 0, 0)` — zero return slots — so it can
*read* the return value but it cannot *mutate* it.

In CommonLibSSE, the daily pattern is:

```cpp
result = OriginalFn(args);
if (some condition on result) result = mutate(result);
return result;
```

Today in kcdx you can implement the `if (some condition on result)
log_it(result)` shape. You cannot implement `result = mutate(result)`
short of either (a) using `pre_callback` with `call_original = "skip"`
and manually invoking the trampoline from Lua, which means re-routing
the entire call through Lua just to mutate a return value, or
(b) writing a C++ plugin.

**Suggested resolution:** wire `lua_post_callback` to allow a single
return value that, if present, overwrites the original's return before
the calling code sees it. Mechanically: change the `lua_pcall(..., 1
+ param_count, 0, 0)` on [`src/scripting.cpp:358`](../src/scripting.cpp#L358)
to allow one result, pop it if present, marshal back into
`return_value` via `lua_memory::from_lua_return` (mirror of the
existing `to_lua_return`). The signature surface in TOML is already
established by `[[mid_hook]]` which has the same "return-table-or-nil"
shape; reuse the pattern.

Until that lands, the deferred-to-later entry in `design.md` should
include a worked example showing how authors structure code today for
the mutate-return pattern (typically: skip original + manually invoke
trampoline + decide what to return). That's the example item below
(#8).

---

## 3. Address-Library coverage is the actual UX, not the API

`design.md` documents the Address Library schema and
`kcdxInterface::ResolveAddress` precisely. What's missing on disk:
the `address-library/` directory doesn't exist yet. Phase 7 hasn't
shipped. The deeper concern beyond just landing it: **the database is
only as useful as its seeded content.** SKSE's Address Library
succeeds because the community has been curating it for years. kcdx
ships v0.1 with a brand-new CSV — if it has 5 rows the day Phase 7
ships, "use Address Library IDs" isn't a real option for any plugin
author.

**Suggested resolution:** before Phase 7 ships,

1. Define an explicit *initial seed list* — at minimum every site any
   in-tree example mod (mempatch's `outfit-swap-in-combat`, every
   conflict-test plugin, every `kcdx/examples/` plugin, every
   test-suite matrix entry) should appear in the database with a
   stable ID. That's free coverage from work already done.
2. Document the contribution flow in `design.md` itself, not just
   "submit a PR." Where does the ID number come from (next-unused-
   integer? hash of the name? caller picks?), how is the `unverified`
   → `ok` transition gated, what's the cadence for refreshing per
   game update.
3. Decide and document whether kcdx will accept community-supplied
   IDs *without* a Ghidra-verified RVA — i.e. is `unverified` a place
   for "I think it's here, please confirm" entries, or strictly "I
   have an RVA, awaiting a second pair of eyes."

Without this, Phase 7 ships an *interface* but not a *feature*.

---

## 4. Semantic locators in TOML — close the loop from Lua surface to declarative path

The Lua surface ships the pieces: `kcdx.lua.cfunction_address` (Phase
5c.7d) resolves a Lua-bound C function by name; `kcdx.memory.dynamic_hook`
(Phase 5c.7b) installs a hook on the resolved address. A pak-Lua
author who knows the name `System.LogAlways` can hook it without
ever touching Ghidra. That's the win.

What's missing: **the TOML declarative path can't do this.** TOML
hooks today need `pattern` / `address_id` / `target_symbol` — none of
which let a non-Ghidra author identify a target by name. The engine
already has the runtime mechanism wired up; the TOML schema just
doesn't expose it.

```toml
# Today (pak Lua, after kMessage_LuaReady fires):
local addr = kcdx.lua.cfunction_address(System.LogAlways)
kcdx.memory.dynamic_hook({ name = "log_intercept", target = addr, ... })

# What's missing (declarative TOML):
[[hook]]
name                = "log_intercept"
target_lua_cfunction = "System.LogAlways"   # new locator type
lua_callback        = "MyMod.OnLog"
```

The runtime resolution would happen at `kMessage_LuaReady` time (the
locator can't resolve before the Lua VM is up and the C-function name
is bound). The engine work is a few hundred lines: a new locator
variant, a deferred-resolution path in `hook_engine` that queues the
hook install until `LuaReady` fires.

**Suggested resolution:** ship `target_lua_cfunction` as a first-
class TOML locator in v0.1. It is the single-biggest discoverability
improvement available, and the mechanism it depends on already works
in the imperative Lua path. Document it in `design.md`'s
`[[hook]]` / `[[mid_hook]]` sections as a peer of `pattern` /
`address_id`.

Three other semantic-locator candidates worth at least mentioning in
`design.md` even if deferred:

- `target_action_map = "outfit_swap"` — resolved via the engine's
  action map registry.
- `target_console_command = "g_outfit_swap"` — resolved via the
  CVar/command table.
- Flash event names once Scaleform hooks land in v0.2.

---

## 5. Lua callbacks fire on the hooked function's thread, with no runtime guard

Documented as a hard rule in `CLAUDE.md` (#16) but **absent from
`design.md` outside the scripting-interface sub-section.** Authors
reading the spec to figure out whether their plugin will work will
not learn that hooking the audio mixer or a physics worker silently
races the Lua VM and likely crashes.

The Phase 5d commit ([`ad98ea8`](https://github.com/violetanvil/kcdx))
explicitly chose to document rather than enforce. That's a reasonable
v0.1 call, but the documentation needs to be in the right place.
Today it's only in `design.md`'s `kcdxScriptingInterface` section
— most authors will hit it via `[[hook]] lua_callback` in TOML,
where the constraint isn't called out at all.

**Suggested resolution:** add a short subsection to `design.md`
under the `[[hook]]` / `[[mid_hook]]` schema docs (or as a peer
top-level section) listing:

- The constraint (Lua callbacks run on the hooked function's thread;
  KCD2's Lua VM is single-threaded with `lua_lock`/`lua_unlock`
  compiled out).
- The "safe targets" list (game's `update` tick descendants, anything
  kcdx already hooks, Lua-bound C functions resolvable via
  `kcdx.lua.cfunction_address`).
- The "unsafe targets" list (audio, physics, IO workers).
- A pointer to `kcdxTaskInterface::AddTask` as the escape hatch for
  "I hit this from a worker thread and need to get back to main."
- The v0.2 plan: runtime `GetCurrentThreadId()` guard in the
  dispatchers with a logged skip.

Cross-reference both the TOML-schema and C++-interface sections to
this subsection so a reader landing in either spot sees it.

---

## 6. No diagnostic-only "scan" entry for declarative authors

`kcdx.memory.scan_pattern` (pak Lua) and `kcdxMemoryInterface::ScanPattern`
(C++ plugins) let an *author with a script or a DLL* find where an
AOB resolves. A pure-TOML author has no equivalent — no way to ask
the engine "did this pattern resolve, and to what?" without committing
a write or installing a hook.

A new modder writing their first AOB ends up reading kcdx.log after
a failed apply, or writing a no-op `[[patch]]` with
`replacement = original` just to see the log line. Neither is
discoverable.

**Suggested resolution:** add a `[[scan]]` entry type:

```toml
[[scan]]
name    = "find_outfit_swap"
pattern = "48 81 C1 60 0B 00 00 ..."
context = "..."
# logs to the plugin's <plugin>.log:
#   [scan 'find_outfit_swap'] resolved 1 match at WHGame.dll+0x12345
#   [scan 'find_outfit_swap']   surrounding disasm (N before/after):
#   [scan 'find_outfit_swap']     0x...  mov  rcx, [rax+0x90]
#   [scan 'find_outfit_swap']     ...
```

Zero behavioral effect. Pure diagnostic. Helps a new author learn
whether their pattern is unique, where it landed, and what instructions
surround it — without risking a botched write. Engine work is trivial:
the resolver and module-scan paths already exist; just call them and
log.

This is the highest-leverage UX add for new modders who haven't
graduated to writing Lua glue yet. It should be in v0.1.

---

## 7. No worked example for the "wrap with mutate" pattern, even acknowledging it's partial

`design.md`'s examples section shows: a byte patch, a `[[mid_hook]]`
with Lua, a cross-plugin trampoline, a lifecycle subscription, a
serialization pattern. All useful. Missing: a worked example of the
most common SKSE pattern — "hook a function, decorate its output,
return modified value."

Even if mutate-return stays deferred (#2), an example showing **how
to do it today** would save every porting author the same hour of
figuring it out. The shape is roughly:

```cpp
// Pattern for "wrap with mutate" in kcdx v0.1, until lua_post_callback
// can return a value: skip original, invoke trampoline manually.
extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    // 1. Install hook with call_original = skip (or use the C++ trampoline path directly)
    // 2. Capture the trampoline (from kcdxTrampolineInterface) so you can call original yourself
    // 3. In your detour: call the trampoline, inspect return, optionally mutate, return.
}
```

A 20-line example removes the ambiguity for everyone porting an SKSE
plugin. Goes away once #2 ships.

---

## 8. The cross-plugin "API" pattern is mentioned-and-dismissed without replacement guidance

`design.md` deferred item: "Cross-plugin function-call API formalization
(the SKSE `enb-api` / `TrueDirectionalMovementAPI` pattern). Available
implicitly via `GetModuleHandle` + `GetProcAddress` against the
plugin's DLL; v0.1 doesn't add a higher-level wrapper."

This is correct but unhelpful. The SKSE-ecosystem convention for these
APIs has tradeoffs (versioning, ABI stability, init ordering vs
`kMessage_PostPostLoad`) that a new author will get wrong. One
paragraph showing the canonical shape would prevent every plugin
author from inventing their own.

The `kcdxMessage_LuaReady` work ([`3e7403e`](https://github.com/violetanvil/kcdx))
already established a pattern of giving plugins a defined "the runtime
is ready for X" message. Cross-plugin API discovery has the same
flavor — `kMessage_PostPostLoad` is the moment peer plugin APIs
are safe to fetch — and the doc should say so.

**Suggested resolution:** a short subsection in `design.md` showing:

- Use `kMessage_PostPostLoad` (not `PostLoad`) to fetch peer plugin
  APIs.
- Export a versioned struct from your DLL with the same
  `kcdxPluginVersionData`-style approach (data block + getter
  function).
- The naming convention (`<plugin>.API.<version>` symbol or similar).
- When to use the cross-plugin symbol table (already implemented;
  good for "the other plugin exports an address") vs.
  DLL-to-DLL function calls (good for "the other plugin exports
  behavior").

Doesn't add engine code. Adds knowledge.

---

## 9. No story for "my AOB broke when KCD2 patched"

`design.md` and `CLAUDE.md` both warn that AOBs may break per game
update. Neither describes what the plugin author or player *does*
about it. The current implicit answer is "the engine logs a clear
error, your patch doesn't apply, you ship a new release with a new
pattern." That's accurate but it puts every plugin author on the hook
for every game update.

This intersects with #3 (Address Library coverage) — the whole point
of Address Library is to make IDs stable across updates while RVAs
shift. But the doc doesn't connect those dots. A new plugin author
reading `design.md` won't realize that **using `address_id` instead
of `pattern` is the survive-game-updates story**, because the section
on locators presents the three (pattern / address_id / target_symbol)
as equivalents.

**Suggested resolution:** in `design.md`'s `[[hook]]` section (and
the patch / mid_hook sections), add a one-line guidance: "Prefer
`address_id` when the site is in the Address Library — it survives
game patches that shift RVAs. Fall back to `pattern` only when no ID
exists; consider submitting a PR to add one. The same applies to
`target_lua_cfunction` once that ships (#4) — name-keyed locators
survive game patches that don't rename the function." Same line,
three places. Connects the dots.

---

## Priority ordering (suggested)

If only some of these land before v0.1.0:

**Must address before v0.1.0** (visible to first-day users, low engine cost):

- #5 (thread safety in design.md) — silent footgun, pure docs
- #6 (`[[scan]]` diagnostic) — biggest new-modder UX leverage, trivial engine work
- #9 (address_id-as-update-survivor guidance) — pure docs
- #11 (`kcdxLuaApi::Call/Pcall/LoadString`) — interface advertises
  bidirectional C++↔Lua but currently only Lua→C works. ~30 LOC
  total. Blocking clean CAP-04 ship.

**Should address before v0.1.0** (defines the audience):

- #3 (Address Library seed list + contribution flow)
- #4 (`target_lua_cfunction` TOML locator) — biggest discoverability win since the runtime mechanism is already shipped
- #2 (let `lua_post_callback` return a mutated value) — small mechanical change to an existing dispatcher

**Can defer to v0.2 with explicit note in design.md**:

- #1 (`[[call_redirect]]` schema) — closes the biggest CommonLibSSE
  feature gap, but workarounds exist via `kcdxMemoryInterface` for
  C++ authors
- #7 (wrap-with-mutate worked example) — disappears if #2 ships
- #8 (cross-plugin API conventions) — once one or two real plugins
  ship and a pattern settles

---

## 10. CryEngine `IGameFrameworkListener` second-source-of-truth for save/load

Phase 6 shipped 2026-05-19 with the function-entry detour path:
five hooks on `wh::framework::C_SaveGameManager` and the adjacent
slot resolver, surfacing five lifecycle messages
(`kcdxMessage_SaveGame` / `kPreLoadGame` / `kPostLoadGame` /
`kDeleteGame` / `kLoadGameSelected`). See
[`_research/phase6-save-load/SAVE-LOAD-CANDIDATES.md`](../_research/phase6-save-load/SAVE-LOAD-CANDIDATES.md)
and [`_research/phase6b-recon/SAVE-SELECTION-HOOK.md`](../_research/phase6b-recon/SAVE-SELECTION-HOOK.md)
for the full recon trail.

CryEngine offers an alternate path via the `IGameFrameworkListener`
interface (proved working by muyuanjin/kcd2db at
[`_research/predecessor-sigs/muyuanjin-kcd2db/src/db/LuaDB.cpp:280`](../_research/predecessor-sigs/muyuanjin-kcd2db/src/db/LuaDB.cpp))
which exposes `OnSaveGame(ISaveGame*)` / `OnLoadGame(ILoadGame*)`
as virtual callbacks via
`gEnv->pGame->GetIGameFramework()->RegisterListener(this, "kcdx", FRAMEWORKLISTENERPRIORITY_DEFAULT)`.
Not adopted for v0.1 for three concrete reasons:

1. **Coverage gap.** The interface fires `OnSaveGame` and `OnLoadGame`
   only — there is no `OnDeleteGame` and no `OnPreLoadGame`. Two of
   the five lifecycle messages have no listener-side equivalent.
2. **Infrastructure cost.** Adopting the listener path requires
   ~400 LOC of new glue inside kcdx: a `gEnv` resolver (muyuanjin's
   `"exec autoexec.cfg"` string-anchor + `MOV rXX, [rip+disp]`
   reverse-walk), a vtable hook on `IGame::CompleteInit` (so the
   `RegisterListener` call happens after the framework is ready), a
   C++ class deriving from `IGameFrameworkListener` with three virtual
   slots (`OnSaveGame` / `OnLoadGame` / `OnActionEvent`), and the
   CryEngine type headers (available at
   [`_research/predecessor-sigs/muyuanjin-kcd2db/external/cryengine/`](../_research/predecessor-sigs/muyuanjin-kcd2db/external/cryengine/)
   but not yet pulled into the kcdx build tree).
3. **Schema gap.** kcdx's `[[hook]]` TOML block addresses function
   entries by AOB. There's no equivalent declarative way for plugin
   authors to subscribe to a framework listener event from
   `kcdx.toml`. Adopting the listener as the dispatch mechanism for
   `kcdxMessage_*` would require a new internal subsystem (the
   listener instance lives in the engine and fans out to plugin
   messaging) — that's fine, but it's a real chunk of work.

The detour-only path was the deliberate v0.1 pick on 2026-05-19.
All open questions from that decision have since been resolved by
live testing:

- **Fire ordering** — function-entry hooks correctly bracket
  deserialization. `kPreLoadGame` fires before the world hydrates;
  `kPostLoadGame` fires after.
- **Filename payload** — `kSaveGame` and `kLoadGameSelected` carry
  a basename in `data` (e.g. `"save561.whs"`). `kPreLoadGame` /
  `kPostLoadGame` data is null; use `kLoadGameSelected` if you need
  the filename for a load.
- **Multi-fire** — `kSaveGame` fires once per user save action;
  `kPreLoadGame` may fire twice per user load (engine cold-load
  pattern); `kLoadGameSelected` is engine-deduped to fire once per
  user-visible load.
- **Main-thread assertion** — confirmed across all five hooks on
  every observed fire.

**When to revisit:**

- **Cross-check trigger.** If Round 2 in-game testing reveals that
  one of the four detours has surprising fire semantics — e.g.,
  `LoadGame` returns *before* deserialization, so the function-entry
  hook isn't actually a "pre-load" moment, or the orchestrator at
  `0x180FBEE78` is the right hook frame instead — then standing up
  the listener probe as a second source of truth becomes a fast
  diagnostic step.
- **v0.2 schema work.** If `[[event]]` (the design.md §`[[event]]`
  schema, currently described as Lua-callback-only) gets generalized
  to subscribe to engine messages, the listener path is the natural
  internal-fan-out mechanism for messages that have a CryEngine
  equivalent. At that point the gEnv resolver + `CompleteInit` vtable
  hook stop being one-off save/load probe code and start being
  shared infrastructure.

**Suggested resolution path (v0.2):** promote the gEnv resolver to a
first-class kcdx subsystem (`src/gEnv.{h,cpp}`), with the
`CompleteInit` vtable hook installed by the engine itself at boot.
Build the listener subclass as one of the consumers. Other future
consumers: `gEnv->pConsole` for kcdx's `[[command]]` registration
(currently planned to use a different mechanism), `gEnv->pScriptSystem`
for an alternate Lua-state-capture path (belt-and-suspenders for the
existing `lua_pcall` hook capture).

Not a v0.1 blocker. Detours alone deliver the full lifecycle catalog.

---

## 11. `kcdxLuaApi` is missing `Call` / `Pcall` / `LoadString` — plugins can't actually invoke Lua functions

**Discovered 2026-05-20 during Phase 5g closeout (CAP-04 mid-hook test plugin authoring).**

`kcdxScriptingInterface::lua` (a.k.a. `kcdxLuaApi`) exposes ~30
function pointers covering stack manipulation, type queries, value
push/pull, table read/write, globals, and error reporting. What it
DOESN'T expose:

- `lua_call` (or its safe variant `lua_pcall`)
- `lua_loadstring` / `luaL_loadbuffer` / `luaL_loadstring`
- `lua_dostring` / `luaL_dostring`

The result: **a C++ plugin can't invoke Lua functions through the
public API.** It can build a function on the stack (PushValue,
GetGlobal, GetField) and push args (PushString, PushInteger, etc.),
but it can't fire the call. The closest workaround is registering a
Lua C function that mutates state — but even that can't call OTHER
Lua functions, so cascading callbacks are blocked.

### How this surfaces in practice

The CAP-04 mid-hook self-test wants to:

1. Register a Lua callback `kcdx.Cap04Test.OnB(args)` that mutates
   the captured `rax` register via `args[1]:set(555)`.
2. From a C++ test plugin, invoke the hooked target function and
   verify the return.

Step 1's `args[1]:set(...)` is a method call — needs `lua_pcall` to
invoke. From a C plugin's RegisterFunction callback, you can:

```cpp
int OnB(lua_State* L, void* ud) {
    g_lua->RawGetI(L, 1, 1);          // args[1] = value_wrapper userdata
    g_lua->GetField(L, -1, "set");     // its set method
    g_lua->PushValue(L, -2);           // self arg
    g_lua->PushInteger(L, 555);        // newValue
    // *** No g_lua->Call() or g_lua->Pcall() to invoke ***
    return 0;
}
```

The closest fallback is to write the mutation in pak Lua (which has
unrestricted access to the Lua VM via the in-VM globals), but that
requires authors to ship a pak alongside their DLL — defeating the
"pure C++ plugin" model the kcdxScriptingInterface advertises.

### Suggested resolution

Add three methods to `kcdxLuaApi`:

```cpp
// Standard Lua 5.1 call. nresults can be LUA_MULTRET (-1).
int (*Call)(lua_State* L, int nargs, int nresults);    // lua_call (no pcall)
int (*Pcall)(lua_State* L, int nargs, int nresults, int errfunc);  // lua_pcall

// Load Lua source (or precompiled bytecode) as a chunk; pushes the
// resulting function onto the stack. Returns 0 on success.
int (*LoadString)(lua_State* L, const char* chunk);    // luaL_loadstring
```

Implementation is trivial (each is one function-pointer thunk wrapping
the bundled Lua 5.1 API; same pattern as the 30 existing thunks).
Risk surface: `Call` can crash on bad args; `Pcall` catches that. Both
are mentioned explicitly in Lua 5.1's reference manual as the standard
invocation primitives, so authors will know what they do.

### Why "must address before v0.1.0"

- **The kcdxScriptingInterface advertises bidirectional C++↔Lua
  interop**, but without Call/Pcall it's one-directional (Lua can
  call C, C can't call back into Lua). That's a documentation hole
  vs. promised semantics.
- **CAP-04's mid-hook mutation test cannot ship cleanly without it**,
  blocking [[mid_hook]] full-verification.
- Risk to ship-readiness: a plugin author tries to use the SDK as a
  full-featured C++ plugin host, hits the wall, files a "this is
  unfinished" issue on day 1.

### Workaround in the interim

CAP-04 ships as **C++ DLL + pak Lua sidecar** rather than pure C++ DLL.
The pak Lua side defines `Cap04Test.OnB` etc. as in-VM Lua functions
that can use `:set()` directly. The C++ DLL handles invocation +
verification. This matches the cap-05 paklua-sidecar pattern.

When this gap is closed (Call/Pcall/LoadString land in
kcdxLuaApi), the pak Lua sidecar collapses into the C++ DLL.

---

## 12. `[[hook]]` schema grew organically; verbs + body shapes need rationalization

> **ADDRESSED (as-built note, 2026-05-22) — the restructure resolved the
> SPIRIT of this gap differently than the shape proposed below.** The
> organic `[[hook]]` schema is gone: TOML is manifest-only, and intent
> is declared via the single `kcdx.hook{ mode = ... }` verb (before /
> after / around / replace / mid / callsite) with one locator family
> (`target = "<name>"` carrying address + verified ABI, advanced raw
> locators behind the disassembler-test escape hatch). The specific
> `[[intercept]]`/`[[replace]]`/`[[constant]]` TOML verbs + reusable
> `[function.Name]` blocks proposed below were NOT built as written.
> The declarative-arg-rewriter idea (`rewrite_arg.szApp = {...}`) is
> NOT shipped — it is the one part of this gap that remains open as a
> possible future ergonomic. Original gap text retained below.

**Discovered 2026-05-21 during BugSplat PROBE R/S/T investigation
(see `docs/known-issues/BugSplat dmp files don't reach disk for AV
crashes.md`).**

The current `[[hook]]` schema accumulated three locator types
(`pattern` / `target_symbol` / `address_id` — soon to include
`export` per gap #15), two body shapes (`bytes` for raw asm vs
`lua_callback` for typed Lua), and no first-class verb distinguishing
"replace function entirely" vs "intercept and call original." Plugin
authors have to read several pages of docs to decide which combination
they want, and the two body shapes barely have anything in common.

A reasonable cleanup direction surfaced during the investigation
discussion (transcript, 2026-05-21):

- **Verb-first entry types.** Replace the catch-all `[[hook]]` with
  verbs reflecting intent: `[[intercept]]` (before/after/around), 
  `[[replace]]` (full function-entry override), `[[constant]]` (byte
  rewrite, mempatch-style — would replace `[[patch]]`). The verb
  determines body semantics; authors don't pick a "kind" and then
  separately figure out what fields apply.
- **One locator family.** `function = "Module.dll!ExportName"` as
  the default form, with `pattern`/`address_id`/`offset` as fallback
  options for the cases an export name doesn't cover. SKSE-style
  Address Library IDs stay as a separate `address_id` field; AOB
  patterns stay as a separate `pattern` field; but the *common* case
  is one string.
- **Function signatures as first-class data.** Today `return_type`
  and `param_types` are inline on every hook entry. Pull them into
  a reusable `[function.Name]` declaration that hooks reference by
  name. Decouples "what is this function" from "what do I want to
  do to it."
- **Declarative arg rewriters.** For the BugSplat-class case — "if
  arg N's string matches X, substitute Y, then call original" — a
  pure-declarative shape (`rewrite_arg.szApp = {find=":", with=" -"}`)
  removes the need for a C++ or Lua callback. Narrow, but extremely
  natural for engine-fix authors.

**Why this is a v0.1 gap, not a v0.2 nice-to-have:** the surface is
about to grow further (export locator from gap #15, before-game-zone
hooks from PROBE T's findings, the BugSplat fix itself). Rationalizing
now costs less than rationalizing after three more plugins are
written against the current shape.

**Migration strategy when this lands:** the existing 21+ test plugins
all use the current `[[hook]]` schema. Options:
1. Hard break — bump engine version, ship migration script.
2. Parallel schemas with deprecation — both `[[hook]]` (legacy) and
   `[[intercept]]` (new) work; `[[hook]]` logs a deprecation WARN.
3. Pure additive — keep `[[hook]]` as-is, ship verb-first as new
   entries without deprecation. Authors gradually migrate.

Option 2 is the SKSE-shaped answer (long deprecation windows).

---

## 13. Function-locator by exported name is missing

**Discovered 2026-05-21 during BugSplat PROBE R/T (cross-module
hooking).**

Today's locator surface is `pattern` / `target_symbol` /
`address_id` — none of which let an author say "the function
exported by this DLL with this name." This matters specifically for:

- **Cross-module hooking** of game-shipped DLLs that aren't WHGame
  (BugSplat64.dll, dinput8.dll, Steam APIs the game links). These
  are guaranteed-stable APIs with well-known mangled names; AOB
  patterns are overkill, Address Library doesn't cover them, and
  symbol-table lookup is for plugin-published symbols only.
- **Loader-safe before_game timing.** Export resolution is just
  `GetModuleHandleW` + `GetProcAddress`, which is safe under the
  loader lock. AOB scanning is not. This means export-locator hooks
  could qualify for the before_game zone (today restricted to
  `[[patch]]` only per `load_order::DeriveMinZone`), unlocking
  DllMain-time interception of DLL APIs.

**Suggested resolution:** new locator field, mutually exclusive
with the existing three:

```toml
[[hook]]
module = "BugSplat64.dll"
export = "??0MiniDmpSender@@QEAA@PEB_W000K@Z"   # C++ mangled name
# ... rest unchanged
```

Resolves via `GetProcAddress(GetModuleHandleW(module), export)`.
Capability gating in `load_order` extended so export-locator
`[[hook]]` entries are allowed in before_game zone (no AOB scan, no
text-section read, loader-safe).

`ldr_notify::ApplyEntriesForModule` extended to apply
before_game-zoned export-locator hooks at module-mapped time, mirror
of today's patch handling.

PROBE T proved the timing works (BugSplat64.dll already mapped at
kcdx.asi DllMain → immediate install via direct call; alternatively
LDR notification catches the load). The plumbing is half-built; this
gap is mostly schema + capability-matrix work.

**Why this matters beyond BugSplat:** any future kcdx engine-fix
targeting a non-WHGame DLL (Denuvo overlays, third-party crash
reporters, anti-cheat shims if Warhorse ever adds one, dinput8 input
hooks) needs this primitive. Without it, every such fix becomes a
hand-rolled `LoadLibrary` + `GetProcAddress` + manual MinHook setup
in a C++ engine builtin.

---

## 14. No "intercept, mutate args, call original" first-class shape

> **CLOSED (as-built note, 2026-05-22) — superseded by the restructure.**
> `kcdx.hook{ mode = "before" }` (and `around`) gives the callback the
> args and lets it mutate an argument by returning the new value, then
> the original runs with the mutated arg — the exact "before-advice
> with args" / BugSplat `szApp` pattern (Phase 2b sub-4, commit
> `27ee126`; matrix rows CAP-20-before "mutates arg via return" and
> CAP-20-wstr "wstr arg read + mutate" in `test-plugins/README.md`).
> The proposed `before`-callback C typedef + `[[intercept]]` TOML verb
> below are obsolete (TOML is manifest-only; the capability is on the
> `kcdx.hook` verb). Original gap text retained below for context.

**Discovered 2026-05-21 alongside gap #12.**

The two body shapes today are:

- `bytes = "..."` — raw asm. Full replacement of function prologue.
  Author writes their own return-to-original via trampoline or
  abandons the original entirely. No declarative way to wrap.
- `lua_callback = "..."` — typed marshaling via the
  `runtime_func_t` JIT machinery. Lua callback fires before the
  original; return value handling is partially shipped (gap #2);
  arg mutation is doable in Lua but requires the Lua VM to be
  populated.

What's missing: **"intercept this function, mutate one of its
arguments, then call the original."** This is the most common
detour pattern in SKSE / Frida / AOP frameworks generally
("before-advice with args"). It's also what the BugSplat fix needs:
read `szApp`, if it contains `:`, substitute a colon-free copy,
call original.

Today's options for this pattern:
1. Write raw asm in `bytes` that mutates the register holding the
   arg, then jumps to the original via a trampoline. C++ expertise +
   asm knowledge required.
2. Use `lua_callback` — but then the Lua VM must be alive at the
   detour-fire time. For BugSplat, the constructor fires during
   WHGame.dll's startup init, well before the Lua VM is populated.
   So this option is unavailable for the very case that motivated
   the discovery.
3. Ship the substitution as a C++ engine builtin (kcdx-internal),
   hardcoded. Works but skips TOML entirely, and isn't available
   to plugin authors.

**Suggested resolution:** add a `before` field to `[[hook]]` (or to
the new `[[intercept]]` verb from gap #12) that names a kcdx-internal
or plugin-DLL-exported C function. Signature:

```cpp
typedef bool (*kcdxBeforeCallback)(uintptr_t* args, size_t nargs);
```

Returns true to call original, false to skip. Args mutable in place.
Pure C signature, no Lua VM required. Engine handler name resolves
via a small registry (kcdx-internal handlers) plus per-plugin export
lookup (DLL-side handlers).

Combined with gap #13's `export` locator and gap #12's verb-first
schema, the BugSplat fix becomes (one entry):

```toml
[[intercept]]
function = "BugSplat64.dll!??0MiniDmpSender@@QEAA@PEB_W000K@Z"
signature = "void(ptr, wstr, wstr, wstr, wstr, u32)"
before = "kcdx.builtin.bugsplat_filename_fix"
```

Where `kcdx.builtin.bugsplat_filename_fix` is a 4-line C function
in the engine that swaps the colon for a dash if present.

---

## 15. Game-event API beyond lifecycle messages

**Discovered 2026-05-21 during SKSE-parity audit (total-conversion
ambition).**

Today's `kcdxMessage_*` catalog covers the kcdx-engine lifecycle:
PostLoad, PostPostLoad, InputLoaded, NewGame, PreLoadGame /
PostLoadGame / SaveGame / DeleteGame, LuaReady, LoadGameSelected.
Ten messages. All originate from kcdx itself, fired at well-known
moments in the engine's own lifecycle.

What's missing: **gameplay events.** SKSE / Papyrus has tens of
in-game-state events plugins subscribe to:

- OnEffectStart / OnEffectFinish (magic effect lifecycle)
- OnHit (damage received/dealt)
- OnObjectEquipped / OnObjectUnequipped
- OnContainerChanged (inventory)
- OnLocationChange (player moves between named locations)
- OnDying / OnDeath
- OnCellLoad / OnCellAttach
- OnActivate (player interacts with object)
- OnDialogueBegin / OnDialogueEnd

Each maps to a Papyrus virtual method or a specific hook the SKSE
plugin installs. The Skyrim mod community is so productive *because*
these events exist as a stable subscription surface — no plugin
re-implements "did the player just take damage" hook-and-state code.

kcdx today: plugin authors must install their own hooks for any
gameplay event they care about. Two plugins both wanting
"OnPlayerDamageTaken" install conflicting MinHook detours; the
first wins; the second silently doesn't fire (per first-wins rule).

**Why this is the biggest F:L-class blocker:** a total conversion
needs *dozens* of these events to coordinate quest / dialogue / AI
behavior. Without a shared event surface, every TC contributor
re-implements the same hooks, and the conflict matrix explodes.

**Suggested resolution path (v0.2+, incremental):**

1. Identify the 10-15 highest-value events from KCD2's existing
   gameplay surface (likely candidates: damage taken/dealt, save
   created, dialogue line spoken, item picked up, location entered,
   combat started/ended, perk unlocked, level-up, quest stage
   advanced, NPC interacted with).
2. For each, RE the underlying CryEngine call path, install a
   single kcdx-side hook, and surface it via a new
   `kcdxMessage_Game*` ID in the catalog. One hook per event, kcdx
   owns it, plugins subscribe via `kcdxMessagingInterface`.
3. Document the catalog + the contribution flow ("here's how to
   add a game event") so the community can extend it without
   waiting on us.

Incremental shipping: ship the catalog one event at a time. Each
event is a small PR.

**Why this is a v0.2+ item, not v0.1:** the engine plumbing already
exists (`kcdxMessagingInterface` was built for exactly this kind of
fan-out). What's missing is the RE work to identify the hook sites
and the catalog growth. Doesn't block v0.1 ship.

---

## 16. High-level Lua surface for gameplay

**Discovered 2026-05-21 during SKSE-parity audit (total-conversion
ambition); companion to gap #15.**

`kcdxScriptingInterface::lua` (a.k.a. `kcdxLuaApi`) exposes the
primitive Lua 5.1 C API as function pointers — ~30 calls covering
stack, types, values, tables, globals, errors. This is the *floor*
of what a plugin can do; it's the same shape any C extension to
Lua 5.1 sees.

What's missing: **the *ceiling*.** SKSE / CommonLibSSE plugins live
in C++ wrapper APIs like `RE::PlayerCharacter::GetSingleton()`,
`actor->GetActorValue(RE::ActorValue::kHealth)`, etc. These wrap
the underlying engine pointers and offsets in named, type-checked
helpers. Plugin authors write `player->ModActorValue(...)` instead
of `*(float*)(player_ptr + 0x320) += 10.0f`.

kcdx today has NONE of this for KCD2's gameplay surface. A plugin
wanting "give the player +1 health" writes:

```cpp
// Today (illustrative; KCD2 vtable layout TBD):
uintptr_t player = kcdx::ResolveAddress(ADDR_GLOBAL_PLAYER);
uintptr_t health_addr = *(uintptr_t*)(player + KCDX_OFFSET_HEALTH);
*(float*)health_addr += 1.0f;
```

A plugin wanting the same in pak Lua writes the same in Lua, via
`kcdx.memory.pointer`. **Every TC contributor re-implements the same
wrappers, with their own offsets and their own names.**

**Suggested resolution path (v0.2+, incremental):**

Build out a `kcdx.gameplay.*` (Lua) and `kcdxGameplayInterface`
(C++) surface covering common operations:

- `kcdx.player.health` / `:set(n)` / `:add(n)`
- `kcdx.player.position` (read-only Vec3; set via teleport API)
- `kcdx.player.inventory:add(item_id, count)` / `:remove(id)`
- `kcdx.player.gold` (and gold-like resource accessors)
- `kcdx.world.spawn(npc_id, position)`
- `kcdx.dialogue.replace(line_id, new_text)`
- `kcdx.quest.set_stage(quest_id, stage_n)`

Each function is just a thin wrapper over the existing primitives
(ResolveAddress + offset arithmetic + WriteBytes / read). The
value is in **naming them once, in one place, with one canonical
implementation.**

Incremental shipping: pick the 10 most-needed by RE'ing real KCD2
mod ideas (the "I want to" list from the modding community). One
helper per PR. Document each in a "Gameplay API reference" doc.

**Why this is v0.2+:** the underlying primitives exist. The work is
RE + naming + documentation, not engine architecture. Doesn't block
v0.1 ship; absolutely blocks F:L-class TCs.

---
