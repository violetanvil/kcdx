# P7 step 1 — serve-AND-execute via the kcdx slot; KI-0006 execute-leg

## What

Confirm KI-0006's open criterion: a served `.lua` EXECUTES end-to-end — run via the
early kcdx-owned slot (P5), NOT the engine's crashing mod-init loader. With FIX A's
single runtime (P6), the dual-CRT/dual-runtime that created the confirmed cross-CRT
hazard class is collapsed; this step proves the execute leg and, if a crash still
reproduces, root-causes it with that variable eliminated.

## Scope

- A served `.lua` the kcdx early slot runs self-reports a file-scope marker
  (`System.LogAlways` / a `kcdx`-side signal) the agent reads — proving SERVE (HOOK 2
  returns kcdx's bytes, already proven CAP-73) AND EXECUTE (the marker reaches the
  log) end-to-end.
- The execution path is the instrumentable kcdx slot, NOT a `scripts/mods/<modid>.lua`
  mod-init overlay (the crashing path KI-0006 falsified as a vehicle).
- **If a crash reproduces post-FIX-A:** root-cause it in mechanism terms (AP17,
  `.claude/rules/anti-patterns.md`) with the cross-CRT variable eliminated — the
  surviving evidence (`WHGame+0xB2DBA0` victim site, the cap-78-`overlay_entry`
  correlation) is the starting point; the falsified theories (record-synth,
  re-entrancy, mod-init-serve) stay falsified. **If no crash reproduces:** KI-0006
  closes on the confirmed execute.
- Close KI-0006: Resolution section + `git mv` to `known-issues/closed/` + reindex,
  same change (`.claude/rules/doc-organization.md`). Return the asset-system Phase 3
  step-10 row to DONE.

## Test bar

A permanent `test-plugins/cap-NN-serve-execute/` regression: a served `.lua` executes
via the kcdx slot, self-reporting the execute marker the agent reads from the log —
the falsifiable SERVE-AND-EXECUTE claim (KI-0006's open criterion). PROBE Q silent.
Confirmed by the user's launch + the agent's log read. **Honest scope:** the
deliverable is the execute confirmation; a residual crash is root-caused only if it
reproduces (not a guaranteed crash-fix — `lua-vm-design.md` §7.2).

## Dependencies

P6 step 1 (the single runtime must be in place — FIX A is what collapses the hazard
class this re-attempt depends on), P5 (the early kcdx slot is the instrumentable
execution path).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §7.2 (the deliverable + the honest
scope) + [`../../before-game-hooks.md`](../../../before-game-hooks.md) §6c +
[`../../../known-issues/KI-0006-serve-execute-vehicle-not-found.md`](../../../../known-issues/KI-0006-serve-execute-vehicle-not-found.md)
(the investigation trail + the Phase-11 re-attempt plan).

## RE / author-burden note

No author hex. The served `.lua` is author-shipped content; the execution path is
engine-owned. No new DB rows. A residual-crash root-cause, if owed, follows
results-driven (probe, don't theorize) and AP17 (mechanism before close).

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E16, E17; design §7.2.
