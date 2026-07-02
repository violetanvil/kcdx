# P5 step 9 — the author startup-sequence doc (the timeline)

## What

The well-documented half of the contract: a NEW author-facing startup-sequence
reference — the timeline an author reads to learn the WHOLE startup sequence and place
their code with certainty. Every phase in real-time order, what kcdx does at it, what
subsystems are up, what the author can SAFELY do there, the event token to subscribe
to, the query value it returns, and which context (A/B/C) it runs in. This is the
"paramount to mod authors" deliverable — one timeline, learned once.

## Scope

- A new author-facing doc under the author-doc tree (the user-facing reference the
  author reads — e.g. `docs/startup-sequence.md` or the appropriate
  `docs/lua/`+`docs/cpp/`-adjacent operational-doc location, per
  `.claude/rules/structure-by-responsibility.md`): the timeline in real-time order,
  each phase with — context (A/B/C), what kcdx does there, what subsystems are up,
  what the author can safely do (and what they can't yet), the `kcdx.on` event token,
  the `kcdx.startup.phase()` value.
- **ctx-A phases are SHOWN** (so the author sees the whole sequence) but marked
  "engine-internal, pre-plugin — no subscribe-able event" (design §4). Internal
  plumbing phases likewise shown as internal markers.
- The doc is GLANCEABLE (`.claude/rules/docs-discipline.md`): the timeline is the
  front door; common-path-first; a copy-paste-runnable snippet for "react to phase X"
  + "query the current phase".
- `docs/init.md` (the internal engine contract) CROSS-REFERENCES this author doc (and
  vice versa): init.md = the engine's internal ordering contract; the author doc = the
  what-can-I-do-when view.
- Verify the doc against the AS-BUILT contract (`docs-discipline` — nothing documented
  that isn't built): every event token + query value + phase the doc names is the one
  steps 2-8 actually shipped.

## Test bar

This is a DOC step — its "test" is the docs-discipline completeness check: every
author-reachable phase from steps 2-8 appears on the timeline with its event token +
query value + context; every ctx-A/internal phase is shown-not-subscribable; the
snippets are copy-paste-runnable; `docs/init.md` cross-references resolve. Verified at
`step-review` / the phase gate (a doc step has no `cap-NN` runtime row — the
runtime behavior was tested in steps 2-8; this step proves the DOC describes it
faithfully). No new code; no PROBE Q concern.

## Dependencies

P5 steps 2-8 (the doc describes the AS-BUILT contract — every phase, event, and query
value the doc names must already be shipped; documenting an unbuilt surface is the
docs-discipline violation). Ordered LAST so the doc is written against reality, not an
intended surface.

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §5.3 (the timeline doc — what it
covers, the init.md cross-reference) + §4 (the shown-not-subscribable ctx-A treatment)
+ §10 (the UX — one mental model, the author learns the timeline once) +
`.claude/rules/docs-discipline.md` (the completeness criterion). Build to §5.3, not
this summary.

## RE / author-burden note

No author hex, no game-binary target, no DB rows. The doc is pure author-facing
reference — the disassembler-test posture rendered in docs (the author declares
intent by phase NAME; the doc teaches the timing model).

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" row "The author startup-sequence doc";
design §5.3, §4, §10.
