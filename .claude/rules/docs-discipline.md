---
paths:
  - "src/**"
  - "include/**"
  - "docs/lua/**"
  - "docs/cpp/**"
  - "src/lua_bind*.cpp"
  - "src/lua_bind*.h"
  - "src/*_interface.cpp"
  - "include/kcdx/Interfaces.h"
  - "include/kcdx/Kcdx.h"
---

# Documentation is a delivery requirement — it moves with the code

kcdx ships two authoring sublanguages (Lua + C++). Their reference docs ARE
the surface a mod author learns from; a capability the author cannot find
documented does not exist to them. Documentation is therefore a first-class
delivery requirement, on the same footing as the test plugin
(`test-suite.md`): a capability is **not done** until its documentation moved
with it, in the SAME change, in the SAME commit.

This is the doc counterpart of the full-parity invariant (`lua-api-surface.md`)
and an instance of the UX cornerstone — doc clarity is author UX
(`cornerstones.md`).

## What counts as a capability requiring docs

Same trigger as `test-suite.md` "new functionality":

- A new `kcdx.*` Lua surface or a new C++ interface / interface method.
- A new mode / knob / arg form / locator / resolver tier on an existing
  surface.
- A new TOML key, lifecycle event, console command, save/cosave field, or any
  engine behaviour an author or user can observe.

A behaviour-changing bug fix that alters a documented contract updates that
doc entry in the same change.

## The completeness criterion — all three, same commit

A capability is documented when ALL of these landed with it:

1. **A reference entry in the surface's per-call file.** Lua → the matching
   per-call file under `docs/lua/` (e.g. `docs/lua/hook.md` for `kcdx.hook`),
   discoverable from `docs/lua/index.md`'s map. C++ → the matching
   per-interface file under `docs/cpp/`. Follow the existing entry structure:
   call shape, arguments (type + meaning), return value, error behaviour,
   minimal correct snippet. An entry that omits the error behaviour or the
   snippet is incomplete.

2. **A glossary term for every new concept or noun.** A new author-facing
   concept (a new locator kind, a new hook mode, a new zone, a new handle
   method family) gets a glossary entry in that doc's Glossary section. If the
   author has to learn a new word to use the feature, the word is defined.

3. **The cross-surface entry — both docs map the capability, even when only
   one is built.** The two languages are rarely built at the same time, so at
   any moment the engine is usually NOT at structural parity. The docs are.
   When a capability ships in one language, the OTHER surface's doc gets a
   **full entry marked "not yet implemented (NYI)"** in the same change —
   build `kcdx.code` in Lua → `docs/cpp/code.md` gets a NYI entry, and the
   reverse for a C++-first capability. The NYI entry maps the planned mirror
   (same model, the other language's spelling — `kcdx.hook{...}` ↔
   `kcdxHookInterface::Install` / `K.hook->Install`) so both docs stay
   structurally parallel while the engine catches up. The NYI marker is
   removed when that side is built and verified callable. A built capability
   with no mirror entry — not even NYI — is the violation.

   **The only sanctioned non-parity is genuinely single-surface capability.**
   A thing that exists in one language because the other handles it natively
   (the C++ author gets it from the language/runtime; the Lua author needs an
   explicit kcdx surface for it, or vice versa) owes NO mirror. Mark it
   **explicitly "single-surface: <reason>"** in its own doc so it is
   distinguishable from a NYI debt (owed, not yet built) and from an oversight
   (owed, forgotten). Absent that explicit marker, a single-surface entry
   reads as a missing mirror and is a finding.

   End-state invariant (`lua-api-surface.md`): the shipped product is full
   feature parity — every NYI entry resolves to a built, callable mirror;
   only explicitly-marked single-surface entries stay single, by design.

4. **The entry is glanceable, not just complete.** The author must reach it
   and build from it by glancing — not by digging. Three properties, each a
   gate finding when missing:

   - **Discoverable.** The entry's call appears in its surface's index map
     (`docs/lua/index.md` for Lua, `docs/cpp/index.md` for C++ — the map is the
     discoverability mechanism, the front door that points to every call's file)
     AND the entry slots into the folder as its own per-call file under
     `docs/lua/` / `docs/cpp/`, so the author predicts where it lives
     and finds it without searching. It honours the index's contract — *"if a
     call is not in this map, it does not exist yet"* — so the map stays the
     single place to look. An entry whose call is missing from the index map,
     or one that has no per-call file to slot into, is not discoverable even
     though it exists.
   - **Common-path-first.** The everyday path is shown FIRST and prominently;
     any hex/ABI/offset/expert form is explicitly demoted and labelled
     ("advanced/expert escape hatch"). This is the disassembler test
     (`cornerstones.md`, AP12) rendered in docs: documenting the expert form
     before — or instead of prominent — the `target = "<name>"` common path is
     a UX defect, not a complete entry.
   - **Snippet is copy-paste-runnable.** The minimal snippet is self-contained
     and runs as-is (the literal glance-and-build artifact, time-to-first-
     working-plugin). A snippet with elisions the author must resolve before it
     runs is not minimal-runnable.

## The doc is verified, not assumed

A doc-comment or example referencing `kcdx.x.y(...)` is NOT proof the binding
exists (`lua-api-surface.md`). The reference entry describes a capability that
is actually registered and callable — written against the as-built binder, not
an intended one. `docs/lua/index.md`'s own contract: "if a call is not in this
map, it does not exist yet." Keep that true in both directions — nothing
documented that isn't built, nothing built that isn't documented.

## How to apply

- Adding a `kcdx.*` surface or C++ interface method → its per-call file under
  `docs/lua/` / `docs/cpp/` entry + any new glossary term land in the same
  commit, the same way its test plugin does (`test-suite.md`). Not a follow-up,
  not "doc it after it stabilizes."
- `/feature` decomposition makes documentation part of a step's deliverable or
  its own ordered step; `step-review` flags a binder/interface change whose
  diff carries no matching doc entry; `verification-checkpoint` enumerates the
  doc/glossary/parity entries alongside the behaviours.

The subtractive mirror — deleting a surface leaves no prescriptive doc behind —
is `deletion-hygiene.md`. Both directions keep `docs/lua/index.md`'s contract
true: nothing documented that isn't built, nothing built that isn't documented.

Related: `lua-api-surface.md` (the surface the docs describe + full parity),
`test-suite.md` (the parallel test mandate), `cornerstones.md` (doc clarity is
author UX), `toml-schema.md` (manifest keys to document),
`deletion-hygiene.md` (the subtractive direction).
