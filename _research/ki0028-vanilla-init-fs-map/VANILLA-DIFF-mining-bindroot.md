# KI-0028 — VANILLA_DIFF mining (PROBE W metadata half, bind-root run 2026-06-22 22:41)

**Verdict:** the metadata-EXISTENCE half of PROBE W already fired on the bind-root build (it's wired into
`metadata_slots.cpp` + deployed). 671 divergences, ALL one shape: `IsFileExist3 kcdx=1 vanilla=0` on
pak-resident vanilla files. NONE are geometry/mesh files. The ENUM differential + caller attribution (the
design's STRONGEST suspects) are NOT built — that is the genuinely-missing instrument.

## The 671 divergences — uniform shape, no numeric/enum diff

```
by slot:   IsFileExist3 = 671   (GetFileSize=0, IsFileExist2=0 — zero numeric/size divergences)
by ext:    cfi=294  cfx=287  xml=53  adb=28  gfx=6  bk2=2  dat=1
all:       kcdx=1  vanilla=0   (kcdx says a pak-resident file EXISTS; the engine original says it doesn't)
```

Every divergence is the SAME: kcdx answers EXISTS from its index for a pak-resident vanilla file, the engine's
own `IsFileExist3` answers 0 (it would not find it via its pak-dir at that point). Spread across shaders
(`.cfi`/`.cfx`), configs (`Scripts/AI/Factions.xml`, `Libs/MovementTransitions/*.xml`), animation
(`Animations/Mannequin/ADB/*`), UI (`.gfx`), video (`.bk2`).

## DO NOT over-read this (CORRECTION 1's warning holds)

`kcdx=1 vanilla=0` is NOT inherently a bug — kcdx IS the filesystem; vanilla's answer is not ground truth
(user's principle). The FIRST diverging file here, `data/GameShaders/HWScripts/CryFX/whanim.cfi`, is the EXACT
case `ROOT-CAUSE-existence-overreport.md` already proved HARMLESS (kcdx says exists, then SERVES it fine,
`want==got`). So "671 divergences = the bug" would be the over-read CORRECTION 1 forbids. An existence-EXISTS
divergence on a file kcdx then serves correctly steers nothing wrong.

## What this metadata-existence differential CANNOT answer (why it's insufficient)

- **No caller attribution.** The as-built `TraceVanillaDiff` (`boot_trace.h:117`) takes no `_ReturnAddress()`,
  so a divergence cannot be tied to the geometry/UI loader vs a harmless shader-include existence check. The
  671 lines name WHAT file, never WHICH engine subsystem asked.
- **No geometry-file divergence.** Zero `.cgf`/`.chr`/`.skin` mesh-geometry files in the 671 — the
  `draw_indexed=0` path's assets do not appear as existence divergences. So existence is not the geometry
  steering signal.
- **No ENUM differential.** `find_slots.cpp` / `enum_slots.cpp` have NO `TraceVanillaDiff` (confirmed by grep).
  The design's STRONGEST suspect (PROBE V outcome B) — does kcdx hand the geometry loader a different
  directory LISTING than vanilla (the unified-set vs engine-loose-only set) — is completely UNBUILT and is the
  one question this run could not answer.

## The genuinely-missing instrument (the build owed)

PROBE W's two unbuilt pieces, in priority order:
1. **The ENUM differential** — on a `ForEachFile`(14)/`FindFirst`(63) index-HIT, also run the original
   enumeration and log `count_kcdx` vs `count_original` + the set difference, with caller. This directly
   answers "does kcdx hand the geometry loader a different listing." (Design care: must NOT regress the
   table-DB glob path — assert in-log that table-DB prefixes still get the unified set, caller-scoped.)
2. **Caller attribution** — add `_ReturnAddress()` (module-relative) to `TraceVanillaDiff` so the existing
   existence diffs are tied to a subsystem. Cheap, and it sharpens the 671 immediately (which caller asks for
   the shader `.cfi` existence checks — the shader-system, already exonerated — vs anything on the geometry
   path).

## Provenance
- Probe (existing): `metadata_slots.cpp` DiffExist3 (slot 67) + IsFileExist2 (70) + GetFileSize (45), via
  `boot_trace.h:117 TraceVanillaDiff`. Log: `kcdx-dev_2026-06-22_22-41-11.log` 671 VANILLA_DIFF lines.
- Design: `_research/ki0028-cshaderman-pso-consumer-recon/HANDLE-STRADDLE-LEAD.md` §PROBE W.
