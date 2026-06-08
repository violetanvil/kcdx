# Step 1 — verified_by → the confirm author identity (D17a)

## What

Wire the FE so the row's `verified_by` value is SENT as the confirm request's `author_name`,
so the git commit is authored by whoever signed the row off (TRD D17a — the signer and the
commit author are one identity, honoring D17's intent on the FE side). The FE currently sends NO
author identity on confirm (`client.test.ts` asserts `author_name` absent), so the backend
`_resolve_author` always falls to the configured identity regardless of the typed `verified_by`
— this step closes that gap. `verified_by` stays prefilled-from-the-resolved-identity
(`maintainerName` = `/health` `maintainer_identity.name`) and overrideable (already built); this
step asserts + preserves that and adds the send-as-author wiring.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/`):
- Extend the confirm request type(s) that carry the `_AuthContext` seam — `ConfirmUpdateVersionRequest`
  (and the sibling create-version confirm body if it shares the audit trio) — to carry an optional
  `author_name` (the backend `_AuthContext` already accepts `author_name` / `author_email`; the FE
  type currently omits it).
- In the confirm flow (the SaveConfirmOverlay → `apiClient.confirmUpdateVersion` path), populate
  `author_name` from the row's prospective `verified_by` value before sending. (Decide `author_email`:
  the design names only `author_name` as the signer-derived field — leave `author_email` to the
  backend's resolution chain unless the design/seam requires it; do NOT invent an email derivation.)
- Keep `verified_by`'s prefill (`applyAuditTrioCoupling` from `maintainerName`) + its overrideable
  editable field UNCHANGED.

OUT of scope (design-deferred): the login portal + a real per-session identity. The v1 prefill
source stays the configured `/health` identity (the seam is wired here; the portal plugs into it
later).

## Test bar

Vitest in the FE repo (runnable AT this step — the confirm client + the audit-trio prefill exist):
- the confirm request body carries `author_name` equal to the row's `verified_by` (incl. an
  OVERRIDDEN `verified_by` — the sent author follows the override, not the prefill);
- `verified_by` is still prefilled from `maintainerName` and remains overrideable (assert the
  existing behavior is preserved — falsifiable: an override changes both the row's `verified_by`
  and the sent `author_name`);
- the `client.test.ts` assertion that previously expected `author_name` ABSENT is updated to expect
  it PRESENT (= `verified_by`) — the contract change is pinned.

## Dependencies

None — this is the first step; it rests only on the already-landed confirm client + the existing
`maintainerName` prefill (both in the FE repo at HEAD).

## Reference

- [`../plan-spec.md`](../plan-spec.md) — the coverage map (the verified_by rows) + the grounding facts.
- **Design authority:** `data/maintainer-tool/design.md` §6 US-3 (the `verified_by` rule) + §10 D17a;
  `data/maintainer-tool/ui/screens/s04-field-editor.md` §"Contents" (the `verified_by` field row —
  "on Confirm it is SENT as the request `author_name`"). Build to these, not to this doc's summary.

## UX

Carried from the s04 spec (`.claude/rules/ux-first-class.md` — not invented):
- **`verified_by` field** — an editable `text well`, **prefilled** with the resolved identity when
  the maintainer verifies a row (sets `last_verified_at_version`); the maintainer may type over it
  (an on-behalf sign-off or correction). No visible change to the field itself from this step — the
  change is that its value now travels to the commit author.
- **Feedback** — the existing save-confirm flow is unchanged from the maintainer's view (they still
  see the field delta + "Saved"); the author-identity wiring is invisible UX (it changes who the git
  commit is attributed to, which the maintainer does not see — git is invisible per US-4). No new
  visible state, error, or empty case is introduced.
- **Consistency** — reuses the existing confirm path + the existing `_AuthContext` seam; no one-off.

## Disassembler-test / author-burden

N/A — no author-facing game-binary input is added (this is an identity-wiring change; no
address/offset/signature is involved).
