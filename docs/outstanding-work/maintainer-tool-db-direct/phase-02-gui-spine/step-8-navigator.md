# Step 8 — s01 navigator (search + filters + entity list + status chips)

**What.** Build the left-pane entity navigator (s01): a search box filtering the list
by name OR `kcdx_id` as-you-type, a status filter (all / active / deprecated /
superseded) + a kind filter (all / the nine kinds), and the scrollable entity list
where each row shows name · `kcdx_id` (mono) · a status chip. The status is DERIVED
(not an authored column — `policy.md` §"Status is NOT an authored column") from the
entity's lifecycle flags + current-version verification; the chip uses color + glyph
(never color-alone, law 7). Selecting a row drives the detail pane (step 9). The
`+ New entity` action is rendered here but wired in Phase 3 (step 14).

**Scope.** The navigator pane: search, the two filters, the list rendering + selection
intent, the status-chip derivation. Calls the data-core for the curated set + the
derived status. No detail rendering (step 9), no editing. The `+ New entity` button is
placed but its flow is step 14.

**Test bar.** The status-derivation logic is headless (it reads authored columns →
status per `policy.md`) — covered by a `seeds_shared/` test (or extends an existing
one). The navigator's search/filter/list/selection rendering is verified at the
phase's user-facing acceptance gate.

**Test bar runnable now?** Yes — the status-derivation test runs now; the list
rendering is an eyeball gate at the phase acceptance (the curated set from step 7 is
the data it renders).

**Dependencies.** Step 7 (the loaded curated set + the window shell + the token
layer). Sequenced after step 7 so the list has data + a pane to render in.

**Design authority.** [`data/maintainer-tool/ui/screens/s01-navigator.md`](../../../../data/maintainer-tool/ui/screens/s01-navigator.md)
(the full contents table + states). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-1/US-2. `data/seeds/policy.md` §"Status is NOT an authored column" (the status
derivation).

**UX** (`.claude/rules/ux-first-class.md`, from s01):
- **Populated** — the filtered, scrollable list; selected row uses the
  `surface_selected` token + `body_strong`; hover uses `surface_hover`.
- **Empty (no search match)** — *"No entity matches '`<query>`'."* + clear-search
  (user-caused copy — distinct from the no-DB empty state, which is step 7's).
- **Loading / Error** — list-area progress / a load-failed message + retry.
- **Edge content** — a long name truncates with ellipsis (full on hover/in s02); the
  `kcdx_id` column is fixed mono width; the list scrolls smoothly at ~143+ rows.
- **Flow + feedback:** type to filter → pick a row → the detail pane fills (step 9).
  Selection feedback is immediate; the status chip tells lifecycle at a glance.
- **Accessibility + consistency:** keyboard-navigable list (Up/Down + Enter), labelled
  search + filters, status chip is glyph+color (law 7).

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
