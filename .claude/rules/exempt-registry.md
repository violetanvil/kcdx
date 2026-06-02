---
paths:
  - "**/.claude/exempts/**"
  - ".claude/exempts/**"
---

# Exempt registry — the system mechanism for recording a sanctioned test exemption

Comprehensive testing is required in every repo (`.claude/rules/test-discipline.md`). The disciplined way to record the genuinely-untestable — a bucket-2 / bucket-1b-deferred / bucket-3 exemption — is therefore a SYSTEM convention, identical in every repo: the same tree shape, the same artifact schema, the same agent-immutability, the same region-identity gate. This rule owns that MECHANISM. The TAXONOMY (what qualifies as bucket 1/2/3, the per-entry user approval, the gated independent audit) is `.claude/rules/test-discipline.md` — cited, not restated; that rule and this one cross-reference. Only the language FILL (the coverage tool that produces the missed regions, the unit-naming, the concrete paths/extensions) is the repo's, named in the relevant append.

## The convention is available everywhere; entries materialize on demand

Every repo HAS this convention — the schema, the immutability, the gate apply the moment an exempt entry exists. A repo with nothing to exempt has an empty or absent `.claude/exempts/` and that is correct; the baseline is the convention's AVAILABILITY + enforcement-if-populated, NOT a mandate to create an empty tree. An exempt entry exists ONLY after the `test-discipline.md` approval flow lands it (operator-applied — see §"Agent-immutable").

## The tree

```
.claude/exempts/
  README.md                              the registry's own index/spec (optional per repo)
  <unit>/                                a code unit (the repo names the unit grain — crate / package / module — in its append)
    <source_basename>__<symbol>.md       ONE file = ONE exempt entry (one uncovered closure / function / branch)
```

- **One file = one exempt entry.** Never multiple closures in one file.
- **Naming: `<source_basename>__<symbol>.md`** — the source file's basename, a double underscore, the closure/function/symbol label. So `ls` reads as `file → symbol` per row. The authoritative source path + symbol live in the frontmatter; the filename is mechanical metadata.
- Bucket-1 closures (testable now, or after a source-fix the repo controls) do NOT live here — they get a test in the same change (`test-discipline.md` §"Bucket 1"). Only bucket-1b-deferred, bucket-2, and bucket-3 entries are recorded here.

## The artifact schema (system — identical every repo)

Each entry's frontmatter carries:

```yaml
---
unit:     <the code unit>                # crate / package / module — the repo's unit grain
source:   <path/to/source.ext>           # forward slashes; the gate normalizes Windows paths to match
symbol:   <human-readable closure label> # NO line numbers here — those live in `regions:`
bucket:   <1b-deferred | 2 | 3>          # 1b-deferred = source-fix queued (a TD row REQUIRED, naming the fix + follow-up)
                                         # 2 = future-testable, external blocker (a TD row REQUIRED, naming the trigger)
                                         # 3 = permanently untestable (no TD — nothing will change it)
approved_by_user: <YYYY-MM-DD>           # the date the user landed this entry per test-discipline.md's approval flow
regions:                                 # the line ranges this entry claims — the gate's identity anchor
  - start_line: <int>                    # inclusive lower bound
    end_line:   <int>                    # inclusive upper bound; end_line - start_line <= 10 (span guard)
    description: <one-line label>         # what this region covers
tech_debt: <TD-NNNN | none>              # REQUIRED for bucket 2 / 1b-deferred (the trigger's tracked row); `none` only for bucket 3
---
<body: the structural argument — why no public-API path and no source-fix (test-discipline.md's bucket-1 challenge, answered)>
```

A repo's append MAY add fields (a bucket-self-check matrix, primary-source URLs); it never drops a schema field. The `approved_by_user` + `bucket:` markers are the audit trail — NEVER an in-source annotation escape (`// test-ok:`, a coverage-ignore comment); a self-designation marker absent the user-approval frontmatter is the AP "bucket self-designation" violation (`.claude/rules/anti-patterns.md`).

## What the agent may change vs what needs user approval — field-scoped, not file-scoped

An exempt entry is NOT wholesale-immutable. The line is: **a net-new exemption (or a change to WHAT is exempted) is the user's per-entry approval act; keeping an already-approved exemption pointing at the SAME closure as code moves is agent maintenance.** Two classes:

- **Agent-mutable (no fresh approval) — region-drift maintenance.** When an agent adds/removes code that shifts an existing exempt closure's line numbers, the agent MAY update that entry's `regions[].start_line` / `regions[].end_line` and `regions[].description` to keep the SAME approved exemption tracking the SAME closure. This is bookkeeping, not a new exemption — forcing a user round-trip for every unrelated edit above an exempt would be churn, and the region-identity gate would otherwise fail on pure line drift. The span guard still holds (`end_line - start_line <= 10`); the entry must still bracket the same closure.

- **User-approved (per `test-discipline.md` §"Approval") — a net-new exemption or a change to what's exempted.** Creating a NEW entry file, deleting an entry, or editing `bucket` / `source` / `symbol` / `approved_by_user` / `tech_debt` (the fields that define WHICH closure is exempted, under WHAT bucket, with WHOSE approval) is a fresh exemption decision the operator lands; the agent surfaces the proposal, then COMMITS what the operator landed. An agent that adds a brand-new exempt entry, or re-points an entry to a different closure, or changes its bucket, is creating a net-new exemption — blocked.

Enforcement is field-scoped:
- The system snapshot+verify hook `hooks/guard-exempt-mutation.py` (PreToolUse snapshot of each entry's parsed fields + PostToolUse verify) compares WHICH fields changed: a change confined to `regions[].{start_line,end_line,description}` of an EXISTING entry is ALLOWED (agent region-drift maintenance); a new file, a deletion, or a change to any `bucket`/`source`/`symbol`/`approved_by_user`/`tech_debt` field is BLOCKED with the approval procedure. A revert-to-HEAD-committed state is always allowed (how a mis-added entry is undone).
- The deliberate human override (for a blocked field) is a text-editor edit (bypasses the agent's tool surface) — the same override path as every system immutable doc.

## The region-identity gate — an exempt cannot drift off its closure

Every exempt entry's `regions:` must correspond to a CURRENTLY-uncovered closure, and every uncovered closure must be claimed by an entry. The gate algorithm is SYSTEM (language-agnostic); the coverage tool that PRODUCES the uncovered-region data is the repo's (the append names it — e.g. `cargo-llvm-cov --json` for Rust, a c8/istanbul JSON reporter for TS).

**Scope the uncovered set to first-party source FIRST.** Before either check runs, drop every uncovered region whose normalized path is not under one of the repo's declared first-party source roots (`.claude/rules/test-discipline.md` §"The coverage universe" — inclusion-based; the append names the roots). A coverage tool attributes monomorphized / inlined regions back to a dependency's, a vendored tool's, or a generated file's own source path; those are outside the discipline's universe and MUST NOT enter the forward check (they are not the repo's to test or exempt) or the line denominator. Scoping is inclusion-based (keep only paths under a declared root), never an exclusion list of dependency markers — the latter is open by default and leaks the next unanticipated path back in. A region surviving this scope is first-party; only then do the two checks apply:

1. **Forward — every uncovered line is claimed.** For each uncovered `(source, line)`, an entry whose `source` matches must have a `regions[]` entry bracketing `line` (`start_line <= line <= end_line`). An uncovered line no entry claims → FAIL: write a test or add an approved exempt.
2. **Reverse — every entry claims a real uncovered line.** For each entry, at least one `regions[]` entry must bracket a currently-uncovered canonical line. An entry claiming nothing currently-uncovered → FAIL: the closure became covered or moved; remove or re-point the exempt (operator-applied).

The span guard (`end_line - start_line <= 10` per region) keeps a claim tight to its closure — a wide region that drifts to cover newly-added unrelated uncovered code is the failure this prevents.

The gate runs at the repo's build/coverage gate (the append wires the coverage-JSON-producing command into it). The system ships the algorithm; the repo feeds it the coverage JSON.

## What this is NOT

- NOT the taxonomy — when a closure is bucket 1/2/3, the bucket-1 challenge, the per-entry approval + independent audit are `.claude/rules/test-discipline.md`. This rule is the registry MECHANISM that taxonomy lands its decisions into.
- NOT the coverage tool / unit grain / concrete paths — those are the repo's fill (the append). The tree shape, the schema, the immutability, and the gate algorithm are system.
- NOT a doc-organization lifecycle tree — the exempt tree has no open→closed movement and no `<TYPE>-NNNN` id; it is operator-immutable and its authority is testing. `doc-organization.md` governs lifecycle trees; this rule governs the exempt registry.
- NOT a mandate to populate an empty tree — entries exist only when something is genuinely exempted, via the approval flow.
