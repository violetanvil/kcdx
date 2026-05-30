# Phase 8.5 step 2 — parse `[entrypoints].assets` + build overlay map

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

At plugin discovery, parse each plugin's `[entrypoints].assets` directory and
build an in-memory overlay map: virtual-asset-path → (owning plugin, loose-file
path on disk). The map is the lookup the resolver hook (step 3) consults.

## Scope

- Extend manifest parsing to read `[entrypoints].assets` (a directory, relative
  to the plugin root).
- Walk that directory at discovery; each loose file's path-relative-to-the-assets-root
  is its virtual asset path.
- Populate the overlay map in unified load order so the conflict-report "lost to
  plugin X" semantics (step 4 / the gate) fall out of load-order precedence.

## Test bar

Exercised at step 5. This step's own check: a plugin with an `assets/` dir
produces the expected overlay-map entries (verifiable via a dev-log dump of the
map at discovery).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5b".
