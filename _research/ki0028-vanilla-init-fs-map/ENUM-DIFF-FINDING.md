# KI-0028 — PROBE W enum-differential FINDING (gated build, run 2026-06-22 23:36)

**Verdict:** the enum differential FIRED and is decisive. EVERY engine directory enumeration kcdx serves
returns `count_vanilla=0` — the engine's OWN `FindFirst` (captured-original thunk over its pak vector) finds
NOTHING for these pak-resident dirs, while kcdx returns the full pak-index listing. kcdx hands every
content-discovery loader a DIFFERENT (non-empty) listing than the engine would. The strongest leads are the
scene-content enumerations: `Entities/*.ent` (+144), `Prefabs/*.xml` (+69).

## Cost fix confirmed (the regression is gone)

Gated once-per-(caller×kind) + lock-free seat-resolved WHGame bounds: VANILLA_DIFF 1425→31, enum_diverge
754→20, log 24.2MB→23.2MB, NO lag (user-confirmed responsive). The differential's diagnostic value is fully
preserved (every distinct caller still attributed) at near-zero steady cost.

## The 20 distinct enum divergences — ALL count_vanilla=0 (uniform shape)

| pattern | pak_added | caller_rva | class |
|---|---|---|---|
| `Entities/*.ent` | 144 | 0x11A9CB8 (18512120) | **SCENE CONTENT (entities)** |
| `Prefabs/*.xml` | 69 | 0x41D757 (4313815) | **SCENE CONTENT (prefabs)** |
| `Prefabs/*.*` | 69 | 0x41D9A0 (4314336) | **SCENE CONTENT (prefabs)** |
| `%ENGINE%/Shaders/Cache/D3D12//*.*` | 193 | 0x144C800 (21264576) | shader cache |
| `engineassets/textures/detailatlas/*.dds` | 31 | 0xAEA9C2 (11445762) | textures |
| `Libs/UI//UIElements/*.xml` | 28 | 0x41D087 (4311175) | UI |
| `Shaders/*.ext` | 23 | 0x171D80A (24225418) | shaders |
| `Animations/Mannequin/Preview/*.xml` | 20 | 0x12C747A (19665658) | anim |
| `Scripts/Utils\*.*` | 20 | 0xADE5AE (11392174) | scripts |
| `Libs/MaterialEffects/FlowGraphs/*.xml` | 15 | 0x60D9D7 (6344151) | material fx |
| `Config/CVarGroups/*.cfg` | 14 | 0xB97F4C (12154508) | config |
| `libs/materialeffects/fxlibs/*.xml` | 14 | 0x4DD7BA (5099258) | material fx |
| `scripts/entities/items/XML/*.*` (×2 case) | 8 | 0x11A8F5D / 0x11A817C | entity scripts |
| `Libs/FlowNodes/*.node` | 5 | 0x606E5A (6316058) | flow |
| `Scripts\Startup\*.lua` | 4 | 0x17ECB1E (25085022) | startup |
| `scripts/entities/actor/parameters/*.*` | 3 | 0xE3ED64 (14938660) | actor |
| `data/GameShaders/HWScripts/*.*` | 1 | 0x921F7C (9574972) | shader |
| `Libs/EntityArchetypes/*.xml` | 1 | 0x1853F5F (25518815) | entity archetype |
| `Libs\Tables\prefab\prefab_phase_category__*.xml` | 1 | 0x974D2F (9913711) | table glob |

(caller_rva is WHGame-relative; image base 0x180000000, so VA = 0x180000000 + rva.)

## Mechanism (falsifiable, the named divergence)

The engine's OWN `FindFirst` (captured original, over its pak vector `FUN_1804631f0`) returns 0 for every
pak-resident content dir at boot — the engine pak-dir is NOT searchable-by-enumeration the way kcdx's flat
index is. kcdx returns the full unified listing (correct content). So for EVERY content-discovery enumeration,
kcdx hands the loader a non-empty list the engine would have gotten empty. This is the design §5.1 totalizing-
set behavior — and it is the FIRST measured divergence on the CONTENT path (entities/prefabs), the
`draw_indexed=0` geometry the menu fails to build.

**Why this can steer geometry:** an entity/prefab loader that enumerates `Entities/*.ent` / `Prefabs/*.xml` to
DISCOVER and register scene objects gets 144 + 69 entries from kcdx vs 0 from the engine's own walk. If that
loader's contract assumes the engine-native enumeration (0 here, with content discovered via a DIFFERENT
mechanism — a manifest, a registered pak-dir walk), kcdx's extra entries could mis-sequence or double-register
or mis-time the entity build → the indexed geometry never assembles → draw_indexed=0 → black.

## DO NOT over-read (the honest caveat)

`count_vanilla=0` is what the engine's captured-original `FindFirst` returns NOW. Whether vanilla (swap-OFF)
ALSO gets 0 here (making kcdx's add the divergence) OR whether the engine populates its pak-dir differently
swap-OFF (so vanilla's FindFirst would return >0) is NOT yet A/B-confirmed. The prior shader-existence
divergence (`whanim.cfi`) was proven HARMLESS (kcdx serves it fine). So an enum divergence is a LEAD, not a
proven cause — the A/B (swap-OFF, same enum_diverge read) is owed before asserting.

## The next probe (owed, before any fix)

1. **A/B the enum differential swap-OFF** (kcdx-noswap marker, reaches menu): do the SAME enum patterns show
   `count_vanilla=0` + a kcdx add on the working menu too? If swap-OFF ALSO adds entries and STILL reaches the
   menu → the enum add is harmless (like whanim.cfi). If swap-OFF does NOT add (engine's own walk returns >0
   there) → kcdx's swap-ON add IS the divergence → the mechanism is named.
2. **Resolve the entity/prefab caller_rvas** (0x11A9CB8 Entities, 0x41D757 Prefabs) — disassemble to identify
   the loader + its enumeration contract (does it expect 0 and discover via another path, or expect the list?).

## ⚠ CORRECTION + REAL DEFECT FOUND (2026-06-22, static ground-truth) — PROBE Q synthetic-dirs IGNORE the mask

Two flaws found while setting up the A/B, then a REAL defect via static ground-truth:

1. **The swap-OFF A/B is impossible** — the `kcdx-noswap` marker `return`s BEFORE the FS-slot install
   (`seating_hook.cpp:215`), so kcdx's `FindFirst` never runs swap-off → the differential cannot fire there.
2. **`count_vanilla=0` is a TAUTOLOGY, not a divergence** — it is the engine's OWN pak-vector walk
   (captured-original `FindFirst`) during the swap-ON run, which is naturally empty BECAUSE kcdx took over the
   pak vector. It is NOT what real vanilla returns. So "kcdx added 144" vs "engine's hollow walk returns 0" is
   not a real comparison. The differential's vanilla-baseline is broken by design.

**But the REFRAME ("is kcdx's enum RESULT correct?") found a REAL defect via on-disk ground truth**
(`enum_truth.py` — replicates kcdx's exact bind-root index + enum predicate over the real pak set):

- `Entities/*.ent`: ground-truth top-level = **144**, kcdx logged 144 → CORRECT.
- **`Prefabs/*.xml`: ground-truth top-level `.xml` = 3, BUT kcdx logged 69** = 3 real `.xml` + **66 synthetic
  DIRECTORY entries** (the 66 immediate subdirs under `prefabs/`: animal, armory, …). PROBE Q
  (`find_slots.cpp:214-230`) emits a synthetic DIR entry for EVERY immediate-child subdir **and DELIBERATELY
  BYPASSES the filename mask** (line 219: "The mask filter does NOT apply to a directory"). So
  `FindFirst("Prefabs/*.xml")` — a `.xml` FILE glob — returns 66 directory entries that do not match `*.xml`.

**The divergence is REAL (not the tautology):** a real Windows `_wfindfirst64("Prefabs/*.xml")` returns ONLY
`.xml` files — subdirs don't match the `.xml` extension glob, so vanilla returns 3. kcdx returns 69 (3 + 66
bogus dir entries). The 66 are kcdx-specific pollution vanilla NEVER produces. A prefab loader iterating
`Prefabs/*.xml` and expecting only `.xml` files gets 66 directory "entries" → tries to open them as prefab XML
(fail), or mis-registers, or chokes. PROBE Q's unconditional synthetic-dir emission OVER-APPLIES: it was right
for the ONE shader-recursion case (`Shaders/HWScripts/*.*`, a `*.*` glob wanting the `CryFX` subdir) but wrong
for a specific-extension file glob like `*.xml` that does NOT want directories.

**The fix shape (surfaced):** PROBE Q's synthetic DIR entries should be emitted ONLY when the caller's glob
would match a directory — i.e. a `*.*` / `*` glob (which the engine's own dir walk returns subdirs for), NOT a
specific-extension glob like `*.xml` / `*.ent` / `*.cfg` (where vanilla returns files only). The mask DOES
apply to the directory-vs-file decision: a `*.<ext>` glob excludes dirs, a `*.*`/`*` glob includes them. This
matches what `_wfindfirst64` actually does. Likely affects more than prefabs — any `*.<ext>` enum over a dir
with subdirs (UI, materialeffects, anims) gets the same bogus dir entries (re-check the 20 enum_diverge rows
against this rule). NOT YET a confirmed CAUSE of the black screen — but a real serve-incorrectness defect on
the content path, exactly "kcdx is NOT serving the engine what it asked for" (`*.xml` ≠ directories).

## ⚠ LIVE RESULT (2026-06-23, MaskMatchesDirectories fix deployed) — fix CORRECT, but NOT the cause

The fix works exactly as designed AND was necessary-not-sufficient:
- **Serve corrected:** `Prefabs/*.xml` enum_diverge 69→**3**, `Shaders/*.ext` 23→**21** — the bogus dir entries
  are GONE; `Prefabs/*.*` stays 69 (the match-all glob correctly still includes the 66 subdirs). The serve is
  now vanilla-correct (`*.xml` excludes dirs, `*.*` includes them).
- **draw_indexed=0 UNCHANGED** (`kcdx-dev_2026-06-23_00-08-22.log`: `draw_instanced=39696 draw_indexed=0
  ia_set_ib=0`). The black screen persists. **The PROBE Q bogus-dir defect was REAL but is NOT the
  black-screen cause.** A correct fix, an exonerated lead.
- **cap-118 (a) regression (not mine):** cap-118 was last built at `4befc07`, BEFORE PROBE Q's synthetic-dir
  emission landed (`d265732`). The 22:41/23:36 PASS runs used a STALE cap-118.dll (compiled before PROBE Q
  existed → no synthetic dir → size=2). This run is the FIRST rebuild against PROBE-Q-live find_slots → (a)
  correctly now sees the synthetic `sub` dir (size=3). Fixed (a) to expect it + added (g) for the mask gate.
  PROBE Q (`d265732`) shipped without updating its test — a latent test-staleness, surfaced by the rebuild.

## The enum-differential is EXONERATED as the cause (the tautology confirmed)

The `count_vanilla=0`-everywhere is a takeover tautology (the engine's own pak-walk is naturally empty
post-swap), NOT a real divergence — and the one REAL serve-defect it surfaced (the bogus dirs) is now fixed
and proven not-the-cause. So the enum-result-correctness axis joins the exonerated set: served bytes (PROBE
T/U), handle (PROBE T), object (PROBE U), shader/PSO (R/R2/R3/R4/P), present (K), draws-to-valid-RT (S),
FS-resolution (bind-root), and now enum-result-correctness. Every kcdx FS OUTPUT is measured correct. The
wedge is a kcdx-perturbed STATE/init-ORDER the geometry build depends on, NOT anything kcdx serves/answers —
the PROBE T refinement's dropped-state-mutator lead (a vtable slot that is BOTH a file op AND a state mutator,
kcdx did the file half, dropped the state half).

## Provenance
- Probe: gated PROBE W (`boot_trace.h` TraceVanillaEnumDiff + find_slots.cpp/enum_slots.cpp split counters).
  Log: `kcdx-dev_2026-06-22_23-36-06.log` (pre-fix) + `kcdx-dev_2026-06-23_00-08-22.log` (post-fix) enum_diverge.
- Ground truth: `enum_truth.py` (on-disk pak enum, replicates kcdx's bind-root index + enum predicate).
  Fix: `find_slots.cpp` MaskMatchesDirectories + the synthetic-dir gate; test cap-118 (a) updated + (g) added.
