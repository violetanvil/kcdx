# Code review — main at 86fcf46 (vs private/main 23563a8)

Scope: 3 commits, 22 files (+2136 / -98). First review on `main`; no prior marker. Falling back to `private/main..HEAD` as the implicit unreviewed range.

Commits reviewed:
- 7c9abfc — Smart-resolver `kcdx.hook.<name>.<mode>` + `kcdx.bytes.<name>{...}`; `kcdx.code` recast as producer-side.
- a13c216 — Phase 9.2 + 9.6 + Phase 12 added to restructure-plan (docs only).
- 86fcf46 — C++ mirror: `kcdxDeclareInterface` + `kcdx::bytes::Write` wrapper; `docs/cpp/{hook,bytes}.md` lead with the empowered wrapper floor.

## Build status

GREEN. `pwsh ./build.ps1` exit 0. All three artifacts produced (`kcdx.dll` 2,974.5 KB, `kcdx.exe` 138.0 KB, `kcdx-watchdog.exe` 263.0 KB). No new warnings in the truncated output.

## Verdict

**reject** — one correctness defect (C1 — `kcdxDeclaredValue::stringValue` lifetime contract is broken by `std::vector<DeclaredEntry>::push_back` reallocation, despite the in-header / in-code / in-doc claims of process-lifetime stability); one docs-discipline defect (C2 — three broken `../lua/declare.md` links in the newly-added `docs/cpp/declare.md`, plus no Lua peer doc landed for the C++ surface); one AP7 defect (H1 — four new author-facing surfaces ship with zero new `test-plugins/` rows).

## Counts

- Critical: 2 (C1, C2)
- High: 1 (H1)
- Medium: 2 (M1, M2)
- Low: 1 (L1)

## Files

- 01-critical.md — must-fix items
- 02-high.md — architecture-level
- 03-medium.md — quality
- 04-low.md — cleanup
