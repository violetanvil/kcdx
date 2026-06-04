# Maintainer Tool — UI design changelog

Newest-first. Tracks revisions to the UI design layer ([`design.md`](design.md) + the
per-screen specs in [`screens/`](screens/)). The functional TRD changelog is
[`../changelog.md`](../changelog.md).

## 2026-06-04 — s04 field-editor: per-field tooltips + kind-conditional field visibility

- **s04 §"Contents" / §"Region & position" / new §"Field relevance by kind"** — the field editor
  gains two UX features found during live acceptance (user-approved):
  - **Per-field tooltips** — every field (editable + the read-only identity key) carries a
    plain-language tooltip on a keyboard-focusable `?` info affordance beside its label (the
    Survival sub-heading carries a group tooltip). Content is SOURCED from
    [`../../seeds/policy.md`](../../seeds/policy.md) + the schema.py column comments — domain facts,
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
