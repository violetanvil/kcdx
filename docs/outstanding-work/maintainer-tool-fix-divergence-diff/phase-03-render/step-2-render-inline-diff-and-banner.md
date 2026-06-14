# 3.2 [FE] Render the inline per-field diff + the extended banner (E6, E7, E9, E10) — milestone UAT

## What

Render the per-field divergence diff on s04, consuming the `fixDivergence` worker (2.1) over the
report's DLL context (carried by 3.1). The visible deliverable — the thing the user asked for ("the
actual field you would edit should be very clear showing what is different"):
- **(E6) inline per-field recorded-vs-actual** — for each diverged kind-relevant field, inline in its
  reserved gutter (the same space the dirty marker + "was:" line + verdict badge already reserve — no
  reflow, law 1): the recorded value, the derived actual (what the linked build shows), and a diverged
  marker (glyph + text, never color-alone — law 7). A kind-relevant field that did NOT diverge shows
  no marker.
- **(E7) the "What diverged" banner extended** — the existing E5 banner (`COPY.divergenceTitle`,
  step 3.5) now also NAMES the diverged field(s) at a glance once the diff has computed (e.g.
  *"signature, rva diverge from the linked build"*) — the glance-level overview above the inline
  detail.
- **(E9) the DLL-linked-and-diffed state** + **(E10) the no-divergence-found / cannot-check states** —
  when the check PASSES against the linked build, surface it honestly (*"no field diverged against the
  linked build"*), never a silent empty; when a kind can't be checked (a `vtable_index` deferral, a
  function row with no recorded `content_hash`), surface the honest CannotCheck reason in the banner
  (advisory, never a faked pass — AP14).

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/src/editor/`):
- `FieldEditor.tsx` — once a DLL is linked, call `fixDivergence` (2.1) with the report's DLL context
  (3.1) and render the per-field `{recorded, actual, diverged}` inline in each kind-relevant field's
  reserved region; extend the "What diverged" banner to name the diverged field(s); render the
  no-divergence-found + cannot-check states.
- The inline per-field render component (extend the existing field-row / VerdictBadge reserved-gutter
  pattern, or a small sibling render — reuse the reserved-space pattern, do not introduce reflow).
- The banner extension (the existing `divergenceDetail` banner gains the named-fields summary).

Does NOT change the worker (2.1) or the carry channel (3.1); does NOT touch s08.

## Test bar

Vitest component tests, runnable AT this step (the worker + the carried context + the recorded/prompt
baseline all exist):
- With a DLL linked and a `function` divergent fixture: the diverged field(s) render recorded-vs-actual
  inline with the diverged marker (glyph+text); a non-diverged field renders no marker; the banner
  names the diverged field(s).
- With a matching DLL: the banner reads "no field diverged against the linked build" (the
  no-divergence-found state), not a silent empty.
- With a `vtable_index` row / a function row missing `content_hash`: the cannot-check reason renders
  in the banner (advisory), no faked pass.
- Law 1: the inline diff appearing/changing does not reflow the field grid (the gutter is reserved).

**FALSIFIABLE:** a diverged field that renders NO inline recorded-vs-actual fails (the literal ask
unmet); a no-divergence-found that renders an empty banner / nothing (silent success, AP14) fails; a
cannot-check that renders a faked pass fails; an inline diff that reflows the grid (law 1 violation)
fails; a marker conveyed by color alone (law 7 violation) fails. Gate: `npm run build` exit 0 +
`npx vitest run` green; AND the phase milestone UAT (below) accepted.

## Dependencies

- **3.1** — the report's-DLL context carried to s04 + the no-DLL baseline + the DLL-link prompt. Hard
  prerequisite (this step renders the diff the 3.1 context feeds).
- **2.1** — the `fixDivergence` worker (the diff this step renders). Hard prerequisite.
- The existing FieldEditor reserved-gutter / VerdictBadge render pattern (law 1 reserved space) —
  reused for the inline diff.

## Reference

[`../plan-spec.md`](../plan-spec.md) — E6, E7, E9, E10; the settled facts (fact 4 — inline + banner;
fact 5 — the states); the law-1 / law-7 / law-4 invariants.

## Design authority

`data/maintainer-tool/ui/screens/s04-field-editor.md` §"Per-field recorded-vs-actual (law 1 / law 7)"
+ §"The 'What diverged' banner (E5, extended)" + the three States & variants entries tagged "(TRD
D45)" (no-DLL / diff-computed / no-divergence-or-cannot-check) + §"Applicable laws" (law 1 reserved
space, law 4 advisory, law 7 glyph+text, law 9 tokens). Build to THIS screen spec, not this doc's
summary.

## UX

Carried from the s04 screen spec (`.claude/rules/ux-first-class.md`):
- **The diff is at the edit point** — each diverged field shows recorded-vs-actual inline, directly
  where the maintainer edits it (the literal "the actual field is clear showing what is different"),
  with the glance-level "What diverged" banner above naming the field(s).
- **States exhaustive** — diffed (inline + banner), no-DLL-yet (3.1's recorded + prompt), no-divergence
  -found (honest "no field diverged", not silent), cannot-check (honest reason, not a faked pass).
- **Advisory, never blocks** (law 4) — the diff is context; it never gates `[Review changes]`. Reserved
  space (law 1 — no reflow), glyph+text (law 7 — never color-alone), tokens only (law 9).
- **Milestone UAT (this step)** — the maintainer opens s04 via a `[Fix ▸]` on a `failed` row and
  experiences the recorded + prompt → link-the-DLL → inline per-field recorded-vs-actual + the "What
  diverged" banner flow. Substantive + under-specified UI (the agent chose the visible inline-diff
  specifics) → the milestone checkpoint fires (loop §F.1 / `.claude/rules/ux-first-class.md`); Phase 3
  → DONE on this acceptance, not build-green alone.

## Disassembler-test / author-burden

None — a maintainer-tool FE screen; no author-facing plugin input, no game-function target.
