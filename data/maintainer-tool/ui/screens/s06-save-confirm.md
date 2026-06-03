# s06 — Save confirm (field delta · approval) + the toast/overlay concern

**Phase & fidelity:** v1, high.

## Purpose / when shown
The confirm gate every mutation passes through (law 5), an overlay shown when the maintainer
hits `Review changes` in s04 or s05. It is the moment of "absolute clarity about what's
changing": a plain-language list of exactly which fields change, `old → new`. On Confirm the
backend runs the whole atomic transaction (validate → write DB → export CSVs → round-trip →
commit + push, server-side — D16). The maintainer reasons about record fields here — never
CSV cells, never git. **This screen also owns the toast concern** — the save RESULT (and
non-blocking notices) surface as a top-anchored toast, since the desktop status bar (s07) is
dissolved.

## Region & position
An **overlay surface** (`Modal` centered on wide, full-screen sheet on phone — law 2: dims,
never permanently displaces). Sized to its content; the body scrolls internally if many
fields changed. Dismissed back to the originating editor state. The **toast** is the
separate top-anchored `Notification` layer (not part of the modal) — it appears AFTER the
modal closes on a successful save.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Title | `heading` | "Confirm changes — `<name>` `<version>`" | — |
| Field-delta row | `field-delta list` item ×N | each changed field: `label  old → new` | — |
| Unchanged note | `caption` | "`<k>` fields unchanged" (collapsed) | — |
| Approval block | `warning banner` (`Alert`, law-8, conditional) | for a NEW entity/version only | `acknowledge_new_row()` |
| `[Confirm]` / `[Save]` | `button` (primary) | runs the transaction (disabled until approval acknowledged, if shown) | `commit_change()` |
| `[Cancel]` | `button` (subtle) | — | `cancel()` → back to the editor, nothing lands |
| Save-result toast | `toast` (`Notification`, top) | "Saved `<name> <version>`" / "Save blocked — Retry" | `retry_save()` (when blocked) |

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

**The override (law 4):** if the change carries an unresolved verify state (a version picked
but not checked against a DLL, or a resolver failure — D15), the modal restates it —
*"Saving without verifying this version against a game DLL."* — and the `[Confirm]` action
reads **"I accept — save anyway"**. The maintainer's explicit acknowledgment is the
override; it is never a silent bypass.

## The toast concern (the dissolved s07's result surface)
The save RESULT is a **top-anchored toast** (`Notification`), not a persistent bar:
- **Success** — *"Saved `<name> <version>`"* (`success`), auto-dismisses after a few
  seconds; the git short-hash is NOT shown (law 5 — git invisible).
- **Blocked** — *"Save blocked — the files are locked by another process. Retry."*
  (`error`) + a `[Retry]` action; persists (does not auto-dismiss) until retried; never
  reaps the lock (`../design.md` §8, D16). The maintainer doesn't learn it's "git" — it's
  "the files are locked".
- **Non-blocking notices** — a transient one-line info/warning toast, auto-dismisses.
The toast is top-anchored so it stays out of the thumb-reach of the primary action on a
phone (law: touch); it floats above every layout and navigates nothing (law 3).

## States & variants
- **Populated (update)** — the field delta + Confirm/Cancel; no approval banner.
- **Populated (new row)** — the field delta + the law-8 approval banner; Confirm disabled
  until acknowledged.
- **Committing** — after Confirm: the overlay shows a brief "Saving…" state (the backend
  transaction + push run); controls disabled. On success the overlay closes and the success
  toast appears.
- **Write failure** — the atomic transaction failed and rolled back: the overlay shows
  *"Save failed — nothing was written. `<reason>`."* (the backend's reason); Confirm becomes
  Retry; the DB + CSVs are in their pre-action state (law 5). System-caused copy.
- **Save blocked** — a live shared-index lock: the overlay closes and the blocked toast
  (above) appears with `[Retry]` (`../design.md` §8, D16).
- **Override-required** — the unverified/resolver-failure variant: Confirm reads "I accept —
  save anyway" (law 4, D15).
- **Edge content** — many changed fields → the field-delta list scrolls within the overlay;
  a long `old → new` value wraps within its row.

## Links in / out
- **In:** s04 `review_changes`; s05 `review_changes` (new entity/version).
- **Out:** `commit_change` → the atomic backend transaction → the **save-result toast**
  ("Saved `<name> <version>`" or "blocked — Retry"); on success the overlay closes back to
  s02 with the row updated; `Cancel` → back to the editor, nothing lands.

## Applicable laws
- **Law 4** — an unresolved verify state surfaces the explicit "save anyway" override.
- **Law 5** — Confirm runs ONE atomic transaction (incl. the server push); failure rolls
  back fully; git is invisible.
- **Law 6** — the change has already passed the shared validator (s04/s05, via the API); the
  overlay re-asserts nothing it didn't.
- **Law 8** — a new entity/version requires the approval acknowledgment before Confirm.
- **Law 9** — tokens only.

## Responsive behavior
- **Wide:** a centered `Modal`; the toast top-anchored.
- **Phone:** a full-screen sheet (the field delta gets the full viewport; `[Confirm]`/
  `[Cancel]` pinned reachable at the bottom; the approval acknowledge + override read
  clearly without a cramped modal); the toast top-anchored (out of thumb-reach of the
  bottom-pinned primary action).
