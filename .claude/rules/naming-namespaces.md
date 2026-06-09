---
paths:
  - "src/lua_registry.*"
  - "src/hook_chain.*"
  - "src/lua_bind*.cpp"
  - "src/lua_bind*.h"
  - "src/*_interface.cpp"
  - "src/config.cpp"
  - "src/config.h"
  - "src/serialization*.cpp"
  - "src/save_load*.cpp"
  - "include/kcdx/Interfaces.h"
  - "include/kcdx/Kcdx.h"
  - "**/kcdx.toml"
  - "docs/lua/**"
  - "docs/cpp/**"
---

# Shared-namespace naming — the `<author>.<plugin>.<bare>` model

Binding law for every name the engine registers into a **shared, cross-plugin
namespace**. One model for ALL such names — not just hook targets. Applies to:

- hook / byte targets (the named-target surface)
- `kcdx.code{ export = }` published symbols (the cross-plugin symbol table)
- `kcdx.publish` / `kcdx.on` custom events
- cosave record identity
- asset-overlay virtual paths
- any future shared-namespace surface

A name that lives only inside one plugin (a local Lua variable, a private
function) is NOT a shared name and this rule does not touch it.

## The model

**Every shared name is identified as `<author>.<plugin>.<bare>`.** Three
components, two dots:

- **`<author>`** — the author/publisher namespace, taken from `[plugin].author`.
- **`<plugin>`** — the plugin name within that author, taken from `[plugin].name`.
- **`<bare>`** — the shared name the plugin declares (a target, an export, an
  event, a cosave record, …).

- **The engine derives `<author>` and `<plugin>` from the manifest.** The author
  NEVER types their own prefix in their own declarations. They write
  `name = "open_inventory"`; the engine registers `redmoon.outfit.open_inventory`
  (for `[plugin].author = "redmoon"`, `[plugin].name = "outfit"`).
- **`.` (dot) is the canonical separator** and is **semantic** — the engine
  parses on it to split the triple. Not "convention only."
- **Bare references resolve by precedence: self > engine > other-plugin.** A
  bare `foo` resolves to the calling plugin's own `(author, plugin, foo)`
  triple first, then an engine `foo` in the seed table, then any other plugin's
  `foo`.
- **The explicit prefixed forms are unambiguous** and callable from anywhere —
  including a plugin referencing one of its own names by full path.
- **`kcdx.*` is reserved for the engine.** Engine-registered shared names live
  under the `kcdx.` root; `[plugin].author = "kcdx"` and `[plugin].name = "kcdx"`
  are rejected. Author and engine namespaces are disjoint at the top level by
  construction, so a bare reference is never ambiguous between an author's own
  name and an engine name.

### Explicit-form depth matches actual nesting

The number of dots in an explicit reference tells you what's named:

- **1 dot — `kcdx.<seedname>`.** Explicit engine seed reference. Seed names live
  directly under the reserved `kcdx` author root (no plugin layer needed for
  engine seeds). Example: `kcdx.luaL_loadfile`.
- **2 dots — `<author>.<plugin>.<event>` (or `.<alias>`, etc).** Explicit
  author + plugin reference, used by pub/sub events, by aliases, and any other
  surface that names a plugin's whole identity rather than a specific export.
- **3 dots — `<author>.<plugin>.<bare>`.** Explicit cross-plugin export
  reference. Used to refer to another plugin's shared name (hook target, export,
  cosave record, …) from outside the declaring plugin.

## Collision behavior

- **A bare-reference collision warns once per session, per colliding name.** The
  warn line names both owners and teaches the fix: prefix any name you did not
  declare in your own space.
- **A prefixed reference NEVER warns** — it is already unambiguous.
- **Precedence, not partition.** An engine release later adopting a name an
  author already used never displaces the author: `self` resolves before
  `engine`. This is the binding mechanism behind the three guarantees in
  `cornerstones.md` ("Manual and official names COEXIST — no silent clobber;
  author-first wins"). There is no globally-unique-or-reject rule on prefixed
  names.

## `[plugin].author` and `[plugin].name` are the namespace prefix

`[plugin].author` and `[plugin].name` together form the short, stable namespace
ID — **not** the display title. Because they stamp every shared name the plugin
exports, both must be short and ergonomic to type as a prefix.

- **`author`** — short, stable, lowercase identifier for the author/publisher
  namespace. Charset `[a-z0-9_]`, 2–128 chars. Used as the leading namespace
  component on every exported name. Prefer short: `redmoon`, not
  `red-moons-modding-collective`.
- **`name`** — short, stable, lowercase identifier for the plugin within its
  author namespace. Charset `[a-z0-9_]`, 2–128 chars. Used as the second
  namespace component on every exported name AND as the dependency / messaging /
  cosave key. Prefer short: `outfit`, not
  `red-moons-immersive-inventory-overhaul`. The 128-char cap (raised from the
  original 32) accommodates engine-prefix author families like
  `kcdx_builtin_bugsplat_filename_fix` without distorting the plugin name to
  fit the prefix; runtime cost of a longer cap is nil (one strlen at
  launch-time discovery). Short-component limits (aliases, bare target
  names — typed by hand) stay at 32 — see `kcdx.alias`.
- **`display_name`** — the human-facing title shown in the launcher UI and
  `kcdx_list_plugins`. Free-form.

Different fields, different jobs. See `toml-schema.md`.

**Invalid `author` or `name` is a hard manifest rejection.** Wrong charset,
over 128 chars, under 2, empty, or set to the reserved `kcdx` engine root → the
plugin is REFUSED to load with a teaching error naming the rule. A bad prefix
would corrupt every name the plugin exports and every other plugin's reference
to it — fail loud at the door, never warn-and-proceed. Consistent with the
loud-full-rejection-over-silent-partial stance. Enforced in `src/config.cpp`
`ParsePluginManifest` via `kcdx::address_library::ValidatePluginName` (for
`[plugin].name`) and `kcdx::address_library::ValidateAuthorName` (for
`[plugin].author`).

## Aliasing

Authors alias a name with the runtime call
`kcdx.alias(short, "author.plugin.bare")`. An alias is a **local handle**,
plugin-scoped to the calling plugin:

- It aliases another plugin's long-prefixed name (`kcdx.alias("inv",
  "redmoon.outfit.open_inventory")` → refer to `inv`) OR the author's own
  long/awkward name.
- It resolves ONLY in the declaring plugin's space. It **cannot shadow** an
  engine name or another plugin's bare name — it only adds a handle, never
  displaces resolution. (It cannot collide with the reserved `kcdx.` root
  either.)
- Substitution happens BEFORE the precedence walk: a call referring to the
  short handle is rewritten to the full triple, then resolved by self > engine
  > other.
- No alias precedence subclause is needed: an alias is pure local convenience
  on top of the self > engine > other model.

## Examples

- **Own-plugin (bare) reference** — calling plugin's own target:
  `kcdx.hook.before("WHGame.dll", "open_inventory", fn)`. The engine stamps it
  as `<owningAuthor>.<owningPlugin>.open_inventory` and resolves it self-first.
- **Cross-plugin reference** — referring to another plugin's published target:
  `kcdx.hook.before("WHGame.dll", "redmoon.outfit.open_inventory", fn)`.
  Three-segment explicit form, resolved directly.
- **Explicit engine seed reference** — bypassing the bare-name precedence walk
  for an engine-supplied locator:
  `kcdx.hook.before("WHGame.dll", "kcdx.luaL_loadfile", fn)`. Two-segment
  explicit form.
- **Pub/sub event** — publishing and subscribing to a custom event:
  `kcdx.publish("inventory_opened")` from plugin `redmoon.outfit` stamps the
  event as `redmoon.outfit.inventory_opened`; another plugin subscribes with
  `kcdx.on("redmoon.outfit.inventory_opened", fn)`.
- **Alias** — `kcdx.alias("inv", "redmoon.outfit.open_inventory")` then
  `kcdx.hook.before("WHGame.dll", "inv", fn)` in the declaring plugin.

## How to apply

- Registering a name into ANY shared surface → take the bare `<name>` from the
  author, derive `<author>` from `[plugin].author` and `<plugin>` from
  `[plugin].name`, register `<author>.<plugin>.<name>`. Never ask the author to
  type their own prefix.
- Parse the prefixed form on the dot. Resolve a bare `<name>` by
  self > engine > other; warn once per session per bare collision; never warn a
  prefixed reference.
- A new shared-namespace surface uses THIS model — it does not invent its own
  collision scheme.
- **Trigger — about to give a shared name its own ad-hoc collision scheme
  (globally-unique-or-reject, a different separator, author-types-the-prefix,
  per-surface duplicate handling):** STOP. That is a name-model fork. Use the
  `<author>.<plugin>.<bare>` model with self > engine > other precedence. A
  per-surface naming scheme is the tell.

## Known debt to reconcile (do not leave two models silent)

- **`kcdx.code{ export }` historically treated the dot as convention-only,
  unparsed** (design.md "Cross-plugin symbol table"). This rule makes the dot
  semantic and replaces the globally-unique-or-reject diagnostic with the
  precedence model. The design.md section is annotated superseded-by this rule.

Related: `cornerstones.md` (the coexist / author-first guarantees this
implements), `lua-api-surface.md` (the learnable sublanguage this is part of),
`toml-schema.md` (`author` / `name` vs `display_name`), `docs-discipline.md`.
