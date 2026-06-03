---
paths:
  - "src/**"
  - "include/**"
  - "vendor/lua/**"
---

# Anti-patterns and the invariants-vs-gates frame

This file carries TWO layers. The **system AP-class floor** (immediately below) is the language-agnostic set of CLASSES every repo inherits — each a shape to reject. The **kcdx numbered table (AP1–AP18)** that follows is this repo's concrete, domain-worded instances. Every system class stays represented by at least one kcdx AP; kcdx adds domain-specific rows beyond the classes; a class is never deleted. A surfaced option matching any class — system or numbered — is labeled and cannot be Recommended (§"Surfacing-an-option self-check").

## The system AP classes (the floor — each a shape to reject)

The build gate and the test gate are **necessary but not sufficient.** They check that nothing is obviously broken; they do not check that the system does what it is supposed to do. The **invariant** is the goal — the property the gate exists to protect. Any change that satisfies a gate while violating the invariant the gate protects is **rejected**, regardless of how green the build looks. Each class is a shape to reject; the kcdx AP1–18 below state the concrete, numbered, domain-worded instances.

- **cfg-test early-return** — a test-only conditional that early-returns or shims behavior inside production code; the test exercises the wrapper, the real path ships unverified while coverage counts it covered. (kcdx instance: AP15.) **Instead:** test the real path with a real fixture; if it genuinely cannot run in tests, exempt it through the bucket-2/3 flow with primary-source justification — never a test-only shim.
- **DI-only-for-tests** — a function-pointer/seam injected solely so a test can substitute a fake; the seam exists for the test, not the design. **Instead:** test against the real collaborator or a real fixture; introduce a seam only when the design needs more than one implementation.
- **extract-then-test-wrapper** — extract logic into a helper, then test only the thin wrapper; the moved behavior is now untested, the wrapper test passes vacuously. (kcdx instance: AP15.) **Instead:** test the extracted behavior directly.
- **ABI / wire invention** — inventing an ABI byte, wire field, offset, serialization slot, or interface signature without the design source's pre-approval; a guessed contract is a silent breaking change. (kcdx instances: AP1, AP2, AP3, AP11.) **Instead:** read the authoritative record (the function body / the spec / the disassembly) and surface any contract change for pre-approval before writing it.
- **stub-returns-Ok** — a verifier/parser/validator/check that returns success unconditionally; the gate looks present but checks nothing. (kcdx instance: AP14.) **Instead:** implement the real check or surface that it cannot be implemented yet — never a green stub.
- **self-reported-gate** — claiming a gate passed ("it builds", "tests pass", "N% coverage") without the command output that proves it. (kcdx instance: AP8.) **Instead:** the manager runs the gate itself and reads the output; a self-report is not evidence.
- **coverage-gaming** — exempting items, lowering a threshold, dropping a check, or adding a suppression to make a coverage/test failure disappear rather than fixing it. (kcdx instances: AP7, AP9.) **Instead:** fix the failure at its source, or surface the exemption as an explicit per-instance user ask.
- **bucket self-designation** — self-labeling a closure as exempt without surfaced user approval; the label is the agent's framing, not a fact. (kcdx adjacent: AP18 — a gated DB addition is the user's call, not the agent's.) **Instead:** run the bucket-1 challenge first; surface the exempt designation for approval — never self-assign it.
- **deferred-correctness-gap** — a known-wrong code path buried as a someday-maybe instead of fixed or surfaced; "handled defensively so it won't crash" treats safe as fixed. (kcdx instances: AP13, AP17.) **Instead:** fix it in the same unit of work, or surface it as an explicit fix-now / next-cycle / genuine-scope decision (`design-authority.md`); never a quiet deferral.
- **silent-success** — a path that swallows an error, drops unparseable input, no-ops a degenerate value, or returns success having done nothing; author intent vanishes with no signal. (kcdx instance: AP14.) **Instead:** fail loud with a structured error naming what was rejected and why (`logging.md`, `input-validation.md`); a validate-OK path must actually do the work.

## The kcdx invariants-vs-gates frame

The build gate (`pwsh ./build.ps1` green) and the test gate (`test-plugins/` matrix `X/Y passing`) are **necessary but not sufficient** — several can be silently defeated. The invariants are the goal:

- A hook installs through the conflict engine; two plugins on one site can't silently clobber each other.
- A game-function offset matches the binary — verified by an Address Library ID, not recalled from a CryEngine header.
- A hooked function's signature matches the real ABI (arg count, types, `this`) — not a prologue-shape guess.
- kcdx never writes a kcdx-side Lua sentinel pointer into a GCObject the engine's GC touches.
- Every feature has a permanent regression plugin that exercises it.

A change that satisfies a gate while violating the invariant the gate protects is **rejected**. Before any non-trivial change ask: *what invariant does the gate protect, and does this change preserve it?* Unclear → stop and ask.

Each AP below is a **detection signature** — the forbidden code shape a reviewer scans a diff against — plus a pointer to the rule that prescribes the fix. The **why** for each AP lives in `.claude/anti-pattern-rationale.md` (audit-only, not auto-loaded, writes are user-consent-gated). Adding or changing an AP here triggers an accept-prompt; an unblessed AP is an unauthorized rule.

---

## AP1 — Hardcoded game address instead of an Address Library name/id

**Forbidden:** a literal per-version-volatile game-binary target in `.cpp`/`.h` — an RVA / module offset, an AOB or byte-pattern locator, a vtable slot index, or a game-struct member offset — used to reach a game function or field. Anything whose correct value shifts when KCD2 patches.

```cpp
auto fn = reinterpret_cast<UpdateFn>(base + 0x180ABCDEF);  // hardcoded offset
```

**Fix:** resolve by name/id through the DB (`kcdx::ResolveAddress` / `ResolveByName` / `target = "<name>"`); the name supplies address AND verified ABI. A new entity goes in the DB via the seed CSVs under `data/seeds/` (user-approved, AP18), never an in-source literal. IDs are append-only — never renumber. The full in-source prohibition + the narrow non-address exception: `no-hardcoded-addresses.md`. Resolution order + DB shape: `address-library.md`.

---

## AP2 — ABI inferred from prologue shape

**Forbidden:** typedef'ing a hooked function's signature by eyeballing its prologue / register touches.

**Fix:** walk the reuse ladder (`reverse-engineering.md`); for facts none cover, run `_research/phase6-save-load/phase6_abi_walker.py` (capstone body-wide stack-arg analyzer) on the target. Cite the evidence in a `// SOURCE:` comment.

---

## AP3 — vtable index from the canonical CryEngine header

**Forbidden:**

```cpp
// CryEngine's IConsole::AddCommand is slot 32
call_vtable(pConsole, 32, ...);   // canonical-header slot; KCD2 uses 33
```

**Fix:** probe each interface against the binary (decompile prologue + suffix, cross-check engine callers, cross-check error-message strings). Each interface needs its own probe — don't generalize "canonical slot N holds X".

---

## AP4 — Installing a hook outside the conflict engine

**Forbidden:** calling MinHook directly, or calling `patch::ApplyAll()` / `hook_engine::ApplyAll()` from production orchestration in `hooks.cpp`.

**Fix:** produce a footprint (writes + reads + priority + name); let conflict_engine classify and log. New engine (e.g. a future vtable-hooking primitive) → extend `WriteKind`/`Category`, register footprints, add no cross-engine knowledge. Per `hook-engine.md`.

---

## AP5 — A new kcdx-side Lua static-const sentinel

**Forbidden:**

```cpp
static const Node kEmptyNode = { ... };   // kcdx .rdata sentinel
t->node = &kEmptyNode;
```

**Fix:** raw Lua C API on the live state (`luaL_newmetatable` + `lua_pushcfunction` + `lua_setfield`), registry refs (`luaL_ref`) for callback storage, raw userdata (`lua_newuserdata` + `luaL_setmetatable`) for marshaled types. Per `lua-bridge.md`. PROBE Q must still read zero.

---

## AP6 — Lua callback invoked off the main thread

**Forbidden:** firing a registered Lua callback from a hook that runs on a worker / render / audio thread.

**Fix:** marshal the invocation to the main thread. If a hook site only runs off-thread, surface it — don't fire the callback there. Per `lua-callback-threading.md`.

---

## AP7 — Feature without its permanent regression plugin

**Forbidden:** landing a new primitive / Lua surface / engine behavior and calling it done because the build is green.

**Fix:** ship a suite-gated (`test_suite_only = true`) plugin under `test-plugins/<row-id>-<short-name>/` that self-checks via `ReportTestResult(...)` / `kcdx.test.report(...)`, prefers an auto-pass boot check, and adds a matrix row. A behavior-changing bug fix gets a sub-test in the existing plugin reproducing the bug. Per `test-suite.md`.

---

## AP8 — Self-reported gate without command output

**Forbidden:**

> "Build is green. The test suite passes."

**Fix:** when claiming a gate's state, paste the verbatim output (or excerpt) — the actual `build.ps1` exit, the actual `suite: X/Y passing` line.

---

## AP9 — Silencing a check instead of fixing the cause

**Forbidden** without explicit user approval per use: marking a test plugin `test_suite_only` to hide a failure it's reporting; dropping a hook's conflict-engine footprint to silence an overlap warning; weakening a self-check assertion; introducing a new `// X-ok:`-style annotation escape; reaching for mempatch because a kcdx hook is harder (mempatch is deprecated — byte-rewrite / hook / trampoline / engine-fix all ship through kcdx).

**Fix:** fix the cause. If a check is genuinely wrong, state that to the user with the reasoning and wait for direction.

---

## AP10 — Theorizing on a checkable unknown instead of probing it

**Forbidden:** changing code on a checkable-but-unchecked assumption ("this is probably because…" / "I think the engine…"); a second fix on the same symptom after the first failed without a variable-isolating probe between; or a probe **shaped by the theory it claims to test** (confirm-only, predicted/"expected" outcome, or testing a *theory about* a fact instead of observing the fact). Theory-hopping after a disconfirmation (kill A → propose B + B-shaped probe instead of re-observing) is the same pattern.

**Detection signature (scan a diff/history against this shape):** two consecutive edits to the same symptom's code path (same test plugin's report/assertion logic, or the same engine site) with NO `// === DIAGNOSTIC (PROBE …)` edit or read-only probe between them — i.e. fix → relaunch FAIL → fix, the probe step skipped. The fresh-frame *subagent* threshold is 2+ hops, but the *probe-not-fix-#2* obligation triggers after the FIRST failure (`results-driven.md` §Fresh-frame escalation floor).

**Fix:** the full discipline — theory-independent probe, FALSIFY-not-confirm, outcome→meaning map, fresh-frame escalation, hand-over-the-launch — is in `results-driven.md`. Hard bug → `/debug`.

---

## AP11 — Inserting a member mid-struct in a plugin-facing interface (ABI break)

**Forbidden:** adding a new function pointer or field anywhere except the END of a struct plugin DLLs compile against (`kcdxInterface`, `kcdxMessagingInterface`, `kcdxScriptingInterface`, the other `kcdx*Interface` structs, `kcdxPluginInfo`, etc.).

**Fix:** **append-only** — new members go at the END after an `// --- APPEND-ONLY BELOW ---` marker. Never reorder or insert. Mirror the positional initializer order in `interfaces.cpp` exactly. A genuine shape change → bump `kcdx<Name>Interface_Version` and gate the new layout. Verify by re-launching with the EXISTING (not-rebuilt) plugin set: the InputLoaded listener count unchanged = no break; a drop = an ABI break.

---

## AP12 — Author hex/ABI burden: making the author do the engine's heavy lifting

**Forbidden:** designing an author-facing surface whose only path for a **common task** makes the author supply hex-tier input the engine could resolve (address, offset, register, instruction length, hand-written signature / byte-pattern) when a name-based form is possible. Also: an expert-hatch target that is NOT shareable; a later engine-shipped name that silently overrides or breaks an author's pre-existing manual declaration. Tell: *"the author can just provide the signature/address/offset."*

**Fix:** run the disassembler test on every author-facing input; the doctrine (name-supplies-address-AND-ABI, expert-hatch labeling, declare-once/share/coexist, surface-the-exception) is in `cornerstones.md`. Surface any common-task hex burden to the user.

---

## AP13 — Recording a known correctness gap as a someday-maybe follow-up

**Forbidden:** on finding a code path that mis-resolves / mis-attributes / mis-serves (a wrong SOURCE behind a defensive NULL-guard, an identity or edge case "that probably won't come up"), recording it as deferred instead of fixing it. Tell-phrases: *"if a real plugin hits it"*, *"revisit when someone needs it"*, *"v0.2 adds…"*, *"flagged for later"*, and *"handled defensively so it won't crash"* (safe ≠ fixed — a degraded-but-non-crashing path still mis-serves the author).

**Fix:** fix it at the source in the **same** unit of work. If it is genuinely separate scope, surface it to the user as a DECISION (fix-now-this-cycle / its-own-next-cycle / genuine-deferred-scope) — never bury it in matrix Notes, a known-issue, or a deliverable as a someday-maybe. The one legitimate deferral is genuine scope-creep / an undesigned feature, **surfaced as such** with a real next step — not a known bug in shipped behavior. Per `cornerstones.md` (implementation effort never justifies deferring the correct answer).

---

## AP14 — Silent failure: dropping/neutralizing input instead of failing loud

**Forbidden:** swallowing an error, dropping unparseable author input, no-op'ing on a degenerate value, or returning success from a path that did nothing — author intent silently vanishes (the 0xC8-bug class: a write that misses its target but reports OK).

```cpp
if (!parsed) return;            // author's key silently dropped
if (idx >= count) return true;  // out-of-range → "success", did nothing
```

**Fix:** fail LOUD — a structured error naming what was rejected and why, surfaced where the author sees it. An allowlist rejects unknown input with a message, never a silent skip; a validate-OK path must actually do the work or report that it didn't. Per `logging.md` (structured KV) + `cornerstones.md` (errors that teach).

---

## AP15 — A test self-check that cannot fail (non-falsifiable PASS)

**Forbidden:** a test-plugin row whose PASS is a tautology — asserts something always true, reports PASS before the behavior under test could fire, or checks a value it just set. A test that can never go red proves nothing.

```lua
report("loaded", true)   -- always true once the plugin loads; tests nothing
```

**Fix:** every row carries a FALSIFIABLE claim — state per row exactly what makes it FAIL ("FAILS if neither slot fires"; "the reject-row reads the actual accessor output, not the input"). A hook-fired test self-reports from the callback's first fire, not a pre-fire lifecycle point. Per `test-suite.md` + `results-driven.md` (falsify-not-confirm).

---

## AP16 — A private citation in a public-facing file

**Forbidden:** a file that ships to public (an allowlisted public dir / root file per `public-private-boundary.md`) referencing anything private — a `.claude/` or `_research/` path, `CLAUDE.md`, a bare `AP<n>` rule citation, the words Claude/Anthropic/subagent/orchestrator, a governance slash-command. Internal house style leaking outward:

```cpp
// diverging from the canonical header per AP3       // AP-citation in public .cpp
// Verified by Ghidra, _research/phase7/FINDINGS.txt // private-path provenance
```
```markdown
See [`naming-namespaces.md`](../.claude/rules/naming-namespaces.md).   <!-- broken link on public -->
```

**Fix:** state the fact self-contained — keep the knowledge, drop the private pointer. "per AP3" → "this slot is empirically probed against the binary, not assumed from a header"; a `.claude/rules/` link → restate the rule inline; `_research/...` provenance → "verified via Ghidra analysis". AP-numbers and rule-file citations are **internal shorthand — legitimate in a private file, never in a public-facing one.** Per `public-private-boundary.md`; warn-only `guard-public-private-refs.py` flags it at author-time.

---

## AP17 — Fixing the symptom without naming the root cause

**Forbidden:** landing a fix whose `docs/known-issues/<title>.md` Resolution section cannot answer "why did this happen?" in one concrete mechanism paragraph — *what value was wrong, who wrote it, in what order, why the original code path made that wrong write inevitable*. A passing repro after a code change is identical-looking whether the change fixed the bug or merely masked it; the matrix passes either way. "X no longer crashes" / "the AV is gone" / "now boots to menu" are restatements of the symptom going away, NOT root cause.

```markdown
## Resolution
- Root cause: removed the call that was triggering the AV.   <!-- AP17: not a mechanism -->
- Fix: deleted ModManager_ParseManifest invocation.
```

```markdown
## Resolution
- Root cause: the manifest ptr written into [rbx+0x30] at frame-4 is a CryStringT
  pointer-to-chars without the {pad,nRefs,nLength,nAllocSize} header preceding it;
  the engine reads nLength from the 16 bytes BEFORE the chars, gets garbage,
  passes it to CryStringT::reserve which calls the allocator with a multi-GB
  size, the allocator's invariant assert fires.                <!-- mechanism -->
```

**Fix:** before landing, write the root cause as a falsifiable mechanism. If you cannot — "I don't know exactly why" / "it just stopped" — you don't know yet; another probe is owed (`results-driven.md`). The `Root cause:` paragraph names the data flow + control flow + the specific invariant the original path violated. A fix without root cause is **provisional masking**, not a fix — land it only on explicit user approval, and only under a labeled "Provisional mask, root cause unknown" Resolution section that says so.

---

## AP18 — Adding an Address Library seed row without explicit user approval

**Forbidden:** appending a NEW entity/version row to `data/seeds/address_names_seed.csv` or `data/seeds/address_versions_seed.csv` without the user's explicit sign-off on that specific entity. A seed addition grows the Address Library DB — a new curated game-binary target (RVA / AOB pattern / vtable slot / game-struct offset) the project commits to maintaining across game versions — and that growth is the user's decision, not the agent's.

```diff
# data/seeds/address_names_seed.csv
+144,SaveGame,,,,,,"7-arg save hook target"        # AP18: new entity, no user approval
# data/seeds/address_versions_seed.csv
+144,1.5.1164953,WHGame.dll,0x019DDE78,...          # AP18: lands the DB row u.gated
```

**Detection signature (scan a diff against this shape):** a diff that ADDS a data row (not a comment, not the header) to either curated seed CSV, with no recorded user approval of the entity in the landing conversation. An UPDATE to an existing row (re-verify, bump `last_verified_at_version`, deprecate, supersede) is NOT this AP — only a NEW entity/version row is. Resolving a game address by name/id in code (AP1's fix) is NOT this AP either — that is the always-on expectation, not a gated DB addition.

**Fix:** STOP before writing the row; surface the proposed entity to the user (name + what it targets + why it's needed) and get explicit approval, THEN append. Per `address-library.md` + `data/seeds/policy.md` §"DB additions require explicit approval". Warn-only `guard-seed-approval.py` flags the addition at author-time; the review gates carry the hard check.

---

## Surfacing-an-option self-check

Before any surfaced option (per `_shared/architectural-review.md` §"Design decisions surface"), audit each option against the AP classes above AND the numbered AP1–18 table. An option matching one is labeled prominently — `[AP<N> hit — <pattern>]` — and **cannot be marked Recommended**; a rule-compliant alternative must be surfaced first.

## Scope

This file covers the class where code looks fine to every gate yet violates an invariant. The system AP-class floor at the top is the language-agnostic set every repo inherits; AP1–18 are kcdx's domain instances of it. Every class stays represented; a class is never deleted; new domain rows append as new failure modes surface. Domain-specific wrong-ways live in their own rules (`hook-engine.md`, `lua-bridge.md`, `address-library.md`, `reverse-engineering.md`, `logging.md`); cite the rule each pattern enforces.
