# Step 2a — data-core read seam (read-for-display + the single status derivation)

**What.** Add the read-for-display surface to the data-core (`seeds_shared`), so the
backend (step 2b) and any future consumer reads the curated set / an entity's detail /
its version rows — and the DERIVED status — by CALLING the data-core, never by
re-querying the DB or re-deriving status itself. The landed data-core is
write+validate+export+round-trip; it has NO read/query-for-display function, and the
status derivation (DEPRECATED/SUPERSEDED/VERIFIED/UNVERIFIED) exists ONLY as prose in
`data/seeds/policy.md` §"Status is NOT an authored column". This step makes that prose
a single tested function and gives the read surface a home in the data-core where the
rule logic belongs (D13/law 6). **Read-seam decision, settled 2026-06-02** (plan-spec
§"Cross-step invariants"): the status rule lives in the data-core, not the backend
(reimplementing it there) nor the frontend (deriving it in JS) — both rejected as
law-6 violations that drift.

**Scope (a new module `seeds_shared/read_api.py` + its re-export):**
- `derive_status(current_version_ordinal, version_row, entity)` — the SINGLE
  implementation of policy.md's 4-rule precedence, returning one of the four status
  tokens. The exact rules (policy.md §"Status is NOT an authored column", verbatim):
  1. `entity.is_deprecated` AND `current >= entity.deprecated_at_version` → `DEPRECATED`
  2. else `entity.superseded_by` AND `current >= entity.superseded_at_version` → `SUPERSEDED`
  3. else `row.last_verified_at_version >= current` AND `row.valid_from <= current` → `VERIFIED`
  4. else → `UNVERIFIED`
  Ordinal compare (the DB stores `*_at_version` / `valid_from` / `last_verified_at_version`
  as `game_versions.id` FKs whose ordinal is the compare key — policy.md §"ordinal compare";
  read the schema to confirm the join). `current_version_ordinal` is the max
  `game_versions.ordinal` in the DB (the baseline the importer wrote — read it from
  `game_versions`, do NOT hardcode GAME_VERSION_TAG; reuse the existing
  `MAX(ordinal)`-reading helper in `import_to_sqlite.py:262` if it fits).
- `read_curated_set(out_dir)` → the curated entity list for s01: per entity
  `name` · `kcdx_id` · derived `status` (+ the current-row `kind` for the s01 kind
  filter — s01 §Contents "the entity's current-row kind"). Reads the USER
  `reference.sqlite` `address_names` (the curated registry) joined to its current
  `address_versions` row. ~143 entities (the curated set).
- `read_entity_detail(out_dir, kcdx_id)` → identity (`kcdx_id`, `name`) + the
  entity-level lifecycle fields s02 renders: `superseded_by`, `superseded_at_version`,
  `is_deprecated`, `deprecated_at_version`, `deprecation_replacement`, `notes`.
- `read_version_rows(out_dir, kcdx_id)` → the entity's `address_versions` rows,
  NEWEST-first (s02 §Contents "newest first"), full columns for the version table +
  s03 history/compare (the `address_versions` USER_COLUMNS — read schema.py), each row
  carrying its derived `status` (via `derive_status`).
- Read-only: open the DB read-only (`mode=ro`, the `_open_*` convention or sqlite URI);
  no write, no schema change.
- Re-export the four names from `seeds_shared/__init__.py` (the public-surface
  convention every other module follows).

**Out of scope.** No HTTP/JSON (step 2b). No write/save. No new DB column. No
re-derivation of anything policy.md already specifies — `derive_status` IS the
policy, implemented once.

**Test bar (same change; the data-core test tree + the mini-dump fixture exist):**
a new `data/refdata-extractor/tests/test_read_api.py` over the mini-dump-built DB:
- `derive_status` returns each of the 4 tokens for a constructed (current, row, entity)
  input that exercises each precedence rule (deprecated-and-past-its-version,
  superseded-and-past, verified, unverified) — INCLUDING the boundary cases
  (`current == deprecated_at_version` is DEPRECATED; `last_verified == current` is
  VERIFIED) the `>=`/`<=` rules turn on. The expected tokens come from reading
  policy.md's rules, not from the implementation (the test is the policy's oracle).
- `read_curated_set` returns the curated entities with name/kcdx_id/status/kind; the
  count matches the fixture's curated set; a known deprecated/superseded fixture entity
  shows the right status.
- `read_entity_detail` returns the identity + lifecycle fields for a known kcdx_id.
- `read_version_rows` returns the entity's rows newest-first, each with its derived
  status; a multi-version fixture entity shows the older row VERIFIED-at-its-version and
  the status flipping at the current version per policy.md's "a new game version flips
  VERIFIED→UNVERIFIED" rule.
- Status is computed end-to-end against the real DB (not a mock of the rows).
Run: `python -m pytest data/refdata-extractor/tests/test_read_api.py -q`. The full
data-core suite stays green (this is additive — a new module + new tests; no existing
oracle touched): baseline is `1 failed (TD-0004, pre-existing) / 47 passed / 1 skipped`
as of step 1b — capture the data-core suite on HEAD before the edit and confirm 2a adds
only passing tests + turns no green oracle red.

**Dependencies.** Phase 1 (the data-core + its DB schema + the mini-dump fixture) —
landed. This is the PRODUCER; step 2b (the backend read endpoints) is the CONSUMER,
ordered after it (`.claude/rules/incremental-delivery.md`).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 (the data-core is the single gate; the backend holds zero rule logic) + §10 D13.
`data/seeds/policy.md` §"Status is NOT an authored column" (the 4-rule derivation —
the authority `derive_status` implements verbatim) + §"address_versions columns"
(the row fields) + §"Supersession" / the lifecycle pair-integrity (the entity fields).
The display shapes the consumers need: [`ui/screens/s01-navigator.md`](../../../../data/maintainer-tool/ui/screens/s01-navigator.md)
(name·kcdx_id·status chip + the kind filter),
[`s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
(identity + lifecycle + version rows newest-first).

**UX.** N/A — a data-core library seam; no user-facing surface.

**Disassembler-test / author-burden.** N/A — internal Python read module; no
author-facing game-function input.
