# Step 5 — entity list + pick + current-row-first/full-history + read-only triple (US-2)

**What.** Implement US-2: the maintainer browses the curated entity list and
picks one to re-verify. On selection the screen shows the entity's **current-
version row first** (the row whose `[valid_from, valid_through]` interval contains
the linked module's resolved ordinal — R12), with a separate action revealing the
**full version history** (every `address_versions` row for the entity). The three
read-only fields — `kcdx_id`, `name`, `valid_from_version` — render visibly
non-editable (R8). This step wires in the existing `version_resolver.py` (the
`.rdata` scan — R12, already built) for the current-row interval filter, with the
degraded-mode fallback when a module is unlinked.

**Scope.** The list surface (searchable/scannable), selection → row view, the
current-row-first + full-history toggle, the read-only-field rendering, and the
version-resolver wiring (CONSUME `version_resolver.py`; do not rebuild it). No
editing yet (step 6), no save (step 7). The audit-trio fields render here but are
not yet editable.

**Test bar.** The current-row interval-filter logic + the degraded-mode fallback
are headless data-core logic — covered by a `tests/test_*.py` (the resolver
already has `test_version_resolver.py`; the interval-contains-ordinal filter gets
its assertion there or in a sibling). The GUI rendering (list, history toggle,
read-only styling) is verified at the phase's user-facing acceptance gate.

**Dependencies.** Step 4 (the loaded curated set + the window shell). The existing
`version_resolver.py` (present). Sequenced after step 4 so the list has data to
render.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-2 + the current-version-row-resolution paragraph (R12 interval filter +
degraded mode) + §7 (the populated + edge states) + R8 (the read-only triple, the
current-row-first + full-history view). `policy.md` §"valid_from_version vs.
last_verified_at_version" (why `valid_from_version` is read-only).

**UX** (`.claude/rules/ux-first-class.md`, from design §7):
- **Populated** — the curated entity list (searchable); on selection, the
  entity's current-version row with the audit-trio fields shown (editable in step
  6) and `kcdx_id` / `name` / `valid_from_version` rendered read-only — visually
  distinct, not merely disabled-looking (conveyed by more than color).
- **Edge content** — an entity with multiple version-history rows: the
  all-versions view handles zero/one/many rows; long names/signatures don't break
  the layout. (Today most entities have one row; the view is meaningful once Job 6
  lands, but the layout handles many now.)
- **Degraded mode (R12)** — module not linked: the current-row filter has no
  answer, so show ALL rows for the entity with a "module not linked; showing all
  versions" notice — the maintainer knows why the filter is degraded.
- **Flow + feedback:** pick from the list → current row appears → optional "show
  full history" reveals the rest. Selection feedback is immediate.
- **Accessibility + consistency:** keyboard-navigable list + selection; labels on
  every field; the read-only state has a non-color affordance.

**Disassembler-test / author-burden.** N/A — the version resolver derives the
current game version from the linked DLL's `.rdata` (the engine carries the
mechanism, R12); the maintainer supplies no address/offset/signature.
