# KI-0028 ROOT CAUSE — kcdx indexes pak entries by bare `pe.name`, dropping the pak's bind-root prefix

**Date:** 2026-06-22 · **Trust:** ground-truth-confirmed (level-pak central directories dumped this pass;
vanilla mount mechanism cited from prior recon). Mechanism-level, falsifiable.

## The decisive ground-truth facts (theory-independent, dumped this pass)

1. **Level-pak entries carry NO `Levels/<lvl>/` prefix in their central-directory keys.** Dumping
   `Data/Levels/kutnohorsko/*.pak` (Python `zipfile`, PKZIP format): `level.pak` keys are bare/top-level
   (`leveldata.xml`, `levelprototypes.xml`, `brushlist.txt`) or one-segment (`terrain/...`, `physics/...`,
   `layers/...`); `terrain.pak` → `terrain/...`; `cestool.pak` → `cestool/...`. Across ALL 10 kutnohorsko paks:
   **zero entries** prefixed `levels/` or `kutnohorsko/` (`top-segs` are content roots, never the level dir).

2. **`resourcelist.txt` / `auto_resourcelist.txt` exists NOWHERE** — not loose under `Data/Levels/kutnohorsko/`,
   not in any of its paks, not anywhere under `Data/Levels`. The loader's read of it legitimately MISSES in
   BOTH vanilla and kcdx (confirms the earlier "resourcelist misses are benign" fact). It is NOT the cause —
   the level-info record is populated from a DIFFERENT read (`leveldata.xml` / `LevelInfo.xml`, which IS in
   `level.pak` keyed bare `leveldata.xml`).

3. **Vanilla `OpenPack` (slot 7, `FUN_18193cb14`) auto-derives the pak's bind-root from its directory path**
   (`phase8.5-pak-resolver/front2-open-mount-archive.md:30,56` — "auto bind-root from path dir",
   `auto root = strrchr(path,'\\')`). Mounting `Data/Levels/kutnohorsko/level.pak` binds it at mount-root
   `Levels/kutnohorsko/` (the path dir minus the `Data/` data-root). A request `Levels/kutnohorsko/leveldata.xml`
   then strips the bind-root → `leveldata.xml` → resolves to the pak's internal entry.

4. **kcdx's `IndexPakRoot` keys every entry by the BARE `pe.name`** (`src/fs_takeover/asset_index.cpp:122`:
   `index[NormalizeVPath(pe.name)] = src`) and **never prepends the pak's bind-root**. `NormalizeVPath`
   (`asset_overlay.cpp:524`) is a pure case+slash fold — it adds no prefix. `FoldEngineAliasToIndexKey`
   (`asset_index.cpp:242`) folds only `%engine%/` and `data/gameshaders/` — no `Levels/<lvl>/` rule.

## Root cause (falsifiable mechanism)

Vanilla binds each pak at a mount-root auto-derived from the pak's directory path, so the engine's request
`Levels/kutnohorsko/leveldata.xml` resolves to the pak's internal entry `leveldata.xml`. **kcdx's index keys
every pak entry by the bare central-directory name and discards the bind-root.** So every
`Data/Levels/<lvl>/*.pak` entry is indexed under a key MISSING the `Levels/<lvl>/` prefix the engine requests
it by → kcdx returns a MISS for every level-resource open → the level-info loader (`CResourceList::Load`
@0x4dcb60 and/or the `leveldata.xml` reader) gets nothing → the current-level record `Game->[0x88]->[0x58]` /
name @`[+0xc8]` is never populated → `C_Game::CreateInstance`'s empty-record gate fires → `MessageBoxA` +
`RaiseException(0xD2)` → black screen.

The cause is NOT a wrong serve, NOT an enumeration drop, NOT the swap mechanism — it is an **index KEY
construction gap**: kcdx drops the mount-point prefix that vanilla's path-derived bind-root supplies. Consistent
with every settled fact (a served indexed byte is correct; the MISS is on a key kcdx never stored).

## This reconciles the recursive-walk "regression" (VANILLA-MAP edge #5)

The `directory_iterator`→`recursive_directory_iterator` change (asset_index.cpp:56) fixed pak DISCOVERY — kcdx
now PARSES the nested `Data/Levels/<lvl>/*.pak`. But discovering a pak and keying its entries correctly are
DIFFERENT: the walk indexed those entries under bare `pe.name` keys, still missing the `Levels/<lvl>/` prefix.
So the `18-37` recursive-walk run discovered the paks yet still missed every level resource under a full swap —
the abort returned. Discovery was necessary, not sufficient; the bind-root prefix is the remaining gap.

## The fix shape (for design — NOT yet built)

kcdx's index build must key each pak entry by `<bind-root>/<pe.name>`, where `<bind-root>` is the pak's
directory path relative to the data-root (vanilla's `strrchr(path,'\\')`-derived mount point), NOT bare
`pe.name`. E.g. `Data/Levels/kutnohorsko/level.pak`'s `leveldata.xml` → key `levels/kutnohorsko/leveldata.xml`.

Open design questions (surface to user):
- The bind-root derivation: is it ALWAYS `<pak-dir relative to data-root>` for every pak, or do top-level
  `Data/*.pak` (e.g. `Data/Tables.pak`) bind at root (bare keys) while only NESTED paks carry a prefix? The
  vanilla `OpenPack` body (`FUN_18193cb14`) auto-root rule must be read precisely to pin which paks get which
  bind-root — a checkable unknown (read the body), not a guess.
- Whether existing bare-keyed hits (the engine paks under `%engine%/`, the `data/gameshaders/` fold) must keep
  their current keys → the bind-root rule must reproduce them, not regress them.

## Confirmed this pass — bind-root rule pinned + collision-safety proven (2026-06-22)

Three ground-truth checks closed the open questions and confirmed the fix is correct + safe.

1. **The bind-root IS the pak's directory relative to the Data/Engine root** (`%engine%`-style data-root NOT
   part of the key). From `front2-open-mount-archive.md:70-78`: slot 7 `FUN_18193cb14` auto-derives the
   bind-root as `strrchr(path,'\\')` = the pak's directory path; the data-root (`this[0x31]`/`+0x188`) is a
   *recognized root* the leaf normalizer (`FUN_1804621bc`, FRONT-4 Stage 2) leaves un-prefixed. So a top-level
   `Data/*.pak` / `Engine/*.pak` binds at an EMPTY bind-root (bare keys, unchanged) and a nested
   `Data/Levels/<lvl>/*.pak` binds at `levels/<lvl>/`.

2. **The engine requests level-pak files WITH the `Levels/<lvl>/` prefix** (body-read, `_bodies.txt:18-115`):
   `CResourceList::Load` @ `0x4dcb60` builds `"Levels/" + <lvl>` (`0x4dcbb3`+`0x4628a0`), and the pathbuilder
   `0x4dd384` prepends that root to `auto_resourcelist.txt`/`resourcelist.txt` (`0x4dcd2e`/`0x4dcd54`). The
   engine asks for `Levels/<lvl>/<file>`; kcdx stored it bare → MISS. (`resourcelist.txt` itself is the benign
   red herring — it exists in NO pak, so its miss is benign in vanilla too; the file that MATTERS,
   `leveldata.xml`, IS in `level.pak` keyed bare, and is requested via the same `Levels/<lvl>/` path family.)

3. **The bind-root fix REDUCES cross-pak collisions and is collision-safe** (`_collision_check.txt`, full
   vanilla pak set — `collision_check.py`): keying by `<bind-root>/<name>` drops cross-pak collisions
   **448 → 182**. Bare keying TODAY silently masks 448 collisions via LAST-pak-wins (e.g. every level's
   `terrain/svo/*.idx` collides with the global `svo.pak`); the bind-root disambiguates 266 of them by mount
   point. The 182 residual are genuine vanilla duplicates (`ShaderCache.pak` ≡ `ShadersBin.pak` shader-cache
   entries) — vanilla resolves those by mount order too, so LAST-pak-wins stays the correct deterministic
   fallback. Engine paks keep their content-rooted keys (`config/…`, `shaders/cache/…`) because their
   top-level paks bind at the EMPTY root, so `%engine%/`/`data/gameshaders/` folds still land. `leveldata.xml`
   now keys as `levels/<lvl>/leveldata.xml` — exactly what the engine requests.

**Fix decision (user-approved 2026-06-22):** key each pak entry by `<pak-dir relative to its root>/<pe.name>`,
NormalizeVPath-folded. The data/gameshaders fold's `"This IS KI-0028"` comment in `asset_index.cpp` is a
PARTIAL prior finding (it fixed the shader-alias miss, a real but separate sub-case) — it is not the
level-load-abort cause; the bind-root gap is. Reconcile that comment when the fix lands.

## Still-unverified (does NOT block the fix)

- That `leveldata.xml` (vs another level-metadata read) is the specific file populating
  `[ILevelSystem+0x58]`/the name — narrowed but not body-proven (LOADER-TRACE §4). Does NOT block: every
  level-resource read fails the same bind-root gap, so fixing key construction fixes whichever file the record
  reads. The live boot is the falsifier — menu→level-load success confirms the record populates.
