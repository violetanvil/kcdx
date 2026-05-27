# Address Library seed — coverage report

Companion to `address-library-seed.csv`. Phase 7 reconnaissance, 2026-05-19,
against `WHGame.dll` from `release_1_5_1164953_841`.

## Row count + breakdown

**22 rows total**, all targeting game version `1.5.1164953`:

| ID range | Count | Source category | Status |
|---|---|---|---|
| 1000–1003 | 4 | kcdx engine + yobson1 — function-entry RVAs from existing code | verified |
| 1004–1007 | 4 | kcdx + mempatch in-tree TOMLs — mid-function call sites used by shipping plugins | verified |
| 1008–1011 | 4 | muyuanjin gEnv resolver — static data anchors + string anchor | verified |
| 2000–2003 | 4 | Canonical CryEngine IConsole vtable slots — Phase 7 derives the RVAs at runtime | unverified |
| 3000–3005 | 6 | Documented vtable indices (IGame, IScriptSystem, IScriptTable) from muyuanjin/kcd2db | unverified |

**Verified rows: 12.** Each one is either ship-confirmed in production by a
predecessor (yobson1, muyuanjin) or live-verified by kcdx's own test suite
or mempatch's shipping plugin against KCD2 1.5.1164953.

**Unverified rows: 10.** These are real, useful entries — but they either
need a vtable-resolved RVA (the IConsole ones) or use a different schema
than `[[hook]]` / `[[patch]]` consume (the vtable-index rows). They're seeded
so the IDs are stable from day one even though the *values* land in a later
phase.

## Predecessor repos — what they contributed

### yobson1/kcd2lua — MIT, shipping plugin (commit `fbe0080`)

| ID | Contribution |
|---|---|
| 1000 (lua-pcall) | Sig copied directly. kcdx already ships this sig in `src/hooks.cpp`. |
| 1001 (cgame-update) | Sig copied directly. kcdx already ships this sig in `src/hooks.cpp`. |
| 1002 (luaL-loadfile) | Sig copied. NOT used by kcdx engine; bundled as a seed entry for future plugins. |

Attribution required when these ship: `yobson1/kcd2lua@fbe0080 (MIT)`.

### muyuanjin/kcd2db — MIT, shipping plugin (commit `22b3cd1`)

| ID | Contribution |
|---|---|
| 1008 (genv-pconsole-mov) | The 27-byte unique AOB derived from muyuanjin's pipeline. RVA reproduced live by `find_genv.py`. |
| 1009 (genv-pconsole-ptr) | Computed from id 1008 — the .data slot where gEnv->pConsole lives. |
| 1010 (genv-base) | id 1009 minus 0xA8. The actual gEnv static. |
| 1011 (anchor-exec-autoexec-cfg) | String anchor in .rdata used as the resolver seed. |
| 3000–3005 | Six vtable-index constants for IGame and IScriptSystem/IScriptTable, all documented in muyuanjin's source as live-verified against KCD2 1.4+. |

Attribution required when these ship: `muyuanjin/kcd2db@22b3cd1 (MIT)`.

### xiaoxiao921/ReturnOfModdingBase — MIT, generic CryEngine extender (commit `87c51f8`)

**Did not contribute KCD2-specific sigs.** The repo's purpose here is to be
the upstream for the four vendored files in `kcdx/src/rom_borrowed/`. Its
sample code in `src/main.cpp.test` targets Hades, not KCD2 — those AOBs do
not apply.

ReturnOfModdingBase remains documented in `_research/predecessor-sigs/` for
the upstream-port discipline required by `kcdx/CLAUDE.md` hard rule #5: any
future fix to the vendored RoM code has to be hand-ported from a future
commit of this repo.

## In-tree contributors

### kcdx engine (`kcdx/src/hooks.cpp`)

Two sigs in production:

- `lua_pcall` (id 1000) — used by the engine's `dynamic_hook` Lua surface +
  the Phase 5 scripting interface to dispatch Lua callbacks. Live-verified
  through Phase 5f.
- `update` (id 1001) — used to fire kcdx's lifecycle messages on
  first-update-tick. Live-verified by Phase 3.

### kcdx in-tree TOMLs (`kcdx/test-plugins/` + `kcdx/examples/`)

| ID | Plugins consuming the sig |
|---|---|
| 1003 (cgame-update-callee) | `test-plugins/cap-03-hook-lua-callback/` |
| 1004 (outfit-swap-aob) | `examples/outfit-swap-in-combat/`, `examples/outfit-swap-lua-gate/`, `examples/phase5f-lua-callback-test/`, `examples/conflict-test-incidental/` (mempatch), `examples/conflict-test-on-original/` (mempatch), `test-plugins/cap-01-patch/`, `test-plugins/scan-demo/` |
| 1005 (outfit-swap-context) | `examples/outfit-swap-in-combat/`, `test-plugins/cap-01-patch/`, `test-plugins/scan-demo/` |
| 1006 (isincombat-callsite-26b) | `examples/conflict-test-hook-on-hook/`, `examples/conflict-test-hook-on-patch/`, `examples/conflict-test-patch-on-hook/`, `examples/no-combat-state-hook/`, `test-plugins/comp-02-hook-on-patch/` |
| 1007 (isincombat-call-w-stack-frame) | `test-plugins/comp-03-hook-on-hook-A/`, `test-plugins/comp-03-hook-on-hook-B/` |

These five sigs cover **every static AOB used by every kcdx
test-plugin or example.** That's not a coincidence — the in-tree mods
were the most concrete answer to "what does the seed need to cover for
phase-7 ship?" Authors can replace `pattern = "..."` with `address_id =
1004` (etc.) in any of these mods after Phase 7 lands.

### mempatch in-tree TOMLs (`kcd2-mempatch/examples/` + `mempatch-plugins/`)

| ID | Plugins consuming the sig |
|---|---|
| 1004 (outfit-swap-aob) | `kcd2-mempatch/examples/outfit-swap-in-combat/`, `mempatch-plugins/outfit-swap-in-combat/` |
| 1005 (outfit-swap-context) | same as 1004 |

mempatch does not consume the Address Library directly today (it has its
own `Pattern`/`Resolve` machinery), but per kcdx hard rule #11, kcdx
accepts the full mempatch `[[patch]]` schema. Authors moving from
`mempatch.toml` to `kcdx.toml` should immediately get access to
`address_id = 1004` as the more-stable locator.

## What is NOT covered (the gaps)

The seed is intentionally lean and verified. These are sites a new
plugin author will likely want but the seed does not provide:

1. **IConsole vtable function RVAs (ids 2000–2003).** Recorded as
   unverified placeholders. The runtime resolution chain is:
   `gEnv (id 1010) → gEnv+0xA8 = pConsole_ptr (id 1009) → *pConsole_ptr =
   IConsole* → IConsole*->vtable[N] = AddCommand`. The actual RVA depends on
   where the vtable lives (it's a per-build constant but moves per game
   patch), and the vtable slot N is unconfirmed — see
   `console-command-abi.md`. Phase 7's `[[command]]` engine code will
   resolve these at runtime; the seed pins the IDs for future authors
   referencing them.

2. **Other engine subsystems.** No sigs for `pScriptSystem`,
   `pEntitySystem`, `pInput`, etc. These are accessible via gEnv's vtable
   walks at known offsets (documented in `external/cryengine/env.h`), but
   the seed only includes the slots a Phase 7 plugin would actually use
   (just pConsole). If Phase 8 brings new bindings (entity-system access,
   input remapping), more IDs land then.

3. **Game-logic functions not used by shipping mods.** No sigs for
   inventory, dialog, quest system, ECS components, save serialization
   internals. These will get added in Phase 6 (save/load) or by community
   contributions afterward. The seed represents "what's been
   reverse-engineered and shipped against," not "what's
   theoretically useful."

4. **Mid-function offsets for `[[mid_hook]]`.** Resolved 2026-05-19:
   `rva` columns store **pattern-hit positions**, and the TOML-level
   `offset` is applied after resolution (same as mempatch's existing
   `pattern + offset` semantic at `patch_engine.cpp:240`). `address_id`
   and `pattern` are drop-in substitutes; both resolve to the same kind
   of value and have the same relationship to `offset`. Full reasoning
   and contract statement in `id-assignment-policy.md` under "What an
   `rva` column stores: pattern-hit semantics."

   Consequence for the seed: id 1004 (RVA `0x56174C` = start of the
   16-byte AOB) is correct as stored; consumers writing the `mov r14b,
   al` site continue to declare `offset = 13`. Id 1006 (RVA `0x5605BC`
   = start of the 26-byte AOB) is correct as stored; consumers hooking
   the function entry continue to declare `offset = -4`.

5. **No anchors-only entries.** The seed records exactly one string anchor
   (id 1011 for `"exec autoexec.cfg"`). Most game strings don't survive
   localization-key interning at boot (workspace `CLAUDE.md` hard rule 5).
   This is by design — anchor-only entries without a concrete derivation
   pipeline aren't actionable.

## Honest assessment: shippable on day one?

**Mixed verdict — leaning yes for the core use case, no for the
"address_id is your survive-game-updates story" UX framing.**

**Yes, the core use case works:**

- Every patch + hook in every in-tree kcdx mod can be re-expressed using
  `address_id` instead of `pattern` after Phase 7 ships. That's 5 IDs
  covering 7 example mods + 8 test plugins. A new plugin author who copies
  one of these as a template gets address-ID-based locators by default.
- The `lua_pcall`, `update`, and `luaL_loadfile` IDs cover the three
  classic "hook a Lua VM thing" use cases on the first day.
- The gEnv/pConsole infrastructure (ids 1008–1011) gives plugins a real
  path to live IConsole / IScriptSystem / IGame access from C++.

**No, "use address_id, survive game updates" isn't quite real yet for
two reasons:**

1. **All 12 verified rows are pinned to game version 1.5.1164953.** The
   schema supports multi-version rows but the seed doesn't ship any. The
   first KCD2 update will require either (a) a kcdx maintainer to refresh
   every row (RVAs will shift) or (b) the verified rows revert to
   unverified until the community re-confirms. The contribution flow
   (see `id-assignment-policy.md`) needs to be alive AND well-documented
   before a real "update KCD2 → my plugin keeps working" claim is
   credible.

2. **The IConsole RVAs (ids 2000–2003) — the most-likely-wanted addresses
   for first-day adopters writing `[[command]]` blocks — are placeholders.**
   Their actual RVA is computable at runtime once `[[command]]` ships,
   but until then `address_id = 2000` returns 0 and the engine logs a
   "unknown/unverified ID" warning. Authors won't get a working command
   from address_id alone in v0.1.

**Recommendation:** ship Phase 7 with this seed but be explicit in
`docs/address-library.md` about its v0.1 state:

> "v0.1 ships with 22 IDs targeting KCD2 1.5.1164953. 12 of those are
> live-verified by shipping plugins and represent the full address surface
> any current in-tree mod actually uses. The remaining 10 are placeholders
> for IDs we expect to flesh out in Phase 7+ (IConsole vtable resolution)
> or future phases (vtable-hook schema, expanded engine surface).
>
> Use `address_id` only when status=verified for the running game version.
> Until v0.2 ships a community update workflow, expect address_id-resolved
> plugins to break on every KCD2 game update and need a kcdx release to
> refresh."

That's honest. It says "use this, but know the limitations." Better than
overpromising on the survive-game-updates story.

## Replication recipe

```pwsh
cd C:\Users\Michael\Documents\KCD2 Mods\_research\phase7-recon
py verify_seed_sigs.py        # uniqueness scan
py find_genv.py               # gEnv resolution
py find_extra_anchors.py      # extra string anchors
py extend_genv_sig.py         # pConsole-MOV unique-prefix derivation
```

All four scripts are self-contained, take an optional `WHGame.dll`
path argument (defaulting to the live game install), and emit
text suitable for diffing across game updates.
