# Phase 2 — author surface (namespace + Lua + C++)

The author-facing surface: the navigable `kcdx.plugin.<author>.<plugin>.*`
namespace (+ the stale-comment sweep it relies on), the `kcdx.assets.*` Lua verbs,
and the `kcdxAssetInterface` C++ mirror (full Lua↔C++ parity).

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design authority:
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [5 — navigable `kcdx.plugin.<author>.<plugin>.*` namespace (`__index` resolvers)](step-5-navigable-namespace.md) | NOT STARTED | — |
| [5b — stale-comment sweep (dotted `__index` resolution)](step-5b-stale-comment-sweep.md) | NOT STARTED | — |
| [6 — `kcdx.assets.*` Lua surface + docs](step-6-lua-surface.md) | NOT STARTED | — |
| [7 — `kcdxAssetInterface` C++ mirror + docs (parity)](step-7-cpp-mirror.md) | NOT STARTED | — |

## Verification gate (whole phase)

A Lua plugin resolves its own asset by path (`kcdx.assets.get_by_path`) and
another plugin's asset by the navigable namespace
(`kcdx.plugin.<a>.<p>.assets.get_by_name` / `.get_by_path`), each returning a
path a game API loads; a runtime `kcdx.assets.register` / `replace` takes effect;
the equivalent C++ plugin (`kcdxAssetInterface`) does the same (parity proven by a
C++-driven test); every verb has its `docs/lua/` + `docs/cpp/` entry. The
self-report rows pass on the user's launch; the cross-plugin resolution is read
from the dev log (`acceptance-signal.md`).
