---
id: TD-0010
opened: 2026-06-09
status: Open
area: kcdx.statement.* surface / test-suite (cap-92)
closure_gate: a maintainer-chosen boot-safe statically-neutralizable game function (a named source-mechanism)
owner: continuous (the next statement-surface work, or whenever a boot-safe live-write target is identified)
commit_at_filing: c2322791f9e37ad95e5f3ddb8ac394e6c0cc257a
related:
  - TD-0006 (statement layer DEV-only — the USER-DB backing of statement-level named things; this TD is the test-coverage gap, TD-0006 is the data-backing gap)
affected_sites:
  - test-plugins/cap-92-statement-replace/plugin.lua  (the 4 structural rows — none exercises a live native-execution readback)
  - docs/outstanding-work/restructure/phase-09.3-namespaces/step-5-statement-namespace.md  (the step doc's Test bar names the runtime proof this TD defers)
---

# TD-0010 — `kcdx.statement.replace_with` live native-execution readback test

## Context

Phase 9.3 step 5 built the `kcdx.statement.*` static-bytes surface
(`src/lua_bind_statement.cpp`) and its regression plugin
`test-plugins/cap-92-statement-replace/`. The step doc's headline **Test bar**
names a *runtime* proof:

> A `kcdx.statement.replace_with(...return_const(0))` produces ZERO per-call Lua
> dispatch — verified by the ABSENCE of dispatch log lines during a tight loop
> hitting the target (a falsifiable claim that FAILS if a per-call dispatch line
> appears, proving the bytes execute natively, not via a callback).

cap-92 proves the **structural** contract for that claim, not the live
native-execution readback:

- `cap-92-replace-with-registers` — `replace_with` accepts a STATIC op as the
  required positional (no callback path), registers as a `Kind::Statement`
  entry, and reaches the apply path (Pending / Applied / deploy-state-degraded
  all honest PASS).
- `cap-92-kind-mismatch` — a branch op on an "assign" statement is rejected at
  apply with a teaching reason naming both kinds; never a silent apply.
- `cap-92-deferred-op` — a statement-bytes-dependent op surfaces a
  not-yet-emittable deferral, never a fabricated byte.
- `cap-92-zero-dispatch` — `replace_with` rejects a function in the op slot
  (static-op only). This makes the zero-per-call-dispatch claim STRUCTURAL: no
  callback positional exists, so no per-call Lua dispatch CAN fire.

What cap-92 deliberately does NOT do: destructively rewrite a live game
function and read back that the modified bytes execute natively in a tight loop
with zero dispatch lines. The rows target `SaveGame` and do NOT neutralize it —
a real `replace_with` that neutralized SaveGame's bytes would break saving.
Choosing a curated function that is BOOT-SAFE to statically neutralize (and
whose neutralization is observably provable at runtime) is a maintainer
DECISION, not the test's to make.

The user accepted (2026-06-09) cap-92's structural proof as step 5's "done"
and approved deferring the live native-execution readback to tracked work,
with the maintainer owning the target choice (the alternative — block step 5
until a boot-safe target is identified and live-readback-proven — was the
weighed-and-rejected option; `.claude/rules/design-authority.md`,
`.claude/rules/spec-conformance.md` — design silence on the live target is a
surfaced decision, not an agent default).

This is a **bucket-2 test debt** (`.claude/rules/test-discipline.md`): the test
becomes constructible once the named future thing lands — a chosen boot-safe
statically-neutralizable target. The structural rows ship now; the live row is
the deferred coverage.

## Closure blocker

A maintainer identifies a **boot-safe, statically-neutralizable curated game
function** — one whose `replace_with(...)` neutralization:

1. does NOT break boot or a core game path (unlike SaveGame), AND
2. is observably exercised at runtime (the function is actually called during a
   reachable game state — boot-to-menu, or a named in-game gesture), so the
   tight-loop readback has something to observe, AND
3. has a determinate-op replacement that FITS its statement byte range (the
   same-size write path — not the deferred trampoline path).

Once that target is named: add a cap-92 row that `replace_with`'s it with a
determinate op, drives a tight loop hitting it, and asserts the ABSENCE of any
per-call `STATEMENT` dispatch line in `kcdx-dev.log` during the loop (the
native-execution proof). The row flips the cap-92 matrix from
structural-only to structural + live readback. This is a named
source-mechanism (the target choice + the readback row), not a vague "later".

## Activity log

- **2026-06-09** — Initial filing. Step 5 (`kcdx.statement.*`) shipped with
  cap-92's structural proof; the live native-execution readback deferred with
  user approval, maintainer owning the boot-safe-target choice.

## What this entry does NOT do

- Does not double as a bug report — the structural surface is correct and
  tested; this is a deferred *coverage* gap, not a runtime defect.
- Does not block any current capability — `replace_with` is LIVE and exercised
  structurally; only the live-readback row is deferred.
- Closure is appended by the skill that lands the live-readback row (typically
  `/execute` or the next statement-surface `/feature` step), which then moves
  this file to `closed/` + reindexes per `doc-organization.md` — never at
  filing.
