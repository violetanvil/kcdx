# Step 7 — s01 navigator (search + filters + entity list + status chips)

**What.** Build the entity navigator (s01): a search box filtering by name OR `kcdx_id`
as-you-type, a status filter (all / active / deprecated / superseded) + a kind filter
(all / the nine kinds), and the scrollable entity list (Mantine `NavLink`/rows) where each
row shows name · `kcdx_id` (mono) · a status `Badge`. Status is DERIVED (from the read API,
step 2 — `policy.md`); the chip uses color + glyph (never color-alone, law 7). Selecting a
row drives the detail (step 8) — on phone it **drills** to the full-screen detail. The
`+ New entity` action is placed but wired in Phase 4 (step 13). Binds the Phase-2 read API.

**Scope.** The navigator: search, the two filters, the list rendering + selection intent, the
status-chip rendering. Calls the read API (step 2). No detail rendering (step 8), no editing.
`+ New entity` placed; its flow is step 13.

**Test bar.** A component test (Vitest + Testing Library): the search filters the list as
typed; the filters narrow by status/kind; a row renders name/kcdx_id/status-chip; selecting
a row emits the select intent. The data is the Phase-2 read API (mocked in the unit test;
real at the phase acceptance). Runnable now (the read API exists, step 2).

**Dependencies.** Step 6 (the shell + theme + API client). Phase 2 step 2 (the read API).
Sequenced after step 6.

**Design authority.** [`data/maintainer-tool/ui/screens/s01-navigator.md`](../../../../data/maintainer-tool/ui/screens/s01-navigator.md)
(the full contents table + states + responsive behavior). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-1/US-2. `policy.md` §"Status is NOT an authored column".

**UX** (`.claude/rules/ux-first-class.md`, from s01):
- **Populated** — the filtered list; selected row uses `surface_selected`; hover
  `surface_hover`.
- **Empty (no search match)** — *"No entity matches '`<query>`'."* + clear-search (user-caused
  — distinct from the no-DB empty state).
- **Loading / Error** — list-area Loader/skeleton / a load-failed message + retry (the API's
  reason).
- **Edge content** — a long name truncates with ellipsis; the `kcdx_id` column fixed mono
  width; the list virtualizes/scrolls smoothly at ~143+ rows on a phone viewport.
- **Responsive** — wide: the left pane (selection fills the right detail pane). Phone: the
  home view; tapping a row drills to s02 full-screen (a `‹ back` returns, selection + scroll
  preserved — law 1/3).
- **Accessibility + consistency:** keyboard-navigable list (Up/Down + Enter) + touch-operable;
  labelled search + filters; status chip is glyph+color (law 7).

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
