# Step 5.3 — close KI-0027

**What.** Close KI-0027 (the table-DB load failure from unserved wildcard-glob
directory enumeration) now that step 5.2 made kcdx own the `FindFirst`/`FindNext`/
`FindClose` triplet and a launch confirmed the table database loads. The closure
lands the Resolution mechanism paragraph + the open→closed move + reindex in ONE
commit (`.claude/rules/doc-organization.md` §2).

**Scope.** Append the Resolution section to
`docs/known-issues/KI-0027-fs-takeover-table-glob-enumeration-unserved.md`: the
root-cause MECHANISM paragraph (AP17 — WHY the load failed in falsifiable terms: the
table loader globs `<base>__*.<ext>` and dispatches it through CCryPak slots
63/64/65, which still thunked to the engine original after Phase 3 owned only slot
14; the engine's FindFirst walked only its own on-disk view and saw zero
pak-resident override entries, so the table-DB worker returned false and
`CSystem::FatalError` raised `err_id=259` — NOT "the table DB now loads"), the Fix
(the 5.2 commit + what changed + why owning the triplet over the unified set
addresses the mechanism), and the Verification (cap-118 + the repro-clean launch).
Flip frontmatter `status: open → closed` + `closed: <date>` + `closed_by_commit`.
`git mv` the file to `docs/known-issues/closed/`. Move the index row Active→Closed in
`docs/known-issues/README.md` + repoint its link to the `closed/` path. One commit.

**Gate.** The Resolution paragraph is gated through `root-cause-verifier` (the debug
Gate-B discipline — the verifier reads the KI doc + the 5.2 fix diff + the recon
headers with the working-agent reasoning WITHHELD, returns `land-fix` /
`probe-required` / `rewrite-resolution`). A HALT blocks the close; "mostly agrees"
is a HALT (`.claude/skills/_shared/verification-contract.md`).

**Test bar.** No new test — the closure is bookkeeping over the cap-118 row + the
repro-clean launch step 5.2 already established. The "test" is the
`root-cause-verifier` `land-fix` verdict on the mechanism paragraph + the close
ceremony landing intact (Resolution + `git mv` + reindex all in one commit, the
broken-closure check, `.claude/rules/doc-organization.md`).

**Dependencies.** Step 5.2 (the fix landed + the KI-0027 repro confirmed clean at a
launch — the closure cannot land until the user has experienced the table DB loading
and the matrix row passed). Ordered last in the phase.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §5.1;
[`KI-0027`](../../../known-issues/KI-0027-fs-takeover-table-glob-enumeration-unserved.md);
`_research/ki0027-table-glob-dispatch-recon/FINDINGS.md` (the dispatch fact the
mechanism paragraph cites).

**Disassembler-test / author-burden.** N/A — closure bookkeeping.
