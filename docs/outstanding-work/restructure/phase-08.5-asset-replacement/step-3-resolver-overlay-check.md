# Phase 8.5 step 3 — overlay-map check in the resolver hook

> **SUPERSEDED (2026-06-04) — historical record.** This 5-step stub was re-planned into the standalone [`../../asset-system/`](../../asset-system/README.md) tree (see this dir's [`README.md`](README.md)); the asset-system step docs are authoritative. Kept as the pre-spinout authoring record — NOT live work.

**Status: NOT STARTED (superseded before build).** Ledger row: [`README.md`](README.md) → step 3.

## Design authority + scope

> **⚠ SUPERSEDED MECHANISM — re-plan against the canonical design (2026-06-02).**
> This step doc describes the old `CCryPak::FOpen` overlay-check mechanism. The
> canonical design REPLACES `CCryPak::AdjustFileName` (slot 1, id 152) instead —
> reusing the pak/disk/normalizer leaves (153/154/155) by calling through,
> **independent of `sys_pakPriority`** (which is dropped, not "not viable" — it
> is simply not part of the mechanism). Build to the canonical doc, not this step
> doc's FOpen framing. Re-decompose this Phase-8.5 tree against it via `/plan`.

- **Authority:** [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
  — the canonical asset-replacement design (§7 verified seam, §8 install timing).
  Build to that doc, not to this step doc's summary (the summary is a lossy
  pointer, and its mechanism is superseded).
- **Asset-class scope (still valid):** the overlay covers BOTH **handle-consumed**
  classes (`.lua`/`.xml`/scripts) AND **memory-mapped** classes
  (`.dds`/`.gfx`/textures/models — the headline TC class). No class is deferred.
  Owning the one resolution seam (`AdjustFileName`) owns both classes (canonical
  §2, §7). The handle-consumed end-to-end resolution + any staging is the
  canonical §9 build probe.

## What

In the pak-resolver hook (step 1), check the overlay map (step 2) FIRST: if the
requested virtual path has an overlay entry, open the loose overlay file instead
of the pak-resident asset; otherwise fall through to the original resolver.

## Step 3a — the probe (runs FIRST, settles the mechanism before the body is built)

The redirect mechanism rests on a checkable unknown the DB/wiki did NOT settle
(`results-driven.md`). The probe overlays, in ONE launch, one handle-consumed
asset (`.lua`/`.xml` — U.4 baseline, also settles whether the plugin's actual
`assets/`-dir path resolves) AND one memory-mapped asset (`.dds`/`.gfx` — the
genuine second fork). Outcome map (flat, theory-independent — designed to
FALSIFY the "simple rewrite covers everything" theory):

- **(a)** both read back changed → the simple `pName`-rewrite-to-resolvable-path
  covers all classes → step 3's body is one path.
- **(b)** `.lua` reads back changed but `.dds` does NOT → memory-mapped needs the
  return-our-own-handle mechanism (the archive's U.4 Outcome B) → step 3 grows
  that second path.

## Step 3b — the hook body (built to whatever 3a resolved)

- Fill the step-1 production hook's body with the overlay-map lookup.
- On hit: emit the overlay-hit log line (virtual path + winning plugin), open the
  loose file via the 3a-resolved mechanism, return it through the resolver's
  normal return path.
- On miss: call original unchanged.

## Dependencies

Steps 1 + 2 (the production hook site + the populated overlay map). Step 3a (the
probe) precedes 3b (the body) within this step.

## Test bar

Exercised at step 5 (the end-to-end in-game replacement). This step's own check:
a known overlaid path produces the overlay-hit log line and reads the loose file;
a non-overlaid path is untouched.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5c".
