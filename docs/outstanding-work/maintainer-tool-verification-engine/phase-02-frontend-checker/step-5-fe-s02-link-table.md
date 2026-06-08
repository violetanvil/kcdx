# 2.5 [FE] s02 install-set link surface (Bin-folder pick) + version-match gate + the layout + verify states

## What

Build the s02 version&verify surface as the **install-set link model** (D30, revised) inside the
**compact-header + collapsible-section layout** (the s02 layout revision). The maintainer **links
the game's Bin FOLDER once** (a single in-session `<input webkitdirectory>` directory pick — the
DLLs read in-page, NEVER uploaded, D15/D26); the tool reads **WHGame.dll** to resolve the install
version (the existing `.rdata` scan) and finds **each referenced module's DLL by its filename** in
that folder. A **non-WHGame module inherits the install's version from WHGame.dll** (the CryEngine
DLLs carry no KCD2 version string — D30). The surface is a **compact pinned header** (identity +
the version `Select` + a one-line verify summary) + a **collapsible "Verify against a DLL" section**
(collapsed by default) holding the folder pick + the `per-module link row` ×M. The **version-match
gate** (a module's check is available only when its DLL is in the folder AND the install version
matches the selected row — D30) is exposed for the s04 verdict (step 6). The verdict renders in s04
(step 6), NOT here. First UI step — wires the static checker (steps 1–4) into the surface that
supplies its DLL bytes.

## Scope

One commit in the frontend repo: the **compact pinned header** (identity + version `Select` + the
one-line verify summary), the **collapsible "Verify against a DLL" section** (the `collapsible
section` silhouette — collapsed by default), the **Bin-folder pick** (`<input webkitdirectory>`,
read in-page) + WHGame.dll version resolve + the per-module DLL-by-filename lookup, the
**`per-module link row` ×M** with its **reflow-safe structure** (a stable top line + the verify
message in reserved space below), the install-version-inherited **version-match indicator**, the
in-memory per-session pick state (re-pick each session, no persistence — D30), and the
**version-match gate** exposed for step 6. Built to the revised s02 screen spec + the revised
Layer-1 silhouettes. NOT in this step: the s04 verdict badge (step 6); the link-to-create prompt
(step 7 — the mismatch STATE renders, but not the `[Add a version row]` flow). The **collapsible
"Lifecycle" section** wrapper is part of this layout rework (the lifecycle CONTENT already exists;
this step moves it into the collapsible section + adds the compact-header/work-surface split).

## Test bar

Vitest unit/component tests in the frontend repo: the folder pick reads the Bin folder in-page (no
network call — the no-upload invariant) and resolves the install version from WHGame.dll; each
referenced module's DLL is found by filename in the folder; the `per-module link row` renders each
verify state (no-folder / folder-resolving / not-a-Bin-folder / resolve-failure / per-module
DLL-not-found / version-match / version-mismatch) with the spec copy; a non-WHGame module's row
shows the **inherited** install version; the **version-match gate** returns "check available" only
when the module's DLL is present AND the install version matches the selected row (false on every
other state); the **reflow-safe row** holds the affordance position when a long verify message
wraps (the affordance never shifts off the row — law 1); the **collapsible section** expands in
place without moving its header (law 1) and is keyboard-operable (Enter/Space). Runnable at this
step (the resolver + checker exist) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`, `.claude/rules/spec-conformance.md`.

## Dependencies

- **2.1–2.4** — the static checker (the surface gates its availability + feeds it the module DLL bytes).
- The existing `versionResolver.ts` (the WHGame.dll `.rdata` version scan feeding the install version).
- The existing s02 `EntityDetail` + `DllCheckControl` (extended — the single-DLL pick→read→resolve
  mechanism is reused for the folder pick; the surface is re-laid-out to the compact-header +
  collapsible model).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group F (install-set link, version-match gate, degraded) +
Group H (s02 verify-surface UX + the layout) + cross-step invariants 1 (no upload) + 2 (advisory).

## Design authority

`data/maintainer-tool/ui/screens/s02-entity-detail.md` §"Region & position" (the compact pinned
header + the collapsible sections + the work-surface split) + §"The version & verify surface" (the
Bin-folder pick install-set, the per-module rows, the install-version inheritance, link-to-create)
+ §"Contents" (the compact summary + the collapsible Verify/Lifecycle sections + the per-module
row) + §"States & variants" (the verify states — no-folder / folder-resolving / not-a-Bin-folder /
resolve-failure / per-module DLL-not-found / version-match / version-mismatch; the section
collapsed/expanded state; the per-module reflow-safe state). Plus `data/maintainer-tool/ui/design.md`
§"Responsiveness & sizing" (the detail-pane lead-with-the-work-surface model) + the `collapsible
section` / `version & verify surface` / `per-module link row` silhouettes (Layer-1) + law 1
(no reflow) / law 4 (advisory) / law 7 (glyph+text). The functional model is `design.md` **D30**
(revised — the install-set, the folder pick, the version inheritance, the new-module step) + **§9**
(multi-store out of scope). Build to these REVISED sections, not to this doc's summary.

## UX

Carried from the revised s02 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Compact pinned header** — identity (read-only, law 7) + the version `Select` + a **one-line
  verify summary** ("Bin folder linked — `<version>` ✓" / "no folder linked"); always visible, the
  verify state glanceable without expanding the section. On phone it carries `‹ back`.
- **Collapsible "Verify against a DLL" section** (collapsed by default) — the `collapsible section`:
  a clickable header (label + chevron, glyph+text — law 7) that expands the body IN PLACE; the
  header row never moves, the work surface above is never reflowed (law 1 — a user-toggled
  disclosure); keyboard-operable (Enter/Space). Holds the folder pick + the per-module rows.
- **No folder linked (degraded)** — "link the game Bin folder to verify"; never a block (law 4 / D30);
  authoring proceeds.
- **Folder resolving (loading)** — a brief loading indication while the browser reads the folder's
  DLLs (the WHGame.dll `.rdata` scan over the ~86 MB ArrayBuffer — law 1, the surface reserves its
  space, no reflow).
- **Not a KCD2 Bin folder (error)** — "that folder has no WHGame.dll — pick the game Bin folder"
  (WHGame.dll absent → the install version can't resolve; system-caused).
- **Resolve failure (error)** — "couldn't resolve a version from WHGame.dll (interns disagree)"
  (the folder has WHGame.dll but its `.rdata` gave no agreed version; advisory + override).
- **Per-module link row** — a **stable top line** (the module name (mono) · the linked status
  found/not-found · the version-match glyph) that never moves, with the verify **message in reserved
  multi-line space below** (a long message grows downward, never pushing the affordance off the row
  — law 1, the reflow-safe structure). Per-module states: **DLL not found** — "`<module>` not found
  in the linked folder" (that module's check unavailable, the others + authoring proceed; advisory);
  **version match** — "`<module>` (`<install-version>`) ✓ matches" (the s04 check is now available
  for that module); **version mismatch** — "`<module>` (`<install-version>`) ≠ no row at this
  version" (surfaces the step-7 link-to-create prompt — NOT built here).
- **New module** — a CryEngine module not yet in the `module` table is registered as a surfaced
  step when the maintainer authors the first address for it (the AP18 deliberate-addition posture,
  law 8 — D30); the link table then shows its row, found by name in the linked folder.
- **Edge** — a long DLL filename / verify message wraps within its row without pushing siblings
  (law 1); every link/re-pick affordance is keyboard-reachable + touch-operable; read-only/match
  state conveyed by more than color (law 7).

## Disassembler-test / author-burden

The surface adds a "link the Bin folder" affordance, not a hex/offset/signature input — the
maintainer picks a folder; the engine reads the install version + (in s04) verifies each row
against its module's DLL. No author hand-hex; consistent with the disassembler-test direction (the
engine does the binary work, the author declares intent).
