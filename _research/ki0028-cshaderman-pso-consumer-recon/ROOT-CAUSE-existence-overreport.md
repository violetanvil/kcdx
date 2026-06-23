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

## PINNED (2026-06-22, code + disk read) — it is a COVERAGE GAP: the index build never walks the per-level subdir paks

CONFIRMED it is a coverage gap, NOT a prefix mismatch:

- **kcdx's index build (`IndexPakRoot`, `asset_index.cpp:37`) enumerates only `<root>/*.pak` via `std::filesystem::directory_iterator` — SINGLE-LEVEL, non-recursive** (`asset_index.cpp:47`). It walks the 42 top-level `Data/*.pak` files and the `Engine/*.pak` files. It NEVER descends into subdirectories.
- **The level component paks live one level deeper: `Data/Levels/kutnohorsko/*.pak`** — 10 paks on disk (`cestool.pak`, `hlod.pak`, `hlod_vegetation.pak`, `level.pak`, `svo-part0..2.pak`, `terrain.pak`, `recast.pak`, `IPL_svo.pak`). These are exactly the paks the engine requested under `data/levels/kutnohorsko/` and kcdx missed.
- So the per-level component paks are **never indexed** → kcdx returns `how=miss-original result=0` for every `data/levels/<level>/<component>.pak` request → falls to the engine original (which also can't resolve them at this point) → the engine finds the level pak set nowhere → `RaiseException(0xD2)` level-load-fail exit.

**AP17 mechanism (falsifiable):** the wrong value = the un-indexed `Data/Levels/<level>/*.pak` component paks (absent from kcdx's index); who wrote it = `IndexPakRoot`'s single-level `directory_iterator` glob (`asset_index.cpp:37-69`); why inevitable = `directory_iterator` does not recurse, so any pak nested below `<root>/` (level paks, and potentially other nested vanilla pak trees) is structurally invisible to the index — kcdx then misses every request for them, and the takeover (which removed the engine's own ability to find them via the swapped vtable) has no fallback that resolves them.

The `data/gameshaders/` alias (`asset_index.cpp:227`) is a SEPARATE KI-0026 fix (a prefix REPLACE for shaders) — NOT this bug. The level paks need COVERAGE (walk the nested level dirs), not a prefix fold.

## The fix (kcdx owns the serve — the user's principle)

kcdx must index the level component paks so it serves them when the engine asks. The fix shape is a design decision (surfaced):
- **Recurse the pak walk** (`recursive_directory_iterator`) so ALL nested vanilla paks (level paks + any other nested pak tree) are indexed — broadest, most total-ownership-faithful, but picks up every nested pak everywhere (verify no unwanted/duplicate ingestion).
- **OR specifically walk `Data/Levels/<level>/*.pak`** (a targeted second pass over the known level-pak location) — narrower, but a named special-case rather than a general "index everything nested".

Under what key the nested paks are indexed must match the engine's request shape (`data/levels/<level>/<component>.pak` — the engine requests with the `data/` prefix here, per the trace), so the resolve path covers the exact request. KI-0028 OPEN; fix pending the design choice.
