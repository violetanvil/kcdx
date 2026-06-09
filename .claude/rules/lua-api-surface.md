---
paths:
  - "src/lua_bind*.cpp"
  - "src/lua_bind*.h"
  - "src/lua_registry.*"
  - "src/hook_chain.*"
  - "src/scripting_interface.cpp"
  - "src/*_interface.cpp"
  - "include/kcdx/Interfaces.h"
  - "include/kcdx/Kcdx.h"
  - "docs/lua/**"
  - "docs/cpp/**"
---

# kcdx authoring surface — the learnable sublanguage (Lua AND C++)

## North star (in perpetuity)

**kcdx's authoring surface is a sublanguage that mod authors write their
entire mod in. It MUST be easy to learn.** Two first-class authoring
languages — Lua (`plugin.lua`) and C++ (DLL plugins) — and the mandate
applies to BOTH. A TC author may write in Lua, C++, or both; neither is
the "real" surface.

The bar is higher than "each function is correct": a newcomer must hold
the whole surface in their head, guess where a call lives and what shape
it takes, and have those guesses be *right*.

## One model, two languages — FULL FEATURE PARITY, always

The Lua and C++ surfaces are two expressions of ONE model — same
concepts, names, structure, each idiomatic in its language.

**The hard invariant: Lua and C++ are at feature parity AT ALL TIMES.**
Anything achievable in one language is achievable in the other. NO
Lua-only or C++-only capability — no loophole, no "tracked exception."
A capability that exists in only one surface is an incomplete feature,
not a shipped one.

- Verbs match: `kcdx.hook` (Lua) ↔ `kcdxHookInterface::Install` /
  `K.hook->Install` (C++). Same modes (before/after/around/replace),
  locators (address, address_id, target_symbol, pattern, function_name),
  domains (log, memory, addr, …).
- **ONLY the syntax differs — never what's achievable.** Lua's
  `{named table}` → a C++ options-struct (`kcdxHookOptions`); positional
  Lua args → typed C++ params; `kcdx.log.info(...)` → the `kcdxLogger`
  the DLL uses.
- **Binding rule:** a capability added to one surface MUST be added to
  the other in the same change, reachable and complete (`kcdx.hook` mode
  `X` in Lua → `kcdxHookMode_X` in C++, both working). "Awkward to express
  in language Y" is not a reason to omit it — find the idiomatic way
  (effort never cuts capability, `cornerstones.md`).
- **Parity is tested, not assumed.** Regression coverage must exercise a
  capability from BOTH surfaces (a Lua plugin AND a C++ plugin, or one
  driving both) (`test-suite.md`). Extends `skse-parity.md` to kcdx's own
  two surfaces.

### Timing: invariant on the SHIPPED product; sequenced during the restructure

Full parity is a hard invariant for the **finished** product. The v0.2
restructure (`docs/outstanding-work/restructure/`) is built in ONE
language first (Lua), then backfilled to parity — restructure one surface
to settle WHAT we're building, then mirror to C++. During the
restructure, a Lua-only capability carries a **tracked parity debt** (the
C++ mirror is built in its phase — e.g. `kcdxHookInterface` in Phase 3
mirrors `kcdx.hook`, with a C++ test). This is construction sequencing,
NOT a permanent exemption: the parity rule governs the backfill, end-state
is full parity. A Lua-only capability is "restructure-in-progress," never
"done."

The rules below use Lua syntax for concreteness; each has its C++ mirror
per the binding rule.

Mod-author UX is priority #1 (`cornerstones.md`); on this surface it
decides over almost everything else. **"Easy to learn" demands, on every
surface, forever:**

1. **Predictable** — a small number of consistent rules (below) the
   author internalizes once, then *predicts* the rest. Every
   inconsistency breaks a correct guess; one ad-hoc placement forces the
   author to check the docs for everything.
2. **Minimal ceremony for the common case** — the everyday call (log a
   line, read an arg) is short; only configurable things carry weight.
3. **Errors teach** — wrong author code gets WHERE (file:line) and WHY in
   the author's terms. An opaque error is a usability bug. (Live gap:
   CryEngine Lua runs `storedebug` off, so plugin.lua errors surface as
   `plugin.lua:0 [Error] Lua error` with no line — tracked outstanding
   work.)
4. **Consistent with author muscle memory** — match Lua idioms (`string.`,
   `table.`, `os.`) and SKSE-ecosystem conventions (`skse-parity.md`)
   rather than inventing a novel shape.

A change that adds power but costs learnability loses: the cornerstone
order (UX > Capability > Perf) gives learnability the win unless there's
a hard technical reason.

## The disassembler test governs this surface

The author declares WHAT they want; the engine resolves the WHERE and
HOW. The full doctrine — the disassembler test, name-supplies-address-AND-ABI,
expert-hatch labeling, declare-once/share/coexist, surface-the-exception —
is in `cornerstones.md` (AP12 in `anti-patterns.md`). On this surface:

- ❌ `kcdx.hook.before("WHGame.dll", "IsInCombat", fn, { signature = "i32 (i32)" })`
  — the author hand-writes the ABI.
- ✅ `kcdx.hook.before("WHGame.dll", "IsInCombat", fn)` — the name resolves to
  address **and** verified signature.

## The rules

1. **One global: `kcdx`.** Everything an author calls hangs off the
   single `kcdx` global. No other globals (no `KCDX`, no per-plugin
   globals). A plugin's C functions registered via the scripting
   interface land under `kcdx.<table>.<fn>` (NOT global) — see
   `scripting_interface.cpp` `Thunk_RegisterFunction`.

2. **Core authoring verbs are top-level: `kcdx.<verb>`.** This is a
   CLOSED set — the actions every plugin uses to register intent with
   the engine, one per engine primitive:

   - `kcdx.hook.*` — function interception (→ `hook_chain`); sub-verbs
     `before` / `after` / `around` / `replace` / `insert_before` / `insert_after`
   - `kcdx.bytes{...}` — byte rewrite (→ patch engine)
   - `kcdx.code{...}` — trampoline / code allocation
   - `kcdx.on(event, fn)` — lifecycle / event subscription
   - `kcdx.command{...}` — console command
   - `kcdx.scan{...}` — diagnostic AOB scan

   Adding a 7th top-level verb requires it to be a genuine core
   authoring primitive (maps to an engine primitive, used by most
   plugins). When in doubt, it's a domain (rule 3), not a verb.

3. **Everything else is grouped: `kcdx.<domain>.<verb>`.** Capability
   domains are sub-tables:

   - `kcdx.log.*` — `info` / `warn` / `error` / `debug` / `trace`
   - `kcdx.memory.*` — pointer, scan_pattern, allocate, dynamic_call, dynamic_hook
   - `kcdx.addr.*` / `kcdx.address(id)` — Address Library
   - `kcdx.test.*` — `report`
   - `kcdx.cosave.*` — save/load persistence
   - `kcdx.functions.*` — function-reference namespace; `kcdx.functions.<stem>.<name>`
     resolves a function to a reference value the hook/statement verbs accept.
     Game-DLL stems are dot-free (sourced from the SQLite reference DB); plugin-DLL
     stems are `<author>.<plugin>` (sourced from the plugin's own `kcdx.dll.declare`
     or an auto-loaded `.pdb`). `kcdx.functions.by_id[N]` is the game-only stable-ID
     accessor. (restructure Phase 9.3)
   - `kcdx.dll.declare(plugin_namespace, fn_map)` — a plugin declares its own DLL's
     functions (signatures from the author's source, no disassembly), exposing them
     for cross-plugin hooking. (restructure Phase 9.3)
   - `kcdx.behavior.*` — `set` / `get` / `list` / `declare` named behaviors
     (engine-shipped `kcdx.behavior.*` + plugin-declared `<author>.<plugin>.<bare>`)
     (restructure Phase 9.5)
   - `kcdx.player.*`, `kcdx.world.*`, `kcdx.dialogue.*`, `kcdx.quest.*`,
     `kcdx.inventory.*`, `kcdx.assets.*` — gameplay (Phase 9+)

   Grouping scales to total-conversion size without a flat wall: typing
   `kcdx.player.` shows ONLY player calls. Matches Lua idiom (`string.`,
   `table.`, `os.`) and SKSE muscle memory.

4. **Required args are positional; optional args live in a trailing table.**

   Required → positional means the author cannot forget — Lua errors
   immediately at the call site, not later at runtime. Optional → table
   means self-documenting, order-free, and additions don't break existing
   call sites. The shape protects what matters: a missing required field
   is impossible to write; a missing optional field is the default.

   - **Verbs with several required args + several optional knobs:**
     required args positional, optional args in a trailing table.
     Example: `kcdx.hook.before(target, [locator], callback, [opts])` —
     target and callback required and positional; locator
     positional-when-needed; opts the trailing optional table.
   - **Simple "do a thing" calls with 1–3 obvious required args and few
     or no optionals:** all positional, no table needed.
     `kcdx.log.info(category, msg)`, `kcdx.on(event, fn)`,
     `kcdx.test.report(name, pass, reason)`, `kcdx.behavior.set(name, value)`,
     `kcdx.addr.X`, `kcdx.player.health:get()`. Forcing a table here is
     ceremony.
   - **Cases where required is "at least one of N":** single table arg
     whose contents are validated at parse-time
     (`kcdx.find({string = "..."})` requires at least one criterion).

   Author rule: **required → positional; optional → trailing table;
   do-a-thing → all positional.**

4a. **Discrete behavioral variants are sub-verbs, not table keys.**

   `kcdx.<verb>.<variant>(...)` makes the variant impossible to forget,
   lets each variant carry its accurate signature, and surfaces variants
   in autocomplete. The author who types `kcdx.hook.` sees the modes
   immediately; cannot misspell one into another; cannot supply
   conflicting modes on one call.

   Examples:

   - `kcdx.hook.before / .after / .around / .replace / .insert_before / .insert_after`
   - `kcdx.statement.replace_with / .insert_before / .insert_after`
   - `kcdx.log.info / .warn / .error / .debug / .trace`
   - `kcdx.cosave.set_uid / .write / .records / .on_save / .on_load`

   Mode-as-key on a shared call is reserved for cases where multiple
   modes legitimately compose on a single call. No verb in the current
   or planned surface fits this exception.

5. **Shared names are `<author>.<plugin>.<bare>`; you type only the bare name.**
   Any name the engine registers into a cross-plugin namespace (hook/byte
   targets, `kcdx.code{export=}` symbols, `kcdx.publish`/`kcdx.on` events,
   cosave records, asset overlays) is identified as `<author>.<plugin>.<bare>`.
   The author writes the bare `name`; the engine derives `<author>` from
   `[plugin].author` and `<plugin>` from `[plugin].name`. Bare references
   resolve self > engine > other; the explicit prefixed form is unambiguous
   from anywhere; `kcdx.*` is the engine's reserved root (engine seeds sit at
   the 1-dot form `kcdx.<seedname>`, no plugin layer needed). Full law
   (precedence, dot-as-canonical-separator, warn-once, `kcdx.alias`):
   `naming-namespaces.md`.

## The author-facing summary (put this in docs/lua/index.md)

> Everything is under one global, `kcdx`. Site-modification verbs split
> by mechanism: `kcdx.hook.*` for callback-based interception (per-call
> Lua cost; use when per-call logic is needed), `kcdx.statement.*` for
> static-bytes modification (zero per-call cost; bytes execute natively),
> `kcdx.bytes` for raw byte rewrites outside functions. Named common
> behaviors live under `kcdx.behavior.*` (engine-shipped) or under
> `<author>.<plugin>.<bare>` (plugin-declared via `kcdx.behavior.declare`);
> all reachable through `kcdx.behavior.set/get/list`. Everything else is
> `kcdx.<domain>.<verb>` (log, memory, addr, test, cosave, find, player,
> world, …).
>
> Required arguments are positional; optional fields go in a trailing
> table. Discrete behavioral variants are sub-verbs
> (`kcdx.hook.before/after/around/replace`), not table keys. Every
> hash-checked verb (`kcdx.hook.*`, `kcdx.statement.*`, `kcdx.bytes`
> with a named locator) takes the module name as its first positional
> argument — `kcdx.hook.before("WHGame.dll", "IsInCombat", ...)`. There
> is no default module.

## How to apply

- New capability that isn't one of the six core verbs → it goes in a
  domain sub-table. Never add a flat `kcdx.foo_bar` compound-name call.
- A doc-comment example referencing `kcdx.x.y(...)` is NOT proof the
  binding exists — verify the binder actually registers it before
  relying on it.
- Every surface ships with its API-doc entry — Lua → its per-call file under
  `docs/lua/` (discoverable from `docs/lua/index.md`), C++ → its per-interface
  file under `docs/cpp/` — plus any new glossary term, in the same change
  (`docs-discipline.md`), AND a test-suite plugin exercising it
  (`test-suite.md`).
