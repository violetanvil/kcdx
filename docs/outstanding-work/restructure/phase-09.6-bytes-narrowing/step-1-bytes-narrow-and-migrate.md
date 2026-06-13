# Phase 9.6 step 1 — narrow `kcdx.bytes` remit + final call-site migration

**Status: STRUCK (2026-06-12).** This step is reversed — the design it
encoded was overturned. Ledger row: [`README.md`](README.md) → step 1 (STRUCK).
Preserved here for history; do NOT execute.

## Why this step was struck

A grounding pass + a cold architectural review (the original thinking withheld
so the review could not confirm it by bias) overturned the narrowing on the
evidence:

- **`kcdx.statement.replace_with` cannot express what `kcdx.bytes` does.**
  `replace_with` emits only *named* `kcdx.op.*` operations
  (`replace_with_noop`, `replace_with_return(0)`, …) — it has no path to write an
  arbitrary byte string. The one real migration target in the corpus, cap-01's
  `44 8A F0` → `45 31 F6`, has no `kcdx.op.*` that produces those bytes. The
  migration this step mandated is **not expressible**.
- **And could not resolve even if it could.** cap-01's target
  (`outfit_swap_callsite_aob`, id 5) is `kind=callsite` — a mid-function AOB with
  no statement metadata; `replace_with` resolves against the statement cache and
  returns `function_no_statements`.
- **The narrowed remit has zero inhabitants.** EVERY existing `kcdx.bytes` call
  site is function-internal (callsite/function kind); there are NO
  data_slot/vtable_base/string_anchor bytes sites, and the only non-function DB
  entities are live engine pointers/vtables unsafe to rewrite in a test. The
  narrowing would have rejected 100% of the passing corpus to gain a region
  nobody uses.
- **It was a UX regression** (cornerstone #1): forcing an author who wrote a
  working one-line `kcdx.bytes{target=<callsite>, replacement="45 31 F6"}` onto
  `kcdx.statement` sends them to a dead end (statement can't do that op).

## The corrected design (what replaces this step)

`kcdx.bytes` STAYS the general low-level raw-byte-write primitive
(function-internal OR not) — the lowest tier under `kcdx.statement.*`.
`kcdx.statement.*` is the higher-level curated tier (named ops, content
locators, statement-hash tracking, zero per-call cost) authors reach for **by
intent**, not by memory region. The steer toward `statement` lives in **docs**
(the tier ladder `docs/lua/index.md` already ships), never in a binder reject.
No `kcdx.bytes` binder change, no hard reject, no forced migration. "No overlap"
was the defect — it mistook an abstraction ladder for an engine-error space;
overlap-by-abstraction-level is the established pattern (raw-address vs
named-target hook forms coexist; a bytes patch + a hook coexist on one site).

## Where this step's surviving remnant went

The only buildable remnant of this step — making `docs/lua/bytes.md` point up to
the `kcdx.statement.*` tier (documenting bytes as the low-level tier, not
narrowing it) — is **folded into [step 3](step-3-docs.md)** (which already owns
the tiered docs front-door, so the work is not duplicated). The `kcdx.hook`
mode-as-key → sub-verb migration this step also named is **already DONE**
(verified 2026-06-12: no live mode-as-key call shape in `src/`/`test-plugins/`,
only descriptive comments).

## Reference

Full reversal rationale: [`../00-original-plan.md`](../00-original-plan.md)
§"Phase 9.6" reversal note.
