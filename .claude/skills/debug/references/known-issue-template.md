# Known-issue file template

Copy into `kcdx/docs/known-issues/KI-NNNN-<slug>.md`. Replace `<placeholder>` text. Append rows to Trail as probes complete.

`/report-bug` writes the OPEN subset (frontmatter + Symptom + Facts + Open questions; Trail/Resolution left empty). `/debug` fills the rest. Sections marked **(required)** must exist before the first probe is logged.

KI-NNNN allocation: scan `docs/known-issues/` AND `docs/known-issues/closed/` for the highest `KI-####` across both, increment by 1. First bug = `KI-0001`. The 14 pre-KI-NNNN human-readable files do not carry IDs; they are not part of the allocation sequence.

---

```markdown
---
id: KI-NNNN
opened: YYYY-MM-DD
status: open
commit_at_filing: <40-char hash>
---

# KI-NNNN — <one-line title — what's broken, where>

**Status:** <open|investigating|resolved>. <One line on user-visible state. `/report-bug` writes `OPEN — not yet investigated`.>

## Symptom (required)

<What the user sees when the bug fires. Reproduction steps if known.
Exception code + faulting RIP if available.>

```
ntdll!RtlReportFatalFailure
ntdll!RtlpHpHeapHandleError
ntdll!RtlSizeHeap+0x213
WHGame+0x459acb
```

Exception code: `<0xC0000374 STATUS_HEAP_CORRUPTION>`.

## Facts (required)

Empirical observations only. Each bullet is a fact, not a hypothesis. Update as probes confirm or eliminate. If `/report-bug` captured no observables, this section says `None captured yet — symptom-only report.` — never fabricate evidence.

- <Concrete observation 1>
- <Concrete observation 2>

## Trail (required)

| Date | Action | Result |
|------|--------|--------|
| YYYY-MM-DD | <first observation> | <what was seen> |
| YYYY-MM-DD | PROBE A: <≤15 words: one variable changed> | <outcome verdict>. <one clause interpretation>. |
| YYYY-MM-DD | PROBE B: ... | ... |

Add row **before launching** with `pending` Result, then update **immediately after** with strict format (≤200 chars, two sentences max). `/report-bug` leaves this empty.

## Open questions

Hypotheses that haven't been tested, with the probe that would test them. Causal claims live ONLY here, labeled — never in Facts.

- **<hypothesis (NOT verified)>** — Probe: <one-line concrete experiment>.

Move to "Resolved" when answered, or delete if redundant with Trail.

## Hard rule / design implications

If a `CLAUDE.md` hard rule, `.claude/rules/*.md`, or design doc is wrong or incomplete, draft what the rule **should** say here. Update the rule itself in the same commit as the fix.

## Active diagnostic instrumentation

Probes whose code still lives in the tree. Each row: file, what it does, status.
Status is one of: `live` (currently building, this investigation), `durable` (pure-read, kept enabled past bug close), `archived` (compile-disabled `#if 0` per debug/SKILL.md §3d).

| File | What | Status |
|------|------|--------|
| `scripting.cpp::dynamic_hook_mid` | fingerprint logging on dispatch enter/exit | durable — pure read |
| `runtime_func_t.cpp` | PROBE E reproducer (`lua_newtable; lua_pop`) | live (this investigation) |
| `mod_absorb/post_bracket_probe.cpp` | PROBE B frame-4 vtable read | archived (#if 0; root cause: bracket built modMgr but never installed it into the engine's getter path) |

When the bug closes, audit each row: keep durable, archive in-place (`#if 0`), or — never — delete.

## Resolution (filled in when bug closes — GATED)

**Cannot be filled until `Root cause:` answers "why?" in mechanism terms** (what value was wrong, who wrote it, in what order, why the original code path made that wrong write inevitable). "X no longer crashes" / "now boots" / "AV gone" are restatements of the symptom, NOT root cause (AP17, CLAUDE.md hard rule — non-negotiable). Cannot write the mechanism → another probe is owed; do not land the fix; leave Status: investigating. The single legitimate escape is an explicit user-approved `**Provisional mask, root cause unknown:**` label below — issue stays OPEN.

- **Root cause:** <one paragraph in mechanism terms — data flow + control flow + the specific invariant the original path violated>
- **Fix:** <commit, what it changed, why this addresses the mechanism above>
- **Verification:** <same repro, same test suite>
- **Diagnostic archive:** <each probe's `#if 0` site + one-line revival hint; durable probes left enabled>
- **Doc updates:** <rule changes, design.md updates, memory updates>
```
