# Step 9 — s02 entity detail (header + version table + default-row select)

**What.** Build the right-pane entity detail READ view (s02): on selection from the
navigator, render the entity header (identity `kcdx_id`/`name` read-only — law 7;
lifecycle flags shown read-only here, editing is step 15), and the version table (the
entity's `address_versions` rows, newest first) with the default row selected. Default
selection is the **newest authored row** (highest `valid_from_version`) — D10; the
DLL-resolver-matched "current" marker is added in Phase 3 (step 12). Selecting a
version row drives the field editor (step 10). The `[Show history]` / `[Compare
versions]` / `[+ New version]` actions are placed but wired in Phase 3 (steps 16/13).

**Scope.** The detail pane's read rendering: header (read-only identity + read-only
lifecycle), the version table, the default-row + selection intent. No editing (step 10
makes the selected row editable), no lifecycle edit (step 15), no history/compare/new
(Phase 3). Calls the data-core for the entity's rows.

**Test bar.** The default-row selection logic (newest `valid_from_version`) is
headless-checkable (a small data-core helper or a tested selector). The detail
rendering (header, version table, read-only treatment) is verified at the phase's
user-facing acceptance gate.

**Test bar runnable now?** Yes — the newest-row selector test runs now; the rendering
is an eyeball gate (the entity rows from the data-core are its data).

**Dependencies.** Step 8 (a selected entity from the navigator). Step 7 (the pane +
tokens). Sequenced after step 8.

**Design authority.** [`data/maintainer-tool/ui/screens/s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
(the read-view contents — header + version table; the lifecycle-edit rows are step
15's part of this screen). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-2 + §10 D10 (default newest row). R8 (read-only triple). `data/seeds/policy.md`
§"valid_from_version vs. last_verified_at_version".

**UX** (`.claude/rules/ux-first-class.md`, from s02):
- **Populated** — header + version table with the newest row selected; identity +
  lifecycle read-only (law 7, non-color affordance).
- **Empty (no entity selected)** — *"Select an entity from the list to view and edit
  it."* (not a blank pane).
- **Loading / Error** — the entity's rows loading / a load-failed message + retry.
- **Edge content** — an entity with many version rows scrolls the version table while
  the header stays fixed (law 1 — no reflow); a deprecated/superseded entity shows its
  state prominently in the header; a long signature wraps within its field.
- **Flow + feedback:** pick an entity → header + versions appear, newest selected →
  pick a version row → the field editor (step 10) fills. Selection is immediate.
- **Accessibility + consistency:** keyboard-navigable version table (Up/Down + Enter);
  read-only identity conveyed by more than color (law 7); standard token styling.

**Disassembler-test / author-burden.** N/A — read view; no author-facing input.
