# s06 — Save confirm (field delta · approval) + the toast/overlay concern

**Phase & fidelity:** v1, high.

## Purpose / when shown
The confirm gate every mutation passes through (a mutation is one atomic, confirmed
transaction), an overlay shown when the maintainer hits `Review changes` in s04 or s05. It is
the moment of "absolute clarity about what's changing": a plain-language list of exactly which
fields change, `old → new`. On Confirm the backend runs the whole atomic transaction (validate
→ write DB → export CSVs → round-trip → commit + push, server-side). The maintainer reasons
about record fields here — never CSV cells, never git. **This screen also owns the toast
concern** — the save RESULT (and non-blocking notices) surface as a top-anchored toast, since
the desktop status bar (s07) is dissolved.

## Region & position
An **overlay surface** (`Modal` centered on wide, full-screen sheet on phone — the overlay
layer dims, never permanently displaces the navigation shell). Sized to its content; the body
scrolls internally if many fields changed. Dismissed back to the originating editor state. The
**toast** is the separate top-anchored `Notification` layer (not part of the modal) — it
appears AFTER the modal closes on a successful save.

## Contents
| Element | Component (Mantine) | Data bound | Intent emitted |
|---|---|---|---|
| Title | `heading` | "Confirm changes — `<name>` `<version>`" | — |
| Field-delta row | `field-delta list` item ×N | each changed field: `label  old → new` | — |
| Unchanged note | `caption` | "`<k>` fields unchanged" (collapsed) | — |
| Approval block | `warning banner` (`Alert`, new-row approval gate, conditional) | for a NEW entity/version only | `acknowledge_new_row()` |
| `[Confirm]` / `[Save]` | `button` (primary) | runs the transaction (disabled until approval acknowledged, if shown) | `commit_change()` |
| `[Cancel]` | `button` (subtle) | — | `cancel()` → back to the editor, nothing lands |
| Save-result toast | `toast` (`Notification`, top) | "Saved `<name> <version>`" / "Save blocked — Retry" | `retry_save()` (when blocked) |

**The field delta (the decided surface):** the modal lists ONLY the changed fields,
semantically — `last_verified_at_version  1.4 → 1.5`, `evidence_kind  maintainer_ghidra →
live_test_plugin`. Values render in `mono` where the data is tabular (versions, rva,
signatures). The literal CSV diff is verified by the round-trip oracle and lands in the git
commit for a reviewer — it is NOT shown here (the field delta, not a raw CSV diff, is the
signal). The field delta IS the human-facing acceptance signal.

**The approval gate (new-row gate):** when the change CREATES a new entity or new version row,
the modal shows the approval banner — *"This creates a new `<entity|version>` in the Address
Library. Confirming commits the project to maintaining it across game versions."* — with an
explicit acknowledge control; `[Confirm]` stays disabled until acknowledged (creating a new
DB row requires explicit maintainer approval). An UPDATE to an existing row shows no approval
banner (the gate applies to additions only).

**The override (advisory verification):** if the change carries an unresolved verify state (a
version picked but not checked against a DLL, or a resolver failure), the modal restates it —
*"Saving without verifying this version against a game DLL."* — and the `[Confirm]` action
reads **"I accept — save anyway"**. The maintainer's explicit acknowledgment is the
override; it is never a silent bypass.

## The toast concern (the dissolved s07's result surface)
The save RESULT is a **top-anchored toast** (`Notification`), not a persistent bar:
- **Success** — *"Saved `<name> <version>`"* (`success`), auto-dismisses after a few
  seconds; the git short-hash is NOT shown (the transaction stays atomic plumbing; git invisible).
- **Blocked** — *"Save blocked — the files are locked by another process. Retry."*
  (`error`) + a `[Retry]` action; persists (does not auto-dismiss) until retried; never
  reaps the lock. The maintainer doesn't learn it's "git" — it's "the files are locked".
- **Non-blocking notices** — a transient one-line info/warning toast, auto-dismisses.
The toast is top-anchored so it stays out of the thumb-reach of the primary action on a
phone (touch ergonomics); it floats above every layout and navigates nothing (a background
result never auto-navigates).

## States & variants
- **Populated (update)** — the field delta + Confirm/Cancel; no approval banner.
- **Populated (new row)** — the field delta + the new-row approval banner; Confirm disabled
  until acknowledged.
- **Committing** — after Confirm: the overlay shows a brief "Saving…" state (the backend
  transaction + push run); controls disabled. On success the overlay closes and the success
  toast appears.
- **Write failure** — the atomic transaction failed and rolled back: the overlay shows
  *"Save failed — nothing was written. `<reason>`."* (the backend's reason); Confirm becomes
  Retry; the DB + CSVs are in their pre-action state (a failure rolls back fully). System-caused
  copy.
- **Save blocked** — a live shared-index lock: the overlay closes and the blocked toast
  (above) appears with `[Retry]`.
- **Override-required** — the unverified/resolver-failure variant: Confirm reads "I accept —
  save anyway" (verification is advisory, the maintainer is final authority).
- **Edge content** — many changed fields → the field-delta list scrolls within the overlay;
  a long `old → new` value wraps within its row.

## Links in / out
- **In:** s04 `review_changes`; s05 `review_changes` (new entity/version).
- **Out:** `commit_change` → the atomic backend transaction → the **save-result toast**
  ("Saved `<name> <version>`" or "blocked — Retry"); on success the overlay closes back to
  s02 with the row updated; `Cancel` → back to the editor, nothing lands.

## Applicable interaction laws
- **Advisory verification** — an unresolved verify state surfaces the explicit "save anyway"
  override.
- **Atomic confirmed transaction** — Confirm runs ONE atomic transaction (incl. the server
  push); failure rolls back fully; git is invisible.
- **Single validator** — the change has already passed the shared validator (s04/s05, via the
  API); the overlay re-asserts nothing it didn't.
- **New-row approval gate** — a new entity/version requires the approval acknowledgment before
  Confirm.
- **Semantic tokens only** — tokens only.

## Responsive behavior
- **Wide:** a centered `Modal`; the toast top-anchored.
- **Phone:** a full-screen sheet (the field delta gets the full viewport; `[Confirm]`/
  `[Cancel]` pinned reachable at the bottom; the approval acknowledge + override read
  clearly without a cramped modal); the toast top-anchored (out of thumb-reach of the
  bottom-pinned primary action).
