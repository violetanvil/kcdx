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
`mod.manifest` `<version>`/game-version field itself and runs the SAME
compatibility decision as the plugin path (`plugin_loader.cpp::ValidateManifest`:
compare against `g_runtimeGameVersion`, graceful-degrade if unknown). ONE
kcdx-owned version policy for BOTH plugins and pak mods — consistent author UX.
Needs a small `mod.manifest` XML reader (the manifest fields are the same set
the native parser reads — name/description/author/version/created_on/modid).

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
