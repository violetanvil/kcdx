# P1 step 1 — startup-ordering probe + static-evidence reads

**What.** Discharge the design's four marked runtime assumptions + two static
reads BEFORE any dependent step builds (`.claude/rules/results-driven.md` — the
design is provisional on these clauses). One instrumented launch observes the
startup timeline; the rest are code reads captured as evidence.

**Scope.**
- Live probe (one instrumented run, ground-truth-first, outcome→meaning map
  written before the launch; tid logged on every line): the post-Lua-wave
  boundary candidate point (after `RunPostGameLoad`, before InputLoaded — design
  §5's assumption), whether registrations queued AT that point still land in an
  `ApplyZone` drain, the C++ main-stop positions (`PostGameLoad`/event
  subscribers vs the Lua wave), and the VM-access window during the worker wave
  (engine adoption point vs `DiscoverAndLoad`).
- Static reads (captured to `_research/`, reuse-first): the marshal pump's
  reuse fit + boundedness for queued toggles (unbounded → the overflow
  disposition escalates to the user per design §5.4); the callback-dispatch
  path's reuse fit for `Invoke` (§8); the per-version verification call site
  backing §9's enforcement-point clause; the builtin pin-ahead path for the
  catalog pack (§7); the Phase 11 P5 tree read on whether the early stop runs
  C++ + `lua_before` entries interleaved per the unified order (§6's second
  assumes-half — provisional until `lua_before` lands; the live confirmation
  rides that trigger with the deferred Lua fixture leg).
- Probe leaves NO residue in live source — finding + wiring captured to
  `_research/behavior-startup-recon/` (FINDINGS.md §4 carries the wiring), probe
  removed (`.claude/rules/working-artifacts.md`).

**Test bar.** The probe's outcome map IS the verification (an evidence step's
observable outcome, per `.claude/rules/incremental-delivery.md`); the captured
findings update behavior-design.md's marked clauses (observed → cited, or the
design revises via its ceremony if a clause is disproven).

**Dependencies.** None — deliberately first.

**Reference.** [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants".

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §5
(boundary assumption), §6 (C++ stops assumption), §7 (pin-ahead assumption),
§8 (invoke/pump assumptions), §9 (enforcement site).

**Disassembler-test / author-burden.** N/A — no author-facing input; pure
evidence.
