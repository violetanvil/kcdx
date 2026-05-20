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
