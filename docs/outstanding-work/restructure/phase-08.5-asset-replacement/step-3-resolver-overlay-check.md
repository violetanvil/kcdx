# Phase 8.5 step 3 — overlay-map check in the resolver hook

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

In the pak-resolver hook (step 1), check the overlay map (step 2) FIRST: if the
requested virtual path has an overlay entry, open the loose overlay file instead
of the pak-resident asset; otherwise fall through to the original resolver.

## Scope

- Fill the step-1 production hook's body with the overlay-map lookup.
- On hit: emit the overlay-hit log line (virtual path + winning plugin), open the
  loose file, return it through the resolver's normal return path.
- On miss: call original unchanged.

## Dependencies

Steps 1 + 2 (the production hook site + the populated overlay map).

## Test bar

Exercised at step 5 (the end-to-end in-game replacement). This step's own check:
a known overlaid path produces the overlay-hit log line and reads the loose file;
a non-overlaid path is untouched.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5c".
