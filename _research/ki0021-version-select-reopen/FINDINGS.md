# KI-0021 test-harness probe — version Select reopen behavior (CreateEntityOverlay)

**Question:** when the `last_verified_at_version` version Select (FieldRow `render:"version"`,
`searchable`, `withinPortal:false`) is reopened after it already holds a value, is the "Custom…"
option still reachable via `screen.getByRole("option", { name: "Custom…" })`? (The clear-direction
KI-0021 regression test failed at the second `pickVersion("Custom…")`.)

**Outcome map (probe, jsdom + @testing-library, Mantine v8 Select):**
- A — first open (no value): `Custom…` present → TRUE.
- B — reopen (holds "1.5.1164953"): options = `["Custom…","1.5.1164953","1.4.0"]` → **Custom… IS present.**
- C — after typing "Cust" into the searchable input: options = `["Custom…"]` → filters fine.

**Finding:** the widget reopen is NOT the cause. A plain `screen.getByRole("option",{name:"Custom…"})`
finds the option on reopen in a clean sequence (the probe). The clear-direction TEST's failure came
from an intervening `await screen.findByTestId("editor-field-verified_date-readonly")` between the two
`pickVersion` calls — a re-render/timing interaction in that specific sequence, NOT a widget filter.
Fix: drive the reopen the same way the probe does (the proven `pickVersion` helper, immediate), and if
an intervening await is needed, re-query the input fresh inside the helper (which it already does).

**Reusable wiring:** the probe is a minimal CreateEntityOverlay mount that fills name+kind+module, then
opens/reopens the version Select and dumps `queryAllByRole("option")` at each step. Reconstruct from
this finding; do not re-add to src/ (vitest only scans src/, so a probe lives there transiently and is
removed — no-residue per working-artifacts).

**Resolution backlink:** docs/known-issues/closed/KI-0021-*.md §Resolution (the clear-direction test).
