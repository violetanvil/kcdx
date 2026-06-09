# 2.7 [FE] s02 link-to-create prompt → s05 prefill + evidence_kind-from-check

> **DONE + milestone UAT ACCEPTED (2026-06-08, FE:7d2d6fa):** the link-to-create on-ramp + s05
> prefill + the D29 evidence_kind-from-check composition. Gate green (typecheck / vitest 418/418 /
> build); step-review land-fix (law 3 never-auto-open + law 4 advisory + AP18-not-bypassed + the D29
> fill-empty-only refine all verified). Milestone UAT accepted on the automated verification (the 5
> behavior assertions + error/edge states in vitest); the one surfaced design call — the create-path
> verdict badge (s05-spec-silent) — was confirmed KEEP (the maintainer sees the verdict driving the
> evidence_kind refine, mirroring s04).

## What

Build the s02 **link-to-create** on-ramp (D30, revised): when the **linked Bin folder's install
version** (resolved from WHGame.dll — D30 install-set) is NOT covered by any of the entity's
`address_versions` rows (a build newer than the DB knows), show an inline advisory prompt in the
collapsible Verify section — "`<install-version>` — this entity has no row for it. [Add a version
row at `<install-version>`]". On click (a user action, law 3 — never auto-opened) it opens the
existing s05 create-version flow PREFILLED at the install version → the maintainer authors
`rva`/`signature` → the static check runs against the module's DLL in the linked folder → on a
passing check the audit trio auto-fills with `evidence_kind` from the check (D29) → save (AP18
gates the new row, law 8). This composes the static checker (steps 1–6) with the existing
create-version flow into the versioning-forward on-ramp.

## Scope

One commit in the frontend repo: the link-to-create advisory prompt in s02's collapsible Verify
section (the uncovered-**install-version** detection → the warning banner → the `[Add a version row
at <install-version>]` affordance), the wiring to open s05 prefilled at the install version, and
the static-check `evidence_kind`-from-check composition on the new-version create path (Unchanged
static check → `pattern_scan` refine, composing with the audit-trio auto-fill — D29). Built to the
revised s02 + s04 specs. The s05 create-version flow + the AP18 confirm already exist from the prior
maintainer-tool build; this step adds the on-ramp + the evidence_kind composition.

## Test bar

Vitest unit/component tests in the frontend repo: the link-to-create prompt appears only when the
linked Bin folder's install version is uncovered by the entity's rows; clicking it opens s05
prefilled at the install version (never auto-opens — law 3); on a passing static check the audit
trio auto-fills with `evidence_kind` refined to `pattern_scan` (D29); the new row is AP18-gated in
the confirm (law 8). Runnable at this step (the checker + install-set link surface + create flow
exist) — `.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`.

## Dependencies

- **2.5** — the s02 install-set link surface + version-match indicator (the uncovered-install-version
  detection sits on the install version WHGame.dll resolved — D30).
- **2.6** — the s04 verdict badge + the `evidence_kind`-from-check refine (composed here on the
  create path).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group F (link-to-create) + Group G (evidence_kind compose
with the trio auto-fill) + cross-step invariant 2 (advisory; user-action navigation, law 3).

## Design authority

`data/maintainer-tool/ui/screens/s02-entity-detail.md` §"The version & verify surface"
(Link-to-create, TRD D30) (the `warning banner` advisory prompt in the collapsible Verify section +
the `[Add a version row at <install-version>]` → s05 prefilled at the install version → check
against the module's DLL → on pass trio auto-fills → AP18 save). `evidence_kind`-from-check on the
create path is `s04-field-editor.md` §"evidence_kind from the check (TRD D29)". The on-ramp +
install-version framing is the revised **D30**; the AP18 gate is law 8. Build to these REVISED
sections, not to this doc's summary.

## UX

Carried from the revised s02 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Populated (prompt shown)** — an inline `warning banner` (advisory, info) in the collapsible
  Verify section: "`<install-version>` — this entity has no row for it. [Add a version row at
  `<install-version>`]". Shown only when the install version (from WHGame.dll) is uncovered by the
  entity's rows (law 4 advisory).
- **The on-ramp flow** — clicking opens the s05 create-version overlay prefilled at the install
  version (a user action — law 3, never auto-opened); the maintainer authors `rva`/`signature`;
  the static check runs against the module's DLL in the linked folder (the s04 verdict badge); on a
  passing check the audit trio auto-fills (`evidence_kind` → `pattern_scan`, D29); save crosses the
  AP18 approval gate in the confirm (law 8).
- **Empty / not-shown** — no prompt when the install version IS covered, or no Bin folder linked
  (the prompt is conditional, not a permanent element).
- **Error** — a resolver failure on WHGame.dll (or no WHGame.dll in the folder) shows the s02
  resolve-failure / not-a-Bin-folder state, not the link-to-create prompt (no install version → no
  uncovered-version claim).
- **Edge** — a long version tag / DLL filename wraps within the banner without reflowing siblings
  (law 1); the affordance is keyboard-reachable + touch-operable.

## Disassembler-test / author-burden

The on-ramp turns "I have a newer build" into a guided add-a-version flow where the engine
verifies the authored row against the linked DLL — the author still authors `rva`/`signature`
for a genuinely-new build (an expert act the engine cannot pre-name), but the engine immediately
verifies it and auto-fills the evidence, so the burden is "author once, verified instantly",
never "author blind". Consistent with the disassembler-test guarantees (author-declared targets
are first-class + verified).
