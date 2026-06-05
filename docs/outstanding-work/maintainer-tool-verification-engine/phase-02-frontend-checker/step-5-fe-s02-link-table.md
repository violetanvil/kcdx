# 2.5 [FE] s02 per-module link table + version-match gate + the 7 verify states

## What

Build the s02 version&verify surface's **per-module DLL link table** — one `per-module link
row` per module the entity references (today only `WHGame.dll`): link a local DLL (browser File
API, read in-page, NEVER uploaded — D15/D26), show its filename + resolved version (the existing
`.rdata` scan) + a version-match indicator (✓ matches the selected row's version / ≠ no match) —
plus the **version-match gate** (a check runs only against a linked DLL whose resolved version
matches the row, D30) and the 7 verify states. The link is re-picked each session (in-memory,
no persistence — D30). This is the verification CONTEXT; the per-row verdict renders in s04
(step 6). First UI step — wires the static checker (steps 1–4) into the surface that supplies
its DLL.

## Scope

One commit in the frontend repo: the per-module link table UI in s02's header (the `version &
verify surface` composite — the version `Select` already exists; this adds the link rows), the
File-API link/re-pick, the resolved-version + version-match indicator, the in-memory per-session
pick state, and the version-match gate that decides whether the s04 check is available. Built to
the s02 screen spec. The link-to-create prompt is step 7; the s04 verdict badge is step 6.

## Scope (commit-grain)

One commit; the s02 link-table surface + version-match gate + the 7 verify states, no s04
verdict and no link-to-create (separate steps).

## Test bar

Vitest unit/component tests in the frontend repo: the link row renders each verify state
(not-linked / resolving / match / mismatch / resolve-failure / non-PE / picked-version); the
version-match gate returns "check available" only when the linked DLL's resolved version matches
the row; the DLL is read in-page (no network call — the no-upload invariant). Runnable at this
step (the resolver + checker exist) — `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- **2.1–2.4** — the static checker (the surface gates its availability + feeds it the DLL bytes).
- The existing `versionResolver.ts` (the `.rdata` version scan feeding the resolved version).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group F (re-pick link table, version-match gate, degraded)
+ Group H (s02 verify-surface UX) + cross-step invariants 1 (no upload) + 2 (advisory).

## Design authority

`data/maintainer-tool/ui/screens/s02-entity-detail.md` §"The version & verify surface" +
§"Contents" (the `per-module link row` ×M, the version-match indicator) + §"States & variants"
(the **7 verify states**: picked-version / module-not-linked / linked-DLL-resolving /
linked+version-match / linked+version-mismatch / resolve-failure / non-PE-unreadable). Plus
`data/maintainer-tool/ui/design.md` law 4 (advisory) + the `per-module link row` + `version &
verify surface` silhouettes (Layer-1). Build to these sections, not to this doc's summary.

## UX

Carried from the s02 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Populated** — the version `Select` + the per-module link table; each row shows the module
  name (mono) · the linked DLL filename + resolved version (or "not linked") · a `[re-pick]` /
  `[link…]` affordance · the version-match indicator (glyph+text, never color-alone — law 7).
- **Not linked (degraded)** — "`<module>` not linked — link a DLL to verify"; never a block
  (law 4 / D30); authoring proceeds.
- **Linked DLL resolving (loading)** — the row reserves its space and shows a brief loading
  indication while the browser reads the 86MB ArrayBuffer + runs the `.rdata` scan (law 1 — no
  reflow).
- **Linked + version match** — "`<dll>`: `<version>` ✓ matches" (the s04 per-author check is now
  available).
- **Linked + version mismatch** — "`<dll>`: `<version>` ≠ no row at this version" (surfaces the
  step-7 link-to-create prompt).
- **Resolve failure (error)** — "couldn't resolve a version from that DLL (interns disagree)"
  (system-caused copy naming the cause); advisory + override, never a block.
- **Non-PE / unreadable pick (error)** — "that file isn't a readable DLL (`<reason>`)".
- **Edge** — many modules / a long DLL filename wraps within its row without pushing siblings
  (law 1). Every link/re-pick affordance is keyboard-reachable + touch-operable; read-only/match
  state conveyed by more than color (law 7).

## Disassembler-test / author-burden

The surface adds a "link a DLL" affordance, not a hex/offset/signature input — the author picks
a file; the engine reads the version + (in s04) verifies the row. No author hand-hex; consistent
with the disassembler-test direction (the engine does the binary work, the author declares intent).
