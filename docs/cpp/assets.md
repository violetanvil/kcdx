# Asset replacement (↔ replaces.toml + kcdxAssetInterface)
> Part of the [kcdx C++ API](index.md).

The C++ view of asset replacement. Two halves, like the Lua surface:

## The no-code sidecar — language-neutral, LIVE

The `replaces.toml` sidecar is plain TOML co-located with your asset; it works
the same whether your plugin is Lua, C++, or both. A C++ plugin that ships
asset files declares replacements with the same `replaces.toml` the Lua surface
documents — there is no C++-specific declaration syntax for the no-code path.
See the [Lua asset-replacement page](../lua/assets.md) for the sidecar schema,
placement/scope rule, conflict resolution, and the loud-error behaviour; it
governs the sidecar for every plugin language.

In short: drop your file under `assets/`, drop a `replaces.toml` beside it with
`[[asset]] replaces = "<vanilla path>"`, and the engine serves your file where
the game would serve the vanilla asset. A file with no declaration is
referenceable but replaces nothing (existence is not replacement); a mistyped
target is a loud log error, never a silent no-op.

## The programmatic surface — `kcdxAssetInterface` (Built)

The C++ mirror of the programmatic Lua surface
(`kcdx.assets.get_by_path / get_by_name / declare / register / replace`) — for a
replacement decided in code rather than declared in a sidecar. Fetch it once via
`QueryInterface`, or reach it through the `Kcdx.h` wrapper's `K.assets`
accessor (the common path):

```cpp
// The common path — fetch every interface once via the Kcdx.h wrapper.
Kcdx K;
K.Init(api, "your_author", "your_plugin");      // K.assets is now live (or null
                                                // if the engine is too old)

const char* icon = K.assets->GetByPath(K.self, "icons/my_icon.dds");
if (icon) {
    // `icon` is the loadable on-disk path — hand it to a game asset API.
} else {
    // The dev log says exactly which path missed (category ASSET_GET).
}
```

### Return shape — the loadable path, or `nullptr`; the error teaches in the log

Every method returns `const char*`: the **resolved loadable path** on success
(the absolute on-disk path the engine opens to serve the file — the same value
the Lua peer returns), or **`nullptr` on failure**. The teaching error is
**logged** to the dev log (the `ASSET_GET` / `ASSET_RUNTIME` lines), NOT handed
back in code — the path is used directly; the error teaches via the log, the
C++ author's native channel (the same convention as
`kcdxConsoleInterface::GetCVar*` and `kcdxDeclareInterface`). A `nullptr` return
with a dev-log line is the loud failure — never a silent empty string.

The returned pointer is stable until your **next `kcdxAssetInterface` call on the
same thread** — copy it if you need it longer (the standard `const char*`-return
contract, the same as `kcdxConsoleInterface::GetArg`).

### The five methods

Each takes your own plugin handle as `self` (the engine resolves the calling
plugin from it — you never type your own `<author>.<plugin>` prefix).

```cpp
// Resolve YOUR OWN asset (relative to your assets/ folder) — a pure read.
const char* GetByPath(kcdxPluginHandle self, const char* path);
// e.g.  K.assets->GetByPath(K.self, "icons/my_icon.dds")  -> loadable path | nullptr

// Resolve a NAME you published (with Declare) -> its loadable path.
const char* GetByName(kcdxPluginHandle self, const char* name);

// Publish a stable NAME for one of your assets as a shared contract.
// Returns the declared file's loadable path (the same value a later
// GetByName(self, name) yields) so you can use it AND publish in one call.
const char* Declare(kcdxPluginHandle self, const char* name, const char* file);

// Make a not-at-load asset available at a runtime virtual path.
const char* Register(kcdxPluginHandle self, const char* vpath, const char* file);

// Register a runtime REPLACEMENT keyed by `target` — a vanilla asset path,
// OR a packed cross-mod published name "<author>.<plugin>.<bare>" (the
// string-key cross-plugin form; how a C++ author replaces another mod's
// asset — there is no navigable-namespace form on the C++ surface).
const char* Replace(kcdxPluginHandle self, const char* target, const char* file);
```

A complete, copy-paste-runnable plugin shell:

```cpp
#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

static Kcdx K;

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "your_author", "your_plugin")) return true;
    if (!K.assets) return true;                       // engine too old

    // Publish a name for your own asset, then resolve it back.
    const char* p = K.assets->Declare(K.self, "shirt", "male/shirt.dds");
    if (p) K.log.Info("ASSETS", "published shirt -> %s", p);

    // Replace a vanilla asset with one of yours (takes effect thereafter).
    K.assets->Replace(K.self, "Libs/UI/Textures/KCDLogo.dds", "ui/my_logo.dds");
    return true;
}
```

Same model as the Lua verbs, idiomatic in C++ (typed params instead of a Lua
table), append-only ABI (`kcdxAssetInterface_Version`). Both this surface and
the no-code `replaces.toml` path above are usable.

> **Take-effect = thereafter, and the boot-asset limit.** A runtime
> `Register`/`Replace` serves assets opened AFTER the call. An asset the engine
> opens and caches at boot (before your plugin loads) cannot be served by a
> runtime replace — use the declarative `replaces.toml` sidecar for it. kcdx
> logs a one-time teaching warn if you runtime-replace a boot-opened vpath
> (never a silent non-serve).

## Glossary

- **asset sidecar (`replaces.toml`)** — see the
  [Lua asset-replacement page](../lua/assets.md); the sidecar is language-neutral.
- **declared replacement** — serving your asset where a vanilla (or another
  mod's) asset would serve, stated by an explicit `replaces` declaration — never
  inferred from a path coincidence.
- **`kcdxAssetInterface`** — the C++ interface fronting the programmatic asset
  surface (the in-code peer of the language-neutral sidecar). Fetched via
  `QueryInterface(kcdxInterface_Assets, kcdxAssetInterface_Version)` or the
  `Kcdx.h` wrapper's `K.assets` accessor. Five methods, each returning the
  resolved loadable path (`const char*`) or `nullptr`.
- **loadable path** — the absolute on-disk path the asset-resolution seam opens
  to serve a file; what every `kcdxAssetInterface` method returns on success.
  Distinct from a *virtual path* (the path the game opens) — a loadable path
  points at a real file on disk.
- **published name** — a stable shared name (`<author>.<plugin>.<bare>`) for one
  of your assets, registered with `Declare`, that another mod resolves with
  `GetByName` or replaces by passing the packed name to `Replace`'s `target`.
