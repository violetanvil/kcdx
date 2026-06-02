# s06 — Save confirm (field delta · approval)

**Phase & fidelity:** v1, high.

## Purpose / when shown
The confirm gate every mutation passes through (law 5), shown as a modal when the
maintainer hits `Review changes` in s04 or s05. It is the moment of "absolute clarity
about what's changing": a plain-language list of exactly which fields change, `old → new`.
On Confirm the tool runs the whole atomic transaction (validate → write DB → export CSVs →
round-trip → commit). The maintainer reasons about record fields here — never CSV cells,
never git.

## Region & position
A modal card over the panes (law 2 — dims, never displaces). Sized to its content;
scrolls internally if many fields changed. Dismissed back to the originating editor state.

## Contents
| Element | Component | Data bound | Intent emitted |
|---|---|---|---|
| Title | `heading` | "Confirm changes — `<name>` `<version>`" | — |
| Field-delta row | `field-delta list` item ×N | each changed field: `label  old → new` | — |
| Unchanged note | `caption` | "`<k>` fields unchanged" (collapsed) | — |
| Approval block | `warning banner` (law-8, conditional) | for a NEW entity/version only | `acknowledge_new_row()` |
| `[Confirm]` / `[Save]` | `primary button` | runs the transaction (disabled until approval acknowledged, if shown) | `commit_change()` |
| `[Cancel]` | `secondary button` | — | `cancel()` → back to the editor, nothing lands |

**The field delta (the decided surface):** the modal lists ONLY the changed fields,
semantically — `last_verified_at_version  1.4 → 1.5`, `evidence_kind  maintainer_ghidra →
live_test_plugin`. Values render in `mono` where the data is tabular (versions, rva,
signatures). The literal CSV diff is verified by the round-trip oracle and lands in the git
commit for a reviewer — it is NOT shown here (this supersedes the TRD's earlier
"CSV-diff-as-signal"; see `../design.md` §7 + `../changelog.md`). The field delta IS the
human-facing acceptance signal.

**The approval gate (law 8):** when the change CREATES a new entity or new version row, the
modal shows the approval banner — *"This creates a new `<entity|version>` in the Address
Library. Confirming commits the project to maintaining it across game versions."* — with an
explicit acknowledge control; `[Confirm]` stays disabled until acknowledged (AP18,
`policy.md`). An UPDATE to an existing row shows no approval banner (law 8 gates additions
only).

**The override (law 4):** if the change carries an unresolved verify state (no DLL linked,
or a resolver failure), the modal restates it — *"Saving without verification against a
game version (no DLL linked)."* — and the `[Confirm]` action reads **"I accept — save
anyway"**. The maintainer's explicit acknowledgment is the override; it is never a silent
bypass.

## States & variants
- **Populated (update)** — the field delta + Confirm/Cancel; no approval banner.
- **Populated (new row)** — the field delta + the law-8 approval banner; Confirm disabled
  until acknowledged.
- **Committing** — after Confirm: the modal shows a brief "Saving…" state (the transaction
  runs); controls disabled. (Result surfaces in the status bar, s07 — the modal closes on
  success.)
- **Write failure** — the atomic transaction failed and rolled back: the modal shows
  *"Save failed — nothing was written. `<reason>`."* (the validator/writer's reason);
  Confirm becomes Retry; the DB + CSVs are in their pre-action state (law 5). System-caused
  copy.
- **Commit blocked** — a live git index lock (another process holds it): *"Save blocked —
  the files are locked by another process. Retry in a moment."* + `[Retry]`; never reaps
  the lock (`../design.md` §8). The maintainer doesn't learn it's "git" — it's "the files
  are locked".
- **Override-required** — the unlinked/resolver-failure variant: Confirm reads "I accept —
  save anyway" (law 4).
- **Edge content** — many changed fields → the field-delta list scrolls within the modal;
  a long `old → new` value wraps within its row.

## Links in / out
- **In:** s04 `review_changes`; s05 `review_changes` (new entity/version).
- **Out:** `commit_change` → the atomic transaction → s07 status "Saved `<name> <version>`"
  (or "blocked — Retry"); on success the modal closes back to s02 with the row updated;
  `Cancel` → back to the editor, nothing lands.

## Applicable laws
- **Law 4** — an unresolved verify state surfaces the explicit "save anyway" override.
- **Law 5** — Confirm runs ONE atomic transaction; failure rolls back fully; git is
  invisible.
- **Law 6** — the change has already passed the shared validator (s04/s05); the modal
  re-asserts nothing it didn't.
- **Law 8** — a new entity/version requires the approval acknowledgment before Confirm.
- **Law 9** — tokens only.
