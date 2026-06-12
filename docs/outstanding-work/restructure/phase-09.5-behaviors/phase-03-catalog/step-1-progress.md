# P3 s1 — catalog loader path: build ledger

One-cycle build of [`step-1-catalog-loader.md`](step-1-catalog-loader.md). The
work items (one row per deliverable); flipped as each lands. Source of truth for
this run, not the conversation.

| # | Item | Status |
|---|------|--------|
| 1 | `src/behavior_catalog_loader.{h,cpp}` — pack discovery + per-file SEH/pcall-guarded execution + reserved-root engine-identity stamping | DONE |
| 2 | `lua_bind_behavior` engine-catalog declare seam (catalog file's `kcdx.behavior.declare` routes to `DeclareEngine`) | DONE |
| 3 | Call-site wiring in `src/hooks.cpp` — catalog runs BEFORE `RunAll(L)` (pin-ahead) | DONE |
| 4 | `data/behavior-catalog/` — proving entry `.lua` (cvar-backed benign) + `README.md` index | DONE |
| 5 | `package-release.ps1` — catalog-dir deploy mapping (ships under `kcdx-engine/`) | DONE |
| 6 | `docs/lua/behavior.md` — catalog/promotion doc increment | DONE |
| 7 | `test-plugins/cap-103-catalog-loader/` cap fixture (Lua) + `kcdx.toml` | DONE |
| 8 | `test-plugins/README.md` — cap-103 detail block + roll-up row | DONE |

Build/matrix UNVERIFIED by the implementing agent (the user runs `pwsh
./build.ps1` at the commit gate; the launch confirms the matrix). Mode: boot-only.

## Same-commit sweep (deletion-hygiene / docs-discipline)

The catalog now ships, so the "catalog is empty until the pack ships" prose is
stale. Swept in-scope: `docs/lua/behavior.md` intro caveat → live state + a new
`## The engine catalog` section; the catalog-miss error text in
`src/lua_bind_behavior.cpp` → drops "empty until the catalog pack ships".

## Surfaced items (the user decides)

1. **`src/behavior_interface.cpp:282-286` carried the same stale "empty until the
   catalog pack ships" clause** (the C++ catalog-miss error mirror) — SWEPT in the
   same commit (the deletion-hygiene survivor sweep requires it land with the
   surface change; the manager scrubbed it to "for the shipped catalog entries"
   after the build re-verified green). No longer outstanding.
2. **The malformed-file builtin boot error has no shipped self-test row** — a
   malformed `.lua` in the live catalog dir would break the boot it tests, so the
   loud-error path (`file_error` / `file_faulted`, `LOG_ERROR_KV`, never a silent
   skip) is exercised by code review of `behavior_catalog_loader.cpp`, not a cap
   row (the headless-testable boundary, per the test bar's "surface it" clause).
3. **`test-plugins/README.md` cap-103 block carries bare AP citations**
   (`AP14`/`AP15`/`AP18`) matching every sibling behavior-cap block in the same
   public file — the in-file AP-scrub is a separate, deferred public-scrub layer
   (per CLAUDE.md). Matched the file convention rather than diverging one block;
   surfaced for the bulk scrub.
