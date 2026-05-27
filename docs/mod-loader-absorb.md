# Mod-loader absorb (Phase 8.5) — RE provenance + design

kcdx absorbs the KCD2 mod loader: **KCDx IS the mod loader, full stop.** The
native loader still constructs the manager and still mounts, but kcdx owns
WHICH mods load and in what ORDER — and kcdx plugins work dropped in EITHER
`kcdx-plugins/` or the vanilla `mods/` directory.

This file is the tracked home for the reverse-engineering provenance and the
settled design. (It supersedes the working dossier formerly at
`_research/phase8.5-pak-resolver/FINDINGS.md`, which is now local-only /
gitignored. The function RVAs live as append-only Address Library rows
3100–3104 in `data/address-library/seed.csv` + `src/address_library.cpp`; this
doc holds the record layout + the design + the live-probe evidence those rows
cite.)

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
| `ModManager_Select` (3100) | 0x00DA104C | Phase-1 SELECT orchestrator — **the absorb hook target** |
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

`U.7` (synthesis viability — does the native MOUNT lambda + downstream passes
accept a kcdx-injected/cloned I_Mod record?) is the remaining probe, the first
step of the absorb build.

---

## Settled design (vision-preserving — approved + this session's audit)

**Takeover depth = NARROW.** Detour ONLY the SELECT phase (3100). kcdx rebuilds
the enabled I_Mod list (at `C_ModManager+0x30`) in ITS order, then lets the
engine's own MOUNT (3102) + localization + table-patch + mod.cfg passes run
verbatim over kcdx's list. kcdx owns WHICH/ORDER; the engine still mounts +
runs every downstream pass, so kcdx cannot silently drop
localization/table-patches (the full-takeover risk).

**SELECT relation = let native run, then re-order.** Call the original SELECT
first (it scans `mods/`, validates manifests via 3104, builds REAL I_Mod
records with real vtables, in the vanilla mod_order.txt baseline order — proven
by U.6). THEN kcdx appends `kcdx-plugins/` records + re-sorts the whole enabled
list by the unified `load_order::Effective` key. Reuses the engine's
manifest-parse + record construction (real vtables for free).

**Record creation = clone-a-real-record** where possible — an appended
`kcdx-plugins/` record clones a native record's vtable pointers (+0x00/+0x18)
and repoints the string fields to the plugin's path/id/name. Confirmed/refined
by U.7.

**Load-order = SUBSUME.** kcdx reads `mod_order.txt` as the vanilla baseline
ordering INPUT each boot (a vanilla mod's initial priority derives from its
mod_order.txt line position, so absent any override the order is identical to
vanilla). kcdx-plugins/ + each plugin's `[load_order]` merge on top, producing
ONE order. A vanilla mod gets a synthesized `mods.<modid>`-style row.

**Order persistence = write back to BOTH files.** The resolved order is written
to `load_order.toml` (a synthesized `mods.<modid>` row per vanilla mod) AND back
to `mod_order.txt` (so the vanilla file + a future UI reorder stay in sync). A
write that no-ops on a degenerate input announces it (AP14).

**Classification = marker-file, both dirs scanned.** A dir is a kcdx PLUGIN if
it has `kcdx.toml`; a VANILLA mod if it has `mod.manifest`; both → kcdx plugin
(richer); neither → skip + WARN. BOTH `kcdx-plugins/` AND `mods/` are scanned
the same way → a kcdx plugin works dropped in EITHER dir (the stated goal).

**`docs/loader-architecture.md` rewrite (design-determined).** The line
rejecting a `mods/` folder ("collides with KCD2's pak folder") is SUPERSEDED —
kcdx OWNS the loader, so there is no collision. kcdx discovers from both
`kcdx-plugins/` and `mods/`.

---

## Build plan (the `/feature` steps)

1. **PROBE U.7 — synthesis viability.** Inject ONE cloned/synthesized I_Mod
   record into the enabled list, let native MOUNT run. Outcome map: paks mount
   + no crash → narrow synthesis works; crash/no-mount → reconsider full
   takeover.
2. **Seed rows + record-synthesis module** (`src/mod_absorb/`). Rows 3100–3104
   already landed (this synthesis); the module does the clone/repoint.
3. **Unified discovery** — scan both dirs, marker-file classify, synthesize
   `mods.<modid>` rows into the `load_order` model.
4. **SELECT detour → real takeover** — call original, append kcdx records,
   re-sort the enabled list by the unified key.
5. **Order persistence** — write back to `load_order.toml` + `mod_order.txt`.
6. **Test plugin(s) + docs** — `cap-NN-mod-absorb`, `comp-NN-plugin-in-mods`,
   `loader-architecture.md` rewrite, `docs/design.md` absorb section,
   `docs/load-order.md` vanilla-row model.
