# Address Library — ID assignment policy

Proposed contribution flow for the kcdx Address Library. Phase 7
reconnaissance, 2026-05-19. Intended to ship as `kcdx/docs/address-library.md`
after Phase 7 lands. Mirrors SKSE/CommonLibSSE's Address Library conventions
where workable, adapts where KCD2's situation differs.

## ID assignment

### Where does an ID number come from?

**Next-unused-integer**, partitioned by range. The repo ships
`data/address-library/seed.csv` (canonical source-of-truth). PR
authors claim the next free id in the relevant range and add a row;
the in-source mirror at `src/address_library.cpp::kEntries[]` is
updated in the same PR.

Suggested ranges:

| Range | Reserved for |
|---|---|
| 1000–1999 | kcdx core: engine-shared sites, lua VM, gEnv anchors, IConsole-via-RVA |
| 2000–2999 | IConsole vtable methods (one ID per method, even though the runtime call walks a single vtable) |
| 3000–3999 | Vtable-index constants (IGame, IScriptSystem, IScriptTable, IGameFramework, ...). These do NOT resolve to RVAs — they're integer offsets into a vtable. Documented separately. |
| 4000–4999 | Save/serialization (Phase 6) |
| 5000–5999 | Entity system, components, ECS |
| 6000–6999 | Inventory, dialog, quest, gameplay logic |
| 7000–7999 | Audio, physics, input subsystems |
| 8000–9999 | Reserved |
| 10000+ | Community contributions, allocated sequentially after the kcdx-curated ranges |

Why ranges instead of pure linear allocation: it lets a kcdx maintainer
review a PR claiming `id = 6042` and immediately know "this is gameplay-
logic territory, the right reviewer is whoever has been working on
inventory mods." Without ranges, IDs are opaque integers.

Why ranges and NOT name-hashing: SKSE went name-hashing-first and it
caused real friction (ambiguity when renaming, hash collisions, harder to
remember). Linear-with-ranges matches what F4SE/AE-DB does today and what
authors find easier to skim in a CSV.

### Can community submit IDs without an RVA?

**No.** Every ID submission must include AT LEAST ONE of:

1. An RVA (resolved by the contributor via Ghidra / pattern-scan / debugger).
2. An AOB pattern that uniquely matches in `.text` of the current
   `WHGame.dll`, plus the description of how to resolve it to an RVA at
   build time.

The kcdx-side build step will resolve every (id, game_version) row with
an AOB into a concrete RVA. If the AOB no longer matches, the build
fails — that's the early-warning that the row needs refreshing.

What's NOT acceptable: "I think this function does X, please find it for
me." That's a Ghidra request, not an Address Library submission. File it
as a discovery issue, not a PR.

## Status transitions

The CSV has a `status` column with these values:

| Status | Meaning |
|---|---|
| `verified` | The RVA has been live-verified against the listed `game_version` by either (a) a shipping plugin successfully calling/hooking it, or (b) a kcdx maintainer manually confirming the disassembly in Ghidra. |
| `unverified` | The RVA was derived (Ghidra'd, AOB-scanned) but no shipping plugin has yet called it. Authors can use the ID but should not assume the row is correct under all game updates. |
| `removed` | The function has been confirmed removed/inlined in this game_version. `ResolveAddress` returns 0 cleanly. Distinguishes "we know it's gone" from "we never had it." |

### unverified → verified

Two paths:

1. **Plugin-ship sign-off.** A real plugin (in-tree example or community
   release) successfully uses `address_id = N` in production against the
   listed `game_version`, with `kcdx-dev.log` confirming the resolution +
   call. The plugin's maintainer (or a kcdx maintainer) opens a PR
   flipping status to `verified` and citing the plugin + kcdx-dev.log
   line.

2. **Maintainer sign-off.** A kcdx maintainer cross-references the RVA in
   the Ghidra project (`third-party-ghidra/ghidra_project/KCD2/`),
   confirms the disassembly matches the documented signature, and signs
   off. Higher friction than (1) but available when no plugin uses the ID
   yet.

`verified` is **a per-game-version statement**, not a per-ID one. Row
`(id=1003, game_version=1.5.1164953, status=verified)` says nothing about
the same id under game_version=1.6.x.

### Adding a new game version

When KCD2 ships a new build, the workflow is:

1. A kcdx maintainer (or interested community member) re-runs each
   verification script (`verify_seed_sigs.py`, `find_genv.py`, etc.)
   against the new `WHGame.dll`.
2. For every existing verified-against-prior-version row: if the AOB
   still resolves uniquely, **add a new row** with the new
   `game_version` and status=`unverified` until a plugin or maintainer
   re-signs.
3. For every row whose AOB no longer resolves: open an issue tagged
   `address-library-refresh`, naming the row, the old game_version, and
   the new one. Do NOT silently overwrite.

**Important: never edit an existing row's `rva` or `name`. IDs are
stable across versions; the RVA lives in a per-version row.**

Rejected alternative: "one row per id with game_version history." That
puts version-tracking inside a single CSV cell and makes diffs
unreadable.

## Cadence for refresh

| Trigger | Action |
|---|---|
| KCD2 ships a new build | Within 1 week, a kcdx maintainer runs the verification scripts and either (a) opens PRs adding new-version rows or (b) opens a `refresh` issue listing what broke. The kcdx-side build will already be broken; this just makes the explanation visible to users. |
| Community member finds a new site | PR against `data/address-library/seed.csv` + `src/address_library.cpp::kEntries[]`. Reviewer checks AOB uniqueness in `.text` of the current build. Merge if uniqueness holds and the description is concrete enough to re-derive. |
| Shipping plugin starts using an `unverified` ID | PR flipping the status to `verified` (see "unverified → verified" above). |
| Maintainer cross-references an `unverified` row in Ghidra | Same — PR flipping status. |

**No automated refresh in v0.1.** Each game update is a human-curated
event. Phase 8 may revisit this and add a CI workflow that re-runs the
verification scripts against any uploaded `WHGame.dll` to generate the
diff for maintainer review. Not v0.1.

## Naming conventions

Format: snake_case for new submissions; CamelCase preserved when it
matches a canonical source-level identifier. The whole point of the
name is that a plugin author can write `kcdx.addr("lua_insert")` or
`kcdx.addr("CGame_Update")` and have it work without translation.

Rule of thumb: **the name should be exactly what you'd type into
Ghidra / a header file / a docs string to refer to this thing.** No
re-spelling, no lossy lowercase, no role suffixes invented after
the fact.

Examples used in the seed CSV:

- `lua_pcall` — Lua C API name, literal
- `luaL_loadfile` — Lua auxlib name, literal (keeps the `L` capitalization)
- `CGame_Update` — CryEngine class method, scope `::` replaced with `_`
- `IConsole_AddCommand` — CryEngine vtable method (CamelCase preserved)
- `IConsole_AddCommand_static_wrapper` — engine-side static helper that
  wraps the vtable call; the `_static_wrapper` suffix disambiguates
- `IConsole_AddCommand_script_overload` — the vtable[32] script-string
  variant (vs. the vtable[33] function-pointer form at id 2000)
- `CGame_per_frame_ui_pump` — descriptive role name for a callee of
  CGame::Update with no canonical engine name (stripped from binary)
- `gEnv_pConsole_mov_instruction` — the MOV instruction that loads
  gEnv->pConsole; NOT the pointer slot itself
- `gEnv_pConsole` — the .data slot holding the pConsole pointer
- `gEnv` — the .data slot holding the SSystemGlobalEnvironment struct
- `outfit_swap_callsite_aob` — domain-specific name for a mid-function
  patch site with no canonical engine name (kcdx outfit-swap patch)
- `IsInCombat_callsite_26b` — a specific 26-byte AOB at a callsite to
  the IsInCombat vtable method; not a function entry
- `string_exec_autoexec_cfg` — a string-literal anchor, not a function
- `IGame_CompleteInit_vtable_idx` — vtable INDEX constant (not RVA)

Internal-helper names DROP any prefix that isn't part of their actual
source name. They are NOT part of the LUA_API surface:

- `index2adr` — lapi.c static helper, no `lua_` prefix in source
- `close_state` — lstate.c static helper
- `f_luaopen` — lstate.c static helper (passed to luaD_rawrunprotected)
- `l_alloc` — CryEngine's default Lua allocator (single underscore prefix per source convention)

Guidance for new submissions:

1. **Match the source-level identifier exactly when one exists.** If
   the function is `IGame::CompleteInit` in muyuanjin's Ghidra
   naming or `CGame::Update` in yobson1's, name the row `IGame_CompleteInit`
   or `CGame_Update` — same CamelCase, `::` becomes `_`.
2. **For C-style names, use snake_case literally.** `lua_pcall`, not
   `lua-pcall` or `LuaPcall`.
3. **Preserve canonical capitalization.** `luaL_` (auxlib), `luaC_` (GC),
   `luaD_` (do.c), `luaF_` (functions), `luaG_` (debug.c errors),
   `luaH_` (tables), `luaM_` (memory), `luaO_` (object/lobject.c),
   `IConsole`, `IGame`, `CGame`, `CScriptSystem`, `gEnv`, `pConsole`.
4. **No invented role suffixes when the source has a real name.** If
   the function has a Ghidra/header name, use it. Add `_static_wrapper`
   / `_script_overload` / `_callsite_N` only when there's no canonical
   source-level identifier.
5. **Spell out abbreviations.** Use `register_function` not `regfn`.
6. **Subsystem vocabulary**: `lua`, `CGame`, `gEnv`, `IConsole`,
   `IScriptSystem`, `IGame`, `IGameFramework`, `physics`, `audio`,
   `input`, `entity_system`, `inventory`, `dialog`, `quest`, `save`,
   `serialization`. New top-level subsystems require a maintainer
   review.

## What the build does with the CSV

For each row with status `verified` or `unverified`:

1. If `rva` is present and non-empty: trust it directly. Validate against
   `WHGame.dll` only when the build is run with `--validate-address-library`
   (CI mode).
2. If `rva` is empty: error out at build time. v0.1 has no AOB→RVA
   resolution at the CSV level — the CSV ships pre-resolved RVAs.
3. The resulting RVAs are compiled into a sorted-by-id binary table
   embedded in `kcdx.asi`. `kcdxInterface::ResolveAddress` does a
   binary search at runtime.
4. The `runtimeGameVersion` field of `kcdxInterface` is checked against
   the row's `game_version` before resolution. If they don't match,
   `ResolveAddress` logs a warning and returns 0.

## What an `rva` column stores: pattern-hit semantics

**Resolved 2026-05-19 during seed audit.** Each row's `rva` stores the
**pattern-hit position** (the RVA of the first byte the locator matched
against), NOT a function-entry RVA. The TOML-level `offset` key on
`[[patch]]` / `[[hook]]` / `[[mid_hook]]` is then applied to produce the
final target address. This mirrors mempatch's existing
`patch_engine.cpp:240` semantic (`patchAddr = pattern_hit + offset`).

`address_id` and `pattern` are drop-in substitutes — both resolve to the
same kind of value (an RVA in the target module), and both have the same
relationship to `offset`:

```
target = address_id_resolved + offset      # if using Address Library
target = pattern_hit + offset               # if using AOB scan
```

**Why:** the same locator anchor is reused by different consumers with
different offsets. Concrete example from the v0.1 seed: id 1004 (the
16-byte outfit-swap AOB) is consumed by `mempatch-plugins/outfit-swap-in-combat`
with `offset = 13` (writes the `mov r14b, al` site), by
`conflict-test-incidental` with `offset = 0` (writes the first byte of
the pattern), and would be consumed by a function-entry hook with
`offset = -N` (where N is the prologue distance). If `rva` stored the
"function entry" RVA, the same author would have to know an opaque
correction factor to reach any other byte. Storing pattern-hit RVA is
the only choice that makes one Address Library row reusable across
patches with different write positions.

This is documented in id 1006's `notes` column (which calls out the
`offset = -4` convention used by every IsInCombat-entry consumer) for
visibility. The `[[hook]]` / `[[mid_hook]]` / `[[patch]]` schema docs in
`design.md` should add a one-line statement of this contract:

> `address_id` and `pattern` resolve to the same kind of value: a single
> RVA. `offset` is added afterward to produce the final target. An
> Address Library entry stores the RVA of the locator anchor, not the
> byte the consumer ultimately reads or writes.

The Phase 7 implementation should not pre-apply any offset stored in the
CSV — there is no offset stored in the CSV. The contract is "ID →
locator-anchor RVA" only.

## Authors must include in the PR description

For a new ID:

- The ID number claimed (matched against the latest `NEXT_AVAILABLE_IDS.md`).
- The `game_version` it's verified against.
- The RVA (hex, with `0x` prefix).
- The AOB or other locator pipeline that produced the RVA.
- One sentence on the function's purpose ("per-frame UI pump", "saves
  inventory data to disk", etc.).
- Whether it's `verified` or `unverified` at PR time.

For a verification-status flip:

- Which existing (id, game_version) row.
- The plugin or Ghidra evidence backing the flip.

## Rejected alternatives

1. **Name-hashing IDs** (SKSE-style): rejected. Higher friction at the
   PR review stage (you can't grep the CSV for "the address near IsInCombat
   should be `42` because we used `41`"), more brittle (renaming an ID
   becomes a hash-collision audit), unfamiliar (F4SE/AE-DB style is what
   most KCD2 modders will be coming from, indirectly).

2. **No ranges, pure-linear allocation**: rejected. Range-based makes PR
   review more tractable and gives community contributors implicit
   guidance about where their new ID belongs. Ranges aren't enforced by
   the engine; they're a curation convention only.

3. **Allow AOB-only entries (no RVA)**: rejected for v0.1. Means the build
   has to resolve AOBs at compile-time, which means the build has to
   ship-against a specific `WHGame.dll`. Adds substantial complexity and a
   licensing question (do we ship a hash of WHGame.dll in the build to
   validate?). v0.2 may revisit.

4. **Allow community-submitted IDs without verification**: rejected. The
   `unverified` status IS that path — the row is accepted, just labeled
   honestly. Authors can use unverified IDs at their own risk.

## Open questions for v0.2+

- **Aliases.** If a function gets a better name (e.g., `cgame-update-callee`
  → `cgame-update-ui-pump` because we now know what it does), do we
  rename the row or add an alias? Currently: rename, but document the
  rename in commit message + changelog. Aliases add metadata complexity.

- **Vtable-index rows (ids 3000–3005).** These don't have RVAs; they
  store integer constants. The current CSV schema accommodates them by
  leaving `rva` empty and putting the integer in `notes`, but that's
  hacky. v0.2 may split into a second CSV (`vtable-indices.csv`) or add
  a `kind` column distinguishing RVA from vtable-index.

- **CI bot that auto-PRs `address-library-refresh` after game updates.**
  Out of scope for v0.1. Eventually: a workflow that runs the
  `verify_seed_sigs.py` script against an artifact upload and posts the
  diff as a comment on the relevant tracking issue.

- **Removing IDs.** v0.1 says never. v0.2 may allow `removed` status to
  be promoted to actual row deletion if a function is gone for several
  consecutive game versions. Until then: keep the row, set status to
  `removed`, `ResolveAddress` returns 0.
