# Phase 5 — delete old TOML behavior parsers

**Status: DONE** (`95854fe`, live-verified 2026-05-26). Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 5".

The legacy `[[patch]]` / `[[hook]]` / `[[mid_hook]]` / `[[trampoline]]` /
`[[scan]]` behavior-table parsers were deleted. `kcdx.toml` is manifest-only.
Live-verified: suite 102/109 (only the pre-existing CAP-20-target-nosig FAIL),
55/55 manifests valid.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| delete legacy behavior-table parsers (narrow cut) | DONE | 95854fe |
