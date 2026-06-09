# Phase 3 — C++ engine survival extension (the authority)

**Intent:** extend the C++ engine survival checker (`src/survival.cpp` /
`src/survival_pass.cpp`, function-hash-only today) to all 9 kinds — the **authority** the JS
browser checker mirrors (D27). Build bottom-up: per-kind dispatch + payload model + the
`Ambiguous` status, then the 5 static non-function kind checks + anchor ordering, then the
reachability check (resolve into live `.text`) + the on-disk version-applicability hash, then
the **JS↔C++ cross-impl agreement test** that pins the engine == the browser on the same bytes.
All in kcdx `src/` — gated by `pwsh ./build.ps1` + a live launch (`.claude/rules/agent-builds-and-deploys.md`:
the agent builds, deploys, hash-verifies, enables dev mode, reads the log; the user launches).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [3.1 [ENG] Per-kind dispatch + payload model + the Ambiguous status](step-1-eng-dispatch-payload-ambiguous.md) | DONE | (landed) — survival checker restructured into a per-kind dispatch + kind-discriminated Payload + the Ambiguous status; function-hash check preserved byte-identical under the dispatch, every non-function kind → a fail-loud stub (not_implemented_3_2 / vtable_index_deferred). cap-84 self-test (build green; step-review land-fix after the AP16 scrub). Live verdict batched to the end-of-feature launch. |
| [3.2 [ENG] The 5 static non-function kind checks + anchor-dependency ordering](step-2-eng-static-non-function-checks.md) | NOT STARTED | — |
| [3.3 [ENG] The reachability check + the on-disk version-applicability hash](step-3-eng-live-functional-check.md) | NOT STARTED | — |
| [3.4 [ENG] The cross-impl agreement test (JS browser == C++ engine)](step-4-eng-cross-impl-agreement.md) | NOT STARTED | — |

## Phase-0 scoping inputs (probe 0.3 — read before steps 1–3 build)

Probe 0.3 (`_research/maintainer-tool-verification-engine/probe-0.3-pe-helpers-surface-finding.md`)
scoped the existing `src/pe_helpers.*` surface — outcome **ROW 1 (reuse, not new-build)**:

- **Reusable primitives already exist.** Section spans (`ExecutableSections` / `ReadOnlyDataSections`),
  a RIP-relative `disp32` follower (`FindLeaXrefsTo`), the `.rdata` literal search (`FindCStringsIn`),
  and a LIVE resolver chain (`patch_engine.cpp` `ResolveAnchor` already chains
  string_anchor→instruction_anchor in production). The per-kind checks are THIN adaptations.
- **4 thin named gaps to build** (none new infra): G1 forward-`disp32` helper (the reverse follower
  exists; lift its decode), G2 a `.data` (`WritableDataSections`) predicate, G3 confirm
  `patch::Pattern` `?`-wildcard support, G4 a vtable-qword→`.text`-pointer classifier.
- **The per-kind on-disk-vs-live access split — SETTLED by the corrected D25.** The rich
  primitives run on a **live loaded `ModuleView`** (relocated bytes), while `survival.cpp` reads
  the **raw on-disk file** (un-relocated, for stable hashing). D25 fixes the split: the
  **version-applicability HASH is computed on-disk** (the loaded image is relocated + kcdx-detoured,
  so a loaded-body hash mismatches a genuinely-good entry), and the **reachability check reads the
  loaded image** (does the resolve land in live `.text`). So each kind's byte/AOB/hash check runs
  on-disk; the loaded-image access is the reachability resolve only (`fingerprint-per-kind.md`
  already flags `.data` content as unstable → data_slot is a derivation check, not a hash). No
  longer an open fork to surface — the corrected D25 decides it.

## Phase verification gate

Phase 3 is done when: the engine checker (steps 1–3) builds clean (`pwsh ./build.ps1`,
exit 0 + the 3 artifacts) AND a kcdx test-suite plugin exercising the per-kind static + live
checks PASSES at a live launch (the matrix row reads PASS in `kcdx-dev.log`, read by the agent —
`.claude/rules/agent-builds-and-deploys.md`); and step 4's JS↔C++ agreement test confirms the
engine and the browser checker return the SAME verdict on the SAME bytes (D27). Not a UI phase —
no maintainer-tool UI acceptance; the user gesture is the game launch only. Build-green is
necessary, not sufficient — the matrix is confirmed by the launch (`.claude/rules/skeptical-expert.md`).
