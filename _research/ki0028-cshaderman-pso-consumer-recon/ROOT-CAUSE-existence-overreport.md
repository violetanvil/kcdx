# KI-0028 ROOT CAUSE (CORRECTED 2026-06-22) — kcdx misses the level component paks under the `data/levels/` prefix

**Status:** root cause IDENTIFIED + dump-confirmed + log-pinned. The earlier "IsFileExist over-report vs vanilla" framing in this file was a RED HERRING — corrected below. Fix is a kcdx serve/resolution fix (no thunk-back). KI-0028 OPEN.

## TWO earlier framings, both FALSIFIED — read this first

1. **"kcdx over-reports existence vs vanilla" (the IsFileExist3 kcdx=1/vanilla=0 divergence) — RED HERRING.** Per the user's correct principle, kcdx IS the filesystem; vanilla's answer is NOT ground truth. AND the data shows kcdx SERVES those files perfectly: for `data/GameShaders/.../whanim.cfi`, after `IsFileExist3=1`, the engine opens `%engine%/shaders/cache/d3d12/whanim.cfib` and kcdx serves it `how=index-pak result=7`, `want==got`, clean. The existence "divergence" is harmless — the shaders flow fine. NOT the cause.

2. **"a downstream consumer of the exists-answer fails" — also wrong.** The shaders are opened AND served successfully. There is no failing shader consumer.

## THE ACTUAL ROOT CAUSE — a `data/`-prefix index/resolution miss on the LEVEL component paks

The engine aborts ("The level can't be loaded, exiting — kutnohorsko") because **kcdx returns MISS (`result=0`) for the level's component paks the engine requests under the `data/levels/kutnohorsko/` prefix**, so the engine finds the level's pak set NOWHERE and gives up.

### The decisive contrast (same log, `kcdx-dev_2026-06-22_18-21-45.log`)

- kcdx's enumeration FINDS the level dir: `enum FindFirst pattern="levels/*.*" matched=3 entries="klaster, kutnohorsko, trosecko"`.
- kcdx says the level pak EXISTS under the bare prefix: `IsFileExist3 vpath="levels/kutnohorsko/level.pak" how=original result=1`; `GetFileStat vpath="data\levels\kutnohorsko\level.pak" result=794343690` (a real stat).
- **BUT the engine then opens the level's COMPONENT paks under `data/levels/kutnohorsko/` and kcdx MISSES every one** — `how=miss-original result=0` for: `data/levels/kutnohorsko/*.pak`, `.../cestool.pak` + `cestool-part0..5.pak`, `.../hlod.pak` + `hlod-part0..5.pak`, `.../hlod_vegetation-part0..N.pak`, etc. (and the same misses under every Workshop-mod root + the `mods/` tree).

### The mechanism

The engine requests the level's component paks under the **`data/levels/kutnohorsko/<component>.pak`** shape. kcdx's index does NOT resolve that key → `how=miss-original` → it thunks the engine original → the original ALSO misses (the engine's own pak-dir doesn't have these mounted under that name at this point) → `result=0`. The engine exhausts every search root (base `data/`, all Workshop mod folders, `mods/`) for the level pak set, finds it nowhere, and calls the controlled `RaiseException(0xD2)` level-load-failure exit (dump `kcdx_..._18-21-45.dmp`, FAULTED_CULPRIT WHGame.DLL+0x23ACACA → KingdomCome.exe main — a deliberate abort, NOT an AV).

The level DOES exist on disk: `Data/Levels/kutnohorsko/` is present (the component paks are there). kcdx indexes/serves the level under the bare `levels/...` key (the enum + `level.pak` exists answer prove it has SOME view) but MISSES the `data/levels/...`-prefixed component-pak requests the engine actually uses to load the level. **This is a `data/`-prefix normalization/index-coverage gap on the level component paks** — the SAME CLASS as KI-0026 (a prefix/alias resolution gap, `gameshaders`→`shaders`), now on the `data/levels/` path.

### Why this is the kcdx defect (the user's principle)

kcdx IS the filesystem; the engine asks it "serve `data/levels/kutnohorsko/hlod.pak`" and kcdx must serve it (the file exists) — instead kcdx returns miss. kcdx is failing to serve the engine exactly what it needs. The fix is to make kcdx's index/resolution cover the level component paks under the `data/levels/` prefix the engine requests (NOT thunk-back — kcdx owns the serve).

## NOT a PROBE W artifact

"level can't be loaded" (`cant_load=1`) is in BOTH prior black runs (17-34, 16-39) with NO differential. The black screen WAS this level-load failure all along — the engine reached the menu's background level load, missed the level paks, and aborted (silent only because prior runs were killed before/around the popup). PROBE W's read-only differential made the investigation finally read the FS trace at the failure point; it did not cause it.

## The fix probe / read owed (AP17 — confirm before fixing)

- Read kcdx's index build (`asset_index` / the pak walk at seat) + `ResolveVPath`: does it index `Data/Levels/<level>/*.pak` at all, and under what key (`levels/...` vs `data/levels/...`)? The enum finding `levels/*.*` but the component-pak open missing `data/levels/...` says the index has the DIR but resolves the component-pak REQUEST shape (`data/levels/...`) to a miss.
- Confirm: is it a PREFIX mismatch (`data/` not folded to the index key) or a COVERAGE gap (the per-level component paks under `Data/Levels/<level>/` never indexed)? The fix differs: prefix-fold in `ResolveVPath` vs walk the level dirs at index build.
- The `how=miss-original result=0` (kcdx missed AND the original missed) is the tell: kcdx's index doesn't carry these, so it falls to the engine original which also can't resolve them at this point — exactly the KI-0026 "kcdx must own the resolution the engine can't do itself" shape.
