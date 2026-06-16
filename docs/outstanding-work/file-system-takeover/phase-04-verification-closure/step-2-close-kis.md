# Step 4.2 — close KI-0019 / KI-0006

**What.** Close [KI-0019](../../../known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)
and [KI-0006](../../../known-issues/KI-0006-serve-execute-vehicle-not-found.md)
now that the takeover has removed the cross-CRT crash class. Each gets a Resolution
section with the root-cause MECHANISM paragraph (AP17): the cross-CRT
boundary-straddle — kcdx minted a `FILE*` with kcdx's CRT, the engine's `ucrtbase`
operated it (`fseek` for KI-0019, `fclose` for KI-0006) on an fd invalid in the
engine's CRT → AV — REMOVED structurally because kcdx now owns the read family
(every kcdx handle operated only by kcdx's CRT, design §9). Not "no longer
crashes" — the mechanism + why the takeover makes it impossible.

**Scope.** Per the close ceremony (`.claude/rules/doc-organization.md` §2): append
the Resolution mechanism paragraph to each KI body, `git mv` each to
`docs/known-issues/closed/`, move each index row Active→Closed and repoint the
link — all in one commit. The Resolution is gated through `root-cause-verifier`
(Gate B, the debug-protocol discipline) BEFORE it lands — the mechanism paragraph
is verified, not self-attested.

**Test bar.** The KI-0019 repro (load save → enter world → open inventory) runs
clean — this is the acceptance check, agent-read from `kcdx-dev.log` (no `FAULTED
… hook=engine.ccrypak_*`). A passing repro after a real fix is indistinguishable
from masking on its own (AP17), so closure rests on the verified MECHANISM
paragraph, not just the clean repro. `root-cause-verifier` returns `land-fix`
before the Resolution is recorded.

**Dependencies.** Step 3.2 (the open+read cutover — where the read family flips to
kcdx's CRT and the class dies) + step 4.1 (the regression rows proving the serving
works). The clean repro is the step-3.2 acceptance carried to closure here.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §9 (the
KI-0019/KI-0006 resolution mechanism); `.claude/rules/doc-organization.md` §2 (the
close ceremony); `.claude/rules/anti-patterns.md` AP17 (root-cause mechanism
required); CLAUDE.md (the root-cause-required hard rule + Gate B).

**Disassembler-test / author-burden.** N/A — bug closure, no author-facing
surface.
