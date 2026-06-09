---
id: KI-0011
reported: 2026-06-08
status: Open
area: lua bytes smart-resolver / errors-that-teach (AP14)
discovered_by: cap-28-lua-bytes-smart-resolver (CAP-28-typo-fails-fast) on the 2026-06-08 launch
commit_at_report: 1ef7c56
---

# KI-0011 — smart-resolver typo error does not name the typoed slot (errors-that-teach gap, AP14)

## Symptom

The `CAP-28-typo-fails-fast` test row (plugin `cap-28-lua-bytes-smart-resolver`)
fails on every launch:

```
FAIL CAP-28-typo-fails-fast: the typo raised but the error did not contain the
  typoed name "cap28_definitely_not_a_real_target_xyzzy"; got:
  ...cap-28-lua-bytes-smart-resolver\plugin.lua:30: attempt to call field '?'
  (a nil value). The author cannot tell WHICH slot was wrong
```

The test deliberately authors a typoed target name and asserts the engine's error
**names the bad slot** so the author can fix it. Instead the smart-resolver raises a
generic Lua runtime error — `attempt to call field '?' (a nil value)` — that does NOT
contain the typoed name. The author cannot tell WHICH slot was wrong.

## What this is NOT

- NOT the TD-0008 stale-fixture class. `cap-28-lua-bytes-smart-resolver` carries no
  retired `address_id`; the failure is unchanged before and after the TD-0008 fix.
  This row was incorrectly bundled into TD-0008's red list at filing; the genuine bug
  lives here.
- NOT a test-fixture defect — the test's claim is correct (a typo SHOULD produce a
  slot-naming error). The defect is the engine's error path, which fails the
  errors-that-teach bar.

## The defect (errors-that-teach / AP14)

This is a `.claude/rules/cornerstones.md` "errors that teach" gap, the shape
`.claude/rules/anti-patterns.md` AP14 names: a failure path that does not tell the
author what was rejected and why. The smart-resolver, on an unresolvable (typoed)
target name, lets a raw Lua `nil`-call error propagate (`field '?'`) instead of
raising a structured error that names the unresolved slot
(`cap28_definitely_not_a_real_target_xyzzy`) and points the author at the fix. The
author-facing UX is the failure: a competent modder hits a typo and gets an opaque
Lua stack message, not "target 'X' did not resolve — check the name".

## Evidence / observations

- The error originates at `cap-28-lua-bytes-smart-resolver/plugin.lua:30` as a
  generic `attempt to call field '?' (a nil value)` — a Lua-level nil-index error,
  meaning the smart-resolver returned `nil` for the typoed name and the call site
  dereferenced it, rather than the resolver raising a named error at the point of
  non-resolution.
- The fix shape (for a later cycle): the smart-resolver detects the unresolved name
  and raises a structured, slot-naming error (`.claude/rules/logging.md` structured
  KV + `cornerstones.md` errors-that-teach) BEFORE returning a nil the caller blindly
  calls. The test row then passes when the error string contains the typoed name.

## Trail

(empty — awaiting `/debug` or a direct `/execute` fix once the resolver error path is
located.)

## Resolution

(empty — open. Root cause required before fix, `.claude/rules/anti-patterns.md` AP17:
name the exact resolver path that returns nil-without-a-teaching-error.)
