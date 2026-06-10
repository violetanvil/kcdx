# 2.2 [BE] Migrate the backend (config / csv_integrity / routes_confirm) + its tests off data/seeds

## What

Move the maintainer-tool backend off `data/seeds/`: `config.py`'s `seed_dir` (and the `out_dir`/
`seed_dir` relationship the tests assert) repoints to the db-export genesis; `csv_integrity.py`'s
rebuild-baseline + `routes_confirm.py`'s seed-path references update to the db-export model; and the
~6 backend tests that currently ASSERT `seed_dir must stay at data/seeds` (`test_config.py`,
`test_backend_skeleton.py`, `test_read_endpoints.py`, `test_save_endpoints.py`, `test_confirm_endpoint.py`,
…) are updated to assert the db-export genesis instead. After this, the backend builds its reference
DBs + runs its save spine over the db-export CSVs, not the retired seeds.

## Scope

One commit in `data/maintainer-tool/backend/app/` (`config.py`, `csv_integrity.py`,
`routes_confirm.py` — the seed-path references) + the backend tests under
`data/maintainer-tool/backend/tests/` that assert the seed location. The test ASSERTIONS flip from
"seed_dir stays data/seeds" to "the genesis is db-export" (per D38); the test FIXTURES that build a
checkout at `<root>/data/seeds/` rebuild at the db-export layout. Does NOT touch `import_to_sqlite`
(2.1) or the data-core rebuild tests (2.3).

## Test bar

The backend test (`data/maintainer-tool/backend/tests/`, pytest): the backend resolves the
db-export genesis (not `data/seeds/`); the config relationship tests assert the db-export paths; the
confirm/read/save endpoint tests build their fixture checkout at the db-export layout and pass. Emits
the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE` to the DB-pipeline sink. FALSIFIABLE: a backend test
still asserting `seed_dir == data/seeds`, or the backend resolving the retired path, fails the row.
Per `.claude/rules/test-discipline.md`.

## Dependencies

- **2.1** — `import_to_sqlite`'s constants point at db-export (the backend's rebuild calls resolve
  the genesis through them).
- **1.4** — the widened round-trip oracle is green (csv_integrity's rebuild-baseline change rests on
  the proven full-corpus round-trip).

## Reference

[`../plan-spec.md`](../plan-spec.md) — the coverage map (the backend migration element) + cross-step
invariant 5 (the data-core stays the sole writer; the save spine is unchanged, only its paths move).

## Design authority

`data/maintainer-tool/design.md` **D38** + the revised **D18** (the container checkout carries the
db-export, not seeds) + the revised **D19** (the write mechanism unchanged). The backend's `seed_dir`
/ genesis references move to D38's db-export model. Build to D38.

## Disassembler-test / author-burden

None — a backend path migration; no author-facing input, no game-function target, no AP18 addition
(no seed ROW is added — the rows already exist; this moves where they're read from).
