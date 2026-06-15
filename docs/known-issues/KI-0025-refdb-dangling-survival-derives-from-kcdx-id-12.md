# KI-0025 — reference-DB integrity: dangling `survival_derives_from` → kcdx_id=12 blocks new-entity confirms

**Status:** Open
**Reported:** 2026-06-15
**Severity:** blocks all new Address Library entity additions (the `/confirm` full-DB integrity gate fails for ANY new-entity add, not just the one that surfaced it)

## Symptom

A new-entity add through the maintainer-tool backend validates clean at PREVIEW
(`/save/create-entity` → `valid: true`) but FAILS at `/confirm/create-entity`'s
full-DB integrity check and rolls back (nothing written):

```
{"status":"failed",
 "detail":"survival_derives_from kcdx_id=12 has no curated address_versions row in the DB (add the dependency entity first)",
 "committed":false,"retry":false}
```

Surfaced 2026-06-15 while adding `CCryPak_FOpenRaw` (kcdx_id=160, slot 35) for
file-system-takeover step 3.2. The FOpenRaw row itself is clean and unrelated to
kcdx_id=12 — the integrity check is whole-DB, so it fails the transaction on a
PRE-EXISTING inconsistency that any new add now trips over.

## Evidence (probed, not theorized — `results-driven.md`)

- **kcdx_id=12 DOES have an `address_versions` row** in `data/reference.sqlite`
  (`id=321128`, `kind=5` = string_anchor, `anchor_string='exec autoexec.cfg'`,
  `valid_from=1`, `last_verified_at_version=1`, `verified_by=VioletAnvil`). It is
  the gEnv-resolver string anchor.
- **Two entities reference kcdx_id=12** (`data/db-export/address_versions_seed.csv`):
  - kcdx_id=9 (`instruction_anchor`, RVA 0x0086AD99, AOB `48 8B 0D ?? ?? ?? ??`) —
    `survival_derives_from=12`.
  - kcdx_id=23 (`vtable_index`) — `vtable_slot=12` (note: a different column — this
    one is a slot index that happens to be 12, likely NOT a derives-from ref; the
    derives-from referer is kcdx_id=9).
- **The mismatch is between the integrity-check's view and the DB.** kcdx_id=12 has
  a row, yet the check reports "no curated address_versions row." The likely cause:
  the row's version columns are stored as integer version-IDs (`valid_from=1`,
  `last_verified_at_version=1`) rather than resolved against the `1.5.1164953`
  version tag, so the integrity check's join/lookup for "a curated row at the
  derived-from entity, valid at this version" finds nothing. (UNVERIFIED mechanism —
  the integer-vs-tag version resolution is the lead to probe; the dangling-report
  vs row-present fact is verified above.)

## Why it matters

The `/confirm` integrity gate is whole-DB, so this latent inconsistency blocks
EVERY new-entity addition until repaired — not just CCryPak_FOpenRaw. The gate is
working correctly (it refuses to commit on a referential break); the defect is the
pre-existing data inconsistency it is catching.

## Scope / fix direction (NOT YET DONE — a maintainer decision, `design-authority.md`)

Repairing it is an UPDATE to existing rows (kcdx_id=9's `survival_derives_from`
linkage, or kcdx_id=12's version resolution) — OUT of `/add-db-entity`'s scope
(UPDATEs are the GUI's) and a DB-maintenance call the user owns. Candidate
directions to investigate (none chosen):
- Resolve kcdx_id=12's version columns to the `1.5.1164953` tag (if the integer-id
  is the break) — via the maintainer-tool GUI's re-verify/edit path.
- Re-examine whether kcdx_id=9's `survival_derives_from=12` is the right reference,
  or whether kcdx_id=12 needs a per-version row at `1.5.1164953`.
- A targeted probe of the backend's integrity-check query (what join it runs for
  `survival_derives_from`) to confirm the integer-vs-tag mechanism before fixing.

Do NOT hand-edit `data/db-export/*.csv` or force the write — the integrity gate is
correct; fix the data at its source through the validated path.

## Resolution

(unfilled — open)
