# Phase 9.1 — SQLite reference DB + lookup primitive + verification cache

**Status: DONE** (DB + engine consumer; survival-cache plumbed-but-not-fed).
Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.1" +
[`../parallel-ghidra-research.md`](../../parallel-ghidra-research.md) §11.9 (the
shipped schema; the original flat schema sketch is HISTORICAL).

The DB ships at `data/reference.sqlite` (schema `address_names` +
`address_versions` + dictionary/registry tables). The lookup primitive is
`refdb::ResolveByName(name)` / `ResolveById(kcdx_id)` (refdb owns the bulk-built
in-memory cache, `498934c`). The `version_check.bin` cache plumbing ships; the
production FEED awaits Phase 9.2's binder wiring.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| SQLite ship + refdb cache + ResolveByName/ById primitive | DONE | 498934c |
| version_check.bin cache plumbing | DONE (plumbed; production feed owed by 9.2) | — |

The originally-planned standalone `hash_at(name, version)` helper was never built
as a separate symbol — the cached `content_hash` + `length` are fields on
`refdb::NameResolution` accessed via the existing resolve. The `behaviors` /
`applicable_ops` DB tables were never built (no current consumer). The
`statements` / `referenced_vars` / `call_edges` tables are DEV-only (Phase 9.4's
`kcdx.find` discovery; not consumed by production).
