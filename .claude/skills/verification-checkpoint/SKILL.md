---
name: verification-checkpoint
description: Use this skill after an /execute cycle (or a batch of related cycles) has landed and committed, and BEFORE the user launches the game to confirm the test-suite matrix. Produces a structured checklist enumerating every behavior, error path, and integration point the change introduced, plus a manual anti-pattern audit, for the user to review before the one game launch that verifies the work in-game.
---

# Pre-acceptance verification checkpoint

The per-step build gate cannot prove a hook fires, a Lua surface marshals, or a save round-trips — only a live launch does. This checkpoint enumerates everything the change claims to do, so the user confirms it item-by-item against ONE game launch.

## When this skill is invoked

After `/execute` (or a small batch of related `/execute` cycles) has landed and committed its work, and before the user launches KCD2. Auto-invoked by the orchestrator at §F.1 when the diff hits the threshold (multiple behaviors / new failure path / hook-ABI-save-schema-prior-phase touch); the user does not make the dispatch call. The user reads the checklist, confirms each item or rejects it, then runs the game and signals back. The agent reads the `suite: X/Y passing` line from `kcdx-dev.log` itself — the user never reads the log.

Deploy and dev-mode enablement happen at the orchestrator's commit gate (`_shared/orchestrator-loop.md` §C.6) AND are re-verified here at the launch gate (§"Deploy status" below) — defense in depth. A user invoking this skill standalone (no preceding `/execute`) gets the same deploy-freshness probe, so a stale live-install state is caught before the launch cycle is wasted.

## Format

```
## Pre-acceptance checkpoint: <one-line change description>

### Deploy status (BLOCKING — checklist body is gated on this section)
For every artifact this cycle (or batch) rebuilt, hash-compare the live-install copy against the `build/Release/...` source via `Get-FileHash`. Emit one line per artifact:
- [x] kcdx-engine/kcdx.dll — live hash matches build/Release/kcdx.dll (<short-hash>)
- [x] kcdx-engine/kcdx-watchdog.exe — live hash matches build/Release/kcdx-watchdog.exe (<short-hash>)
- [x] kcdx-plugins/test-suite/cap-NN-<name>/ — live tree matches test-plugins/cap-NN-<name>/ source
- [x] kcdx-engine/builtin/<fix>/kcdx.toml — manifest synced across all 3 plugin trees per memory `project_kcdx_deploy_all_plugin_trees`
- ...

**Any mismatch** = STOP. Do NOT emit the rest of the checklist. Surface the stale artifact list (source path → live destination → source hash vs live hash) and the likely cause (game running and holding the file open; wrong destination path; permission denied; cycle deployed only the dll and missed the watchdog/manifest sync). Re-run the deploy AND verify hashes equal before re-emitting the checkpoint. A user invoking this skill standalone gets the same gate — old build at the live install is caught here, not at launch.

### What was built
<2–4 sentences summarizing the user- or author-observable behavior the change delivers>

### Files changed (across all cycles in this batch)
- `src/foo.cpp` — <what> (commit <short-hash>)
- `test-plugins/cap-NN-<name>/` — new regression plugin (commit <short-hash>)
- ...

### Build status (historical — verified by the orchestrator at the commit gate, not a user action)
- [x] Build verified by orchestrator (`pwsh ./build.ps1` exit 0; build/Release/kcdx.exe + kcdx.dll + kcdx-watchdog.exe produced) — checkbox records the historical result per `agent-builds-and-deploys.md`; the user does not run build.ps1.
- [x] Test plugin(s) present + matrix row(s) recorded in test-plugins/README.md (in-game result PENDING this launch)

### Behaviors to verify (one item per testable claim, no cap)
- [ ] <behavior 1, one sentence, plain English, falsifiable>
- [ ] <behavior 2>
- ...

### Failure paths to verify (every error/abort branch added)
- [ ] <branch 1 — what condition triggers it, what it logs (KV category), what the user sees>
- [ ] <branch 2>
- ...

### Integration points (engine surfaces + cross-component contracts)
- [ ] <every hook/patch site this change installs, named with its conflict_engine footprint>
- [ ] <every Lua surface, console command, save/cosave field, or C++ interface added>
- ...

### Docs & glossary moved with the change (`docs-discipline.md`)
- [ ] Each new/changed author-facing capability has its API-doc entry (Lua → `docs/lua/index.md`, C++ → `docs/cpp/index.md`): call shape, args, returns, errors, snippet.
- [ ] Every new concept/noun has a glossary term in that doc.
- [ ] Cross-surface entry: a capability built in one language has the other surface's doc entry marked "not yet implemented (NYI)" (both docs map it even though only one is built), OR is explicitly marked "single-surface: <reason>" because the other language handles it natively. A built capability with no mirror entry — not even NYI — is a gap.
- [ ] Glanceable: the entry is discoverable from the doc's model/section structure, shows the common path before any expert/hex form, and its minimal snippet is copy-paste-runnable (author can glance and build, not dig).

### Anti-pattern audit (manual review of the diff)
- [ ] No raw RVA where an Address Library ID belongs (AP1).
- [ ] No ABI inferred from prologue shape — abi_walker evidence cited (AP2).
- [ ] No vtable index from a canonical CryEngine header — probed against the binary (AP3).
- [ ] Every hook/patch registers a conflict_engine footprint; no ApplyAll() from production (AP4).
- [ ] No new kcdx-side Lua static-const sentinel; PROBE Q expected to read zero (AP5).
- [ ] No Lua callback invoked off the main thread (AP6).
- [ ] The feature ships its permanent test-plugin + matrix row (AP7).
- [ ] No silenced check / dropped footprint / weakened assertion (AP9).

### Known non-goals
<anything deliberately deferred; restate so the user can confirm it's intentionally absent>

### What this proves / what I'll look for
<One plain-English falsifiable sentence: what the run confirms or denies. Then the exact log signal: the matrix row(s) that must read PASS plus the `FAIL <row>:` text that would deny it. The user reads this to know why they're launching; I read it to know what to grep for in kcdx-dev.log afterward.>

### Test procedure (run verbatim)
<Render the USER-KEYBOARD-ONLY procedure per `.claude/rules/test-suite.md` ("The test procedure"): the canonical launch-to-menu (Launch → reach menu → Quit → tell me it ran) with every cycle's declared `console`/`in-game` user gestures merged in between "Reach the main menu" and "Quit", each tagged with the matrix row it confirms. Do NOT include deploy, dev-mode, or log-read steps — deploy + dev mode are already done; the log read is mine after you signal. Confirm every checklist item above corresponds to a matrix row I will check post-run.>
```

## Rules for the checklist

- **Every behavior the change introduces or modifies is a separate item, each a single falsifiable claim.** A console command + its arg parsing + its self-test is 3 items.
- **Plain-English framing — required.** Lead each item with the observable behavior or failure-trigger condition; symbol names (functions, hook sites, Address Library IDs, vtable indices, TOML keys, KV categories) go in parentheses as anchors. ❌ *"`HookedAddCommand` stores `r8` at record `[+0x20]` via vtable[33]"*. ✅ *"Registering a console command writes the function pointer into the engine's command record so the engine calls back into kcdx (function-pointer overload of AddCommand at vtable slot 33, `src/console.cpp`)."*
- **Failure paths enumerated separately** — 5 abort/error branches = 5 items, never "error handling works." Each names the condition and the KV log line.
- **Integration points get their own section** — each names what crosses the boundary before naming the symbol.
- **Docs & glossary is its own section**, every checkpoint — a capability without its doc entry + glossary term + parity row is not done (`docs-discipline.md`).
- **Anti-pattern audit is its own section**, every checkpoint.
- **Never write "the matrix passes" as a single item** — the in-game result is what this launch verifies.

## When the user rejects an item

1. Stop. Do not treat the change as accepted.
2. Identify which behavior/branch owns the rejected item and which commit introduced it.
3. Re-task via `/execute` with the rejection as the failure surface — the fix lands as a new follow-up commit, never an amend.
4. After the fix lands, produce an updated checkpoint with the fixed item marked.

Never batch multiple corrections. Each rejected item gets surfaced + fixed + re-presented individually.

## What this skill does NOT do

- Does not launch the game — the user does, after this skill has verified deploy hashes match (§"Deploy status"). When the user signals the run is done, the agent reads `kcdx-dev.log` itself and reports the matrix verdict; the user never reads the log.
- Does not commit — `/execute` already committed the work; a matrix-row status update after the launch is a trivial direct `/commit`.
- Does not modify code. A rejected item is fixed via a re-tasked `/execute` cycle. A failed deploy-hash gate is fixed by re-running the deploy + re-verifying — not a code change.
