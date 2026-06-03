# Step 15 — s03 version history + side-by-side compare

**What.** Build the version history + compare (s03): `[Show history]` expands the full
`address_versions` list (newest first, a Mantine `Table`); `[Compare versions]` enters
multi-select → `[Compare (N)]` shows the selected versions as **N columns side-by-side** (a
`Table` with a field-name gutter + one column per version, in a `ScrollArea`), full records,
with **differing fields marked** (a glyph + a `diff_band` row band — not color-alone, law 7);
identical fields render plain. The compare reads `address_versions` rows (game-version DATA
rows, NOT git history — from the Phase-2 read API) and diffs their columns dynamically. The
column count drives **dynamic horizontal scroll** (law 1 — the primary fit mechanism on a
phone). A column is **editable in place** via `[Edit <version>]`, which enters the s04 field
editor (a full-screen sheet on phone) with the edit-existing confirmation → the spine confirm/
commit (Phase 3 step 10).

**Scope.** The history expander + the compare multi-select + the N-column diff `Table` + the
diff marking + the dynamic horizontal scroll + the edit-from-compare entry (reusing the field
editor + the spine save). No new save shape (it reuses the version-row UPDATE via the field
editor). Completes s03.

**Test bar.** A component test (Vitest + Testing Library): the history list renders newest
first; compare multi-select → the N-column table; differing fields are marked (glyph + band);
identical fields plain; `[Edit <version>]` enters the edit-existing flow. The differing-fields
computation reuses the field-delta logic pairwise (the Phase-2 field-delta API, or a client
pairwise diff over the read rows). Runnable now (the read API + the field editor + the spine
exist).

**Dependencies.** Phase 3 step 8 (the version table + the `[Show history]`/`[Compare]`
placements) + step 9 (the field editor for edit-from-compare) + step 10 (the spine save).
Phase 2 step 2 (the read API — version rows) + step 3 (field-delta, reused for the compare
diff). Sequenced after the spine + the create/lifecycle jobs.

**Design authority.** [`data/maintainer-tool/ui/screens/s03-version-history-compare.md`](../../../../data/maintainer-tool/ui/screens/s03-version-history-compare.md)
(the history list, the compare multi-select + N-column table + diff marking + edit-from-
compare). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-9. R8 (the read-only triple in the compare columns).

**UX** (`.claude/rules/ux-first-class.md`, from s03):
- **Populated (history)** — the full version list, newest first, current row marked.
- **Populated (compare)** — the N-column diff table; ≥2 columns; differing fields banded +
  glyph-marked; identical plain (full records visible).
- **Empty (one version)** — *"This entity has one version."*; Compare disabled with *"Need at
  least two versions to compare."* (zero/one/many edge).
- **Loading / Error** — version rows loading / a row that failed to load shows *"Couldn't load
  version `<v>`."*, the others still render.
- **Disabled** — `[Compare]` disabled until ≥2 selected; copy names why.
- **Edge content** — many versions → dynamic horizontal scroll (law 1, never column collapse
  that reflows); a long signature wraps within its cell, the column width holds; 3+ columns
  scroll rather than shrinking below legibility.
- **Responsive** — wide: 2–3 columns fit inline; more scroll. Phone: within the full-screen
  drill-down; the `ScrollArea` horizontal scroll is the primary fit (the field-name gutter
  stays sticky-left); `[Edit <version>]` opens s04 as a full-screen sheet.
- **Accessibility + consistency:** keyboard-navigable history + compare columns + touch; the
  diff marker is a glyph (not color-alone, law 7); read-only identity columns hold the
  read-only treatment.

**Disassembler-test / author-burden.** N/A — viewing + comparing existing rows; editing from
compare reuses the existing-row editor (no NEW game-function offset authored).
