# Step 1.1 — correct KI-0019 / KI-0006 routing

**What.** Repoint the routing of [KI-0019](../../../known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)
and [KI-0006](../../../known-issues/KI-0006-serve-execute-vehicle-not-found.md)
from the now-stale "bundled → Phase 11 / FIX A" to this design's own track. Both
KIs say their fix is deferred to Phase 11; the user settled that the file-system
takeover is their own track and this design is their home (design §9). Update each
KI's resolution-routing section to point at `docs/design/file-system-takeover.md`
and this plan, keeping the KIs OPEN (they close at step 4.2, not now).

**Scope.** Edit the routing/resolution-routing prose in both KI docs only — no
status change (stay OPEN), no Resolution paragraph (the fix has not landed). A
docs-only commit. The KI index (`docs/known-issues/README.md`) rows need no change
(status stays Open); if a row's "what it is" mentions the Phase-11 bundling,
update that one-liner.

**Test bar.** Docs-only — no code test. Verification: both KI docs name this
design as their home; neither still says "Phase 11 / FIX A" as the fix route; the
KIs remain in the open tree (not moved to `closed/`). A reviewer confirms the
routing reads coherently (`.claude/rules/deletion-hygiene.md` — no stale
prescriptive reference to the old routing survives).

**Dependencies.** None — this is the first step (pure docs cleanup; preserves our
place per design §11).

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §9 (the resolution
routing), design §11 (in-flight state to preserve).

**Disassembler-test / author-burden.** N/A — no author-facing surface.
