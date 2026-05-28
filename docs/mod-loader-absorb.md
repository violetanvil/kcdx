# Mod-loader absorb — RE provenance + design

kcdx absorbs the KCD2 mod loader: **KCDx IS the mod loader, full stop.** The
native loader still constructs the manager and still mounts, but kcdx owns
WHICH mods load and in what ORDER — and kcdx plugins work dropped in EITHER
`kcdx-plugins/` or the vanilla `mods/` directory.

This file is the tracked home for the reverse-engineering provenance and the
settled design. The function RVAs live as append-only Address Library rows
3100–3104 in `data/address-library/seed.csv` + `src/address_library.cpp`; this
doc holds the record layout + the design + the live-probe evidence those rows
cite.

---

## The engine mod loader — `wh::C_ModManager`

CryEngine class `wh::C_ModManager` (source `code/CryEngine/CrySystem/Mods/
ModManager.cpp`), driven from `CSystem::Init` (`FUN_1807a6c64`, RVA 0x007A6C64).
Loading is TWO stages, both reached by direct `call` from `CSystem::Init`:

    FUN_1807a6c64  (CSystem::Init)
      ├─ 0x1807A76FE  call ModManager_ctor (id 3101)   = STAGE 1: SELECT
      │     └─ stores the manager into CSystem+0x2B30, then calls
      │        ModManager_Select (id 3100)
      └─ 0x1807A7F1C  call ModManager_Mount (id 3102)   = STAGE 2: MOUNT

Stage 1 (select) and stage 2 (mount) are separated by unrelated engine init.
The inner workers dispatch via member-fn-ptr `std::function`s, so Ghidra's xref
DB shows 0 refs — the direct call edges were pinned by a capstone E8/.pdata
sweep.

| Address Library | RVA | What |
|---|---|---|
| `ModManager_Select` (3100) | 0x00DA104C | Stage-1 SELECT driver — **the absorb hook target** |
| `ModManager_ctor` (3101) | 0x00DA0EB0 | 3-arg ctor; stores mgr at CSystem+0x2B30, calls SELECT |
| `ModManager_Mount` (3102) | 0x004D9058 | Stage-2 MOUNT driver; OpenPacks lambda over enabled list |
| `ModManager_ReadModOrder` (3103) | 0x00DA1294 | mod_order.txt reader; file line order == enabled order == mount order |
| `ModManager_ParseManifest` (3104) | 0x0243E7B8 | per-mod mod.manifest parse + version-gate; fills the I_Mod strings |

The enabled-mod list is at `C_ModManager+0x30`: a vector of 8-byte `I_Mod`
pointers (begin at +0x30, end at +0x38; `count = (end - begin) / 8`). The
scanned list is at +0x18..+0x20 (same 0x70-byte record stride). MOUNT iterates
the enabled list calling OpenPacks (pak-mgr vtable slot +0x48, the wildcard
plural mount) with `'<modPath>/*.pak'`, flags 0x10400.

---

## The I_Mod record layout — 0x70 bytes (live)

The enabled-list elements are pointers to 0x70-byte `I_Mod` objects. Layout
captured live against the running binary (the first enabled mod was the
Steam-Workshop mod "Inventory In Dialogue + Quicksave"):

| Offset | Field | Example value |
|---|---|---|
| +0x00 | vtable (WHGame image) | — |
| +0x08 | root path, **trailing `/`** | `E:\...\workshop\content\1771300\3728570527/` |
| +0x10 | mod **id / folder name** | `inventory_in_dialogue_quicksave` |
| +0x18 | vtable (2nd, WHGame image — sub-object / MI) | — |
| +0x20 | root path, **no trailing `/`** | `E:\...\workshop\content\1771300\3728570527` |
| +0x28 | **display name** | `Inventory In Dialogue + Quicksave` |
| +0x30 | **description** | `Opens the inventory/character menu ...` |
| +0x38 | **author** | `VioletAnvil` |
| +0x40 | **version** | `1.0.0` |
| +0x48 | **created/modified date** | `2026-05-18` |
| +0x50–0x6F | scalar/flag tail (zeroed for this record) | — |

The string fields are **CryString** values, not bare `char*`. Each field holds
a pointer to the char data, and IMMEDIATELY BEFORE those chars sits a fixed
16-byte header — `{ pad, nRefs, nLength, nAllocSize }`, four 4-byte words — so
the length word `nLength` lives at `data − 8` and the refcount `nRefs` at
`data − 12` (the `pad`/zero word is `data − 16`).
The engine READS `nLength` to size every copy it makes of the string. This is
load-bearing for record synthesis and was verified live against a real record:
a synthesized field that points at a bare char buffer with no header makes the
engine read whatever bytes precede the chars as `nLength`, and a garbage length
drives a multi-gigabyte allocation that fatally crashes the load. So record
synthesis MUST lay down the full CryString header (`nRefs = 1`, the correct
`nLength` / `nAllocSize`) ahead of each string buffer and point the record
field at the chars, exactly as the native records do.

These are the `mod.manifest` fields the engine parses at SELECT time
(`ModManager_ParseManifest`, id 3104). Load-bearing for record synthesis:
+0x08/+0x20 (path, with/without slash) and +0x10 (id); the rest is metadata
kcdx fills from a plugin's `kcdx.toml`. Every one of these fields is a
CryString and carries the header described above.

---

## Detour viability — the three closed gates (live)

A log-only, observe-only SELECT detour (since superseded by the production
`mod_absorb::InstallSelectDetour`, Step 4), wired at the `ModLoaderTakeoverArmed`
init phase, resolved every gate the narrow takeover rested on:

- **TIMING — ctx-B is in time.** The worker-thread detour FIRED
  (`select_fire`, worker tid) at `WHGame+0xDA104C` **before** `CSystem::Init`
  completed mod selection. The narrow takeover installs at the worker-thread
  phase-7 slot; it does NOT need before_game (ctx-A) timing.
- **LAYOUT — captured.** Enabled list = vector of 8-byte I_Mod pointers,
  `count=(end-begin)/8 = 15`; each record 0x70 bytes.
- **FIELD MAP — captured.** The table above.

### RESULT: naive mid-SELECT end-bump append is NOT accepted (crash)

Question: does the engine accept a kcdx-APPENDED enabled-list entry? Mechanism
tested: in the SELECT detour, after the original SELECT ran, duplicate-inject
the first already-enabled `I_Mod*` (same pointer, no clone) by writing it at
`*end` and bumping the `end` pointer by 8. One isolated variable.

Result (live, against the running binary):

- The inject SUCCEEDED structurally: `u7_inject ... cap_plausible=1
  spare_capacity=1 injected=1 count_before=15 count_after=16`. The `+0x40` word
  IS the end-of-storage pointer (begin 0x...36B0, end 0x...3728, cap 0x...3748 =
  0x20/4 slots spare) — **the {begin,end,end_of_storage} std::vector layout at
  +0x30/+0x38/+0x40 is CONFIRMED.** The append itself did not fault.
- The game then CRASHED ~1.1s later: `GUARD FAULTED code=ACCESS_VIOLATION
  module=WHGame.DLL module_rva=0x2440C85 thread=worker(16368)`. That RVA is
  **+0x19 into `FUN_182440c6c`** — the per-mod VERSION-GATE (sibling of
  `ModManager_ParseManifest` id 3104, RVA 0x2440C6C).
- `.ecxr` is decisive: `rsi=0x1B76A253728` = EXACTLY the inject's recorded
  `this+0x38_end`; `rbx=0x1B76A253730` = the slot past it; `rcx=0x0042005C00320065`
  = UTF-16 string bytes, NOT a pointer — the validation code dereferenced a
  record field as if it were a list element / `this`.

Meaning: **appending mid-SELECT by bumping `end` alone is unsafe.** The per-mod
validation/walk that runs after our detour returns control re-walks the range
and chokes on the modified list. This does NOT distinguish WHICH of two
mechanisms (a follow-up probe, not a guess to act on): (1) the consumer cached its
end/count before our append, so the bump desynced it; or (2) this consumer does
not iterate the list as a flat `I_Mod*[]` (different stride), so the duplicate
landed mid-record.

What this SETTLED: the vector layout (+0x30/+0x38/+0x40) is confirmed, the inject
timing lands, but the injection POINT matters — appending while still inside the
SELECT detour (before the native validation pass finishes) is rejected. This
POINTS AT the approved "let native SELECT FULLY finish, then re-order/append"
model — inject AFTER the whole SELECT (+ its per-mod validation) completes and
BEFORE MOUNT, not mid-SELECT. The follow-up tests injection at that later
point (e.g. detour the ctor's return, or a point between SELECT-complete and
MOUNT), and/or rebuilding the vector wholesale rather than bumping `end`.

This is a probe RESULT, not a built capability — the diagnostic edit in
`src/probes/mod_loader_probe.cpp` was reverted per probe hygiene; this record is
its durable home.

### RESULT: from-scratch record survives MOUNT (rebuild-wholesale VIABLE)

Question (the gating unknown for the rebuild-wholesale design): does a
kcdx-ALLOCATED 0x70-byte I_Mod record with harvested vtables survive the native
MOUNT? Mechanism: detour SELECT, after the original runs (1) HARVEST the two
I_Mod vtable VAs from the first real record (read-only, RVA=VA−base), then (2)
build ONE record in kcdx's own static buffer (harvested vtables + string
pointers COPIED from the real record — isolating the allocation+vtable question
from string synthesis) and WHOLESALE-REPLACE the enabled list by repointing its
vector at a kcdx 1-element array, let native MOUNT run.

Result (live, against the running binary — clean, no crash, reached menu):

- **Gate 1 — vtable RVAs HARVESTED + resolve to code.** WHGame base
  0x7FF8FF470000; `vtable0_va=0x7FF903B1AF00` → **RVA 0x46AAF00**;
  `vtable1_va=0x7FF903B1AED8` → **RVA 0x46AAED8**. The vtable dump read OK,
  4 slots for both — both resolve to real function pointers. These are the
  concrete I_Mod-class vtables (the workshop-mod record's), the static fact the
  from-scratch feature sets at +0x00/+0x18. (Not yet a seed row — land when the
  feature graduates the probe; the concrete-class identity should be confirmed,
  since a different I_Mod subclass might use a different vtable.)
- **Gate 2 — from-scratch record MOUNTED, no crash.** The replace repointed
  the vector (orig_count=15 → new_count=1, engine vector → kcdx storage). The
  game log shows EXACTLY ONE `[Mod] Opening paks in ...\3728570527/data/*.pak`
  (our record's copied path), then `[Mod] Loading localization patches...`
  continued. ZERO `FAULTED` in the dev log; no crash zip. MOUNT + the
  localization pass accepted our kcdx-allocated record.

Meaning: **a kcdx-allocated I_Mod record with harvested vtables is accepted by
native MOUNT + downstream passes — rebuild-wholesale is VIABLE.** The earlier crash
was specifically about GROWING the live vector mid-validation; wholesale-REPLACE
(repoint at kcdx storage, done after the native validation pass already ran)
sidesteps it. This green-lights the decided design.

OPEN (deliberately not tested here, owned by the build): this probe COPIED string
pointers from a real record. The feature must SYNTHESIZE its own string buffers
(a kcdx-plugin has no native record to copy from) — a string-LIFETIME concern
(the buffers must outlive MOUNT + every downstream pass), not the vtable/
allocation question this settled. The build proves kcdx-owned strings separately.

This is a probe RESULT — the diagnostic edit was reverted per probe hygiene;
this record is its durable home.

### RESULT: single I_Mod concrete class; vtable RVAs verified + ASLR-stable

Question (the caveat from the previous probe): vtables were harvested from only
the FIRST record. Do all mods share one I_Mod concrete class (one vtable pair
kcdx sets on every synthesized record), or per-mod subclasses (kcdx must derive
the right vtable)? Read-only loop over ALL enabled records dumping +0x00/+0x18.

Result (live, against the running binary — read-only, all 15 vanilla mods loaded
normally, no mutation):

- **ALL 15 records share the IDENTICAL vtable pair**: `vtable0` RVA **0x46AAF00**,
  `vtable1` RVA **0x46AAED8** (every idx 0..14). Single I_Mod concrete class —
  the synthesis module sets ONE pair on every record it builds.
- **ASLR-stable across boots**: this boot's WHGame base 0x7FF8FF000000 →
  `0x7FF9036AAF00 − base = 0x46AAF00` and `0x7FF9036AAED8 − base = 0x46AAED8`,
  IDENTICAL to the earlier harvest's RVAs (its base 0x7FF8FF470000). The RVAs are
  the stable fact; they land as seed rows.
- Records are contiguous at 0x70 stride (0x...D30CD50, +0x70, +0x70, ...) —
  confirms the array layout.

Verified Address Library facts for the synthesis module (land as append-only
seed rows):
  - I_Mod primary vtable     = RVA 0x46AAF00 (ImodVtable_primary)
  - I_Mod sub-object vtable   = RVA 0x46AAED8 (ImodVtable_subobject)

All probing complete. Every fact the synthesis module needs is verified against
the binary: record layout, field map, from-scratch-survives-MOUNT, single-class
vtables + RVA-stability. The only remaining runtime unknown is kcdx-OWNED
string-buffer lifetime (the earlier deferred OPEN) — proven by step 1's build, not
a prior probe. The diagnostic edit was reverted per probe hygiene.

---

## Settled design (vision-preserving — approved + this session's audit)

**A kcdx plugin is a SUPERSET of a vanilla pak mod.**
The defining principle for what the rebuilt enabled list contains. A vanilla pak
mod and a kcdx plugin load the SAME content the SAME way; adding a `kcdx.toml` at
the mod root is purely ADDITIVE — it unlocks kcdx's extra capabilities and takes
nothing away. A mod author turns a vanilla pak mod into a kcdx plugin by dropping
in a `kcdx.toml`, and the pak content keeps loading exactly as before.

Therefore EVERY discovered mod — vanilla pak (`mod.manifest`, no `kcdx.toml`) OR
kcdx plugin (`kcdx.toml` at the content root) — gets a synthesized native I_Mod
record pointed at its folder, so its `Data/*.pak` mounts and its
localization/table-patch/mod.cfg passes run IDENTICALLY via the native MOUNT (the
mount is path-driven, not identity-driven — it never checks vanilla-vs-plugin). A
kcdx plugin ADDITIONALLY runs its `kcdx.toml`/`plugin.lua`/DLL through kcdx's own
loader for the extra capabilities. So:

- `kcdx.toml` presence gates the EXTRA kcdx behavior layer, NOT the asset/pak
  path. The asset path is uniform for every mod.
- A pak-less pure-Lua/DLL plugin still gets a native record (pointed at its
  folder); the native MOUNT finds no `*.pak` and opens nothing — harmless and
  uniform, no special-casing.
- The classification (content-bearing root has `kcdx.toml` → plugin; else →
  vanilla) decides the BEHAVIOR path, not whether content loads.

This is why `cap-05-paklua-runtime` (a kcdx plugin that ships a `Data/*.pak` +
a `mod.manifest`) loads its pak exactly like a vanilla mod would.

**Takeover depth = NARROW.** Detour ONLY the SELECT phase (3100). kcdx rebuilds
the enabled I_Mod list (at `C_ModManager+0x30`) in ITS order, then lets the
engine's own MOUNT (3102) + localization + table-patch + mod.cfg passes run
verbatim over kcdx's list. kcdx owns WHICH/ORDER; the engine still mounts +
runs every downstream pass, so kcdx cannot silently drop
localization/table-patches (the full-takeover risk).

**SELECT relation = REBUILD WHOLESALE.** The
original "let native run then append" model was REVISED after a probe proved
that appending to the live enabled-list vector mid-SELECT crashes the engine's
own per-mod validation pass (it re-walks the modified range and faults). The
decided model: kcdx OWNS the enabled list. kcdx builds the entire list itself
(both `kcdx-plugins/` records and `mods/` pak-mod records) in kcdx's unified
order, and version-gates pak mods itself (below). The native MOUNT (3102) +
downstream localization/table-patch/mod.cfg passes still run verbatim over
kcdx's list, so those are not dropped — kcdx replaces only SELECTION, not MOUNT.

**Record creation = BUILD FROM SCRATCH.** kcdx
allocates + populates each 0x70-byte I_Mod record itself: the two vtable
pointers (+0x00/+0x18), the string fields (path +0x08/+0x20, id +0x10, name
+0x28, etc. per the field map), and the zeroed scalar tail. The gating unknown
was resolved by a probe: the correct I_Mod concrete-class vtable RVAs were not
initially statically resolved (the layout capture recorded runtime VAs of one
workshop mod's record, not the static class vtable), and whether a from-scratch
record survives the native MOUNT was unverified — the same crash risk (a wrong
field/vtable crashes MOUNT the same opaque way). The probe resolved both before
the feature was built: read-only HARVEST the vtable RVAs from a real SELECT-built
record (read +0x00/+0x18 − WHGame base → static RVAs → seed rows), then build ONE
from-scratch record with those vtables + a test `mod.manifest` and wholesale-replace
the list with it, let MOUNT run. Mounts + no crash → from-scratch construction
viable; crash → the record needs more than the string map.

**Version gate for pak mods = kcdx-owned, shared with the plugin path.**
Because kcdx suppresses the native SELECT (and thus
the native version-gate `FUN_182440c6c`), kcdx parses each pak mod's
`mod.manifest` itself and runs the SAME compatibility decision as the plugin
path: it parses the `<supports>` version-pattern list and string-prefix-wildcard
matches it against `g_runtimeGameVersionString` (wh_sys_version), graceful-degrade
if unknown. ONE kcdx-owned version policy for BOTH plugins and pak mods —
consistent author UX. The shared decision lives in `version_compat`
(`DecideGameVersionCompatString`); the pak-mod entry `DecideModCompat` and the
plugin entry `ValidateManifest` both call it.

`mod.manifest` is XML: `<kcd_mod><info>` with `<name>`, `<description>`,
`<author>`, `<version>` (the MOD's own version, NOT a game-version restriction),
`<created_on>`, `<dependencies>`, and the modid. Step 2 parses these into a
`ModRecordInput`.

GAME-VERSION RESTRICTION — RESOLVED via the Warhorse wiki (KM-A-57 "Mod
Manifest"): the restriction element is **`<supports>`**, an
optional list of `<version>` entries inside `<kcd_mod>`:

```
<kcd_mod>
  <info>…</info>
  <supports>
    <version>1.5*</version>
    <version>1.6*</version>
  </supports>
</kcd_mod>
```

Semantics (wiki-verbatim): "If the current version of the game is not in this
list, the mod will be automatically disabled. The version is compared **as a
string** to the version in **`wh_sys_version`** (in `system.cfg`)." The trailing
`*` is a prefix wildcard — `1.5*` matches the runtime version string `1.5…`
(major versions ship without the minor number, so authors use `1.5*`, not
`1.5.*`). Absent `<supports>` → no restriction → enabled. (`<version>` directly
under `<info>` is the MOD's own version — distinct from a `<supports><version>`.)

## Version gate UNIFICATION — adopt the vanilla model for BOTH

The step-2 gate used the kcdx-plugin model (integer `compatible_game_versions`
exact-match). DECISION: **unify on the vanilla `<supports>` model for BOTH pak
mods AND kcdx plugins** — one author mental model, matching what KCD2 itself
documents + does. The kcdx-plugin integer-exact-match model is REPLACED.

The unified gate (one mechanism, both consumers):
- **Runtime version = a STRING**, read from `wh_sys_version` in
  `<game-root>/system.cfg` (the source the wiki names + the engine compares
  against). Captured at init as `kcdx::plugins::g_runtimeGameVersionString`
  alongside the existing integer `g_runtimeGameVersion` (kept for the Address
  Library's per-row `game_version` match, a separate concern). Absent/unreadable
  → graceful-degrade (load anyway with a WARN), mirroring the integer path.
- **The compare = string prefix-wildcard** (`version_compat`): a `supports`
  entry `X*` matches the runtime string iff it starts with `X`; an entry with no
  `*` is an exact string match; empty `supports` list → no restriction →
  compatible; runtime string unknown → UnknownGameVersion (load + WARN).
- **kcdx-plugin schema MIGRATES**: `[plugin] compatible_game_versions` (integer
  list) + `version_independent` → a vanilla-style `supports` string-wildcard list
  (absent/empty `supports` = version-independent, matching the pak-mod meaning —
  so the separate `version_independent` flag folds away). The old key is REMOVED
  outright (fix-forward, prerelease, no external consumers): parser + schema drop
  it, every in-repo `kcdx.toml` migrates, docs/rules move with it
  (no prescriptive survivor doc/rule left behind), an unknown old key warns
  loudly rather than being silently ignored.
- **Read mechanism = `system.cfg` text** (NOT `ICVar::GetString` — that vtable
  slot is unverified in the tree and the cvar getters are documented-but-unbuilt,
  so relying on the slot is unsafe). `system.cfg` is the wiki-named source; a few
  lines of text parsing, no RE.

This unified gate lands in three parts: (a) version-string
source + the unified string-wildcard compare in `version_compat`; (b) plugin
schema migration (`supports`, old key removed, TOMLs + docs moved); (c) pak-mod
`<supports>` parse wired into `mod_manifest` + the version-gate stub replaced;
tests grow on each. The native `FUN_182440c6c` gate no longer needs RE — the
wiki settled the schema, and kcdx owns the gate now.

**Load-order = SUBSUME.** kcdx reads `mod_order.txt` as the vanilla baseline
ordering INPUT each boot (a vanilla mod's initial priority derives from its
mod_order.txt line position, so absent any override the order is identical to
vanilla). kcdx-plugins/ + each plugin's `[load_order]` merge on top, producing
ONE order. A vanilla mod gets a synthesized `mods.<modid>`-style row.

**Order persistence = write back to BOTH files.** The resolved order is written
to `load_order.toml` (a synthesized `mods.<modid>` row per vanilla mod) AND back
to `mod_order.txt` (so the vanilla file + a future UI reorder stay in sync). A
write that no-ops on a degenerate input announces it — failing loud with a
structured signal rather than silently doing nothing.

**Classification = marker-file, both dirs scanned.** A dir is a kcdx PLUGIN if
it has `kcdx.toml`; a VANILLA mod if it has `mod.manifest`; both → kcdx plugin
(richer); neither → skip + WARN. BOTH `kcdx-plugins/` AND `mods/` are scanned
the same way → a kcdx plugin works dropped in EITHER dir (the stated goal).

**`docs/loader-architecture.md` rewrite (design-determined).** The line
rejecting a `mods/` folder ("collides with KCD2's pak folder") is SUPERSEDED —
kcdx OWNS the loader, so there is no collision. kcdx discovers from both
`kcdx-plugins/` and `mods/`.

---

## Build plan (the feature steps)

1. **Synthesis-viability probe.** Inject ONE cloned/synthesized I_Mod
   record into the enabled list, let native MOUNT run. Outcome map: paks mount
   + no crash → narrow synthesis works; crash/no-mount → reconsider full
   takeover.
2. **Seed rows + record-synthesis module** (`src/mod_absorb/`). Rows 3100–3104
   already landed (this synthesis); the module does the clone/repoint.
3. **Unified discovery** — scan both dirs, marker-file classify, synthesize
   `mods.<modid>` rows into the `load_order` model. **(BUILT — see "Step 3"
   below.)**
4. **SELECT detour → real takeover** — call original, then wholesale-REPLACE
   the enabled-list vector with kcdx's rebuilt list (a synthesized record per
   enabled mod, in the unified order). **(BUILT — see "Step 4" below.)**
5. **Order persistence** — write back to `load_order.toml` + `mod_order.txt`.
6. **Test plugin(s) + docs** — `cap-NN-mod-absorb`, `comp-NN-plugin-in-mods`,
   the loader-architecture doc rewrite, the absorb design section, and
   [`docs/load-order.md`](load-order.md)'s vanilla-row model.

---

## Step 3 — unified discovery + the pak-mod registry + the load-order fold

Step 3 makes kcdx DISCOVER vanilla pak mods and fold them into the one
load-order model, so a pak mod sits in the same ordered list as a kcdx plugin
and is reorderable/disableable the same way.

**Discovery is a separate pass.** The plugin discovery walk is untouched. A
second pass (`mod_absorb::Discover`) owns the `mod.manifest` marker-file
classification and runs over THREE discovery roots — `kcdx-plugins/`, the
vanilla `<game-root>/mods/`, and the Steam Workshop content root for KCD2
(`<Steam>/steamapps/workshop/content/1771300/`):

- A folder with a `kcdx.toml` is SKIPPED here — it is a kcdx plugin (even when
  dropped in `mods/`), and the plugin walk already claimed it. Not
  double-registered.
- A folder with a `mod.manifest` and no `kcdx.toml` is registered as a pak mod.
- Neither marker → recurse into it as a container.

**The Workshop walk is one level deep and never recurses.** Steam stores each
subscribed Workshop item exactly one directory below the content root, named by
its Workshop file id (e.g. `.../1771300/3728570527/`). The walk inspects each
immediate child and classifies it loudly:

- A subdir containing a `kcdx.toml` is REJECTED with a clear error — Workshop is
  a pak-mod distribution channel for this game, not a kcdx-plugin one. A kcdx
  plugin belongs under `kcdx-plugins/`, not the Workshop content root.
- A subdir containing a `mod.manifest` and no `kcdx.toml` registers as a pak mod
  with `fromWorkshop = true`. The Workshop file id (the folder name, e.g.
  `3728570527`) is used as the default `modId` when the `mod.manifest` does not
  declare a `<modid>`, so a Workshop pak mod always has a stable, namespaced id.
- A subdir with neither marker is REJECTED with a clear error naming the path —
  never silent-skipped. An unexpected Workshop entry is surfaced, not hidden.
- An empty, absent, or unreachable Workshop content root is a normal install
  state — a player without Steam, or with Steam but no KCD2 Workshop
  subscriptions, has nothing under that path. The walk logs an INFO line
  (`discover_workshop_skipped`) and proceeds; it is not an error.

Each registered pak mod becomes a `PakMod` record: its id (the manifest's
`<modid>` if present, else the folder name), its root path (with and without a
trailing slash, for the I_Mod record), the parsed `mod.manifest`, and its
position in `mod_order.txt`.

**`mod_order.txt` is the vanilla baseline ordering seed.** kcdx reads
`<game-root>/mods/mod_order.txt` (one mod id per line; file order is the
load/mount order; `#` comment lines and blank lines are stripped, entries
trimmed) into a modid→line-index map. Each pak mod's `modOrderIndex` is its line
position; a mod not listed in `mod_order.txt` gets `-1`. An absent `mod_order.txt`
is a normal first-run state, not an error — it logs an INFO line and pak mods
order by mod id.

**The fold into one load order.** Load-order resolution folds every registered
pak mod into the same resolved-order map as plugins, under a synthesized
`mods.<modid>` name. A pak mod defaults to the `after_game` zone at priority `0`
— an early `after_game` block, so the vanilla pak mods lead the author plugins
within `after_game`. Within that block the pak mods keep their `mod_order.txt`
relative order via a secondary ordering key: the load-order sort key gains an
`orderIndex` tiebreaker, applied after priority and before the name tiebreak.
A pak mod's `orderIndex` is its `mod_order.txt` line index; a mod not listed
(`-1`) sorts after the listed ones (then alphabetically by `mods.<modid>`).
Plugins are unaffected — every plugin carries the maximum `orderIndex`, so the
tiebreaker is a no-op among plugins and their relative order still breaks on
name exactly as before. A user `load_order.toml` row keyed `mods.<modid>`
overrides a pak mod's zone, priority, or enabled state — kcdx owns the resolved
order, and `mod_order.txt` is only the seed.

The `mods.<modid>` namespace never collides with a kcdx plugin name: a plugin
name is lowercase letters, digits, and underscores only, so it can never begin
`mods.`.

**Version gating runs once the game version is known.** kcdx suppresses the
native mod selection, so kcdx runs the `<supports>` compatibility decision
itself — the SAME policy kcdx plugins use, so the two paths cannot drift.
Discovery happens early (during the directory scan), where the runtime game
version string is not yet known, so discovery registers every pak mod
unconditionally. A separate later pass runs the version gate at the point the
runtime version string IS known: for each registered pak mod it makes the
compatibility decision and, on an incompatible mod, disables it through the
same enable/disable mechanism a user override and the capability gate use. The
disabled mod's `mods.<modid>` row then reports as disabled to every downstream
reader (including the eventual enabled-list build), with a clear log line naming
the mod and the running game version. A mod with no `<supports>` restriction, or
one matching the running version, stays enabled; a declared restriction that
can't be evaluated because the version is undetected degrades gracefully
(enabled, with a warning).

The downstream consumer — building the actual enabled I_Mod list from this
resolved, gated order and handing it to the native mount — is Step 4 below.

## Step 4 — the production SELECT-detour takeover (the keystone)

Step 4 makes kcdx OWN the engine's enabled mod list. It is the keystone: it
turns the resolved, gated order (Step 3) into the actual list the native MOUNT
walks.

**The enabled-list builder** (`src/mod_absorb/enabled_list_builder.{h,cpp}`,
`BuildEnabledList`) reads the resolved state — the pak-mod registry (Step 3) +
the plugin manifests — and produces the rebuilt enabled I_Mod\* list:

- One synthesized record (`record_synth::BuildRecord`, Step 1) per ENABLED
  discovered mod. The SUPERSET model: a vanilla pak mod and a kcdx plugin alike
  get a record pointed at their folder, so the path-driven native MOUNT loads
  their content identically. A pak-less plugin gets a record too — MOUNT finds
  no `*.pak` and opens nothing, which is harmless and uniform (no special-casing
  on pak presence).
- ENABLED is `IsPluginEnabled(name)`: a user-disabled mod, a version-rejected
  pak mod (the Step-3 version gate), and a zone-rejected plugin are all
  excluded. Pak mods key `mods.<modid>`; plugins key `[plugin].name`.
- ORDER is the one load-order sort key `(zone, priority, orderIndex, name)` —
  the SAME key the load-order surface resolves, so pak mods and plugins sort
  into one unified order.
- A mod whose record synthesis fails (a vtable does not resolve →
  `BuildRecord` returns null) is DROPPED from the list and logged loud — never
  inserted as a null pointer, which would crash MOUNT on the first virtual
  dispatch.

**Path normalization.** Each record's path fields are normalized to the native
record form: a backslash directory body with a trailing forward `/` for the
with-slash form (e.g. `E:\…\3728570527/`), no trailing separator for the
without-slash form. This matches the shape a native record carries and the
shape the native `OpenPacks('<path>/*.pak')` mount expects, regardless of how
the source path's separators were spelled.

**The SELECT detour** (`src/mod_absorb/select_detour.{h,cpp}`,
`InstallSelectDetour`) is the production takeover. It detours the SELECT
driver (`ModManager_Select`, Address Library id 3100). On fire it:

1. Calls the ORIGINAL SELECT first — which builds the native records AND runs
   the per-mod validation pass. The list must not be mutated before that
   completes; mutating it mid-validation crashes the engine's own walk.
2. Builds the rebuilt list (`BuildEnabledList`) and copies it into a
   kcdx-OWNED, process-lifetime array (a module-static `std::vector<void*>`).
   This array — and the records it points at — must outlive MOUNT and every
   downstream pass, so it is never built on the stack.
3. WHOLESALE-REPLACES the enabled-list vector: repoints begin / end /
   end-of-storage (`C_ModManager+0x30 / +0x38 / +0x40`) at the kcdx array. This
   is a full replace, not an append — repointing the vector at kcdx storage,
   AFTER the native validation pass already ran, is the mechanism the binary
   accepts.

The native MOUNT (id 3102) is NOT detoured — it runs verbatim over kcdx's
rebuilt list, mounting each record's `<path>/*.pak` and running the
localization / table-patch / mod.cfg passes per record. The mount is
path-driven, so it treats a synthesized kcdx record exactly like a native one.

**The takeover self-validates each record before the repoint.** Before kcdx
repoints the enabled-list vector at its rebuilt list, every synthesized record
is walked and asserted well-formed against the native invariants — both vtable
slots non-null and inside the WHGame image (the resolved I_Mod vtable pair), and
each of the eight CryString fields carrying a valid 16-byte header whose length
word matches the actual string (`nLength == strlen`, `nRefs >= 1`,
`nAllocSize >= nLength`). The per-field header read is fault-guarded so a wild
pointer fails the record rather than crashing the validator, and the string scan
is length-capped. A record that fails any invariant is DROPPED from the rebuilt
list (never repointed into the engine) and logged at error level, naming the
mod, the field, and the invariant — the same drop-and-log discipline a null
synthesis result already gets. This catches a malformed record loud at build
time instead of letting it surface as the opaque native fatal allocation during
MOUNT that a garbage CryString header length produces (the keystone crash class
the header layout above exists to avoid): the engine sizes every string copy
from that length word, so a garbage value drives a huge allocation that fatally
fails the load.

**Production, not a probe.** The detour install is NOT dev-mode-gated — it is
the feature, it runs every boot. Verbose per-record logging stays
dev-log-routed; the takeover summary (`N mods, M vanilla, K plugins, in kcdx
load order`) is an INFO line. An empty rebuilt list (every mod disabled,
version-rejected, or synthesis-failed) repoints the vector at a stable empty
sentinel (begin == end == cap) with a loud WARN — a real observable state, not
a silent no-op.

**Ordering.** By the time the detour fires (during `CSystem::Init`), the
resolved state is ready: discovery + `load_order::Resolve` ran in kcdx's own
`DllMain` (before the worker thread armed the detour), and the pak-mod version
gate ran on the worker thread at version-detection time (before the
takeover-armed phase). The detour only reads the resolved state — it never
re-discovers or re-resolves.

The kcdx plugin behavior layer (its `kcdx.toml` / `plugin.lua` / DLL) runs
separately through kcdx's own loader — Step 4 adds the native-record synthesis
on top, it does not change the plugin-load path.

## Step 5 — order persistence (write back to BOTH files)

Step 4 rebuilds the native enabled list in kcdx's resolved order, but that
order is recomputed from scratch each boot and is not yet visible or editable.
Step 5 makes it PERSIST + EDITABLE by writing kcdx's resolved order back to the
two files that describe it (`src/mod_absorb/order_persist.{h,cpp}`).

**`load_order.toml` — the kcdx-owned editable authority.** kcdx ADDS a
`[[plugin]]` row for any newly-discovered pak mod, keyed `mods.<modid>`, so the
user can see and edit it. The human mod name (from the mod's `mod.manifest`
`<name>`) is surfaced as a **trailing `#` comment** on the row — not a
`display_name` field — because the `load_order.toml` reader rejects any unknown
key (its recognized keys are `name` / `zone` / `priority` / `enabled`), so a
field the reader does not know would make the file fail its own parse; a comment
is the reader-tolerated way to carry the human name.

The write is a **merge that preserves user edits**, never a
regenerate-from-scratch. Every existing row — plugin AND pak-mod, including any
the user hand-edited, and rows for a mod the user has temporarily removed from
disk — is preserved verbatim. kcdx is **add-only**: it adds a row for a mod that
has none yet, and it never overwrites an existing row's `zone` / `priority` /
`enabled`. Those values ARE the user's authority — kcdx owns the order, but the
user's `load_order.toml` edits win.

**`mod_order.txt` — the vanilla order file, kept in sync.** kcdx writes the
resolved pak-mod order (every registered pak mod, in kcdx's resolved order, one
bare mod id per line, under a `# managed by kcdx` header) so the vanilla file
and a future reorder UI stay in sync. This is the order SEED, not the enable
list — enable/disable lives in `load_order.toml` — so a disabled mod keeps its
position here.

**Write-if-changed (idempotent).** Each writer serializes the merged/resolved
result, compares it to the bytes already on disk, and writes ONLY if they
differ. A steady-state boot — no new mod, no override change, no reorder —
writes nothing, so there is no timestamp churn and no fighting a manual edit.
Writing, re-reading, then writing again yields byte-identical output. The
serialization is hand-written rather than a full TOML round-trip, which both
preserves the file's leading guidance block and the per-row human-name comments
(a round-trip drops comments) and gives the byte-exact stability the idempotence
guarantee needs.

**Fail loud.** A write that FAILS (the file is unwritable, the path is missing)
logs an error naming the file and that the order was NOT persisted — so the user
knows their reorder will not survive the next boot. A write that is SKIPPED
because nothing changed logs at debug level, so even the skip is visible rather
than a silent no-op.

The persist runs at boot, after `load_order::Resolve` and the pak-mod version
gate have produced the final resolved state — and independent of whether the
SELECT-detour takeover fires, since persistence reflects the resolved order, not
the live repoint.
