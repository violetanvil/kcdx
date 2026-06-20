# Phase 5 — directory enumeration (the FindFirst/FindNext/FindClose triplet)

Build kcdx's ownership of the engine's stateful directory-enumeration API — the
`FindFirst`/`FindNext`/`FindClose` handle-iterator triplet at vtable
+0x1F8/+0x200/+0x208 (slots 63/64/65) — over the unified pak+loose+overlay set.
This is the enumeration surface the §1 totalizing invariant requires that Phase 3
did NOT build: step 3.3 owned only slot 14 `ForEachFile` (the single-call callback
API) and correctly left slots 15/101 THUNK, but the table-DB override-glob
(`Libs/Tables/<base>__*.<ext>`) and the engine's general by-name directory listing
both dispatch through the 63/64/65 triplet — which still thunks, so kcdx serves no
pak-resident entries for those globs. The table-database load fails at boot
(`err_id=259`, "Database system error - tables can't be loaded") because the engine
gets zero override files where it expects an enumeration. This is **KI-0027**.

Settled design: [`docs/design/file-system-takeover.md`](../../../design/file-system-takeover.md)
§5.1 (the enumeration takeover), §4.5 (the slot set), §8 P5 (the find-data ABI
probe), §10 (v1 IN scope) — committed `ed402c9` (v1.9). The verified dispatch fact
(the glob routes through slots 63/64/65, NOT slot 14 or 101) is body-read from the
loader `FUN_180974484`, captured in
`_research/ki0027-table-glob-dispatch-recon/FINDINGS.md`.

Depends on Phase 2 (the unified index the iterator walks) + Phase 3 (the seated
swap, the kcdx handle-pool discipline the find-handle reuses, slot 14's union model
this mirrors in stateful form). Slot 14 `ForEachFile` already proves the union-enum
MODEL (disk walk UNION index pak-vpaths, loose-skip de-dup); this phase is the same
model behind the engine's stateful handle API the table loader uses.

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step-grain ledger

The canonical completion surface (`.claude/rules/doc-organization.md`). One row per
step; `/plan` authored all rows `NOT STARTED`; the orchestrator writes transitions.

| Step | Status | Commit |
|---|---|---|
| [5.1 — probe P5 (find-data buffer ABI)](step-1-probe-find-data-abi.md) | DONE | 531d632 |
| [5.2 — build slots 63/64/65 + cut over the table-DB glob + cap-118](step-2-enum-triplet-cutover.md) | DONE | (landed) |
| [5.3 — close KI-0027](step-3-close-ki0027.md) | NOT STARTED | — |

## Verification gate (phase done when)

- 5.1 (P5): the find-data buffer field layout (the attr word incl. the `& 0x10`
  directory bit, the entry-name byte offsets, any size/time fields) is read +
  captured to `_research/`, OR marked ambiguous-from-the-consumer → the engine
  FindFirst body decompiled to read what it writes. Static binary read (reuse-first
  ladder), no live launch — a struct-layout fact. The captured layout is the
  durable artifact; it gates 5.2 (a wrong find-data layout mis-reads every
  enumerated entry).
- 5.2 (the cutover): builds green; the three table rows (slots 63/64/65) flip
  `THUNK→KCDX`; the cap-118 regression plugin's glob over a pak-resident
  `__*`-style override resolves the expected entry set (a falsifiable matrix row);
  and **the KI-0027 repro (boot → table-DB load) reaches the world with no
  `err_id=259` fatal** — agent-read from `kcdx-dev.log`. This is the gate that
  proves the enumeration takeover serves the engine's glob.
- 5.3 (closure): KI-0027 carries a Resolution section with the root-cause MECHANISM
  paragraph (AP17 — the unserved-glob mechanism + the iterator-ownership fix, NOT
  "the table DB now loads"), gated through `root-cause-verifier`; the repro launch
  is clean; the close ceremony lands (move to `closed/` + reindex,
  `.claude/rules/doc-organization.md`).
