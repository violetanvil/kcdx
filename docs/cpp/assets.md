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

## The programmatic surface — `kcdxAssetInterface` (NYI)

**Not yet implemented (NYI).** There is no asset interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. This is tracked parity debt: both docs map a capability even
when only one is built, discharged when the C++ parity phase ships it and it is
verified callable.

The planned C++ mirror of the programmatic Lua surface
(`kcdx.assets.replace/declare/register/get_by_path/get_by_name`) — for a
replacement decided in code rather than declared in a sidecar — will be:

```cpp
// PLANNED — not callable today.
// K.assets->Replace(target, withFile);          // register a replacement in code
// K.assets->Declare(name, file);                // publish a name in code
// K.assets->Register(vpath, file);              // make a not-at-load asset available
// const char* p = K.assets->GetByPath(path);    // resolve your own asset -> loadable path
// const char* p = K.assets->GetByName(name);    // resolve your own published name -> path
```

Same model as the Lua verbs, idiomatic in C++ (an options/string pair instead of
a Lua table), append-only ABI when it ships. The no-code `replaces.toml` path
above is the part you can use today.

## Glossary

- **asset sidecar (`replaces.toml`)** — see the
  [Lua asset-replacement page](../lua/assets.md); the sidecar is language-neutral.
- **declared replacement** — serving your asset where a vanilla (or another
  mod's) asset would serve, stated by an explicit `replaces` declaration — never
  inferred from a path coincidence.
