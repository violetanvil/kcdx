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
| [3.1 [ENG] Per-kind dispatch + payload model + the Ambiguous status](step-1-eng-dispatch-payload-ambiguous.md) | DONE | 8008e3d — survival checker restructured into a per-kind dispatch + kind-discriminated Payload + the Ambiguous status; function-hash check preserved byte-identical under the dispatch, every non-function kind → a fail-loud stub (not_implemented_3_2 / vtable_index_deferred). cap-84 self-test (build green; step-review land-fix after the AP16 scrub). Live verdict batched to the end-of-feature launch. |
| [3.2 [ENG] The 5 static non-function kind checks + anchor-dependency ordering](step-2-eng-static-non-function-checks.md) | DONE | 3c5e065 — the 5 on-disk static checks (callsite AOB→unique/zero/multi=Ambiguous; string_anchor .rdata literal+xref; instruction_anchor resolver chain; data_slot derivation, no content hash; vtable_base N-qword .text classifier) + CheckOrdered (the anchor-dependency DAG — a Changed anchor → transitive CannotCheck/anchor_changed) + vtable_index deferred. Reuses patch::Pattern (G3) + the ResolveAnchor chain; builds G1 forward-disp32 / G2 .data predicate / G4 vtable-qword classifier + an on-disk section walker (D25 on-disk). cap-84 self-test extended to 6 falsifiable sub-checks; build green, step-review land-fix (C4244 dead-param drop + .data virtualSize bound). |
| [3.3 [ENG] The reachability check + the on-disk version-applicability hash](step-3-eng-live-functional-check.md) | DONE | 69c7cc2 — the startup verification pass (src/survival_verify.{h,cpp}): per curated row, the on-disk version-applicability hash (reuses the 3.1/3.2 dispatch) + D34 attribution (the matched address_version id via refdb::CachedAddressVersionId) + the loaded-image reachability resolve (pe::IsVaInLiveText — resolve-into-live-.text, NOT a live-body hash per Probe 0.4). Verdicts resolves_works/wrong_target/dead/cannot_check. cap-84 self-test sub-checks 7-9; build green, step-review land-fix (no live-body hash + AP14 verified; one cosmetic doc-comment fix folded in). The engine-only authority Phase 4's batch plugin drives. |
| [3.4 [ENG] The cross-impl agreement test (JS browser == C++ engine)](step-4-eng-cross-impl-agreement.md) | DONE | ffc51ae — the JS↔C++ cross-impl agreement pin (D27): cap-85-survival-agreement asserts the C++ engine static verdict == the fixture's pinned verdict (== JS == Python) on the SAME bytes, HARD-pinned for the 4 algorithm-identical kinds (function incl. BLAKE3 hash-agreement, callsite, vtable_base, vtable_index). The Python source-of-truth fixture exported to JSON (the cross-language contract, round-trip-tested 6/6) + embedded in a generated C++ header; the engine drives its real checks over synthetic-PE-planted fixture bytes via a new SurvivalCheckOnBuffer headless seam (no check-logic change). The 3 superset kinds (string_anchor/instruction_anchor/data_slot — the engine computes more than the browser/Python subset, by design) are filed as TD-0009 for reconciliation (user decision). Build green; step-review land-fix. |

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

**Gate MET — accepted at the live launch 2026-06-09.** Deployed DLL hash-verified
(`AB48333923E820DB4664A9873BE5402BC998EFE75E2763BC3415731A8D4243FC`); the clean launch
(`kcdx-dev_2026-06-09_12-46-54.log`, after the KI-0012 boot crash was fixed) reported both
survival rows PASS: `RESULT name=cap-84-survival-dispatch verdict=PASS` (per-kind dispatch +
the 5 static checks + the 3.3 startup verification pass — IsVaInLiveText reachability,
RunStartupVerification per-row verdicts, D34 matched-id attribution, wrong_target on a
non-matching fingerprint) and `RESULT name=cap-85-survival-agreement verdict=PASS` (cross-impl
agreement on the 8 planted-PE slices, the 4 algorithm-identical kinds). The 3 unrelated suite
FAILs that run (CAP-20-addrname, CAP-28-typo-fails-fast, cap-90-pdb-internal-address) are other
lanes, not Phase-3 rows. TD-0009 (the 3 superset-kind engine↔browser reconciliation) remains
the deferred follow-up.
