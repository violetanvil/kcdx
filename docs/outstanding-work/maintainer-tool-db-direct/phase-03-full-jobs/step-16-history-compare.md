# Step 16 — s03 version history + side-by-side compare

**What.** Build the version history + compare (s03): `[Show history]` expands the full
`address_versions` list (newest first); `[Compare versions]` enters multi-select →
`[Compare (N)]` shows the selected versions as **N columns side-by-side** (field-name
gutter + one column per version), full records, with **differing fields marked**
(a glyph + a `diff_band` row band — not color-alone, law 7); identical fields render
plain. The compare reads `address_versions` rows (game-version DATA rows, NOT git
history) and diffs their columns dynamically. The column count drives **dynamic
horizontal scroll** when content exceeds pane width (law 1 — non-compare elements don't
reflow). A column is **editable in place** via `[Edit <version>]`, which enters the s04
field editor with the edit-existing confirmation (step 10's flow) → the spine confirm/
commit (step 11).

**Scope.** The history expander + the compare multi-select + the N-column diff table +
the diff marking + the dynamic horizontal scroll + the edit-from-compare entry (reusing
step 10's editor + step 11's save). No new write shape (it reuses the version-row UPDATE
via the field editor). Completes s03.

**Test bar.** The diff computation (which fields differ across the compared set) is
headless-checkable — a small pure helper (or reuse the field-delta unit, step 6,
pairwise across the set) with a `seeds_shared/` test. The GUI compare rendering
(N columns, the marking, the dynamic scroll, edit-from-compare) is verified at the
phase's user-facing acceptance gate.

**Test bar runnable now?** Yes — the differing-fields helper test runs now; the compare
rendering is an eyeball gate (the entity's version rows + the field editor exist).

**Dependencies.** Step 9 (the version table + the `[Show history]`/`[Compare]`
placements) + step 10 (the field editor for edit-from-compare) + step 11 (the spine
save). Phase 1 step 6 (the field-delta, reused pairwise for the compare diff).
Sequenced after the spine + the create/lifecycle jobs (it reuses the editor; ordered
last in Phase 3).

**Design authority.** [`data/maintainer-tool/ui/screens/s03-version-history-compare.md`](../../../../data/maintainer-tool/ui/screens/s03-version-history-compare.md)
(the history list, the compare multi-select + N-column table + diff marking +
edit-from-compare). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-9. R8 (the read-only triple in the compare columns).

**UX** (`.claude/rules/ux-first-class.md`, from s03):
- **Populated (history)** — the full version list, newest first, current row marked.
- **Populated (compare)** — the N-column diff table; ≥2 columns; differing fields
  banded + glyph-marked; identical fields plain (full records visible).
- **Empty (one version)** — *"This entity has one version."*; Compare disabled with
  *"Need at least two versions to compare."* (zero/one/many edge handled).
- **Loading / Error** — version rows loading / a row that failed to load shows
  *"Couldn't load version `<v>`."*, the others still render.
- **Disabled** — `[Compare]` disabled until ≥2 selected; copy names why.
- **Edge content** — many versions → dynamic horizontal scroll (law 1, never column
  collapse that reflows); a long signature wraps within its cell, the column width
  holds; 3+ columns scroll rather than shrinking below legibility.
- **Flow + feedback:** Show history → the list; Compare → multi-select → N columns,
  diffs marked; Edit a column → the field editor (with the edit-existing confirmation)
  → save via the spine. Selection + marking are immediate.
- **Accessibility + consistency:** keyboard-navigable history + the compare columns;
  the diff marker is a glyph (not color-alone, law 7); read-only identity columns hold
  the read-only treatment.

**Disassembler-test / author-burden.** N/A — viewing + comparing existing rows; editing
from compare reuses the existing-row editor (no NEW game-function offset authored).
