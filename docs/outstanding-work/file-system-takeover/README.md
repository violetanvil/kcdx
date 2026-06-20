# file-system-takeover

**Active, in progress.** kcdx takes total ownership of the engine's `CCryPak`
file object — every file call dispatches into kcdx, every handle is operated on
kcdx's own CRT — eliminating the cross-CRT crash class (KI-0019/KI-0006) and
reading both vanilla paks and loose mod files itself.

Settled design: [`docs/design/file-system-takeover.md`](../../design/file-system-takeover.md) (`29a21c5`).
Shared spec + coverage map: [`plan-spec.md`](plan-spec.md).

> **Next: a `/design` decision on the pak-mgmt slot model — step 3.4 is BLOCKED
> on it.** Steps 3.1 (handle rep), 3.2 (open+read cutover — the cross-CRT class
> structurally removed), and 3.3 (existence/metadata + enumeration slots) are DONE;
> the open/read/existence/metadata/enum families are kcdx-owned. Step 3.4 (the
> remaining pak-mgmt/search-path/delete slots + table finalize) hit a design
> collision: the pak-mgmt slots operate the engine's loaded-pak vector + ZipDir,
> which §6 rejects (engine-CRT) — kcdx mounts into its OWN index. The
> mount/enumeration model under the takeover must be settled in `/design`
> (amend §4.5 + §6) before 3.4 builds. Details in the
> [step 3.4 doc](phase-03-file-slots/step-4-mgmt-slots-table.md) BLOCKED note; the
> search-path/alias + delete/copy slots are not blocked by the fork. 3.5 (subsume
> the `asset_overlay.cpp` seam) follows 3.4.

## Phase-grain status ledger

The canonical completion surface (`.claude/rules/doc-organization.md`). One row
per phase; flips to `DONE` when its last step lands. `/plan` authored all rows
`NOT STARTED`; the orchestrator writes transitions.

| Step | Status | Commit |
|---|---|---|
| Phase 1 — seating spike + in-flight cleanup ([phase-01](phase-01-seating-spike/README.md)) | NOT STARTED | — |
| Phase 2 — pak reader + unified index ([phase-02](phase-02-pak-reader-index/README.md)) | DONE | 63daea9 |
| Phase 3 — real file slots + seam subsumption ([phase-03](phase-03-file-slots/README.md)) | NOT STARTED | — |
| Phase 4 — verification + closure ([phase-04](phase-04-verification-closure/README.md)) | NOT STARTED | — |
| Phase 5 — directory enumeration (FindFirst/FindNext/FindClose triplet, slots 63/64/65) ([phase-05](phase-05-directory-enumeration/README.md)) | NOT STARTED | — |

## Phases at a glance

- **Phase 1** proves the load-bearing seating mechanism (P1 ctor timing, P2 swap
  acceptance, P4 thunk-compat) on a cheap reversible stub vtable, and clears the
  in-flight residue (KI routing, PROBE F) — before any large build.
- **Phase 2** builds kcdx's own pak reader (PKZIP/DEFLATE, own CRT) + the unified
  asset index — the byte-source layer, no engine ZipDir.
- **Phase 3** replaces the stub with the full kcdx file-slot family + settles the
  handle representation (P3), then subsumes the live `asset_overlay.cpp` seam.
- **Phase 4** ships the regression tests + subsystem doc and closes
  KI-0019/KI-0006 with a repro-clean launch.
- **Phase 5** (v1.9 — design `ed402c9`) builds kcdx ownership of the stateful
  directory-enumeration triplet (`FindFirst`/`FindNext`/`FindClose`, slots
  63/64/65) over the unified set — the surface the table-DB override-glob
  dispatches through that Phase 3 left THUNK — and closes KI-0027 (the table-DB
  load fatal). Its own track: a distinct KI from Phase 4's KI-0019/0006, so it can
  land independently. P5 (find-data ABI) probes first, then the slot cutover + the
  KI-0027 repro-clean launch, then closure.
