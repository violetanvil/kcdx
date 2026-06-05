# 2.7 [FE] s02 link-to-create prompt → s05 prefill + evidence_kind-from-check

## What

Build the s02 **link-to-create** on-ramp (D30): when a linked DLL resolves to a version NOT
covered by any of the entity's `address_versions` rows (a build newer than the DB knows), show
an inline advisory prompt — "`<dll>` is `<version>` — this entity has no row for it. [Add a
version row at `<version>`]". On click (a user action, law 3 — never auto-opened) it opens the
existing s05 create-version flow PREFILLED at the DLL's version → the maintainer authors
`rva`/`signature` → the static check runs against the linked DLL → on a passing check the audit
trio auto-fills with `evidence_kind` from the check (D29) → save (AP18 gates the new row, law 8).
This composes the static checker (steps 1–6) with the existing create-version flow into the
versioning-forward on-ramp.

## Scope

One commit in the frontend repo: the link-to-create advisory prompt in s02 (the uncovered-version
detection → the warning banner → the `[Add a version row at <v>]` affordance), the wiring to open
s05 prefilled at the DLL's version, and the static-check `evidence_kind`-from-check composition on
the new-version create path (Unchanged static check → `pattern_scan` refine, composing with the
audit-trio auto-fill — D29). Built to the s02 + s04 specs. The s05 create-version flow + the AP18
confirm already exist from the prior maintainer-tool build; this step adds the on-ramp + the
evidence_kind composition.

## Test bar

Vitest unit/component tests in the frontend repo: the link-to-create prompt appears only when the
linked DLL's resolved version is uncovered by the entity's rows; clicking it opens s05 prefilled
at the DLL's version (never auto-opens — law 3); on a passing static check the audit trio
auto-fills with `evidence_kind` refined to `pattern_scan` (D29); the new row is AP18-gated in the
confirm (law 8). Runnable at this step (the checker + link table + create flow exist) —
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **2.5** — the s02 link table + version-match indicator (the uncovered-version detection sits on it).
- **2.6** — the s04 verdict badge + the `evidence_kind`-from-check refine (composed here on the
  create path).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group F (link-to-create) + Group G (evidence_kind compose
with the trio auto-fill) + cross-step invariant 2 (advisory; user-action navigation, law 3).

## Design authority

`data/maintainer-tool/ui/screens/s02-entity-detail.md` §"Link-to-create (TRD D30)" (the
`warning banner` advisory prompt + the `[Add a version row at <v>]` → s05 prefilled at the DLL's
version → check → on pass trio auto-fills → AP18 save). `evidence_kind`-from-check on the create
path is `s04-field-editor.md` §"evidence_kind from the check (TRD D29)". The on-ramp is **D30**;
the AP18 gate is law 8. Build to these sections, not to this doc's summary.

## UX

Carried from the s02 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Populated (prompt shown)** — an inline `warning banner` (advisory, info): "`<dll>` is
  `<version>` — this entity has no row for it. [Add a version row at `<version>`]". Shown only on
  a version mismatch where the version is uncovered (law 4 advisory).
- **The on-ramp flow** — clicking opens the s05 create-version overlay prefilled at the DLL's
  version (a user action — law 3, never auto-opened); the maintainer authors `rva`/`signature`;
  the static check runs against the linked DLL (the s04 verdict badge); on a passing check the
  audit trio auto-fills (`evidence_kind` → `pattern_scan`, D29); save crosses the AP18 approval
  gate in the confirm (law 8).
- **Empty / not-shown** — no prompt when the linked DLL's version IS covered, or no DLL linked
  (the prompt is conditional, not a permanent element).
- **Error** — a resolver failure on the linked DLL shows the s02 resolve-failure state, not the
  link-to-create prompt (no version → no uncovered-version claim).
- **Edge** — a long version tag / DLL filename wraps within the banner without reflowing siblings
  (law 1); the affordance is keyboard-reachable + touch-operable.

## Disassembler-test / author-burden

The on-ramp turns "I have a newer build" into a guided add-a-version flow where the engine
verifies the authored row against the linked DLL — the author still authors `rva`/`signature`
for a genuinely-new build (an expert act the engine cannot pre-name), but the engine immediately
verifies it and auto-fills the evidence, so the burden is "author once, verified instantly",
never "author blind". Consistent with the disassembler-test guarantees (author-declared targets
are first-class + verified).
