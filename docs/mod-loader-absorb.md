# Mod-loader absorb (Phase 8.5) — RE provenance + design

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
Loading is TWO phases, both reached by direct `call` from `CSystem::Init`:

    FUN_1807a6c64  (CSystem::Init)
      ├─ 0x1807A76FE  call ModManager_ctor (id 3101)   = PHASE 1: SELECT
      │     └─ stores the manager into CSystem+0x2B30, then calls
      │        ModManager_Select (id 3100)
      └─ 0x1807A7F1C  call ModManager_Mount (id 3102)   = PHASE 2: MOUNT

Phase 1 (select) and phase 2 (mount) are separated by unrelated engine init.
The inner workers dispatch via member-fn-ptr `std::function`s, so Ghidra's xref
DB shows 0 refs — the direct call edges were pinned by a capstone E8/.pdata
sweep.

| Address Library | RVA | What |
|---|---|---|
| `ModManager_Select` (3100) | 0x00DA104C | Phase-1 SELECT driver — **the absorb hook target** |
| `ModManager_ctor` (3101) | 0x00DA0EB0 | 3-arg ctor; stores mgr at CSystem+0x2B30, calls SELECT |
| `ModManager_Mount` (3102) | 0x004D9058 | Phase-2 MOUNT driver; OpenPacks lambda over enabled list |
| `ModManager_ReadModOrder` (3103) | 0x00DA1294 | mod_order.txt reader; file line order == enabled order == mount order |
| `ModManager_ParseManifest` (3104) | 0x0243E7B8 | per-mod mod.manifest parse + version-gate; fills the I_Mod strings |

The enabled-mod list is at `C_ModManager+0x30`: a vector of 8-byte `I_Mod`
pointers (begin at +0x30, end at +0x38; `count = (end - begin) / 8`). The
scanned list is at +0x18..+0x20 (same 0x70-byte record stride). MOUNT iterates
the enabled list calling OpenPacks (pak-mgr vtable slot +0x48, the wildcard
plural mount) with `'<modPath>/*.pak'`, flags 0x10400.

---

## The I_Mod record layout — 0x70 bytes (PROBE U.6, live)

The enabled-list elements are pointers to 0x70-byte `I_Mod` objects. Layout
captured live (kcdx PROBE U.6 / U.6.3, 2026-05-26 boots; the first enabled mod
was the Steam-Workshop mod "Inventory In Dialogue + Quicksave"):

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

The string fields are raw `char*`/CryString-char buffers (chars at +0; the
first 0x18 bytes were dumped as hex to confirm storage shape). These are the
`mod.manifest` fields the engine parses at SELECT time (`ModManager_ParseManifest`,
id 3104). Load-bearing for record synthesis: +0x08/+0x20 (path, with/without
slash) and +0x10 (id); the rest is metadata kcdx fills from a plugin's
`kcdx.toml`.

---

## PROBE U.6 — the three closed gates (live, 2026-05-26)

A log-only, observe-only SELECT detour (`src/probes/mod_loader_probe.{h,cpp}`,
wired at the `ModLoaderTakeoverArmed` init phase) resolved every probe-first
gate the narrow takeover rested on:

- **U.6.1 TIMING — ctx-B is in time.** The worker-thread detour FIRED
  (`select_fire`, worker tid) at `WHGame+0xDA104C` **before** `CSystem::Init`
  completed mod selection. The narrow takeover installs at the worker-thread
  phase-7 slot; it does NOT need before_game / Phase-11 (ctx-A) timing.
- **U.6.2 LAYOUT — captured.** Enabled list = vector of 8-byte I_Mod pointers,
  `count=(end-begin)/8 = 15`; each record 0x70 bytes.
- **U.6.3 FIELD MAP — captured.** The table above.

### PROBE U.7 — RESULT: naive mid-SELECT end-bump append is NOT accepted (crash)

Question: does the engine accept a kcdx-APPENDED enabled-list entry? Mechanism
tested: in the SELECT detour, after the original SELECT ran, duplicate-inject
the first already-enabled `I_Mod*` (same pointer, no clone) by writing it at
`*end` and bumping the `end` pointer by 8. One isolated variable.

Result (live, 2026-05-27 08:47 boot):

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
and chokes on the modified list. U.7 does NOT distinguish WHICH of two
mechanisms (next probe, not a guess to act on): (1) the consumer cached its
end/count before our append, so the bump desynced it; or (2) this consumer does
not iterate the list as a flat `I_Mod*[]` (different stride), so the duplicate
landed mid-record.

What U.7 SETTLED: the vector layout (+0x30/+0x38/+0x40) is confirmed, the inject
timing lands, but the injection POINT matters — appending while still inside the
SELECT detour (before the native validation pass finishes) is rejected. This
POINTS AT the approved "let native SELECT FULLY finish, then re-order/append"
model — inject AFTER the whole SELECT (+ its per-mod validation) completes and
BEFORE MOUNT, not mid-SELECT. The next probe (U.8) tests injection at that later
point (e.g. detour the ctor's return, or a point between SELECT-complete and
MOUNT), and/or rebuilding the vector wholesale rather than bumping `end`.

This is a probe RESULT, not a built capability — the U.7 diagnostic edit in
`src/probes/mod_loader_probe.cpp` is reverted per probe hygiene; this record is
its durable home.

### PROBE U.8 — RESULT: from-scratch record survives MOUNT (rebuild-wholesale VIABLE)

Question (the gating unknown for the rebuild-wholesale design): does a
kcdx-ALLOCATED 0x70-byte I_Mod record with harvested vtables survive the native
MOUNT? Mechanism: detour SELECT, after the original runs (1) HARVEST the two
I_Mod vtable VAs from the first real record (read-only, RVA=VA−base), then (2)
build ONE record in kcdx's own static buffer (harvested vtables + string
pointers COPIED from the real record — isolating the allocation+vtable question
from string synthesis) and WHOLESALE-REPLACE the enabled list by repointing its
vector at a kcdx 1-element array, let native MOUNT run.

Result (live, 2026-05-27 09:02 boot — clean, no crash, reached menu):

- **Gate U.8.1 — vtable RVAs HARVESTED + resolve to code.** WHGame base
  0x7FF8FF470000; `vtable0_va=0x7FF903B1AF00` → **RVA 0x46AAF00**;
  `vtable1_va=0x7FF903B1AED8` → **RVA 0x46AAED8**. `u8_vtable_dump read_ok=true
  read_slots=4` for both — both resolve to real function pointers. These are the
  concrete I_Mod-class vtables (the workshop-mod record's), the static fact the
  from-scratch feature sets at +0x00/+0x18. (Not yet a seed row — land when the
  feature graduates the probe; the concrete-class identity should be confirmed,
  since a different I_Mod subclass might use a different vtable.)
- **Gate U.8.2 — from-scratch record MOUNTED, no crash.** `u8_replace` repointed
  the vector (orig_count=15 → new_count=1, engine vector → kcdx storage). The
  game log shows EXACTLY ONE `[Mod] Opening paks in ...\3728570527/data/*.pak`
  (our record's copied path), then `[Mod] Loading localization patches...`
  continued. ZERO `FAULTED` in the dev log; no crash zip. MOUNT + the
  localization pass accepted our kcdx-allocated record.

Meaning: **a kcdx-allocated I_Mod record with harvested vtables is accepted by
native MOUNT + downstream passes — rebuild-wholesale is VIABLE.** The U.7 crash
was specifically about GROWING the live vector mid-validation; wholesale-REPLACE
(repoint at kcdx storage, done after the native validation pass already ran)
sidesteps it. This green-lights the decided design.

OPEN (deliberately not tested by U.8, owned by the build): U.8 COPIED string
pointers from a real record. The feature must SYNTHESIZE its own string buffers
(a kcdx-plugin has no native record to copy from) — a string-LIFETIME concern
(the buffers must outlive MOUNT + every downstream pass), not the vtable/
allocation question U.8 settled. The build proves kcdx-owned strings separately.

This is a probe RESULT — the U.8 diagnostic edit is reverted per probe hygiene;
this record is its durable home.

### PROBE U.9 — RESULT: single I_Mod concrete class; vtable RVAs verified + ASLR-stable

Question (the U.8 caveat): U.8 harvested vtables from only the FIRST record. Do
all mods share one I_Mod concrete class (one vtable pair kcdx sets on every
synthesized record), or per-mod subclasses (kcdx must derive the right vtable)?
Read-only loop over ALL enabled records dumping +0x00/+0x18.

Result (live, 2026-05-27 09:07 boot — read-only, all 15 vanilla mods loaded
normally, no mutation):

- **ALL 15 records share the IDENTICAL vtable pair**: `vtable0` RVA **0x46AAF00**,
  `vtable1` RVA **0x46AAED8** (every idx 0..14). Single I_Mod concrete class —
  the synthesis module sets ONE pair on every record it builds.
- **ASLR-stable across boots**: this boot's WHGame base 0x7FF8FF000000 →
  `0x7FF9036AAF00 − base = 0x46AAF00` and `0x7FF9036AAED8 − base = 0x46AAED8`,
  IDENTICAL to U.8's harvested RVAs (U.8 base 0x7FF8FF470000). The RVAs are the
  stable fact; they land as seed rows.
- Records are contiguous at 0x70 stride (0x...D30CD50, +0x70, +0x70, ...) —
  confirms the array layout.

Verified Address Library facts for the synthesis module (land as append-only
seed rows):
  - I_Mod primary vtable     = RVA 0x46AAF00 (ImodVtable_primary)
  - I_Mod sub-object vtable   = RVA 0x46AAED8 (ImodVtable_subobject)

PROBE PHASE COMPLETE. Every fact the synthesis module needs is verified: record
layout (U.6), field map (U.6.3), from-scratch-survives-MOUNT (U.8), single-class
vtables + RVA-stability (U.9). The only remaining runtime unknown is kcdx-OWNED
string-buffer lifetime (U.8's deferred OPEN) — proven by step 1's build, not a
prior probe. The U.9 diagnostic edit is reverted per probe hygiene.

---

## Settled design (vision-preserving — approved + this session's audit)

**Takeover depth = NARROW.** Detour ONLY the SELECT phase (3100). kcdx rebuilds
the enabled I_Mod list (at `C_ModManager+0x30`) in ITS order, then lets the
engine's own MOUNT (3102) + localization + table-patch + mod.cfg passes run
verbatim over kcdx's list. kcdx owns WHICH/ORDER; the engine still mounts +
runs every downstream pass, so kcdx cannot silently drop
localization/table-patches (the full-takeover risk).

**SELECT relation = REBUILD WHOLESALE (revised after U.7, 2026-05-27).** The
original "let native run then append" model was REVISED after PROBE U.7 proved
that appending to the live enabled-list vector mid-SELECT crashes the engine's
own per-mod validation pass (it re-walks the modified range and faults). The
decided model: kcdx OWNS the enabled list. kcdx builds the entire list itself
(both `kcdx-plugins/` records and `mods/` pak-mod records) in kcdx's unified
order, and version-gates pak mods itself (below). The native MOUNT (3102) +
downstream localization/table-patch/mod.cfg passes still run verbatim over
kcdx's list, so those are not dropped — kcdx replaces only SELECTION, not MOUNT.

**Record creation = BUILD FROM SCRATCH (user-decided 2026-05-27).** kcdx
allocates + populates each 0x70-byte I_Mod record itself: the two vtable
pointers (+0x00/+0x18), the string fields (path +0x08/+0x20, id +0x10, name
+0x28, etc. per the field map), and the zeroed scalar tail. GATING UNKNOWN
(PROBE U.8): the correct I_Mod concrete-class vtable RVAs are NOT yet statically
resolved (U.6 captured runtime VAs of one workshop mod's record, not the static
class vtable), and whether a from-scratch record survives the native MOUNT is
unverified — the same U.7-class risk (a wrong field/vtable crashes MOUNT the
same opaque way). U.8 resolves both before the feature is built: read-only
HARVEST the vtable RVAs from a real SELECT-built record (read +0x00/+0x18 −
WHGame base → static RVAs → seed rows), then build ONE from-scratch record with
those vtables + a test `mod.manifest` and wholesale-replace the list with it,
let MOUNT run. Mounts + no crash → from-scratch construction viable; crash → the
record needs more than the string map.

**Version gate for pak mods = kcdx-owned, shared with the plugin path
(user-decided 2026-05-27).** Because kcdx suppresses the native SELECT (and thus
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
Manifest", re-cached 2026-05-27): the restriction element is **`<supports>`**, an
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

## Version gate UNIFICATION — adopt the vanilla model for BOTH (user-decided 2026-05-27)

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

This is the absorb feature's **sub-arc 2.5** (multi-commit): (a) version-string
source + the unified string-wildcard compare in `version_compat`; (b) plugin
schema migration (`supports`, old key removed, TOMLs + docs moved); (c) pak-mod
`<supports>` parse wired into `mod_manifest` + the step-2 graceful stub replaced;
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

1. **PROBE U.7 — synthesis viability.** Inject ONE cloned/synthesized I_Mod
   record into the enabled list, let native MOUNT run. Outcome map: paks mount
   + no crash → narrow synthesis works; crash/no-mount → reconsider full
   takeover.
2. **Seed rows + record-synthesis module** (`src/mod_absorb/`). Rows 3100–3104
   already landed (this synthesis); the module does the clone/repoint.
3. **Unified discovery** — scan both dirs, marker-file classify, synthesize
   `mods.<modid>` rows into the `load_order` model. **(BUILT — see "Step 3"
   below.)**
4. **SELECT detour → real takeover** — call original, append kcdx records,
   re-sort the enabled list by the unified key.
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
classification and runs over BOTH discovery roots (`kcdx-plugins/` and the
vanilla `<game-root>/mods/`):

- A folder with a `kcdx.toml` is SKIPPED here — it is a kcdx plugin (even when
  dropped in `mods/`), and the plugin walk already claimed it. Not
  double-registered.
- A folder with a `mod.manifest` and no `kcdx.toml` is registered as a pak mod.
- Neither marker → recurse into it as a container.

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
resolved, gated order and handing it to the native mount — is the next step.
