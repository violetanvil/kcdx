---
paths:
  - "**/*"
---

# Dependency and licensing policy — every dependency is deliberate and license-checked

Adding a third-party dependency is a deliberate, license-checked, recorded act. This rule is the language-agnostic PROCESS; the concrete **license allowlist** (which licenses are acceptable) follows from the repo's distribution model and is the repo's — it lives in the relevant append, not here. Baking one project's allowlist into the system layer would be a layer-mismatch defect (a closed-source product, a copyleft project, and an internal-only tool each choose differently).

## Stdlib first, new dependency last

Reach for the standard library first, workspace-local / already-present code second, a new dependency last. Never add a dependency to save a few lines. A new dependency is new attack surface, new license obligation, and new version-churn — it earns its place or it does not go in.

## License-check BEFORE adding — against the repo's allowlist

Before adding any dependency:

1. **Check the license** on the package registry AND in the source repository — registry metadata is sometimes stale; verify both.
2. **Compare against the repo's allowlist** (named in the append — the set the repo's distribution model permits). A compound/SPDX expression is evaluated per its semantics (every AND-term must be allowed; each OR-branch resolves to one elected allowed license, recorded so the audit trail is greppable).
3. **Ambiguous, unclear, or dual-listed with a license the repo's allowlist doesn't cover → STOP and ask the user.** The license call is the user's where the allowlist doesn't unambiguously decide it (`.claude/rules/design-authority.md`).

## Record the dependency in the SAME change that adds it

When a dependency is added, its license-manifest row lands in the same change — name, version, license, one-line purpose. Rows are never batch-deferred to a later "license audit" step; that step is a VERIFICATION pass (it confirms every dep already has a row), not the first time rows are written. (The manifest is a tracked doc per `.claude/rules/doc-organization.md`; the repo names its path + columns.)

## A behavioral claim about a dependency needs a SOURCE

A claim about how a third-party dependency behaves (an API contract, a licensing obligation, a platform quirk) requires a `SOURCE:` — a live-fetched doc URL or a vendored-source path read THIS session. Training-data recall is not evidence (the skeptical-expert floor, `.claude/rules/skeptical-expert.md`). This is the same source-bar the review framework applies to any external-platform claim.

## Pin versions; a major bump is a code modification

Pin to a stable version range (the repo's scheme — named in the append); do not float to latest. Bumping a dependency's MAJOR version is a modification of verified code — it requires the repo's impact-analysis procedure before proceeding, not a silent bump.

## What this is NOT

- NOT the license allowlist itself — that is the repo's (the append), because it follows from the repo's distribution model, which the repo owns.
- NOT the manifest's path/columns or the tracked-doc structure — those are `.claude/rules/doc-organization.md` + the repo's append.
- NOT a ban on dependencies — it is a bar: stdlib-first, license-checked, recorded same-change, version-pinned. A dependency that clears the bar belongs.
