# Step 3 — per-asset sidecar declarative model (replace + name)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 3.

## What

The declarative, no-code author surface: an opt-in per-asset metadata sidecar TOML
(co-located with the asset, the `targets.toml` sidecar idiom) declaring what an
asset `replaces` and/or a published `name`. A sidecar's scope is its placement
(file-level beside one asset; directory-level covering its subtree — the more it
abstracts, the more each entry specifies). Parsed at discovery; feeds the overlay
map (the resolution step 2 consults) + the published-name registry. Nothing
auto-applies — a file with no sidecar replaces nothing; a `replaces` target that
doesn't resolve fails LOUD (`anti-patterns.md` AP14).

## Scope

- Parse the sidecar at plugin discovery; populate the overlay map's replacement
  entries + the published-name entries from it.
- `replaces` target forms: ONE string for a vanilla path OR another mod's
  published name; the `replaces_plugin` + `replaces_path` pair for an unnamed
  cross-mod asset by path. Engine errors on an ambiguous / both-forms sidecar.
- `name` publishes `<author>.<plugin>.<name>` (engine derives the prefix from the
  manifest — `naming-namespaces.md`; the author types only the bare name).
- Every failure path teaches (`logging.md` + `cornerstones.md` errors-that-teach):
  a missing target, a malformed sidecar, a `..`-escaping path — named, never a
  silent skip.

## Test bar

Exercised at step 8. This step's own check: a plugin with a sidecar declaring a
`replaces` (vanilla + cross-mod-name + cross-mod-by-path forms) + a `name`
produces the expected overlay-map + published-name entries — verifiable via the
discovery-time dev-log dump; a missing/typo'd target emits the teaching error.

## Dependencies

The landed overlay map (step 3 populates its replacement entries); **step 2**
(the resolution that consumes the map — so a sidecar-declared replacement is
exercisable when this lands). Ordered after both.

## Disassembler-test / author-burden

CLEAN — the author declares files + a `replaces`/`name` by a path or name they
already know; no hex/offset/signature. The `<author>.<plugin>` prefix is
engine-derived, never typed (`cornerstones.md`, AP12; `naming-namespaces.md`).

## UX

Author-facing (a TOML surface, not UI) — the "states" are the teaching errors:
missing target → named error; malformed sidecar → named error; no sidecar →
silently does nothing (correct: existence ≠ replacement); conflict → the
"lost to plugin X" line. Carried from design §4.2 / §4.4.

## Reference

[`../plan-spec.md`](../plan-spec.md); design authority
[`../../restructure/phase-08.5-asset-replacement/asset-design.md`](../../restructure/phase-08.5-asset-replacement/asset-design.md)
§4.2 (the sidecar model + `replaces`/`name` field shapes) + §4.4 (conflict). Build
to §4.2's field shapes, not to this summary.
