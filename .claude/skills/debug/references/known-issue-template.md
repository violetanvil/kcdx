# Known-issue file template

Copy into `kcdx/docs/known-issues/<title>.md`. Replace `<placeholder>` text. Append rows to Trail as probes complete.

Sections marked **(required)** must exist before the first probe is logged.

---

```markdown
# <one-line title — what's broken, where>

**Status:** <open|investigating|resolved>. <One line on user-visible state.>

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

Empirical observations. Each bullet is a fact, not a hypothesis. Update as probes confirm or eliminate.

- <Concrete observation 1>
- <Concrete observation 2>

## Trail (required)

| Date | Action | Result |
|------|--------|--------|
| YYYY-MM-DD | <first observation> | <what was seen> |
| YYYY-MM-DD | PROBE A: <≤15 words: one variable changed> | <outcome verdict>. <one clause interpretation>. |
| YYYY-MM-DD | PROBE B: ... | ... |

Add row **before launching** with `pending` Result, then update **immediately after** with strict format (≤200 chars, two sentences max).

## Open questions

Hypotheses that haven't been tested, with the probe that would test them.

- **<hypothesis>** — Probe: <one-line concrete experiment>.

Move to "Resolved" when answered, or delete if redundant with Trail.

## Hard rule / design implications

If a `CLAUDE.md` hard rule, `.claude/rules/*.md`, or design doc is wrong or incomplete, draft what the rule **should** say here. Update the rule itself in the same commit as the fix.

## Active diagnostic instrumentation

Probes whose code still lives in the tree. Each row: file, what it does, ship-safe.

| File | What | Safe to ship? |
|------|------|---------------|
| `scripting.cpp::dynamic_hook_mid` | fingerprint logging on dispatch enter/exit | yes — pure read |
| `runtime_func_t.cpp` | PROBE E reproducer (`lua_newtable; lua_pop`) | NO — remove before release |

When the bug closes, audit each row: keep or remove.

## Resolution (filled in when bug closes)

- **Root cause:** <one paragraph>
- **Fix:** <commit, what it changed>
- **Verification:** <same repro, same test suite>
- **Diagnostic cleanup:** <kept-durable vs removed>
- **Doc updates:** <rule changes, design.md updates, memory updates>
```
