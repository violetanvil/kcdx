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

## Provenance
- Probe: gated PROBE W (`boot_trace.h` TraceVanillaEnumDiff + find_slots.cpp/enum_slots.cpp split counters).
  Log: `kcdx-dev_2026-06-22_23-36-06.log` enum_diverge lines. Build 8E896C03581D.
