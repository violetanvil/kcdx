# Step 2 — read API (curated set + entity detail + version rows + derived status)

**What.** Add the read endpoints the frontend's browse/view surfaces (s01/s02/s03) call: the
**curated entity set** (name · kcdx_id · derived status — for the navigator list + filters),
an **entity's detail** (identity + lifecycle flags), and its **version rows** (the
`address_versions` rows for the version table + history + compare). Status is DERIVED, not
authored (`policy.md` §"Status is NOT an authored column") — the backend computes it from the
data-core's read of the lifecycle flags + current-version verification; it reimplements no
rule (law 6). Read-only; no write.

**Scope.** The read endpoints + their response shapes (the JSON the frontend binds). Reads
through the data-core / the reference DB at the configured checkout (step 1). No write/save
(step 4), no field-delta (step 3).

**Test bar.** A backend test (`pytest`) on the mini-dump fixture: the curated-set endpoint
returns the ~143 entities with name/kcdx_id/derived-status; an entity-detail endpoint returns
its identity + lifecycle; a version-rows endpoint returns the entity's `address_versions`
rows (newest-first). The derived-status computation matches `policy.md`'s derivation.
Runnable now (the data-core read path + the fixture exist).

**Dependencies.** Step 1 (the backend skeleton + the data-core read seam + the checkout
config). Sequenced after step 1.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-1/US-2/US-9 (load, browse, the version rows + compare data). The frontend surfaces this
feeds: [`ui/screens/s01-navigator.md`](../../../../data/maintainer-tool/ui/screens/s01-navigator.md)
(the list + status chips + filters), [`s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
(header + version table), [`s03-version-history-compare.md`](../../../../data/maintainer-tool/ui/screens/s03-version-history-compare.md)
(history + compare). `policy.md` §"Status is NOT an authored column" (the status derivation).

**Disassembler-test / author-burden.** N/A — read API; no author-facing input.
