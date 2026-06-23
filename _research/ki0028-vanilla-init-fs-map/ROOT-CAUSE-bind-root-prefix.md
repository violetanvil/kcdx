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

## Still-unverified (carried forward)

- The exact `OpenPack` auto-root rule (`FUN_18193cb14` body) — whether the bind-root is the full relative dir
  for all paks, or only nested ones, or whether some paks bind at root. PIN THIS before building the fix
  (it decides the key-construction rule for EVERY pak, not just level paks).
- That `leveldata.xml` (vs another read) is the specific file populating the record — narrowed but not
  body-proven (LOADER-TRACE edge). Does NOT block the fix: every level-resource read fails the same bind-root
  gap, so fixing the key construction fixes whichever file the record reads.
