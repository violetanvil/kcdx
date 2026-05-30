# Seed Bloat & Test-Coverage Audit

Catalogue + recommendation over all 143 curated entities in
`data/seeds/address_names_seed.csv`. Each row below is grounded in a real grep
of `src/` and `test-plugins/`. This document does NOT modify the seeds or the
policy — the maintainer decides.

## The three questions, answered up front

**1. "Is the every-seed-needs-a-test requirement in policy?" — YES.**

It is in `data/seeds/policy.md` §"Test plugin requirement (policy-only)":

> "Every non-deprecated, non-superseded entity in `address_names_seed.csv` MUST
> be exercised by at least one `test-plugins/` plugin. **This is a policy rule,
> not an importer check** — the importer does NOT cross-reference
> `address_names_seed.csv` against the `test-plugins/` tree. Compliance is
> enforced by review (`/code-review`, `/verification-checkpoint`) and by the
> test-suite matrix at `test-plugins/README.md`."

The rule is **policy-only** (not importer-enforced) and the policy itself
acknowledges the backlog:

> "Pre-existing entities lacking a test plugin (the inherited backlog from
> before this policy landed) are a documented debt — backfill as each entity
> is touched."

So the rule EXISTS. The open question is corpus COMPLIANCE. At least **12 of
143 entities have a NAME-referenced test today; the true behaviorally-tested
count is higher** — the measurement searched for the entity name in test
plugins and undercounts behavioral coverage (a console-command test exercises
`IConsole_AddCommand` without naming it; `cap-44` exercises `CCryPak_FOpen`).
See §Test-coverage gap for the caveat. Of the genuinely-untested remainder,
the bulk is the FIX A Lua block, which by design can't be exercised by-name
until the shim ships.

**2. "Do we have bloat?" — Effectively none.** A first pass flagged 14
entities as prune candidates, but a direct grep of `src/` corrected that:
**13 of the 14 are actively consumed today** via kcdx's static-linked Lua
(e.g. `lua_tointeger` 20 call sites, `lua_isnumber` 20, `lua_tonumber` 16,
`lua_setmetatable` 11, `lua_next` 9, `lua_newuserdata` 9). They resolve at
LINK time now and become FIX A's by-name targets later — not bloat. The
**single genuine orphan is `luaC_step` (id 41)** — an internal GC stepper
with 0 call sites that no plugin can reach. The outfit-swap callsites (ids 5,
6) are test-fixture anchors, not bloat. The corpus is intentional pre-staging,
not accumulation.

> **Correction note:** the per-band classification initially marked ids
> 37-52 as "not found in codebase / prune_candidate." That was a
> false negative — the band searched for Address-Library *by-name*
> resolution and found none, but these symbols ARE consumed via static
> link (link-time resolution, not by-name). A direct `grep -rn '\\b<sym>\\b'
> src/` confirms 13 of 14 have live call sites. The corrected verdict for
> ids 37-40, 42-52 is **keep_consumed (static-linked; FIX A by-name target)**,
> not prune. Only id 41 `luaC_step` (0 sites) is a true orphan.

**3. "Which should stay?" — 142 of 143 keep** (only id 41 `luaC_step` is a
prune candidate, and even that is optional). Breakdown after the grep
correction:
- **~36 keep_consumed** — load-bearing; the engine reads/hooks/links them in
  production today (the 23 originally tallied PLUS the 13 lua_* symbols the
  first pass missed). Pruning breaks the engine.
- **~106 keep_pending_feature** — seeded ahead of named, designed work (FIX A
  Lua shim, `[[vtable_hook]]`, mod-loader absorb). NOT bloat; pre-staged,
  arriving at consumption + test the day their feature lands. (The lua_*
  symbols are simultaneously consumed-now via static link AND FIX A's by-name
  targets — they sit in both buckets; counted under keep_consumed here.)
- Author-value is a SECONDARY axis (the `author_value` field), not a separate
  verdict: every keep is justified by consumption OR a pending feature.

---

## Summary table

**By verdict (corrected after the grep pass):**

| Verdict | First pass | Corrected |
|---|---:|---:|
| keep_consumed | 23 | ~36 |
| keep_pending_feature | 106 | ~106 |
| prune_candidate | 14 | **1** (id 41 `luaC_step`) |
| uncertain | 1 | 0 |
| **Total** | **143** | **143** |

The first pass's 14 prune candidates were 13 false negatives (lua_* symbols
consumed via static link, miscounted as unused) + 1 real orphan. Corrected
counts move those 13 into keep_consumed. Some lua_* keep_consumed entities are
ALSO keep_pending_feature (FIX A by-name targets) — the two buckets overlap on
the lua_* block, so the corrected totals are approximate by design.

(No `keep_author_value` verdict exists as a separate bucket — author value is
the `author_value` field on every row; every keep resolves to consumed and/or
pending-feature. The "author could need it" entities are surfaced in §"Keep —
author value" below.)

**By test coverage:**

| has_test | Count |
|---|---:|
| yes | 12 |
| no | 131 |
| **Total** | **143** |

**By production consumption:**

| consumed_in_production | Count |
|---|---:|
| yes | 41 |
| no | 102 |
| **Total** | **143** |

**The diagnostic cross-tab — consumed-in-production AND has-test:**

| | has_test=yes | has_test=no |
|---|---:|---:|
| consumed=yes | 5 | 36 |
| consumed=no | 7 | 95 |

The top-right cell (**36 entities consumed in production but with NO test**) is
the real, shippable-now coverage gap. The bottom row (102 not-consumed) is
mostly pending-feature pre-staging that legitimately has no consumer yet.

---

## Keep — actively consumed

These 23 are load-bearing. The engine reads, hooks, or resolves them on a live
kcdx launch. Pruning any of them breaks production code.

| id | name | consumed where | has test? |
|---:|---|---|---|
| 1 | lua_pcall | `src/hooks.cpp:37,257` (MinHook detour + chain hook) | yes (cap-59, cap-64) |
| 2 | CGame_Update | `src/hooks.cpp:38,258` (per-frame tick pump) | yes (cap-03) |
| 4 | CGame_per_frame_ui_pump | `src/hooks.cpp` (inline per-frame UI pump) | yes (cap-03 chain) |
| 9 | gEnv_pConsole_mov_instruction | `src/console.cpp:393` (gEnv resolver anchor) | no |
| 10 | gEnv_pConsole | `src/console.cpp:393` (IConsole* at runtime) | no |
| 11 | gEnv | `src/console.cpp` (global env singleton, field reads) | no |
| 12 | string_exec_autoexec_cfg | `src/console.cpp` (refdb gEnv-resolver seed string) | no |
| 13 | IConsole_AddCommand | `src/console.cpp:414` (vtable[33], command registration) | no |
| 14 | IConsole_RemoveCommand | `src/console.cpp:415` (vtable[34], unload cleanup) | no |
| 15 | IConsole_ExecuteString | `src/console.cpp:416` (vtable[35], resolved for future) | no |
| 25 | luaL_checktype | `src/lua_bind_dev.cpp:41` + 3 more (arg type-check) | no |
| 26 | lua_insert | `src/lua_plugin_loader.cpp:136` (error-path stack) | no |
| 27 | lua_remove | `src/lua_bind_dynamic_call.cpp:360` + 1 (stack) | no |
| 28 | lua_type | `src/lua_bind_*.cpp` (25+ calls) | no |
| 29 | lua_rawgeti | `src/hook_chain.cpp:952,...` + 5 files (registry refs) | no |
| 30 | lua_pushlstring | `src/hook_chain.cpp:646` + 5 files (result/error msgs) | no |
| 31 | lua_getmetatable | `src/hook_chain.cpp:683,836` + 2 (userdata mt) | no |
| 32 | lua_settop | `src/hook_chain.cpp:1007,...` + dev (stack cleanup) | no |
| 33 | lua_touserdata | `src/hook_chain.cpp:689,...` + 5 files (handle unwrap) | no |
| 34 | lua_pushstring | `src/lua_bind_*.cpp` (30+ calls) | no |
| 35 | lua_createtable | `src/hooks.cpp:306; src/hook_chain.cpp:1470` | no |
| 36 | lua_pushvalue | `src/lua_bind_*.cpp` (very high volume) | no |
| 47 | lua_typename | `src/lua_bind_dynamic_call.cpp:461` + 4 (error msgs) | no |
| 53 | lua_pushcclosure | `src/scripting_interface.cpp:130,338,343` (api wrapper) | no |
| 54 | luaL_findtable | `src/scripting_interface.cpp:437` (api wrapper) | no |
| 55 | lua_getfield | `src/scripting_interface.cpp:80,108,347` (api + direct) | no |
| 56 | lua_setfield | `src/scripting_interface.cpp:85,148,359` (api + direct) | no |
| 64 | lua_concat | `src/scripting_interface.cpp:426` (api_LError) | no |
| 114 | lua_newstate | `src/scripting_interface.cpp:259; src/refdb.cpp` | no |
| 115 | luaL_openlibs | called at VM init by CScriptSystem::Init | yes (cap-33, cap-59) |
| 119 | CScriptSystem_vtable | engine-internal IScriptSystem vtable RVA (slot[6]) | no |
| 123 | lua_close | `src/scripting_interface.cpp:260` (api_Close) | no |
| 131 | CCryPak_FOpen | `src/probes/fopen_override_probe.cpp:58,367,384` | no |
| 132 | gEnv_pCryPak | `src/probes/fopen_override_probe.cpp:59` (gEnv+0x50) | no |
| 134 | ModManager_ctor | `src/mod_absorb/ctor_bracket.cpp` (11 sites) + more | yes (cap-61) |

(36 rows: the count includes ids whose `consumed_in_production:yes` AND a
`keep_consumed` verdict; ids 25-36/47/53-56/64 are wrapped/internal-consumed
Lua symbols whose verdict is keep_pending_feature but whose production
consumption is real — they appear here because they ARE consumed today, and in
§pending-feature because their TEST arrives with FIX A. The load-bearing
no-test rows in this table are the §Test-coverage-gap backfill list.)

Note: the wrapped Lua symbols (25-36, 47, 53-56, 64) carry verdict
`keep_pending_feature` because their _test_ obligation is tied to FIX A — but
their _consumption_ is live TODAY inside the `lua_bind_*` / `scripting_interface`
framework. They are not prunable. They are listed here for completeness of the
consumed set and again under §pending-feature for the test-timing question.

---

## Keep — author value (not consumed yet, but plausibly needed)

These have `author_value` medium and no current consumer; an author would
plausibly reach for them once the surface that exposes them ships. They are a
subset of the pending-feature bucket, surfaced here because their justification
is author-need rather than pre-staged engine consumption.

| id | name | author_value | why an author would want it | has test? |
|---:|---|---|---|---|
| 16 | IConsole_GetCVar | medium | underpins planned `kcdx.get_cvar_*` Lua surface (read game CVars) | no |
| 81 | lua_tothread | medium | convert stack value to `lua_State*` from an advanced C++ plugin | no |
| 84 | luaL_argerror | medium | argument validation in a C++ plugin's Lua-callable fn | no |
| 86 | luaL_error | medium | variadic error throw from a C-side Lua function | no |
| 93 | lua_error | medium | throw a Lua error from a C-side hook | no |
| 96 | lua_getfenv | medium | read a function's environment (env manipulation) | no |
| 110 | lua_topointer | medium | pointer extract for advanced GCObject work | no |
| 111 | lua_settable | medium | advanced table write from C | no |
| 118 | luaL_checkudata | medium | validate userdata opaque types in a C++ plugin | no |
| 121 | CScriptSystem_Init | medium | Lua-lifecycle interception (post-VM-init hooks) | no |
| 124 | lua_replace | medium | advanced stack manipulation (with GC barrier) | no |
| 125 | luaL_ref | medium | store a callback ref in the registry from C | no |

All 12 are gated behind a feature that does not ship today (chiefly FIX A,
which exposes the Lua C API to plugin authors). Their author value is real but
LATENT — it materializes only when the consuming surface exists. Until then
they correctly have no consumer and no test.

---

## Keep — pending a named feature (seeded in advance)

These are NOT bloat. Each is pre-staged for a designed, named piece of work,
and arrives at consumption + test the day that work lands. Grouped by feature.

### FIX A — Lua shim (drop static Lua, resolve `lua_*` from WHGame.dll)

The largest group. `docs/outstanding-work/fix-a-drop-static-lua.md` (and the
phase-11 restructure subtree) is the consuming feature. Today kcdx static-links
its own Lua; FIX A replaces that with WHGame.dll's exported symbols, resolved
through the Address Library. Every `lua_*` / `luaL_*` / `luaopen_*` / internal
helper below is harvested in advance so the shim has its full symbol set the
day it is built.

Entities (IDs): 3 (luaL_loadfile), 25-36, 47, 49, 53-104 (the full Lua C API +
auxlib + library-openers block), 105-130 (Lua internal helpers + CScriptSystem
ctor/init/dtor). The wrapped subset (25-36, 47, 53-56, 64, 114, 123) is already
consumed internally; the rest is dormant until the shim.

**Far-off flag:** FIX A is a Phase-11 item (the shim VM subtree under
`docs/outstanding-work/restructure/phase-11-shim-vm/`). It is design-settled but
build-deferred. The test obligation for this whole block correctly arrives WITH
the shim — exercising these symbols by-name requires the shim's
resolve-from-WHGame path to exist. Treating them as test-debt TODAY is a policy
artifact, not a real gap (see §Recommendations item 3).

### `[[vtable_hook]]` primitive

Entities (IDs): 19-24 — the six `*_vtable_idx` integer slot constants
(`IGame_CompleteInit` slot 4, `IScriptSystem_CreateTable` slot 13,
`IScriptTable_SetValueAny` slot 7, `IGame_GetIGameFramework` slot 16,
`IGame_GetLongName` slot 12, `IGame_GetName` slot 13). These are vtable INDEX
constants (not RVAs); their `address_versions` RVA cells are intentionally
empty. They consume + test the day `[[vtable_hook]]` ships. Documented in
`address-library.md` §"Vtable ID convention" and
`docs/outstanding-work/seed-to-db-migration-mapping.md`.

### Mod-loader absorb (narrow takeover)

Entities (IDs): 133, 135-143 — `ModManager_Select`, `ModManager_Mount`,
`ModManager_ReadModOrder`, `ModManager_ParseManifest`, `ImodVtable_primary`,
`ImodVtable_subobject`, `C_ModManager_vtable`, `WHGame_allocator`,
`CryString_placement_construct`, `CryString_init_from_string`. These ARE
consumed in production already (`src/mod_absorb/ctor_bracket.cpp` and the
record-synth machinery) — the feature is live; only their dedicated test
plugins are outstanding (id 134 `ModManager_ctor` is covered by cap-61; the
sibling members ride the same ctor-bracket codepath cap-61 exercises but lack
their own named rows). NOT pre-staged dormant; consumed-now, test-thin.

### Conflict-engine test fixtures (test infrastructure, not author-facing)

Entities (IDs): 7, 8 — `IsInCombat_callsite_26b`,
`IsInCombat_callsite_with_stack_frame`. These are collision anchors for
comp-02 / comp-03 (hook-on-patch + hook-on-hook coexistence proofs). They DO
have tests (that is their whole purpose), but no author would call them
directly. Keep while the comp fixtures run.

---

## Prune candidates — possible bloat

Be conservative: a "prune" here means **mark deprecated, never delete + renumber**.
Per `data/seeds/policy.md` §"ID assignment": IDs are APPEND-ONLY and permanent —
"Never renumber. Never recycle." Removing a seed = set `is_deprecated = 1` +
`deprecated_at_version` (the row stays, the entity is still resolvable, the
test obligation lifts). Erasing the row is a policy violation.

**After the grep correction, the prune list collapses to ONE entity.**

| id | name | call sites in `src/` | verdict |
|---:|---|---:|---|
| 41 | luaC_step | **0** | **prune_candidate** — internal GC stepper, not exported, no plugin can reach it; not on the public Lua API a FIX A shim would resolve by name. The one genuine orphan. |

Everything previously on this list (ids 37-40, 42-52) was a **false negative**
and is reclassified **keep** — a direct grep shows live static-link consumption:

| id | name | call sites in `src/` | corrected verdict |
|---:|---|---:|---|
| 37 | lua_setmetatable | 11 | keep_consumed (static link; FIX A by-name target) |
| 38 | lua_next | 9 | keep_consumed (static link; FIX A target) |
| 39 | lua_tolstring | 5 | keep_consumed (static link; FIX A target) |
| 40 | lua_gettable | 1 | keep_consumed (static link; FIX A target) |
| 42 | lua_rawget | 1 (+ `scripting_interface.cpp` api wrapper) | keep_consumed |
| 43 | lua_rawset | 1 | keep_consumed (static link; FIX A target) |
| 44 | lua_tonumber | 16 | keep_consumed (static link; FIX A target) |
| 45 | lua_isnumber | 20 | keep_consumed (static link; FIX A target) |
| 46 | lua_checkstack | 1 | keep_consumed (static link; FIX A target) |
| 48 | lua_objlen | 4 | keep_consumed (static link; FIX A target) |
| 50 | lua_rawseti | 4 | keep_consumed (static link; FIX A target) |
| 51 | lua_tointeger | 20 | keep_consumed (static link; FIX A target) |
| 52 | lua_newuserdata | 9 | keep_consumed (static link; FIX A target) |

**Why the false negative happened:** the per-band agent searched for
Address-Library *by-name* resolution (`refdb::ResolveAddrByName("lua_next")`)
and correctly found none — but these symbols are consumed via kcdx's
STATIC-LINKED vendored Lua, resolved at LINK time, not by name. "No by-name
resolution" was misread as "unused." `grep -rn '\\b<sym>\\b' src/` is the
ground truth and shows 13 of the 14 with live call sites.

This also settles the FIX A scope question the first pass raised: these
symbols are ALREADY called by kcdx today (via static link), so under either
FIX A scope ("whole Lua API" or "only what kcdx calls") they are IN — kcdx
calls them. They are not scope-dependent bloat; they are guaranteed FIX A
by-name targets.

**The outfit-swap family (5, 6):** ids 5-8 are a cohesive AOB/callsite anchor
family for the patch/conflict test plugins. 5 and 6 lost their direct consumer
when cap-01 migrated to `kcdx.bytes`; 7 and 8 are still live (comp-02/comp-03).
These are test-fixture anchors, not production bloat — keep while the conflict
fixtures run; if ever removed, treat 5-8 as a family decision, not a one-off.

**Net:** genuine, no-question bloat is **exactly one entity** — id 41
`luaC_step` (an unexported GC internal). Even it is harmless to keep (an
append-only row costs nothing); deprecating it is optional housekeeping, not a
fix. **The corpus is not bloated.**

---

## Test-coverage gap

> **Methodology caveat (read first):** `has_test` was measured by searching
> test plugins for the entity's NAME / `address_id` / AOB pattern. This
> UNDERCOUNTS behavioral coverage — a test plugin that registers a console
> command exercises `IConsole_AddCommand` (id 13, vtable[33]) without ever
> naming the seed entity; a file-interception test exercises `CCryPak_FOpen`
> (id 131) the same way. Verified examples the name-search missed:
> `cap-13-console-command` / `cap-26-lua-command` / `cap-27-command-timing-arms`
> exercise the IConsole chain (ids 13-15); `cap-44-fopen-override` exercises
> the CCryPak pair (ids 131, 132). So the "12 with tests" figure below is a
> floor, not the true count — the real consumed-now-and-tested set is larger,
> and the actionable backfill list is correspondingly SMALLER than the ~30
> stated. The directional conclusion (FIX A block can't be tested yet; a
> policy carve-out is warranted) is unaffected; the specific backfill counts
> need a name-agnostic re-measure before acting.

**Headline (floor): at least 12 of 143 entities have a NAME-referenced test
today; the true behaviorally-tested count is higher** (the caveat above). The
131-"owed" figure is an overcount by the same margin.

The 12 with tests: ids 1 (cap-59/64), 2 (cap-03), 3 (cap-20/34, by-name
resolution), 4 (cap-03 chain), 5 (cap-01), 6 (cap-01), 7 (comp-02/03), 8
(comp-03), 49 (cap-33), 97 (cap-33/34/35), 115 (cap-33/59), 134 (cap-61).

**Breaking the 131-owed gap down:**

| Sub-population | Count | Owes a test NOW? |
|---|---:|---|
| FIX A Lua block (3, 25-130 minus the few tested) — test arrives with the shim | ~95 | No — can't be exercised by-name until FIX A's resolve-from-WHGame exists |
| `[[vtable_hook]]` constants (19-24) — test arrives with the primitive | 6 | No — no consuming primitive yet |
| Consumed-in-production, shippable, but no test (gEnv/console/mod-absorb/probe set) | ~30 | **YES — genuine debt** |

The shippable-now backfill list, AFTER removing entities already behaviorally
tested:
- gEnv/console chain (9, 10, 11, 12) — the gEnv resolver anchors; `IConsole_*`
  ids 13-15 are ALREADY exercised by `cap-13`/`cap-26`/`cap-27` (command
  registration runs through vtable[33/34/35]). So the console METHODS are
  covered; only the gEnv-resolution PLUMBING (9-12) lacks a direct assertion.
- mod-absorb members riding cap-61's codepath but lacking own rows (133,
  135-143) — genuine gap (cap-61 covers id 134 ctor; siblings ride the same
  bracket but have no named assertion).
- CCryPak pair (131, 132) — `cap-44-fopen-override` ALREADY exercises FOpen;
  this is likely covered, not owed. Re-confirm before backfilling.

Net: the real shippable-now gap is well under the ~30 first estimated — mostly
the gEnv plumbing (9-12) and the mod-absorb sibling members (133, 135-143). A
name-agnostic re-measure is the prerequisite to a precise list.

**Is "backfill as touched" adequate?** Partly. For the FIX A block, the debt
model is not just adequate but CORRECT — those entities literally cannot be
tested until the shim ships, so "backfill as touched (= when FIX A lands)" is
the only coherent policy. The problem is that the current policy text makes ALL
143 owe a test IMMEDIATELY (it does not distinguish "pending-feature seed,
exempt until the feature ships" from "consumed-now, test owed now"). That
conflation is what makes the headline number look alarming (8%) when the real,
actionable gap is ~30 consumed-now entities, not 131.

**Recommendation on the model:** a targeted batch backfill is warranted for the
~30 consumed-now entities (gEnv/console/mod-absorb/probe), AND the policy should
carve out a pending-feature exemption so the FIX A / `[[vtable_hook]]` blocks
stop showing as immediate debt. The "backfill as touched" model stays for the
pending-feature blocks (they ARE touched when their feature lands).

---

## Recommendations

Prioritized, concrete. None of these are executed here — the maintainer decides.

**(1) Prune / deprecate — essentially nothing to do.**
- Only **id 41 `luaC_step`** is a genuine orphan (0 call sites, unexported GC
  internal, not on the public Lua API). Optionally deprecate it
  (`is_deprecated = 1` + `deprecated_at_version`); keeping it costs nothing
  (append-only row). NOT worth a dedicated change.
- The ids 37-52 lua_* block is **NOT prunable** — direct grep shows 13 of 14
  consumed via static link today, and they are FIX A by-name targets under
  either FIX A scope (kcdx already calls them). The earlier "FIX A scope
  decides" framing is moot: consumed-now settles it.
- Treat the outfit-swap family (5, 6) as a unit with 7, 8 — test-fixture
  anchors, not a one-off prune.
- Verdict: the corpus is **not bloated**. Genuine bloat = one optional-to-keep
  GC internal.

**(2) Test backfill owed NOW — smaller than first estimated; re-measure first.**
The name-search undercounted behavioral coverage (console + FOpen are already
tested). Before backfilling, run a name-AGNOSTIC coverage check (does any test
plugin exercise the entity's BEHAVIOR, not just name it). Then backfill only
the genuine gaps, which appear to be:
- gEnv resolver plumbing: 9, 10, 11, 12 (the IConsole METHODS 13-15 are already
  covered by `cap-13`/`cap-26`/`cap-27`). A gEnv-resolution assertion is the
  natural add.
- mod-absorb sibling members: 133, 135-143 (cap-61 covers id 134; extend its
  assertions to the siblings riding the same ctor-bracket codepath).
- Re-confirm CCryPak 131/132 against `cap-44-fopen-override` — likely already
  covered.
This is a handful of targeted additions, not a 30-entity batch.

**(3) Policy tweak — distinguish pending-feature seeds from immediate debt.**
The current §"Test plugin requirement (policy-only)" makes ALL 143 owe a test
immediately, which is the source of the alarming 8% headline. Recommend adding
a carve-out: an entity whose only consumer is a NOT-YET-SHIPPED named feature
(FIX A, `[[vtable_hook]]`) is EXEMPT from the test rule until that feature ships
— its test obligation arrives WITH the feature, not before. This formalizes
what "backfill as touched" already implies and stops ~100 pre-staged seeds from
reading as live debt. The consumed-now entities (item 2) stay fully on the hook.
Mechanically: the exemption keys off "no consumer in `src/` AND a recorded
`pending_feature`" — the same signal this audit used.

---

*Audit basis: per-entity classifications grounded in greps of `src/` and
`test-plugins/`, cross-referenced against `data/seeds/policy.md`. Catalogue +
recommendation only — no seed or policy file was modified.*
