# Phase 1 step 4 — sidecar declarative model + load-order conflict report

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 4.

## What

The no-code declarative path: an opt-in per-asset sidecar TOML, co-located with an
asset (the `targets.toml` sidecar idiom), declares what an asset replaces and/or a
published name. This step parses the sidecar and feeds the overlay map, so a
TOML-only plugin can replace a vanilla asset (US-1), replace another mod's asset
(US-4), and publish a name (US-5, declarative half). A file's presence in `assets/`
still replaces NOTHING — only an explicit sidecar declaration does (design §4.1). A
sidecar naming a target that does not exist fails LOUD (AP14 — errors that teach),
never a silent orphan. Two declarations of the same target resolve by load order
with a winner/suppressed conflict-report line (design §4.4; the map's conflict
reporting is already built, `2588b33`).

## Scope

- Parse the sidecar TOML (co-located with the asset; scope = placement, design
  §4.2): `replaces` (ONE string — a vanilla path OR another mod's published name),
  OR the `replaces_plugin` + `replaces_path` pair (unnamed cross-mod by path), and
  optional `name` (publish as `<author>.<plugin>.<name>`). Error LOUD on an
  ambiguous/both-forms sidecar and on a missing `replaces` target (AP14,
  `logging.md` structured KV).
- Feed each declaration into the load-order overlay map (built, `2588b33`); the
  map's existing winner/suppressed conflict reporting emits the §4.4 line.
- `src/asset_overlay.{h,cpp}` (+ a sidecar-parse unit if it warrants its own file
  per `no-monolith.md`).

## Test bar

A `cap-NN` suite-gated test plugin (the permanent regression home is step 9; this
step ships the sidecar-parse coverage as part of its deliverable per
`test-suite.md`): a TOML-only declarative replacement applies in-game; a sidecar
with a missing `replaces` target produces the LOUD teaching error in the log (a
falsifiable claim — FAILS if the bad target is silently dropped, AP15); two
plugins declaring the same target produce the winner/suppressed conflict line. Build
green. Live-confirmed via the launch (`acceptance-signal.md`).

## Dependencies

**Step 2** (the seam consults the overlay map this step feeds) and **step 3** (the
resolution handles both classes, so a sidecar-declared overlay of either class
applies). Ordered after both so a declared overlay actually resolves when this
step lands (`.claude/rules/incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§4.1 (existence ≠ replacement) + §4.2 (the sidecar) + §4.4 (conflict resolution) +
§3 US-1/US-4/US-5. Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Disassembler-test / author-burden

The author declares a path or a published name — never an engine internal, an RVA,
or an asset class (the disassembler test, `cornerstones.md`). The sidecar is the
no-code path; a mistyped target teaches via a loud error, never a silent no-op
(AP14). No hand-written hex/ABI.
