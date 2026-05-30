# Step 4 — `apply` add-entity + add-versions-row

**What.** Extend `apply` with the INSERT/PROMOTE actions: adding a brand-new
curated entity (a names row + a versions row) and adding a new versions row for
an existing entity (a moved/renamed function at a new game version, closing the
prior open interval). Both run the kind-class fingerprint branch and the
baseline-present gate. This is the step that actually writes new
`address_versions` rows via the shared row-builder, so it is where the oracle's
"both writers agree" property gets its real exercise. See `plan.md` §3
"Add new curated entity" + "Add a new versions row".

**Scope (commit-grain).**
- Delta classification extended to detect absent rows (add-entity /
  add-versions-row) alongside step-3's present/changed cases.
- Add-entity: names-row INSERT + versions-row via the **kind-class branch**
  (`../context.md` decision 3): (a) function kinds PROMOTE the bulk dev row
  keeping fingerprint; (b) RVA-bearing non-function kinds force fingerprint NULL;
  (c) `vtable_index` mint all-NULL. All rows built by `seeds_shared/row_builder`.
- **Baseline-present gate** (`../context.md` decision 4, `plan.md` §3): a
  function-kind add with no bulk baseline for the target version → REFUSE +
  direct to `--rebuild`; a bulk row at a wrong `valid_from` → REFUSE. Never mint
  a NULL-fingerprint function row.
- Add-versions-row: close the previous open interval (`valid_through = prev`)
  BEFORE the new INSERT, respecting the partial-unique index's real predicate
  `(kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS NULL` (`plan.md` §3).
- Dev-DB-required enforcement for function-kind promotes (`plan.md` §4 matrix);
  clear refusal when the dev DB is absent/unbaselined.
- Both DBs, user→dev, per-action `BEGIN; …; COMMIT;`. Run report extended.

**Disassembler test.** Add-entity records a `kind` + RVA/signature the maintainer
already determined and wrote into the CSV; `apply` does not ask for an offset it
should derive. The kind-class derivation is the engine's job, not the author's.
Compliant.

**Test bar.** Oracle slice across all three kind-classes: add one function-kind
entity (promotes, keeps fingerprint), one RVA-bearing non-function (NULL
fingerprint), one `vtable_index` (all-NULL); assert each row in both DBs matches
`--rebuild` from the same seeds. Baseline-refusal test: a function-kind add at a
non-baselined version is refused with no row written. Interval test:
add-versions-row closes the old interval and the open-row uniqueness index is not
violated.

**Dependencies.** Step 1 (row_builder + schema), Step 3 (the `apply` scaffold +
delta + version resolve).

**Reference.** [`../context.md`](../context.md);
[`data/maintainer-tool/plan.md`](../../../../data/maintainer-tool/plan.md) §3
(Add new curated entity; Add a new versions row), §4;
[seed-to-db-migration-mapping.md](../../seed-to-db-migration-mapping.md).
