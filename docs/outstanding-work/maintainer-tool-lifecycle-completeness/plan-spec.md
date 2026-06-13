# Plan-spec — maintainer-tool lifecycle completeness (TRD D41)

## Goal

Implement TRD D41 — **entity-lifecycle completeness + report-vs-DB reconciliation**: the maintainer
tool never leaves an entity silently incomplete at the current game version, and the report worklist
reconciles against current DB state instead of treating every report row as new.

## Settled design (the build authority — `/plan` is a pointer; build to these)

- **Functional TRD:** `data/maintainer-tool/design.md` **D41** (committed `88bb582`) — the decisions-table
  row. Three settled facts: (1) a standing tool-wide needs-action view over the version-relative
  incomplete-lifecycle set; (2) the data-core derives already-acted-on from `(report version + current
  DB state)`, reading the live DB per row; (3) s08 reflects it (already-acted → no-further-action; a
  close that orphans flags needs-action; the three Fix-flow gaps). Extends D39/D40, supersedes neither.
- **Screen spec:** `data/maintainer-tool/ui/screens/s09-needs-action.md` (committed `3dcf3e0`) — the new
  Needs-action view (v1/high): peer content screen from the s01 navigator's `[Needs action ▸ N]` badge;
  by-kind collapsible sections (Uncovered at current version / Never verified / Broken references);
  per-row resolution actions navigating to s02/s04/s05; success-framed all-clear empty state.
- **s08 D41 sections:** `data/maintainer-tool/ui/screens/s08-verification-worklist.md` (committed `88bb582`)
  — the report-vs-DB reconciliation, the close→needs-action flow, the Fix-flow completeness, the
  "Already acted on" + "Orphaned by a close" States entries.

## Cross-step invariants

- **The data-core is the sole writer (law 6 / D19).** The resolver/detection compute; the existing
  `_apply_one_db` batch path writes. The needs-action detection + the reconciliation are READ-ONLY.
- **No report-schema change, no engine change (D41).** Already-acted-on is derived from the live DB
  (the report's `kcdx_id` + `version` are the reconciliation keys); the report keeps its current shape.
- **Detection is version-relative + query-time** (above the write-time HARD-ERROR checks): the orphan /
  never-verified / broken-ref conditions are computed at the current game version V, not at import.
- **The s09 view detects + ROUTES; it reimplements no editing surface** — every resolution navigates to
  the canonical s02/s04/s05 flow + its validated write path.
- **The orphan resolves UNVERIFIED-at-V, not "nothing"** (`src/refdb.cpp` `PickBestVersionRow` best-matches
  to a stale closed row; -1 only for a zero-row entity) — the detection condition is the column predicate
  (no interval covers V AND not deprecated AND not superseded), per D41 + policy.md §"Status is derived".

## The detection conditions (the data model the query rests on)

| Kind | Condition (current version V) |
|---|---|
| Uncovered-at-V orphan | No `address_versions` interval covers V (`valid_from <= V <= valid_through` OR `valid_through IS NULL`) AND `address_names.is_deprecated = 0` AND `superseded_by IS NULL`. |
| Never verified | An `address_versions` row with `last_verified_at_version IS NULL`. |
| Broken reference | `deprecation_replacement` / `superseded_by` pointing at a nonexistent or itself-incomplete entity. |

Bases that already exist: `validators.py:check_every_entity_covered` (coverage logic),
`validate_db_shape.py:interval_check_results` (interval validity). The detection query extends these to
the full version-relative set.

## Coverage map (every D41 design element → its step)

| Design element | Source | Covered by | Notes |
|---|---|---|---|
| E1 — close-intervals already-done skip (`valid_through == last_verified` → no edit-spec) | D41 fact 2; s08 reconciliation | 1.1 | mirrors verify-all's existing skip |
| E2 — already-acted-on reconciliation classification (derive from report version + DB state) | D41 fact 2 | 1.1 (logic) + 2.2 (preview surface) | |
| E3 — s08 already-acted row → "no further action" state | D41 fact 3; s08 States "Already acted on" | 3.4 | |
| E4 — s08 close→needs-action flag (a close that orphans flags it) | D41 fact 3; s08 "Close → needs-action" | 3.4 | |
| E5 — `[Fix ▸]` carries the divergence `detail` to s04 | D41 fact 3; s08 Fix-flow | 3.5 | |
| E6 — `[Fix ▸]` PUSHES s02/s04; `‹ back` restores the worklist report intact (no re-import) | D41 fact 3 + D42; s08 Fix-flow + law 10 | 3.5 | rests on the 3.2 back-stack |
| E7 — applied row shows the resulting value | D41 fact 3; s08 Fix-flow | 3.5 | |
| E8 — lifecycle-completeness detection query (orphan + never-verified + broken-refs at V) | D41 fact 1; s09 §Contents kinds | 1.2 | |
| E9 — backend needs-action read endpoint | D41 fact 1; s09 §Contents/Loading | 2.1 | |
| E10 — s09 view shell (placement, header, by-kind collapsible sections) | s09 §Region, §Contents | 3.1 | |
| E11 — s09 per-row resolution actions (PUSH s02/s04/s05 + `‹ back` return + drop-off) | s09 §Contents rows 35–46 | 3.3 | rests on the 3.2 back-stack |
| E12 — s01 `[Needs action ▸ N]` affordance + count badge | s09 §Contents row 29; s01 spec | 3.1 | |
| E13 — s09 empty = all-clear (success-framed) state | s09 States "Empty" | 3.1 | |
| E14 — s09 loading / error / disabled / edge states | s09 States | 3.1 | |
| E15 — in-app content back-stack (push/reset/`‹ back`/state-carrying frames; no URL router) | TRD D42; ui/design.md law 10 + law 2 | 3.2 | the navigation primitive 3.3/3.5 consume |
| E16 — version-new selector (linked-Bin-resolved version selectable, marked "from your linked Bin · new", ephemeral) | TRD D43; ui/design.md `select / dropdown` silhouette; s02 spec | 3.2 | |
| E17 — unsaved-changes guard on navigate-away from a dirty editor (Save/Discard/Cancel) | TRD D44; ui/design.md law 10; s02/s04/s08 "Unsaved-changes guard" states | 3.2 | |

Every element resolves to a step; no deferrals. (E15–E17 are the D42/D43/D44 navigation-redesign
elements added when the settled redesign — TRD `42867e4` + screen specs `52c9ddf` — made the in-app
back-stack a first-class build dependency; the former 3.2 blocker named exactly its absence.)
