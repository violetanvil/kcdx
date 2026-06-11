# P1 step 2 — `behavior_registry` + declare/get/list

**What.** The one runtime registry both tiers share, plus the three read-side Lua
verbs and declare validation. No `set` yet (step 3).

**Scope.** New unit `src/behavior_registry.{h,cpp}` (declares, values,
applied-flags; the unit's reference doc same change per
`.claude/rules/structure-by-responsibility.md` §6) + `src/lua_bind_behavior.cpp`
(thin binder): `kcdx.behavior.declare(name, spec)` with namespace stamping +
missing-required-field teaching error + duplicate-same-full-name error (second
declare errors, first stands); `get` (recorded value else `default`); `list`
(both-tier registry walk, prefix filter). Doc increments: `docs/lua/behavior.md`
(declare/get/list sections) + glossary terms.

**Test bar.** New cap plugin `test-plugins/cap-NN-behavior/`: declare registers
under the stamped name; missing-field fixture errors at the declare site;
duplicate-declare fixture errors the second + first still functions; `get`
returns default; `list()` + `list("<prefix>.")` filter — self-reporting rows per
`.claude/rules/test-suite.md`, runnable at this step.

**Dependencies.** Step 1 (the probe — registry design rests on no marked
assumption directly, but boundary shape informs the applied-flag model).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §4 (the
noun, validation, duplicate rule, rule-4 note), §11 (units).

**Disassembler-test / author-burden.** Declare/get/list take names + values
only; zero hex on any path.
