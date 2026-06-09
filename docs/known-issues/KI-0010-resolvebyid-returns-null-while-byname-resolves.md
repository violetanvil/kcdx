---
id: KI-0010
reported: 2026-06-08
status: Open
area: refdb / Address Library runtime resolution
discovered_by: cap-20-hook-modes (CAP-20-addrname) on the 2026-06-08 launch
commit_at_report: 1ef7c56
---

# KI-0010 — `ResolveById` returns 0x0 while `ResolveByName` resolves the same entity

## Symptom

The `CAP-20-addrname` test row (plugin `cap-20-hook-modes`) fails on every launch
with a name/id resolve **mismatch**:

```
[INFO][cap_20_hook_modes][VERIFY] CAP-20-addrname: name/id resolve mismatch:
  byName=0x00007FF98965A5A4 byId=0x0000000000000000
REPORT name="CAP-20-addrname" pass=false reason="name/id resolve mismatch:
  byName=0x00007FF98965A5A4 byId=0x0000000000000000"
```

The same entity resolves correctly **by name** (`byName` returns a valid VA,
`0x…A5A4`) but resolves to **NULL by id** (`byId = 0x0`). The test asserts the two
paths agree on the address; they do not.

## What this is NOT

- NOT the TD-0008 stale-fixture class. `cap-20-hook-modes` carries no
  `targets.toml` with a retired `address_id`; it was not one of the four plugins
  the TD-0008 fix repointed, and the failure is unchanged before and after that
  fix. This row was incorrectly bundled into TD-0008's red list at filing; TD-0008's
  Resolution has been corrected to its real 7-row scope, and this KI carries the
  genuine bug.
- NOT a missing/renumbered entity per se — the NAME resolves, so the entity exists
  in the shipped `reference.sqlite`. The defect is specifically in the **by-id**
  resolution path returning NULL where the by-name path succeeds.

## Evidence / observations

- `byName` succeeds → the entity is present, verified, and resolvable in the
  running game version. The by-name cache (`refdb` bulk-build at `Open()`) carries it.
- `byId` returns `0x0` → either the test resolves an id that is NOT the same entity's
  current id (a stale id baked into the cap-20 test, post the 1–157 renumber), OR
  `refdb::ResolveById` / `ResolveAddrById` has a genuine lookup defect for this id.
- The discriminator to settle it (a `/debug` probe): read what id `cap-20-hook-modes`
  resolves by-id, and check whether that id exists in the current `reference.sqlite`
  (`SELECT name FROM address_names WHERE id=?`). If the id is absent → the cap-20
  fixture has its OWN stale id (a TD-0008-adjacent fixture fix, different plugin). If
  the id is present + `ResolveByName` of that same name works but `ResolveById` of its
  id returns 0 → a real `refdb` by-id bug (the more serious case).

## Trail

(empty — awaiting `/debug` investigation. The first probe is the discriminator above.)

## Resolution

(empty — open. The mechanism — stale-fixture-id vs refdb-by-id-defect — is the first
thing `/debug` resolves; root cause required before any fix, `.claude/rules/anti-patterns.md` AP17.)
