# Asset replacement — the `replaces.toml` sidecar
> Part of the [kcdx Lua API](index.md).

Replace a vanilla asset (a texture, UI flash, model, script, sound, …) — or
another mod's asset — with your own, **with no code**. Drop your file in your
plugin's `assets/` folder, drop a small `replaces.toml` next to it declaring
what it replaces, and the engine serves your file where the game would have
served the original.

**A file's presence makes it referenceable; it does NOT by itself replace
anything.** A replacement is always an explicit declaration — never inferred
from a path that happens to match a vanilla path. So your mod's effect is
readable at a glance, and a mistyped target is a loud error in the log, never a
silent no-op.

## The common path

Put your asset under `assets/` and a `replaces.toml` beside it:

```
my_plugin/
  kcdx.toml
  assets/
    Libs/UI/Textures/
      KCDLogo.dds        <- your replacement texture
      replaces.toml      <- declares what it replaces
```

```toml
# assets/Libs/UI/Textures/replaces.toml
[[asset]]
replaces = "Libs/UI/Textures/KCDLogo.dds"
```

That is the whole no-code path. Your `KCDLogo.dds` now serves wherever the game
asks for the vanilla `Libs/UI/Textures/KCDLogo.dds`. You declared *what* it
replaces; the engine resolved *which file wins* and *how to serve it* — you
never touch a path-search rule, a pak priority, or an asset class.

## Where the sidecar goes — scope is its placement

A `replaces.toml` covers the directory it sits in (and below). **The more it
abstracts, the more each row must specify:**

- **File-scope** — a `replaces.toml` sharing a directory with exactly one asset
  needs no `file` key. The single sibling file is the one it declares.
- **Directory-scope** — a `replaces.toml` covering several files names each
  one's `file` (a path relative to the sidecar's own directory).

```toml
# A directory-level sidecar (assets/textures/replaces.toml) covering two files:
[[asset]]
file     = "armor/breastplate.dds"
replaces = "Libs/UI/Textures/Armor/Breastplate.dds"

[[asset]]
file     = "armor/helmet.dds"
replaces = "Libs/UI/Textures/Armor/Helmet.dds"
```

You pick the point on that tradeoff that suits your asset tree.

## The `[[asset]]` row

| Field | Type | Meaning |
|---|---|---|
| `replaces` | string | **One of two forms (this XOR the pair below).** What this asset replaces, as ONE string: a **vanilla asset path** (`"Libs/UI/Textures/KCDLogo.dds"`) OR another mod's **published name** (`"redmoon.outfit.belt"`). |
| `replaces_plugin` + `replaces_path` | string + string | The **pair** form (this XOR `replaces`). Replace another mod's asset *by path* when it has no published name: `replaces_plugin` is the other mod's `<author>.<plugin>`, `replaces_path` is the asset's path within that mod's `assets/`. |
| `file` | string | **Optional at file-scope, required at directory-scope.** Which file in the sidecar's own directory the row speaks to (a path relative to the sidecar). Omit it only when the sidecar's directory holds exactly one asset. |
| `name` | string | **Optional.** Publish this asset under a stable name so other mods can reference it as `<author>.<plugin>.<name>`. *(The publish side is a planned later phase — see Status below.)* |

Declare **exactly one** of `replaces` or the `replaces_plugin`+`replaces_path`
pair per row. Setting both is an ambiguous declaration and is rejected.

## Conflict resolution — load order, declared

When two plugins each declare a replacement of the **same** target, the
load-order winner serves and the loser is suppressed — with a conflict line in
the log naming the winner, the suppressed plugin, why (load order), and the fix
(a lower `priority` in the loser's `kcdx.toml`). Because nothing applies
implicitly, a conflict is always between two explicit declarations — never a
surprise. This is the same winner/suppressed model the hook and bytes surfaces
use.

## Errors — a mistyped target teaches, never silently no-ops

A malformed declaration is rejected LOUD with a teaching line in the engine log
(category `ASSET_SIDECAR`) naming the plugin, the sidecar file, and what was
wrong — and the bad row is skipped, so one bad row never kills the good ones in
the same file. Rejected shapes:

- **Ambiguous / both-forms** — a row that sets BOTH `replaces` and the
  `replaces_plugin`/`replaces_path` pair. Declare one form.
- **Incomplete pair** — `replaces_plugin` without `replaces_path` (or vice
  versa). The by-path form needs both.
- **Missing target** — a row whose `file` names an asset that does not exist in
  your `assets/` tree, or (at file-scope) a directory holding zero or more than
  one asset with no `file` key to disambiguate. A typo in a target is a loud
  error, never a silent orphan.
- **Unknown / wrong-typed key** — a key that is not `replaces`,
  `replaces_plugin`, `replaces_path`, `file`, or `name`, or one carrying the
  wrong type.

## Reference an asset in code — `kcdx.assets.*`

The sidecar above is the no-code path. To reference an asset *from your
`plugin.lua`* — to get a loadable path you hand to a game asset API — use the
`kcdx.assets.*` domain.

| Call | Args | Returns |
|---|---|---|
| `kcdx.assets.get_by_path(path)` | string path, relative to *your* `assets/` | a loadable path (string) for *your own* asset; `(nil, err)` if the path is not a file in your `assets/`, or on a non-string/empty argument. |
| `kcdx.assets.get_by_name(name)` | string published name | a loadable path (string) for *your own* published asset; `(nil, err)` if you never declared that name. |
| `kcdx.assets.declare(name, file)` | string name, string file | publishes `<author>.<plugin>.<name>` → the file's loadable path; returns that path. `(nil, err)` if the file is not in your `assets/`. |
| `kcdx.assets.register(vpath, file)` | string vpath, string file | makes `file` serve when the game opens `vpath`, for opens *after* the call; returns the loadable path. `(nil, err)` if the file is not in your `assets/`. |
| `kcdx.assets.replace(target, with)` | string target, string with | makes `with` serve where a **vanilla-path** `target` was opened, for opens *after* the call; returns the loadable path. `(nil, err)` if `with` is not in your `assets/`. A **packed cross-mod** `target` (`"author.plugin.name"`) returns `(nil, err)` for now — it resolves with cross-mod resolution, a later step (see Status). |

Every verb resolves your `file` / `with` through your `assets/` folder, so a
mistyped or missing asset is a loud `(nil, err)` naming the path — never a silent
`nil`. The C++ mirror (`kcdxAssetInterface` — `K.assets->GetByName` / `Declare` /
`Register` / `Replace`) is a later phase; see [planned.md](planned.md).

### `kcdx.assets.get_by_path(path)` — a loadable path to your own asset

Resolves an asset in *your own* `assets/` folder to a loadable on-disk path —
the path you hand to a game asset API (set a UI texture, load a model, …). You
write the path **relative to your `assets/` folder**, with no owner prefix: the
engine knows which plugin is calling, so your own asset needs no namespace.

```lua
local icon = kcdx.assets.get_by_path("icons/my_icon.dds")
-- icon is a loadable path; hand it to a game asset API.
```

| Arg | Type | Meaning |
|---|---|---|
| `path` | string | The asset's path **relative to your plugin's `assets/` folder** (e.g. `"icons/my_icon.dds"`). You write a path — never an address, an asset class, or a handle. |

**Returns:** a loadable path (a string) on success — the disk path the engine
serves the asset from. A path to a file **not** in your `assets/` returns
`(nil, err)`, where `err` is a teaching string naming the missing path so you
can fix the typo — a mistyped path is a loud error, never a silent `nil`. A
non-string or empty `path` is the same `(nil, err)` shape (the standard
kcdx-binder bad-argument idiom).

```lua
local path, err = kcdx.assets.get_by_path("icons/my_icon.dds")
if not path then
    kcdx.log.warn("MYMOD", "asset not found: " .. err)
    return
end
-- use `path` with a game asset API
```

### `kcdx.assets.declare(name, file)` / `get_by_name(name)` — publish + resolve a name

Publish a stable **name** for one of your assets so other mods can reference it
as a contract — the code-side peer of a sidecar `name`. You declare the bare
name; the engine publishes it as `<author>.<plugin>.<name>` (it knows who you
are — you never type your own prefix). Resolve your own published name back to a
loadable path with `get_by_name`.

```lua
-- publish "shirt" -> your assets/male/shirt.dds
kcdx.assets.declare("shirt", "male/shirt.dds")

-- later, resolve your own published name to a loadable path
local shirt = kcdx.assets.get_by_name("shirt")
```

| Arg | Type | Meaning |
|---|---|---|
| `name` (declare / get_by_name) | string | The bare published name (e.g. `"shirt"`). The engine prefixes it with your `<author>.<plugin>`. You write a name — never a class, address, or handle. |
| `file` (declare) | string | The asset to publish, **relative to your `assets/` folder** (resolved exactly like `get_by_path`). |

**Returns:** `declare` publishes the name and returns the file's loadable path;
`get_by_name` returns the loadable path a published name resolves to. A `file`
not in your `assets/` (declare), or a `name` you never published (get_by_name),
returns `(nil, err)` naming what was wrong — a loud error, never a silent `nil`.

### `kcdx.assets.register(vpath, file)` — add an asset at runtime

Make an asset available that was not in `assets/` at load — generated at runtime,
or chosen conditionally. The game serves your `file` when it opens `vpath`, for
opens **after** the call (an already-open handle is not re-resolved).

```lua
-- serve your assets/gen/portrait.dds when the game opens this virtual path
kcdx.assets.register("Libs/UI/Textures/MyPortrait.dds", "gen/portrait.dds")
```

| Arg | Type | Meaning |
|---|---|---|
| `vpath` | string | The virtual path the game opens (the path it asks for). |
| `file` | string | The asset that serves it, **relative to your `assets/` folder**. |

**Returns:** the file's loadable path on success; `(nil, err)` if `file` is not in
your `assets/`. **Takes effect thereafter** — an asset the game already opened
before the call is unaffected; the overlay applies to the next open.

### `kcdx.assets.replace(target, with)` — replace an asset at runtime

Register a replacement in code — the conditional-replacement case (the code-side
peer of a `replaces.toml`). `with` serves where `target` was opened, for opens
**after** the call.

```lua
-- conditionally swap a vanilla texture for your own
if some_condition then
    kcdx.assets.replace("Libs/UI/Textures/KCDLogo.dds", "branding/logo.dds")
end
```

| Arg | Type | Meaning |
|---|---|---|
| `target` | string | What to replace. A **vanilla asset path** (`"Libs/UI/Textures/KCDLogo.dds"`) **serves now**. Another mod's **packed name** (`"redmoon.outfit.shirt"`) — the string-key form of the cross-plugin namespace, for a key the dotted form can't express — **resolves with cross-mod resolution, a later step** (see Status); until then a packed target returns a teaching error rather than silently not serving. |
| `with` | string | The replacement asset, **relative to your `assets/` folder**. |

**Returns:** for a **vanilla-path** `target`, the replacement's loadable path on
success — and **takes effect thereafter**, like `register`. `(nil, err)` if
`with` is not in your `assets/`. A **packed cross-mod** `target`
(`"author.plugin.name"`) returns `(nil, err)` for now — cross-mod resolution
(resolving the packed name to the vpath the other mod's asset serves at) lands in
a later step; the error tells you so, never a silent no-op.

### Reference another mod's asset — the navigable namespace

To reference *another mod's* asset, navigate the cross-plugin namespace
([`kcdx.plugin`](plugin.md)) and call `get_by_path` (by path) or `get_by_name`
(by that mod's published name) on its `.assets`:

```lua
-- by path
local shirt = kcdx.plugin.redmoon.outfit_swap.assets.get_by_path("male/shirt.dds")
-- by the other mod's published name
local belt  = kcdx.plugin.redmoon.outfit_swap.assets.get_by_name("belt")
```

`kcdx.plugin.<author>.<plugin>.assets.get_by_path(path)` /
`.get_by_name(name)` read as native dotted Lua — the namespace segments are bare
(no quoted-namespace ceremony), and the path/name stays a quoted string (it is
data). The same teaching-error behaviour applies: a path/name not in *that* mod's
`assets/` or published set, or a non-existent `<author>` / `<plugin>`, fails loud.
Your own asset needs no namespace — call `kcdx.assets.get_by_path` /
`.get_by_name` directly. (`declare` / `register` / `replace` are own-namespace
only — you publish and register into *your own* namespace, so they are not on the
cross-plugin `.assets` leaf.)

The C++ mirror (`K.assets->GetByPath` / `GetByName`) is a later phase — see
[planned.md](planned.md).

## Status

- **The no-code declarative path (this page) is LIVE** for declaring a vanilla
  replacement. The engine keys your declared target into the asset-resolution
  map and serves your file.
- **All five `kcdx.assets.*` verbs (Lua) are LIVE** — `get_by_path` /
  `get_by_name` / `declare` / `register` / `replace` for your own asset, plus
  `get_by_path` / `get_by_name` for another mod's asset via the navigable
  namespace. The C++ `kcdxAssetInterface` mirror is a later phase — its shape is
  shown above and in [planned.md](planned.md) so the whole surface is visible.
- **Code-side publishing + resolution is LIVE.** `declare` publishes a name into
  your namespace and `get_by_name` resolves it (your own, or another mod's via
  the cross-plugin namespace). `register` / a **vanilla-path** `replace` write a
  runtime overlay that takes effect for opens **after** the call.
- **`replace` with a packed cross-mod target is a later step.** A `replace`
  whose `target` is another mod's packed name (`"author.plugin.name"`) needs
  cross-mod *resolution* — resolving the packed name to the vpath the other mod's
  asset serves at — which lands with the cross-mod resolution phase. Until then a
  packed `replace` target returns a teaching `(nil, err)` saying so, never a
  silent no-op. The **vanilla-path** `replace` form serves now.
- **The declarative `replaces.toml` cross-mod *resolution*** (a sidecar
  `replaces` naming another mod's published name / the cross-mod pair) still
  lands with the sidecar published-name namespace in a later phase — a cross-mod
  *sidecar* row is recorded and reported but does not yet serve. The *code-side*
  `declare` / `get_by_name` published-name path above is live now.

## Glossary

- **asset sidecar (`replaces.toml`)** — an opt-in metadata TOML co-located with
  an asset in your `assets/` tree, declaring what the asset replaces (and/or a
  published name) via `[[asset]]` rows. The no-code peer of the programmatic
  replacement surface; mirrors the [`targets.toml`](targets.md) idiom for code
  sites, applied to assets.
- **declared replacement** — making the engine serve your asset where it would
  have served a vanilla (or another mod's) asset, stated by an explicit
  `replaces` declaration in a `replaces.toml`. Always declared, never inferred
  from a path coincidence: a file with no declaration is referenceable but
  replaces nothing.
- **replacement target** — what a declaration replaces: a vanilla asset path, or
  (later phase) another mod's published name / its asset by the
  `replaces_plugin`+`replaces_path` pair.
- **loadable path** — the on-disk path `kcdx.assets.get_by_path` returns: the
  concrete file path the engine serves the asset from, ready to hand to a game
  asset API. You write a path *relative to your `assets/` folder*; the engine
  returns the loadable path. The opposite direction of the sidecar — the sidecar
  declares *what an asset replaces*; `get_by_path` reads *where an asset lives*.
- **published name** — a stable, dot-addressable name you give one of your
  assets with `kcdx.assets.declare(name, file)` (or a sidecar `name`), published
  as `<author>.<plugin>.<name>` so other mods can reference it as a contract. You
  type only the bare name; the engine adds your prefix. Resolve a published name
  back to a loadable path with `kcdx.assets.get_by_name` (your own) or
  `kcdx.plugin.<author>.<plugin>.assets.get_by_name` (another mod's).
- **runtime overlay** — an asset override registered in code at runtime with
  `kcdx.assets.register(vpath, file)` or `kcdx.assets.replace(target, with)`,
  rather than declared in a `replaces.toml` at load. It **takes effect
  thereafter** — it applies to opens *after* the call; an asset the game already
  opened is not re-resolved. The programmatic peer of the sidecar's load-time
  declaration, for a generated or conditionally-chosen asset.
