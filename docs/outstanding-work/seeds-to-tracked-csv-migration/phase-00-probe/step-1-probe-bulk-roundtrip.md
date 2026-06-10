# 0.1 [PROBE] Bulk DEV DB → CSV → rebuilt DEV DB round-trips byte-identical (no dump)

## What

Probe the D38-gating unknown: can the DEV bulk corpus survive a CSV round-trip with no Ghidra dump?
Build a throwaway verification script that (1) takes the current `reference-dev.sqlite`, (2) exports
its bulk tables (`statements`, `referenced_vars`, `call_edges`) + the `kcdx_id`-NULL bulk
`address_versions` discovery rows to CSV, (3) rebuilds a fresh DEV DB from those CSVs ALONE (no
`dump_dir`), and (4) diffs the rebuilt DEV DB against the original — every bulk table row + every
column, byte/value-identical. The question it answers: **does the dump contribute any bulk data the
DEV DB's own tables do not already store?** If the rebuilt-from-CSV DEV DB matches the original, the
tables capture everything → the export CAN be lossless from the DB alone → D38 is buildable. If it
diverges, the dump provides data the tables don't hold (e.g. a derivation the import computes from
raw dump bytes) → D38's "rebuild from CSV alone" needs the export widened or cannot fully retire the
dump → STOP and surface.

## Scope

A throwaway probe script under `_research/` (the probe-archive convention — `.claude/rules/
working-artifacts.md`: a scratch verification artifact, captured finding, NO residue in production
code). It reads the live `reference-dev.sqlite`, does the export→rebuild→diff over the bulk, and
prints the verdict. It does NOT modify the production exporter, `run_rebuild`, or any committed
toolchain code — it reuses their row-encode/decode logic in-place to test the round-trip hypothesis,
no production change. The finding (byte-identical, or the exact divergence) is captured into
`_research/` as durable process-output that Phase 1 reads.

## Test bar

The probe IS its own verification — it emits a falsifiable verdict against the pre-committed
outcome→meaning map:
- **PASS / byte-identical** — every bulk table (`statements` row count + every column;
  `referenced_vars`; `call_edges`) and every `kcdx_id`-NULL `address_versions` row in the
  rebuilt-from-CSV DEV DB equals the original. Meaning: the DB tables capture the full bulk; the
  export can be lossless from the DB alone → Phase 1 proceeds.
- **FAIL / divergence** — any bulk row/column differs (or is absent) in the rebuilt DB. Meaning: the
  dump contributes data the tables don't store → the export cannot be lossless from the DB → STOP,
  surface the exact divergence (which table/column) as a design-reconciliation decision for the user
  (the D38 model needs the export widened to capture it, or the dump cannot fully retire). FALSIFIABLE:
  a probe that reports PASS while a bulk column is actually absent in the rebuild fails its own bar —
  the diff asserts row-count AND per-column equality, not just "the rebuild ran."

Per `.claude/rules/results-driven.md` (the outcome→meaning map written before the run, one variable,
theory-independent — it observes the raw round-trip fact, it does not assume losslessness).

## Dependencies

- None (the first step). It reads the live `reference-dev.sqlite` (a maintainer artifact present on
  disk) + reuses the existing `csv_exporter` / `import_to_sqlite` row logic in-place.

## Reference

[`../plan-spec.md`](../plan-spec.md) — cross-step invariants 2 (the dump is TODAY the sole source of
the bulk) + 3 (bulk-rebuild-from-CSV completeness is a verified probe outcome, not assumed).

## Design authority

`data/maintainer-tool/design.md` **D38** (the completeness bar: "the exporter extends to capture both
DBs losslessly... rebuild-from-CSV → DB → re-export → byte-identical") — this probe verifies that bar
is ACHIEVABLE for the bulk before Phase 1 builds the exporter to meet it. NOT a UI/contract surface
(no design back-pointer to a screen/schema beyond D38's completeness clause).

## Disassembler-test / author-burden

None — a data-layer verification probe; no author-facing input, no game-function target, no AP18
addition (it reads the existing DEV DB, authors nothing).
