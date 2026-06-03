# Step 8 — s02 entity detail (read) + version dropdown + default-row

**What.** Build the entity detail READ view (s02): on selection/drill from the navigator,
render the entity header (identity `kcdx_id`/`name` read-only — law 7; lifecycle flags shown
read-only, editing is step 14), the **version & verify surface** (the **version dropdown** —
a Mantine `Select` populated from the read API's game-version tags — the default way the
maintainer states the targeted version; the "check against a local DLL" control is placed but
its client resolver is step 11), and the version table (the entity's `address_versions` rows,
newest-first) with the **default row selected** = the newest authored row (highest
`valid_from_version`, D10; the picked/resolved version marks the matching row). Selecting a
version row drives the field editor (step 9). The `[Show history]` / `[Compare]` / `[+ New
version]` actions are placed but wired in Phase 4 (steps 15/12). On phone the header carries
`‹ back`.

**Scope.** The detail read view: header (read-only identity + read-only lifecycle), the
version dropdown + the default-row select, the version table, the select intent. No editing
(step 9), no lifecycle edit (step 14), no client DLL resolve (step 11), no history/compare/
create (Phase 4). Calls the read API (step 2).

**Test bar.** A component test (Vitest + Testing Library): the header renders read-only
identity + lifecycle; the version dropdown lists the game-version tags; the default row is the
newest `valid_from_version`; selecting a version row emits the select intent. Data is the read
API (mocked in unit; real at acceptance). Runnable now (the read API exists).

**Dependencies.** Step 7 (a selected entity from the navigator). Step 6 (the shell). Phase 2
step 2 (the read API — entity detail + version rows). Sequenced after step 7.

**Design authority.** [`data/maintainer-tool/ui/screens/s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
(the read-view contents — header + the version&verify surface + version table; the
lifecycle-edit rows are step 14's part of this screen). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-2/US-10 + §10 D10 (default newest row) + D15 (the version dropdown is the default; the
client DLL check is step 11). R8 (read-only triple).

**UX** (`.claude/rules/ux-first-class.md`, from s02):
- **Populated** — header + the version&verify surface (the dropdown) + the version table with
  the newest row selected; identity + lifecycle read-only (law 7, non-color).
- **Empty (no entity selected)** — wide: *"Select an entity from the list to view and edit
  it."*; phone: no empty detail (you arrive only by drilling in).
- **Loading / Error** — the entity's rows loading / a load-failed message + retry.
- **Verify states** — a picked version (the dropdown default), or "not verified against a
  DLL" (advisory, normal — the client check is step 11). No degraded mode (D9/D10).
- **Edge content** — many version rows scroll the version table, header fixed; a long
  signature wraps; a deprecated/superseded entity shows its state in the header.
- **Responsive** — wide: the right pane, the version&verify surface in the header beside the
  identity. Phone: the full-screen drill-down; the header stacks identity + the surface
  vertically + `‹ back`; the version table gets the full width (scrolls horizontally if
  needed).
- **Accessibility + consistency:** keyboard-navigable version table + touch; read-only
  identity conveyed by more than color (law 7); the version dropdown keyboard/touch-operable.

**Disassembler-test / author-burden.** N/A — read view; no author-facing input.
