# kcdx.alias (↔ kcdx.alias)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.alias`](../lua/alias.md) — declare a short,
plugin-scoped local handle for a long prefixed shared name, then use the short
handle anywhere a target name is expected. The alias only *adds* a handle; it
cannot shadow or displace resolution.

**Not yet implemented (NYI).** There is no alias interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. This is tracked parity debt
(`.claude/rules/docs-discipline.md` criterion 3), discharged when the
restructure plan's C++ parity phase ships it and it is verified callable. This
entry maps the planned shape so both surfaces describe the capability.

## Planned mirror shape (NYI)

A "do a thing" call → typed positional params (the C++ spelling of Lua's
positional `kcdx.alias(short, target)`):

```cpp
// PLANNED — NOT in Interfaces.h yet. Sketch of the mirror shape.
// kcdxTargetInterface::RegisterAlias(kcdxPluginHandle owner,
//                                    const char* shortName,
//                                    const char* target) -> bool
//   false on bad input (invalid short charset/length, empty target, or an
//   owner that resolves to no plugin — an alias must be scoped to a plugin).
```

The alias is scoped to `owner`, resolves only inside that plugin's space,
cannot shadow an engine name / another plugin's bare name / the reserved
`kcdx.` root, and is read only at the apply pass (launch-time only, zero runtime
cost) — same contract as the Lua surface.

This is the C++ mirror of [kcdx.alias](../lua/alias.md).
