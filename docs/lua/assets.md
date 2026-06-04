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

## Status

- **The no-code declarative path (this page) is LIVE** for declaring a vanilla
  replacement. The engine keys your declared target into the asset-resolution
  map and serves your file.
- **Referencing / replacing another mod's asset by published name or by the
  cross-mod pair** parses and validates today, but the cross-mod *resolution*
  (turning a published name into the other mod's file) lands with the
  published-name namespace in a later phase. Until then, a cross-mod row is
  recorded and reported; it does not yet serve.
- **Publishing a `name`** is parsed today but not yet resolvable from another
  mod — the published-name namespace for assets is a later phase.
- **The programmatic equivalents** — `kcdx.assets.replace` / `kcdx.assets.declare`
  / `kcdx.assets.register` (Lua) and their C++ mirrors, for replacement decided
  in code — are a later phase. See [planned.md](planned.md).

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
