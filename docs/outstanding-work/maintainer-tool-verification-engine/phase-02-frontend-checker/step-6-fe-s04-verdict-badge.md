# 2.6 [FE] s04 per-author verdict badge + Ambiguous steer + the 6 check verdict states

## What

Build the s04 field-editor's inline **per-author verdict badge** — when the row's module DLL is
available from the **linked Bin folder** at a version matching the row (the step-5 install-set
version-match gate), run the row's per-kind static check (steps 1–4) over **that module's DLL bytes**
IN THE BROWSER and render the `verdict badge` inline, directly below the kind-relevant fields the
check is about, re-running as those fields change (a dirty re-check). The 4 verdicts (Unchanged /
Changed / Ambiguous / CannotCheck) + the **Ambiguous warn-and-steer** (`[show matches]` listing the
N `.text` match locations, nudging the maintainer to extend the pattern — D31a) + the static-pass
`evidence_kind` refine (Unchanged → `pattern_scan`, D29). The 6 check verdict states. Advisory
throughout — `[Review changes]` is NEVER gated on the verdict (only on validator validity).

## Scope

One commit in the frontend repo: the inline verdict badge in s04 (reserved region, law 1), the
dirty re-check wiring, the `[show matches]` affordance for Ambiguous, the static-pass
`evidence_kind` → `pattern_scan` refine, and the 6 check verdict states. Built to the s04 screen
spec. (The audit-trio auto-fill it composes with already exists from the prior maintainer-tool
build; this step refines `evidence_kind` from that default.)

## Test bar

Vitest unit/component tests in the frontend repo: the badge renders each of the 6 states
(no-badge / checking / Unchanged / Changed / Ambiguous+`[show matches]` / CannotCheck); the
check re-runs on a kind-relevant field change; an Ambiguous verdict lists the N match locations
and never disables `[Review changes]`; a Changed/Ambiguous verdict carries the "I accept — save
anyway" override but does not block save; an Unchanged static check refines `evidence_kind` to
`pattern_scan`. Runnable at this step (the checker + the s02 link table exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **2.1–2.4** — the static checker (the badge renders its verdict).
- **2.5** — the s02 install-set link surface + the version-match gate (the badge appears only when
  the row's module DLL is available from the linked Bin folder at a matching install version — D30;
  the gate supplies the matched module's bytes the check runs over).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group A (advisory-never-blocks) + Group G
(evidence_kind-from-check) + Group H (s04 verdict-badge UX) + cross-step invariants 2
(advisory/Ambiguous-steers) + 3 (JS mirrors the engine).

## Design authority

`data/maintainer-tool/ui/screens/s04-field-editor.md` §"The per-author static check (TRD
D24–D27, D31)" + §"Check verdict states" (the **6 states**: no-badge / checking / Unchanged /
Changed / Ambiguous+`[show matches]` / CannotCheck) + §"evidence_kind from the check (TRD D29)".
Plus `data/maintainer-tool/ui/design.md` law 4 + the `verdict badge` silhouette (Layer-1). The
Ambiguous warn-and-steer is **D31a**; `[Review changes]` not gated on the verdict (law 6 only).
Build to these sections, not to this doc's summary.

## UX

Carried from the s04 spec (`.claude/rules/ux-first-class.md` — not invented):
- **No badge** — the row's module DLL is not available from a linked Bin folder at a matching
  install version (no folder linked, the module's DLL absent from it, or a version mismatch — D30
  install-set) → the check is unavailable, the "Not verified against a game DLL" advisory stands
  (degraded); authoring proceeds.
- **Checking (loading)** — a brief loading state in the reserved badge region while the per-kind
  check runs over the DLL bytes (law 1 — the region is reserved, no reflow).
- **Unchanged** ✓ — "matches the binary at `<resolved site>`".
- **Changed** — "does NOT match the binary (`<what was observed>`)"; advisory; the override
  carries to s06; save not disabled.
- **Ambiguous** (callsite/anchor multiple hits) — "pattern matches `<N>` sites in `.text` — add
  context bytes to make it unique" + a `[show matches]` affordance listing the N locations (the
  warn-and-steer, D31a); never refuses; the check re-runs as the maintainer extends the pattern.
- **CannotCheck** — "can't check this kind against the DLL (`<reason>`)" (a deferred
  vtable_index, or a dependent kind whose anchor is itself Changed — the transitive case).
- **Edge** — the badge's space is always reserved so the verdict appearing/changing never
  reflows the field grid (law 1); glyph+text, never color-alone (law 7); the `[show matches]`
  affordance is keyboard-reachable.

## Disassembler-test / author-burden

The verdict + the Ambiguous `[show matches]` steer ACTIVELY reduce author burden — the engine
tells the author whether their authored `rva`/`signature`/`aob` matches the binary and, when an
AOB is non-unique, points at the exact sites to extend, instead of making the author hunt in a
disassembler. This is the disassembler-test direction realized: the engine carries the verify
work, the author declares intent.
