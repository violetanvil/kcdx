# kcdx v0.1 — design gaps

> Companion to [`design.md`](design.md). That doc is the *spec*; this
> doc is the *what the spec doesn't yet acknowledge*. Items live here
> until they're either resolved in `design.md` itself or explicitly
> deferred to v0.2+ with a written rationale.

The audience this doc serves: an experienced SKSE / F4SE plugin author
sitting down to write their first kcdx mod, and a new modder who has
never opened a disassembler. The current `design.md` covers the SKSE
veteran well; it has visible gaps for both audiences once you try to
actually ship something.

The items below are the gaps that affect real authoring, not nitpicks.
Some are missing features; some are missing UX; some are open
decisions that need to be pinned before v0.1 ships so plugin authors
don't get burned later.

---

## Already partly addressed (not gaps — just context)

Two items frequently called out as gaps actually have work in flight.
Capturing here so this doc doesn't argue against current direction.

- **Logging** is more built than `design.md` reflects. The engine-side
  surface (`kcdx::log::Info/Warn/Error/Debug` and printf variants) is
  live; per-plugin log streams (`<plugin-folder>/<folder>.log`) with a
  20 MB cap-and-drop policy are live; the lazy-open-on-first-write
  contract is live ([`src/log.h`](../src/log.h)). What's still TBD is
  the public function-pointer signature exposed on `kcdxInterface` for
  C++ plugins — `design.md` line 893–897 calls it out as TBD. Pinning
  it is a small follow-up, not a missing capability.
- **`lua_callback` on `[[hook]]` and `[[mid_hook]]`** is wired through
  the runtime_func_t / scripting path in `hook_engine.cpp`
  ([`src/hook_engine.cpp:97-145`](../src/hook_engine.cpp#L97-L145),
  [`src/hook_engine.cpp:267-316`](../src/hook_engine.cpp#L267-L316)).
  Phase 5f is where it gets validated end-to-end; the design
  table for `[[hook]]` already documents the field. Not a gap.

Everything below is something `design.md` does NOT call out, or calls
out only as "TBD" without a plan to resolve.

---

## 1. Single-callsite redirection has no first-class entry

SKSE / CommonLibSSE's most-used hooking primitive is `write_call<5>(callsite_addr, &MyFn)` — patch a *specific call site* so one caller gets your behavior, every other caller still hits the original. kcdx's `[[hook]]` is callee-side: it detours the function entry, so every caller sees the override. They are not interchangeable.

The current workaround per the spec is a manual `[[trampoline]]` plus a `[[patch]]` that writes the rel32. Doable, but it's three TOML entries and hand-written assembly for what CommonLibSSE does in one line.

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

Engine work is small: resolve the callsite, snapshot the displacement to capture the original target (publish as `<name>.original` in the symbol table for chaining), then patch the rel32. Conflict semantics drop out of the existing matrix because it's a 5-byte write.

If this is genuinely v0.2, `design.md`'s **Deferred to later** section should say so explicitly. Right now an SKSE veteran reading the spec won't notice it's missing until they try to port a real plugin.

---

## 2. "Wrap" semantics — call original, inspect return, mutate — is daily-driver SKSE, deferred without a replacement

`design.md` lines 1067–1071 defer "after-hook with return-value introspection" to v0.2+. The deferral note is one sentence; the impact deserves more.

In CommonLibSSE, the typical hook pattern is:

```cpp
result = OriginalFn(args);
if (some condition on result) result = mutate(result);
return result;
```

That's not exotic — it's the default shape. kcdx today gives you "before" (run my Lua, optionally skip original) or "skip" (replace original entirely). For anything that needs to *react to the original's return*, the author has to call the original manually from their detour, which means writing the trampoline-invoke themselves.

**Suggested resolution:** either (a) commit to landing wrap semantics in v0.1 — the engine work is a different trampoline shape and a `call_original = "wrap"` value plus a `result` capture in the callback signature, not a huge lift — or (b) the deferred-to-later entry should include a worked example showing how authors structure code today for the wrap pattern (typically: skip original + manually invoke trampoline + decide what to return) so they don't reinvent it five different ways.

---

## 3. Address-Library coverage is the actual UX, not the API

`design.md` documents the Address Library schema and `kcdxInterface::ResolveAddress` precisely. What it doesn't address: **the database is only as useful as its seeded content.** SKSE's Address Library succeeds because the community has been curating it for years. kcdx ships v0.1 with a brand-new CSV — if it has 5 rows the day Phase 7 ships, "use Address Library IDs" isn't a real option for any plugin author.

**Suggested resolution:** before Phase 7 ships,

1. Define an explicit *initial seed list* — at minimum every site any in-tree example mod (mempatch's `outfit-swap-in-combat`, every conflict-test plugin, every kcdx example) references should appear in the database with a stable ID. That's free coverage from work already done.
2. Document the contribution flow in `design.md` itself, not just "submit a PR." Where does the ID number come from (next-unused-integer? hash of the name? caller picks?), how is the `unverified` → `ok` transition gated, what's the cadence for refreshing per game update.
3. Decide and document whether kcdx will accept community-supplied IDs *without* a Ghidra-verified RVA — i.e. is `unverified` a place for "I think it's here, please confirm" entries, or strictly "I have an RVA, awaiting a second pair of eyes."

Without this, Phase 7 ships an *interface* but not a *feature*.

---

## 4. Semantic locators — the actual new-modder onramp

This is the largest gap and the one most likely to determine whether kcdx attracts a community beyond reverse-engineering enthusiasts.

A new modder asks "I want to gate combat-outfit-swap." Today the answer is: open Ghidra, find the function, write an AOB. That's the entire cliff. The Address Library helps only after someone else has already done the Ghidra work and submitted the ID.

What CryEngine actually provides that kcdx could expose as a locator:

- **Action map names** from `defaultProfile.xml`. The engine matches XML-side names to C++ handlers at load time. kcdx could resolve `target_action_map = "outfit_swap"` by reading the action map registry the same way the engine does.
- **Lua C-function names.** Anything bound into the game's Lua VM (which kcdx can already enumerate via `kcdx.lua.cfunction_address`) has a name → address mapping the engine builds at startup. `target_lua_cfunction = "Inventory.SwapOutfit"` is resolvable without any disassembly.
- **Console commands and CVars.** The engine's own command table maps names to handlers. `target_console_command = "g_outfit_swap"` is the same idea.
- **Flash event names.** UI hooks are deferred to v0.2 (Scaleform), but the same naming idea applies when they land.

**Suggested resolution:** pick one to ship in v0.1 as proof-of-concept. `target_lua_cfunction` is the obvious candidate because the runtime mechanism (`kcdx.lua.cfunction_address`) already works — exposing it as a TOML locator is a few hundred lines. Even one name-keyed locator dramatically changes who can author plugins: it lets someone read a Lua file in the game's PAK, find a function they care about, and write `target_lua_cfunction = "<that name>"` in TOML without ever touching a disassembler.

`design.md` doesn't currently mention semantic locators at all. They belong in the schema reference as a peer of `pattern` / `address_id`, even if only one is implemented for v0.1.

---

## 5. Hand-written machine code in TOML is a hard ceiling for the "no compiler" path

`[[hook]] bytes = "..."` and `[[trampoline]] bytes = "..."` ask the TOML author to supply x86-64 machine code as space-separated hex. Realistic only for stubs that an experienced person already wrote by hand. The example in `design.md` for `[[trampoline]]` (lines 430–438) is six instructions and most of them have placeholder `?? ?? ?? ??` displacements the author has to fill in.

This is fine for SKSE veterans (they don't need TOML; they have a DLL). It's a wall for everyone else.

**Suggested resolution:** two options, pick one.

- **(a) Lean harder into Lua callbacks.** Make the TOML path *not* expect raw bytes for normal cases; tell authors that if they need code, they write a Lua callback and let the engine generate the dispatch trampoline. The asmjit machinery already does this for `kcdxScriptingInterface::RegisterFunction` — the same primitive can back `[[trampoline]]` for the no-compiler crowd, with the callback receiving captured registers like `[[mid_hook]]` does. `bytes = "..."` stays for SKSE-veterans embedding a known sequence.
- **(b) Ship a tiny embedded assembler** (asmjit's text mode does this; it's already vendored). `assembly = """mov rax, [rcx+0x10]; ret"""` is dramatically more authorable than `bytes = "48 8B 41 10 C3"` and the engine already has the toolchain.

Either is fine; doing neither leaves `[[trampoline]] bytes = "..."` as decorative for the TOML audience.

---

## 6. Lua callbacks fire on the hooked function's thread, with no runtime guard

Documented as a hard rule in `CLAUDE.md` (#16) but absent from `design.md`. Authors reading the spec to figure out whether their plugin will work will not learn that hooking the audio mixer or a physics worker silently races the Lua VM and likely crashes.

**Suggested resolution:** add a short subsection to `design.md` under `kcdxScriptingInterface` (or as a peer section) listing:

- The constraint (Lua callbacks run on the hooked function's thread; KCD2's Lua VM is single-threaded with `lua_lock`/`lua_unlock` compiled out).
- The "safe targets" list (game's `update` tick descendants, anything kcdx already hooks, Lua-bound C functions).
- The "unsafe targets" list (audio, physics, IO workers).
- A pointer to `kcdxTaskInterface::AddTask` as the escape hatch for "I hit this from a worker thread and need to get back to main."
- The v0.2 plan: runtime `GetCurrentThreadId()` guard in the dispatchers with a logged skip.

Without this in `design.md` itself, plugin authors will trip the footgun before reading `CLAUDE.md`.

---

## 7. No diagnostic-only "scan" entry for learning the locator pipeline

A new modder writing their first AOB has no way to ask the engine "did this pattern resolve, and to what?" without either committing a write or reading the kcdx.log after a failed apply. mempatch users hit the same issue and the workflow is "write a no-op `replacement` equal to `original`, run the game, read the log." That's not discoverable.

**Suggested resolution:** add a `[[scan]]` entry type:

```toml
[[scan]]
name    = "find_outfit_swap"
pattern = "48 81 C1 60 0B 00 00 ..."
context = "..."
# logs:
#   [scan 'find_outfit_swap'] resolved 1 match at WHGame.dll+0x12345
#   [scan 'find_outfit_swap']   surrounding disasm (8 instructions before/after):
#   [scan 'find_outfit_swap']     0x...  mov  rcx, [rax+0x90]
#   [scan 'find_outfit_swap']     ...
```

Zero behavioral effect. Pure diagnostic. Helps a new author learn whether their pattern is unique, where it landed, and what instructions surround it — without risking a botched write. Engine work is trivial (the resolver already exists; just call it and log).

This is the single highest-leverage UX add for new modders. It should be in v0.1.

---

## 8. No worked example for the wrap pattern, even acknowledging it's deferred

`design.md`'s examples section shows: a byte patch, a mid_hook with Lua, a cross-plugin trampoline, a lifecycle subscription, a serialization pattern. All useful. Missing: the most common SKSE pattern — "hook a function, decorate its output, return modified value."

Even with wrap semantics deferred to v0.2 (item #2), an example showing **how to do it today** would save every porting author the same hour of figuring it out. Roughly:

```cpp
// Pattern for "wrap" in kcdx v0.1: skip original, invoke trampoline manually.
extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    // 1. Install hook with call_original = skip
    // 2. Capture the trampoline (from kcdxTrampolineInterface) so you can call original yourself
    // 3. In your detour: call the trampoline, inspect return, optionally mutate, return.
}
```

A 20-line example removes the ambiguity for everyone porting an SKSE plugin.

---

## 9. The cross-plugin "API" pattern is mentioned-and-dismissed without replacement guidance

`design.md` deferred item: "Cross-plugin function-call API formalization (the SKSE `enb-api` / `TrueDirectionalMovementAPI` pattern). Available implicitly via `GetModuleHandle` + `GetProcAddress` against the plugin's DLL; v0.1 doesn't add a higher-level wrapper."

This is correct but unhelpful. The SKSE-ecosystem convention for these APIs has tradeoffs (versioning, ABI stability, init ordering vs `kMessage_PostPostLoad`) that a new author will get wrong. One paragraph showing the canonical shape would prevent every plugin author from inventing their own.

**Suggested resolution:** a short subsection in `design.md` showing:

- Use `kMessage_PostPostLoad` (not `PostLoad`) to fetch peer plugin APIs.
- Export a versioned struct from your DLL with the same `kcdxPluginVersionData`-style approach (data block + getter function).
- The naming convention (`<plugin>.API.<version>` symbol or similar).
- Pointer to using the symbol table for the same purpose if you don't need DLL-to-DLL function calls.

Doesn't add engine code. Adds knowledge.

---

## 10. No story for "my AOB broke when KCD2 patched"

`design.md` and `CLAUDE.md` both warn that AOBs may break per game update. Neither describes what the plugin author or player *does* about it. The current implicit answer is "the engine logs a clear error, your patch doesn't apply, you ship a new release with a new pattern." That's accurate but it puts every plugin author on the hook for every game update.

This intersects with #3 (Address Library coverage) — the whole point of Address Library is to make IDs stable across updates while RVAs shift. But the doc doesn't connect those dots. A new plugin author reading `design.md` won't realize that **using `address_id` instead of `pattern` is the survive-game-updates story**, because the section on locators presents the three (pattern / address_id / target_symbol) as equivalents.

**Suggested resolution:** in `design.md`'s `[[hook]]` section (and the patch / mid_hook sections), add a one-line guidance: "Prefer `address_id` when the site is in the Address Library — it survives game patches that shift RVAs. Fall back to `pattern` only when no ID exists; consider submitting a PR to add one." Same line, three places. Connects the dots.

---

## Priority ordering (suggested)

If only some of these land before v0.1.0:

**Must address before v0.1.0** (visible to first-day users):

- #6 (thread safety in design.md) — silent footgun
- #7 (`[[scan]]` diagnostic) — biggest new-modder UX leverage, smallest engine cost
- #10 (address_id-as-update-survivor guidance) — pure docs

**Should address before v0.1.0** (defines the audience):

- #3 (Address Library seed list + contribution flow)
- #4 (at least one semantic locator — `target_lua_cfunction` is the natural pick)
- #1 (`[[call_redirect]]` schema) — closes the biggest CommonLibSSE feature gap

**Can defer to v0.2 with explicit note in design.md**:

- #2 (wrap hook) — already deferred; just add the today-workaround example (#8)
- #5 (assembler / Lua-trampoline) — depends on how aggressively the TOML path is positioned
- #9 (cross-plugin API conventions) — once one or two real plugins ship and a pattern settles
