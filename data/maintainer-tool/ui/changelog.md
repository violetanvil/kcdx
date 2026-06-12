# Maintainer Tool — UI design changelog

Newest-first. Tracks revisions to the UI design layer ([`design.md`](design.md) + the
per-screen specs in [`screens/`](screens/)). The functional TRD changelog is
[`../changelog.md`](../changelog.md).

## 2026-06-12 — s09 Needs-action view authored (the standing lifecycle-completeness surface, TRD D41)
- NEW screen `screens/s09-needs-action.md` (v1/high): a standing, tool-wide surface listing every
  entity whose lifecycle is INCOMPLETE at the current game version, grouped by KIND into collapsible
  sections — **Uncovered at current version** (closed interval, no successor, not deprecated/superseded),
  **Never verified** (`last_verified_at_version` NULL), **Broken references** (dangling
  deprecation/supersession). Each row carries a kind-appropriate resolution action that NAVIGATES to
  the existing canonical resolve flow (s05 author-successor / s02 deprecate-supersede-fix-ref / s04
  verify) and returns to s09 (law 2/3/6 — s09 detects + routes, never reimplements editing).
- The EMPTY state is the GOAL (success-framed, not a neutral empty): *"Every entity's lifecycle is
  complete at version <V> — nothing needs action."* (the `success` token). Loading + error +
  disabled + edge states specified per the established pattern.
- s01 navigator gains a `[Needs action ▸ N]` affordance + count badge (un-badged at 0); the screen
  index + navigation map add s09.
**Integrated in:** `screens/s09-needs-action.md` (new); `screens/s01-navigator.md` (the affordance
row); `design.md` (screen index + navigation map).
**Why:** TRD D41 settled the standing needs-action view's BEHAVIOR but not its screen spec; this
authors the visual + interaction layer on it (`.claude/rules/spec-conformance.md` — the screen spec
is the build authority an executor builds to). Surfaced when `/plan` halted: a new-screen build step
needs its screen spec before decomposition.

## 2026-06-12 — s08 reconciles against current DB state + the lifecycle-completeness surface (TRD D41)
- s08 is no longer stateless against the DB: a re-imported row whose recommended action ALREADY
  landed (a `failed` row already closed to `last_verified_at_version`; a verified row already
  covered) now renders in a **"no further action"** state — surfaced, auditable, no checkbox, not in
  any batch — so the actionable blocks show only rows still needing the action (a partly-acted
  re-import shows true remaining work; confirming never produces a no-op write).
- A close-intervals action that ORPHANS an entity (no interval covers the current version, not
  deprecated/superseded) FLAGS it as **needs action** (the close stays atomic; the orphan appears in
  the standing needs-action view). A **standing needs-action view/filter** (tool-wide) lists every
  incomplete-lifecycle entity at the current version (orphan / never-verified / broader integrity
  gaps), catching gaps from any flow.
- The **`[Fix ▸]` flow** now carries the failing row's divergence `detail` to s04 (the maintainer
  sees WHAT diverged) AND preserves a return path back to the worklist (report intact, no one-way
  dead-end); an applied row shows its resulting value, not just an "applied" marker.
**Integrated in:** `screens/s08-verification-worklist.md` (Contents `[Fix ▸]` row; the
report-vs-DB-reconciliation + close→needs-action + Fix-flow sections; the "Already acted on" +
"Orphaned by a close" states).
**Why:** surfaced during live acceptance of Phase 6 (6.3) — a re-imported report framed an
already-closed interval as a fresh failing row + a no-op confirm; the lifecycle was left silently
incomplete after a close. TRD D41 settles the reconciliation + lifecycle-completeness model; this
screen spec builds to it (`.claude/rules/spec-conformance.md`).

## 2026-06-10 — s08 names the static-evidence tier `live_test_plugin` (sync to the settled TRD D29)
- s08's verify-all delta now names the ranks-2–5 `passed_not_verified` `evidence_kind` as
  **`live_test_plugin`** (was "the static-evidence tier", left unnamed pending the TRD fold). The
  TRD rectifying pass settled the value (D29, committed cc5e1c0): the in-game verify-all sweep IS
  the test-suite plugin (D28/D33), so `live_test_plugin` is the honest existing tier — NOT
  `pattern_scan` (which the design-soundness gate rejected as the weakest tier + a silent downgrade
  of the existing `live_test_plugin` rows).
**Integrated in:** `screens/s08-verification-worklist.md` (the verify-all delta).
**Why:** the screen spec referenced the tier vaguely while the fold was pending; now the TRD settled
it, the screen names the exact value the FE writes (`.claude/rules/spec-conformance.md` — build to
the settled design, no vague pointer).

## 2026-06-09 — s08 reconciled to the v3/D36 active-attempt verification contract
- s08 verdict surface re-keyed v2 → v3: the 4 superseded tokens (`resolves_works`/`wrong_target`/
  `dead`/`cannot_check` → Unchanged/Changed/CannotCheck) replaced by D36's **7-state verdict**
  (`verified_working`/`passed_not_verified`/`failed`/`not_applicable`/`cannot_check`/`skipped`/
  `error`) + the **`method_rank`** (1–5) proof rank.
- s08 worklist split from two blocks to **three**: verified (`verified_working`+`passed_not_verified`
  → verify-all) · failing (`failed` → close-intervals) · a NEW **no-action / informational block**
  (`not_applicable`/`cannot_check`/`skipped`/`error` → shown, no action, a collapsed-by-default
  `collapsible section`).
- New s08 **partial-report state** — a `warning banner` driven by the v3 `complete`/`rows_expected`
  signal (D37): a sweep that died mid-run renders the N present rows actionable + names the M−N gap.
- s08 row detail now surfaces the **invoke posture** (`invoke_skip_reason`) only when informative
  (`unsafe_to_call`/`uncontainable`; suppresses `not_a_callable_kind`/null).
- s08 verify-all `evidence_kind` now mapped by proof rank (rank-1 `verified_working` →
  `live_production`; ranks 2–5 `passed_not_verified` → the static-evidence tier — NOT
  `live_production`). A TRD-gap surfaced by this reconciliation; the user approved the rank-keyed
  mapping; folded into the TRD (D29) via `/design` in the rectifying pass.
- Two new Layer-1 silhouettes: **`live verdict badge`** (the s08 7-state badge — the s04 static
  `verdict badge` stays 4-state, unchanged; the two verdict vocabularies diverged at D36) and
  **`proof-rank chip`** (`rank N · <method>`, the proof-strength carrier composed beside it).
**Integrated in:** `screens/s08-verification-worklist.md` (Contents, the verdict section, the
three-block model, the verify-all delta, States & variants incl. the partial state, Laws 4 + 7);
`design.md` (the `verdict badge` note + the new `live verdict badge` + `proof-rank chip`
silhouettes).
**Why:** the producer (Phase 5, live + accepted) emits a v3 report — the 7-state verdict + proof
ladder + the complete/rows_expected partial signal — that the pre-D36 s08 spec couldn't display;
reconciling the screen spec to it before the FE build (`.claude/rules/spec-conformance.md` — build
to the design, not a stale spec).

## 2026-06-08 — s04 audit-trio render: verified_by identity-prefilled; verified_date read-only + conditional

The s04 milestone UAT settled the audit-trio identity + verified_date model (TRD D17a/D17b). The
s04 screen spec's `verified_by` / `verified_date` render is revised to match:
- **`verified_by`** — `field row (editable)` → `text well`, **prefilled from the resolved identity
  (`/health` `maintainer_identity.name`), overrideable**; on Confirm its value is SENT as the
  request `author_name` (the signer becomes the git commit author, D17a).
- **`verified_date`** — `field row (read-only)`, **moved OUT of the always-shown set**: it renders
  ONLY when the row is verified (`last_verified_at_version` non-empty), read-only, system-set to
  today on verify (D17b); an unverified row shows no `verified_date` cell. The validation line
  drops the maintainer-facing "malformed verified_date" path (the FE never authors the date; the
  validator stays the authority — law 6).

**Integrated in:** `screens/s04-field-editor.md` §"Contents" (the two field rows), §"Field
relevance by kind" (always-shown set), §"Validation". The functional decision is in the TRD
changelog ([`../changelog.md`](../changelog.md), D17a/D17b).
**Why:** honor D17's one-identity intent on the FE + render `verified_date` as the system fact it
is (read-only, only-when-verified).

## 2026-06-06 — s02 layout reworked: compact pinned header + collapsible sections; the link-row reflow fix

The milestone UAT of the s02 link table surfaced that the screen lost most of its real estate —
the header + verify + lifecycle sections each took natural height and ate the pane, leaving only
the version area scrollable — and the link-row affordance moved off-screen when a long verify
message wrapped (a law-1 reflow defect). The s02 layout is reworked + the install-set link surface
(the prior TRD D30 revision) is laid out:

- **Compact pinned header + collapsible sections (the detail-pane model).** A compact pinned summary
  (identity + the version `Select` + a one-line verify summary — "Bin folder linked — `<v>` ✓" / "no
  folder linked") stays visible; the heavy/secondary sections (the DLL link table → "Verify against
  a DLL", lifecycle) are **collapsible sections, collapsed by default**; the **work surface (version
  table + inline editor) is expanded by default and takes the majority of the pane**, scrolling
  within it. The screen leads with what the maintainer works on.
- **A new `collapsible section` Layer-1 silhouette** — a clickable section header (label + state
  chevron) that expands its body IN PLACE; the header row never moves (a user-toggled disclosure,
  not a state-change reflow — law 1); glyph+text state (law 7), keyboard-operable.
- **The `version & verify surface` + `per-module link row` silhouettes revised** to the install-set:
  the version&verify surface splits into the compact summary + the collapsible "Verify against a
  DLL" section (the Bin-folder pick — `<input webkitdirectory>` — + the per-module rows). The
  per-module link row gains the **reflow-safe structure** — a stable top line (module name +
  affordance + match glyph) that never moves, with the verify message wrapping in reserved space
  BELOW it (growing downward, never pushing the affordance off-screen — the fix for the reflow bug).
- **§"Responsiveness & sizing" gains the detail-pane model** (lead with the work surface; the
  compact-header + collapse on both breakpoints). The nav-map prose swept to the folder-pick
  install-set (deletion-hygiene — the stale "link a DLL per module" prescription removed).

**Integrated in:** `design.md` §"Responsiveness & sizing" + the `collapsible section` /
`version & verify surface` / `per-module link row` silhouettes + the nav map;
`screens/s02-entity-detail.md` §"Region & position" + §"Contents" + §"States & variants" +
§"Responsive behavior".
**Why:** the milestone UAT (step 2.5) rejected the per-DLL-stacked s02 layout — the screen wasted
real estate and the link row reflowed. The compact-header + collapsible-sections model gives the
work surface the room and the reserved-space row structure fixes the reflow; both are built to in
the 2.5 rework. Pure visual/interaction revision — no functional (TRD) gap surfaced (the install-set
functional model was settled in the prior `design.md` D30 revision).

## 2026-06-05 — the verification-engine surfaces (link table · per-author check · s08 worklist)

The visual + interaction layer for the verification engine (functional TRD D24–D31, US-11).
Four surfaces, settled through the `/ui-design` dialogue:

- **Layer 1 (`design.md`):** **law 4 extended** from version-resolve-advisory to cover the
  per-author verification checks + the ingested live report (all advisory, no-upload, never
  block; an Ambiguous callsite steers, never refuses); **four new component silhouettes** —
  `per-module link row`, `verdict badge` (Unchanged / Changed / Ambiguous / CannotCheck, glyph+
  text per law 7), `ingest progress bar` (determinate + row count), `batch field-delta list`
  (the bulk-re-verify confirm); the `version & verify surface` silhouette grew the link table +
  link-to-create; the **screen index + navigation map** gained s08.
- **s02 (extended):** the one-shot "check against a local DLL" control became the **per-module
  DLL link table** (link/re-pick per module each session, the resolved version + a version-match
  indicator — D30) + the **link-to-create prompt** (a linked DLL newer than the entity's rows →
  inline "[Add a version row at `<v>`]" → s05 prefilled, user-action per law 3, AP18 per law 8).
  The verify states grew the link/match/mismatch/degraded set.
- **s04 (extended):** the **per-author static check verdict** renders inline (the `verdict
  badge`) below the kind-relevant fields it checks, re-evaluating on a dirty edit; the
  **Ambiguous** verdict shows match locations + steers to extend the pattern (D31); a passing
  check refines `evidence_kind` (→ `pattern_scan`, D29); advisory — never gates `[Review
  changes]` (law 4). New verdict states added (no-badge / checking / the four verdicts).
- **s08 (new):** the **verification report worklist** — import the in-game plugin's `report.json`
  via the File API (D31), a determinate **ingest progress bar**, a pass/fail split, and **batched
  bulk re-verify** → one s06 batch-delta confirm → one atomic transaction (law 5 at batch scale;
  re-verify is an UPDATE, so law 8 doesn't apply). Full state set incl. empty / ingesting / error
  (malformed report, stale-id row) / all-pass / all-fail / long-report.

**Integrated in:** `design.md` (law 4, component silhouettes, screen index, nav map),
`screens/s02-entity-detail.md`, `screens/s04-field-editor.md`, `screens/s08-verification-worklist.md`
(new), `screens/README.md` (index).
**Why:** the verification engine's 4 new UI surfaces had no screen specs (s02's covered only the
version-read control); `/plan` HALTED on the gap and the user chose to author them via
`/ui-design` first. These are the build authority each UI step conforms to
(`spec-conformance.md`). One functional gap surfaced — the batched bulk-re-verify confirm
(law-5 save-spine designed one-at-a-time) — recorded for the §E TRD rectifying pass.

## 2026-06-04 — s04 field-editor: per-field tooltips + kind-conditional field visibility

- **s04 §"Contents" / §"Region & position" / new §"Field relevance by kind"** — the field editor
  gains two UX features found during live acceptance (user-approved):
  - **Per-field tooltips** — every field (editable + the read-only identity key) carries a
    plain-language tooltip on a keyboard-focusable `?` info affordance beside its label (the
    Survival sub-heading carries a group tooltip). Content is SOURCED from
    [`../policy.md`](../policy.md) + the schema.py column comments — domain facts,
    not invented. The map lives in one place (`frontend/src/editor/fieldModel.ts` `FIELD_TOOLTIPS`).
  - **Kind-conditional field visibility** — the editor shows the fields the current `kind` USES
    (the per-kind used set, grounded in policy.md §"Address kinds" + §"Survival columns"), hides the
    empty-irrelevant ones, and ALWAYS shows a populated stray (an irrelevant field carrying a value)
    flagged "not used by this kind". Changing the `kind` Select updates the visible set live. The
    kind→field map is captured in the new §"Field relevance by kind" (policy.md as authority) and
    encoded in `fieldModel.ts` `KIND_FIELD_RELEVANCE`.
  - Save/validate/dirty-tracking behavior is unchanged — only which fields render + the tooltips.
    Law 1 (reserved dirty/was/error space) holds per SHOWN field; law 9 (theme tokens) preserved.

## 2026-06-04 — s04 field-editor layout: vertical list → content-sized grid

- **s04 §"Region & position"** — the field editor changes from a vertical field list to a
  responsive grid sized to content: short fields (version tag, kind, dates, the survival
  integers, evidence_kind) are narrow, 2–3 per row on wide; long fields (signature, aob,
  anchor_string) span the full width. Grouped under the existing sub-headings; collapses to one
  column on phone. The per-field law-1 reserved space (dirty marker + "was:" + error line) still
  holds within each grid cell. User-approved during live acceptance.

## 2026-06-02 — desktop → web re-expression (the web-app pivot)

Re-expressed the UI layer from a PySide6 desktop GUI to a **React + Mantine web app**,
building on the settled TRD pivot (`../changelog.md` D14–D18). The information architecture,
the interaction laws' intent, the token system, the field-delta confirm (D8), and every
screen's states carry over; the shell, the laws' wording, the version surface, and the
component vocabulary re-express. Settled in the UI design dialogue (each the user's call).

- **Product framing + stack** → a private maintainer **web app**, React + **Mantine**,
  responsive incl. phone. Replaces "PySide6/Qt6 desktop window."
- **Window skeleton → responsive app shell** — wide: the two-pane split (navigator |
  detail); **phone: master-detail drill-down** (list fills the screen → tap → full-screen
  detail + `‹ back`). The **persistent bottom status bar is dissolved** (see s07 below).
- **Interaction laws → responsive-aware** — law 2 rewritten ("the navigation shell persists;
  wide = two panes, narrow = drill-down"); law 4 re-expressed around the **version dropdown +
  the client-side DLL check** (D15); modals → "centered on desktop, full-screen sheets on
  phone"; law 5/6 note the server-side commit+push (D16) + the API as the validator path.
- **Tokens** — carry over near-verbatim (already semantic hex/rem/factors); now mapped to the
  Mantine `theme` + CSS variables. Added a `bp_two_pane` breakpoint token.
- **Component silhouettes** → named to Mantine primitives (`TextInput`/`Select`/`Table`/
  `Modal`/`Drawer`/`Notification`/`Badge`/`Alert`…); the composed patterns (dirty marker,
  "was:" line, diff cell, the version&verify surface) preserved as app components.
- **s07 (status bar + DLL link) DISSOLVED** — a persistent bottom bar is awkward on web/
  mobile. Its version/DLL-check content moved into **s02's header** (the version&verify
  surface: a version dropdown + a "check against a local DLL" control running the client-side
  `.rdata` scan, D15); its save-result + notices became the **top-anchored toast**, specified
  in **s06's toast concern**. The screen set is now 6 (s01–s06).
- **s05/s06 overlays** → centered modals on desktop, **full-screen sheets on phone**; the
  save-result toast top-anchored (out of thumb-reach of the bottom-pinned primary action).

**Integrated in:** `design.md` (product framing, app shell, all 9 laws, the design-system
contract incl. breakpoints, component silhouettes, screen index, nav map, the per-screen
template); `screens/s01`–`s06` (web/Mantine vocabulary, the responsive-behavior section, the
version&verify surface in s02, the toast concern in s06, the stale `<exe-dir>` empty-state →
the configured-checkout error); `screens/README.md` (index 7 → 6); `screens/s07-*.md`
**removed** (dissolved). Stale desktop survivors swept (s07 references repointed, the
PySide6/Qt vocabulary re-expressed).

**Why:** the TRD pivot (`32df16d`) made the tool a hostable web app; the UI layer must
re-express to match (the `/ui-design` hand-off the TRD changelog named). ~80% of the UI
design is toolkit-agnostic and survived; this records the desktop→web deltas.
