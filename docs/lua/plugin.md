# kcdx.plugin
> Part of the [kcdx Lua API](index.md).

Plugin introspection from inside Lua — query the engine's view of another
plugin without coupling to its internals. Today the one accessor here lets you
ask *"did the engine reject this plugin at load time?"* so your own plugin can
notice a missing dependency up front and degrade gracefully instead of falling
over on a hook that was never installed.

This is a grouped domain (`kcdx.plugin.*`), not a top-level verb — plugin
introspection is a query domain like [`kcdx.dev.*`](dev.md) /
[`kcdx.test.*`](test.md), not one of the closed-set core registration verbs
(see [the model](index.md#1-the-model)).

## `kcdx.plugin.is_rejected(name)`

Ask whether the named plugin was rejected by the [zone gate](#glossary) this
session — i.e. its declared [zone](index.md#2-glossary) made some
[`requireZone`](#glossary) API unreachable, so the engine refused to load it.

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** The full prefixed plugin name `"<author>.<plugin>"` — the two-component identity of the plugin you are asking about (e.g. `"redmoon.outfit"`). You do NOT pass a bare name here; this call is reaching into another plugin's identity, so it takes the explicit `<author>.<plugin>` form (see the [naming model](index.md#2-glossary)). |

**Returns:**

- `(true, reason_string)` — the plugin was rejected. `reason_string` is the
  same teaching message the engine logged when it rejected the plugin (it names
  the unreachable API and the zone the plugin would need to declare to reach
  it).
- `(false, nil)` — the plugin was not rejected. This covers three real cases,
  collapsed because the predicate doesn't need to distinguish them: the plugin
  loaded normally, the plugin was disabled by the user, or no plugin by that
  name is known. In every case there is no rejection on file.
- `(nil, err)` — bad call: `name` is missing, not a string, or empty. The
  error names the field and the call shape.

## Minimal snippet — degrade gracefully when a dependency was rejected

Realistic use case: your plugin extends another plugin's behaviour. If that
dependency was rejected by the zone gate, your registrations would silently do
nothing — so check up front and either skip the dependent work or fall back.

```lua
if kcdx.plugin.is_rejected("redmoon.outfit") then
    -- The dependency didn't load. Skip the integration cleanly and tell the
    -- user once, rather than installing hooks that target functions the
    -- missing plugin was supposed to set up.
    kcdx.log.warn("MYMOD",
        "redmoon.outfit was rejected at load time; skipping the outfit "
        .. "integration. Run with this plugin alone, or fix redmoon.outfit "
        .. "to enable the integration.")
    return
end

-- Dependency is live — proceed with the integration.
kcdx.hook{ target = "redmoon.outfit.open_inventory", before = function(...) ... end }
```

The C++ mirror of this surface is [`kcdxPluginInfoInterface`](../cpp/plugin.md)
(NYI).

---

## Glossary

- **zone gate** — the engine's plugin-init capability check. After
  [load order](index.md#2-glossary) resolves every plugin's zone, the gate
  cross-references each enabled plugin's declared zone against the engine's
  static capability table; if any `requireZone` API would be unreachable from
  that zone, the plugin is rejected (its `engineAccepted` flag flips false and
  it is skipped at every init site). The gate runs ONCE per session, BEFORE
  any plugin's `plugin.lua` or DLL `Load` runs — so no half-loaded state is
  possible. `kcdx.plugin.is_rejected` is the query into the gate's result map.

- **requireZone** — the engine's per-API zone capability annotation. Each
  `kcdx.*` API the engine ships carries a `requireZone` value declaring which
  load-order [zone](index.md#2-glossary) it can legally be called from
  (`Either` / `Before` / `After`). Today every shipped API is `Either` —
  deferred registration handles the "called early but work must happen later"
  cases — so the gate only ever rejects against a synthetic test entry; a
  future API requiring a specific zone would reject a plugin whose declared
  zone makes that API unreachable.
