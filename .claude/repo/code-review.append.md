## Repo additions — code-review

- **Full build command** — `pwsh ./build.ps1`; the pre-flight asserts exit 0 AND that `build/Release/kcdx.exe` + `kcdx.dll` + `kcdx-watchdog.exe` were produced.

- **Immutable-review-files guard** — `guard-review-files.ps1` makes the `.claude/skills/code-review/<branch>/<hash>/` findings paths immutable to working agents; the heredoc-write requirement is enforced, not convention.

- **Design anchor** — `docs/design.md` (the subagent's authority clause cites it; the review substance — subsystem names, hazards, AP rows, source order, no-amend-ceremony — is in the architectural-review append the subagent also reads).
