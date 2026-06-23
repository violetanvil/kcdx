# KI-0028 — PROBE Y enumeration vanilla-differential: RESULT

**Date:** 2026-06-22 · **Trust:** primary evidence (live measurement, 4 probe iterations).
**Verdict: kcdx's enumeration NEVER drops an entry the engine would return — a strict
SUPERSET. Enumeration is EXONERATED as a geometry-drop cause. The black screen is NOT a
kcdx enumeration divergence.**

## The question

KI-0028: with the FS-takeover swap ON the game boots to a black screen (sound, no video,
no input); swap OFF reaches the menu. FS SERVE was already exonerated (every asset served
`index-pak`, `want==got`). Enumeration (FindFirst/FindNext/FindClose 63/64/65, ForEachFile
14) was the ONE FS surface kcdx synthesizes with no captured original + no differential —
PROBE W (metadata) was blind to it, and it is where "draws abandoned upstream of recording"
(PROBE X, `draw_indexed=0`) would originate for a filesystem takeover (the engine discovers
renderable content by WALKING DIRECTORIES). Does kcdx's synthesized enumeration return a
DIFFERENT set than vanilla, dropping geometry before any FOpen?

## Method evolution (4 iterations — each artifact diagnosed, not declared)

1. **PROBE Y — replay the captured engine-original iterator.** RAN AWAY: 655/760 walks hit
   the drain cap on real 3-entry dirs. NOT declared an outcome — a probe artifact.
2. **PROBE Y.2 — instrument the drain.** Observed: engine FindFirst returns a CCryPakFindData
   OBJECT POINTER; FindNext returns 0 + a valid name every call. Root cause = the drain
   memset wiped the find-data header (bytes 0x01..0x23) that carry FindNext's iteration
   state. (Body-verified, `ki0028-findfirst-replay-contract-recon/FINDINGS.md`.)
3. **PROBE Y.3 — memset-once fix, then REBUILD instead of replay.** Memset fix cured 630/655
   but 25 stateful dir-walks still cycled; driving the engine iterator out-of-band is fragile
   AND re-entrant against kcdx's own live enumeration (CCryPak find-state pak+0x138/+0x148).
   ABANDONED the replay. Switched to REBUILDING vanilla's set deterministically from kcdx's
   own captured sources (disk-walk UNION raw index pak-vpaths under prefix) — no engine
   iterator, no re-entrancy. Result: 84 "drops" — but ALL were the rebuild IGNORING the
   filename glob (it counted the whole directory as "vanilla"; the "dropped" names did not
   match the pattern). Another artifact, diagnosed not declared.
4. **PROBE Y.4 — apply the glob mask to the rebuild** (the engine's _findfirst64 applies it).
   FINAL: **190 enum_diverge walks, ZERO with only_in_vanilla populated.** Every divergence
   is `only_in_kcdx` (the synthetic-dir + alias-fold superset kcdx adds). Artifact-free.

## The result (FINAL, clean)

**Across 190 boot-window enumeration walks, kcdx drops NOTHING the engine would return.**
- `only_in_vanilla` empty on every walk → no entry the engine's bare FindFirst would surface
  is missing from kcdx's synthesized set.
- `only_in_kcdx` populated on the divergent walks → kcdx is a strict SUPERSET: it returns the
  correct glob matches PLUS its synthetic immediate-child subdir entries (PROBE Q) and
  alias-folded pak content (`data/gameshaders/`→`shaders/`).
- A superset enumeration CANNOT make the engine drop geometry — the engine sees everything it
  would have, plus more.

**∴ Enumeration is exonerated. The KI-0028 black screen is NOT a kcdx enumeration divergence
— it is downstream, in the render/PSO/scene layer (the `draw_indexed=0` path PROBE X found).**

## A SEPARATE observation (NOT the probe's verdict — a probe-COST artifact)

On the Y.3/Y.4 runs the boot took ~2.5 min and ended with the engine's "level can't be loaded,
exiting — kutnohorsko" abort. This is the PROBE'S COST, not a kcdx serve failure: the rebuild
loops the full 509,362-entry index on EVERY enumeration walk (O(walks × index)), blowing the
engine's level-load time budget. Evidence it is timing, not a serve failure:
- The level enumerates fine (`FindFirst "levels/*.*" matched=3` klaster/kutnohorsko/trosecko).
- The level pak resolves (`IsFileExist3 levels/kutnohorsko/level.pak result=1`, real GetFileStat).
- The abort appeared ONLY on the two slow rebuild runs, never on the fast pre-rebuild runs.
- `asset_index_built` 20:12:37 → boot still grinding at 20:15:19 (~2.5 min).
RETIRING the probe restores the fast boot (the confirming test: the abort vanishes with the
probe gone). The level-load abort here is the instrument perturbing the boot it measures, NOT
a regression of the recursive-walk level-pak fix.

## Reuse

- The vanilla-differential PATTERN (rebuild from captured sources, not iterator replay) is the
  reusable tool for any "does kcdx's synthesized X match vanilla" question — deterministic,
  re-entrancy-free. The glob must be applied to match the engine's _findfirst64.
- kcdx enumeration = strict superset of vanilla. Confirmed. Do not re-investigate enumeration
  as a drop cause for KI-0028.
