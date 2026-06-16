---
id: TD-0015
opened: 2026-06-15
status: Open
area: fs_takeover slot-66 FGetModificationTime (pak arm)
closure_gate: a confirmed consumer that depends on a pak asset's modification time (then thread the pak entry's stored DOS time through the CDR parse + a ByteSource field + the slot-66 pak arm)
owner: continuous (the next fs_takeover cycle, or whenever a consumer that compares a pak asset's mtime is confirmed)
commit_at_filing: pending
related:
  - design §4.4/§5 (the totalizing invariant — kcdx owns the mtime too; this is the one currently-deferred case, user-approved)
affected_sites:
  - src/fs_takeover/file_handle.cpp  (GetModificationTime — the 0-return site for a Pak source; left as-is, by design this step)
  - src/fs_takeover/read_slots.cpp  (kcdx_FGetModificationTime — slot 66, 2-arg, returns GetModificationTime(handle))
  - src/fs_takeover/asset_index.h / asset_index.cpp  (ByteSource has no DOS-time field; the CDR parse does not record it)
---

# TD-0015 — pak-asset modification-time returns 0 (FGetModificationTime / slot 66 pak arm)

## Context

kcdx's slot-66 `FGetModificationTime` returns the file's last-write time as a
packed FILETIME. The LOOSE arm returns the real mtime (the `_fileno →
_get_osfhandle → GetFileTime` chain, on kcdx's CRT/handle). The PAK arm returns
**0** — `file_handle.cpp` `GetModificationTime` returns 0 for a Pak byte-source,
with an inline comment stating it is a defined "no mtime available", not a
failure.

The pak entry's stored DOS modification-time IS available in the central
directory record. Per `_research/fs-takeover-readslot-abi-recon/FINDINGS.md`
(slot-66 body): the engine's own pak arm converts the entry's packed 16-bit DOS
date/time fields at `entry+0x1c` / `entry+0x1e` via `SystemTimeToFileTime` into a
FILETIME (or returns a FILETIME already cached at `entry+0x20`/`+0x24`). kcdx
does not thread that DOS time through the asset index → `ByteSource` → the
inflated buffer, so the pak arm has nothing to return and yields 0.

The user decided (2026-06-15) to SHIP 0-for-pak-mtime this step — a defined,
documented, surfaced limitation — and file this tracked debt for threading the
real pak mtime through when a consumer needs it. The `file_handle.cpp` 0-return
is left as-is by that decision.

This is a deferred-correctness case the design's totalizing invariant
(`.claude/rules/spec-conformance.md` — kcdx owns ALL file operations, including
the mtime) keeps in scope: kcdx OWNS the pak mtime; this is the one currently
deferred arm, user-approved, with a named closure trigger (below), not a quiet
hand-back to the engine.

## Closure blocker

A **confirmed consumer that depends on a pak asset's modification time** — e.g.
an engine cache-invalidation path that compares a pak asset's mtime against a
cached value. It is a runtime-checkable unknown whether the engine calls
`FGetModificationTime` on pak assets in a way that matters; the trigger is a
*confirmed* such consumer, and the close re-verifies via that consumer
(`.claude/rules/results-driven.md` — probe the unknown, don't theorize it).

Once a consumer is confirmed, closure is:

1. Add the DOS date/time to the CDR parse (`asset_index.cpp`) — record the
   `entry+0x1c`/`+0x1e` 16-bit fields when reading the central directory.
2. Add a DOS-time (or pre-converted FILETIME) field to `ByteSource`
   (`asset_index.h`) so the pak source carries it.
3. Thread it to the slot-66 pak arm (`file_handle.cpp` `GetModificationTime`) —
   convert via `SystemTimeToFileTime` (mirroring the engine's pak arm) and
   return the real FILETIME instead of 0.
4. A cap-NN row asserting the pak arm returns the entry's real mtime, verified
   against the confirmed consumer.

This is a named source-mechanism (the CDR-field + ByteSource-field + pak-arm
thread, gated on a confirmed consumer), not a vague "later".

## Activity log

- **2026-06-15** — Initial filing. The open+read cutover (feature step 3.2)
  shipped slot-66's pak arm returning 0; the user approved deferring the real
  pak mtime to this tracked work, with closure gated on a confirmed consumer.

## What this entry does NOT do

- Does not double as a bug report — the 0-return is a defined, documented
  limitation in working code, not a runtime defect.
- Does not block any current capability — the loose arm returns the real mtime;
  only the pak arm is deferred, and no confirmed consumer needs it yet.
- Closure is appended by the skill that lands the pak-mtime thread (the next
  fs_takeover cycle), which then moves this file to `closed/` + reindexes per
  `.claude/rules/doc-organization.md` — never at filing.
