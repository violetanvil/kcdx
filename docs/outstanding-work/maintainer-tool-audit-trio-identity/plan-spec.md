# Plan spec — maintainer-tool audit-trio identity + verified_date model

**Goal:** implement the settled audit-trio identity + verified_date model (TRD D17a/D17b)
in the maintainer-tool frontend — `verified_by` becomes the signer identity that is sent
as the git commit author, and `verified_date` becomes a read-only system fact shown only
when the row is verified.

## The settled design (verbatim source — the build authority)

Read these directly; this plan-spec is a pointer, not a replacement (`.claude/rules/spec-conformance.md`).

- **`data/maintainer-tool/design.md` §6 US-3** — the audit-trio edit story (rewritten); the
  per-field rules for `verified_by` and `verified_date`.
- **`data/maintainer-tool/design.md` §10 D17a** — `verified_by` ← the signer/author identity
  (prefilled, overrideable, SENT as the confirm `author_name` so the signer becomes the git
  commit author; closes the D17 FE gap). Landed `c99438c`.
- **`data/maintainer-tool/design.md` §10 D17b** — `verified_date` is a SYSTEM fact: read-only,
  set to today on verify, shown ONLY when the row is verified (`last_verified_at_version`
  non-empty), removed from the always-shown set. Landed `c99438c`.
- **`data/maintainer-tool/ui/screens/s04-field-editor.md`** — §"Contents" (the `verified_by` /
  `verified_date` field rows), §"Field relevance by kind" (the always-shown set), §"Validation".
  THE SCREEN-SPEC build authority for the s04 render.

## The grounding facts (read from the code this session — not assumption)

- `verified_by` today: a `render: "text"` editable field (`fieldModel.ts`), prefilled from the
  `maintainerName` prop (= `/health` `maintainer_identity.name`, the configured dev identity)
  via `applyAuditTrioCoupling` when `last_verified_at_version` is set. **The FE sends NO author
  identity on confirm** (`client.test.ts` asserts `author_name` absent), so the backend
  `_resolve_author` (`routes_confirm.py`) always falls to the configured identity regardless of
  the typed `verified_by` — the signer and the git commit author silently diverge. The backend
  seam (`_AuthContext` / the `author_name` body field / the `X-Kcdx-Author-Name` header) is
  ALREADY BUILT (D17) — the FE just never populates it.
- `verified_date` today: `render: "text"` (editable `YYYY-MM-DD`), in the `ALWAYS_SHOWN` set
  (`fieldModel.ts`), auto-filled to `todayIsoLocal()` when `last_verified_at_version` is set
  (`applyAuditTrioCoupling`). It is shown on every row, even unverified, and hand-editable.
- The audit trio (`last_verified_at_version` / `verified_by` / `verified_date` / `evidence_kind`)
  is all-set-or-all-null (`policy.md`); the shared validator (law 6) is the authority; the FE
  reimplements no rule.

## Cross-step invariants

- **FE-repo only.** Both steps land in the separate gitignored frontend repo
  (`data/maintainer-tool/frontend/`); the gate is `npm run typecheck` + `npx vitest run` +
  `npm run build`, run from INSIDE the dir (NEVER `npm --prefix`). The kcdx ledger references the
  FE commits with `FE:<hash>`.
- **The validator stays the authority (law 6).** No rule is reimplemented in the FE; the backend
  validator still validates `verified_date`'s shape even though the FE never authors it.
- **The all-or-null trio coupling is preserved.** `applyAuditTrioCoupling` sets all four trio
  cells together on verify and clears them together; `verified_date` becoming system-set/read-only
  must keep this coherent (set with the trio on verify, cleared with it).
- **Login portal is OUT of scope** (design-deferred): the v1 prefill source stays the configured
  `/health` identity; a login portal later injects the per-session identity into that surface (the
  seam unchanged). These steps define/wire the seam, not the portal.

## Coverage map — every design element → its step

| Design element | Covered by | Notes |
|---|---|---|
| `verified_by` prefilled from the resolved identity (design.md §US-3 / D17a) | Step 1 | Already built (the `maintainerName` prefill via `applyAuditTrioCoupling`); Step 1 asserts + preserves it |
| `verified_by` overrideable — on-behalf / correction (design.md §US-3 / D17a) | Step 1 | Already built (editable field); Step 1 asserts + preserves it |
| The FE SENDS `verified_by` as the confirm request `author_name` (design.md §US-3 / D17a; s04 §Contents) | Step 1 | NEW — the FE currently sends no author; the seam (`_AuthContext`) exists backend-side |
| The signer becomes the git commit author (design.md D17a) | Step 1 | The consequence of sending `author_name = verified_by`; asserted via the confirm body |
| `verified_date` read-only / never hand-typed (design.md §US-3 / D17b; s04 §Contents) | Step 2 | NEW render kind (read-only); the maintainer cannot type a date |
| `verified_date` shown ONLY when verified (`last_verified_at_version` non-empty); removed from the always-shown set (design.md §US-3 / D17b; s04 §"Field relevance by kind") | Step 2 | NEW conditional render; an unverified row shows no `verified_date` cell |
| `verified_date` system-set to today on verify (design.md §US-3 / D17b) | Step 2 | The coupling already sets it to today; Step 2 makes it system-set-only (not editable) |
| The all-or-null trio coupling stays coherent — `verified_date` set/cleared with the trio (design.md D17b) | Step 2 | Step 2 preserves `applyAuditTrioCoupling`'s set/clear behavior for `verified_date` |
| s04 §Validation — drop the maintainer-facing "malformed verified_date" entry path (s04 §"Validation") | Step 2 | Consequence of read-only; the validator stays the authority (law 6) — no FE path to author a bad date |
| The login portal + a real per-session identity (design.md D17a §scope; D17b) | OUT-OF-SCOPE | User-decided in the `/design` dialogue: the v1 prefill source is the configured `/health` identity; the portal is its own feature plugging into this seam |
