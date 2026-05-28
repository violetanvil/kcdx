# Anti-pattern rationale — audit record (NOT auto-loaded)

The durable record of **why** each anti-pattern in `.claude/rules/anti-patterns.md` exists. This file has no `paths:` frontmatter and is referenced by nothing — it never auto-loads into agent context. Its job is audit, not enforcement: the detection signatures live in `anti-patterns.md`, the prescriptions live in the matching `.claude/rules/*.md`, and the reasoning lives here.

**Writing here requires the user's express consent.** A PreToolUse hook (`guard-anti-pattern-consent.ps1`) forces an accept-prompt on every edit to this file and to `anti-patterns.md`. An agent cannot silently add an anti-pattern or rewrite its rationale — every change surfaces to the user for an explicit accept. An AP entry whose rationale here was not blessed by the user is an unauthorized rule; the accept-prompt is the bless.

One section per AP, numbered to match `anti-patterns.md`. New AP → a new section here, accepted by the user at write time.

---

## AP1 — Raw RVA instead of an Address Library ID

RVAs shift on every KCD2 update; a hardcoded offset compiles fine and silently points at wrong code after a patch.

## AP2 — ABI inferred from prologue shape

A wrong arg count compiles, hooks, and silently corrupts state, and fails no gate.

## AP3 — vtable index from the canonical CryEngine header

KCD2 orders its vtables differently from canonical CryEngine headers (`IConsole::AddCommand` is slot **33**, not 32). A header-derived index compiles and calls the *wrong method*.

## AP4 — Installing a hook outside the conflict engine

A hook installed without a footprint bypasses conflict detection and two plugins on one site clobber each other with no log; `patch::ApplyAll()` in production reintroduces the patches-always-before-hooks ordering bug.

## AP5 — A new kcdx-side Lua static-const sentinel

kcdx and WHGame each embed their own Lua sentinels in their own `.rdata` over one shared `lua_State`; a kcdx-side sentinel pointer in a GCObject on `g->rootgc` is misread by WHGame's GC and freed → `STATUS_HEAP_CORRUPTION`. Compiles clean; corrupts on the next allocator activity.

## AP6 — Lua callback invoked off the main thread

The live `lua_State` is single-threaded (main-thread-only contract); off-thread invocation races the engine's Lua activity and corrupts state — passes a one-shot test, crashes under load.

## AP7 — Feature without its permanent regression plugin

A feature with no test plugin has no guard against silent regression, and "it built" says nothing about whether it works in-game.

## AP8 — Self-reported gate without command output

A self-report is unverifiable until the command runs and its output is read.

## AP9 — Silencing a check instead of fixing the cause

When the check is the goal, the cheapest path to green rarely fixes the cause.

## AP10 — Theorizing on a checkable unknown instead of probing it

An unverified theory drives a change that may miss the cause, and a confirm-only probe launders the assumption rather than testing it.

## AP11 — Inserting a member mid-struct in a plugin-facing interface (ABI break)

Plugins call interface members at fixed byte offsets; an inserted member shifts every later offset, so a pre-built plugin calls through a garbage pointer → `ACCESS_VIOLATION` on first use (often at load). Compiles clean; a plugin that AVs on load never reports, so a "no FAIL lines" read looks green while half the suite silently died.

## AP12 — Author hex/ABI burden: making the author do the engine's heavy lifting

The UX cornerstone is *the engine does the heavy lifting*; an author forced to reverse-engineer ABIs / hunt offsets doesn't adopt kcdx. Passes every gate (field parses, hook installs) while failing the cornerstone. "The author can just provide the signature/address/offset" is the tell.

## AP13 — Recording a known correctness gap as a someday-maybe follow-up

Once a gap is KNOWN, waiting for a user to trip it is shipping a known defect. Same forbidden shape as AP12's "expert-only-for-now" and the disassembler-test's "the author can just provide it." Implementation effort never justifies deferral (`cornerstones.md`); only genuine undesigned scope does, and that gets surfaced with a next step, not buried. The class is invisible to every gate — the build is green and the suite passes precisely because the gap was written down instead of fixed.

## AP14 — Silent failure: dropping/neutralizing input instead of failing loud

A path that swallows the error keeps the build green and the launch crash-free while the author's intent silently evaporates — the worst failure mode for an authoring tool, because nothing tells the author their config did nothing. (The 0xC8-bug class: a write that misses but reports OK; the author debugs a phantom.) "Handled defensively so it won't crash" is the tell — safe ≠ served (cf. AP13). The cornerstone is errors that teach; a silent drop teaches nothing. Formalized 2026-05 from house vocabulary used widely before it was written down.

## AP15 — A test self-check that cannot fail (non-falsifiable PASS)

A test exists to go red when the behavior breaks. A PASS asserting a tautology (always-true), reporting before the behavior could fire, or reading back a value it just set, is green theater: it grows the matrix count without adding coverage, and a real regression slips past it. Specialization of AP10's falsify-not-confirm into the test-suite surface. Formalized 2026-05 from house vocabulary used widely before it was written down.

## AP16 — A private citation in a public-facing file

The public repo is the sanitized projection (allowlist; `public-private-boundary.md`) and deliberately shows no trace of AI-assisted development. A `.claude/`/`_research/` link or a bare `AP<n>` in a public `.cpp`/`.md` is a double defect: a **broken link** on public (the target was never published) AND a **build-trace** (the one thing the projection most must hide). The root cause is the governance teaching AP-annotation + rule-citation as house style, which agents faithfully carry into public code — so the fix names AP-citation-in-public as itself an anti-pattern. The knowledge is kept (the fact is restated self-contained); only the private pointer is dropped. The why for an internal reader still lives in the private rule; the public file owes the mod author the *what*, not the governance provenance.

## AP17 — Fixing the symptom without naming the root cause

A passing repro after a code change is visually identical whether the change fixed the bug or merely masked it. The matrix passes either way; the build is green either way; the user's repro stops firing either way. The forbidden shape is closing a known-issue with "X no longer crashes" — a restatement of the symptom going away, never of mechanism. The bug is still present and will resurface the next time its trigger conditions align (often as a different-looking crash in a nearby subsystem, which a future investigator then chases as an unrelated bug). The root-cause paragraph is the durable artifact that lets a future agent recognize the same mechanism re-firing under a new symptom; without it, every reoccurrence costs a fresh investigation. House rule per user: this is non-negotiable. The only legitimate escape is an explicit user-approved "Provisional mask, root cause unknown" Resolution label — the bug stays open and the label marks the gap so a future agent doesn't read the masked file as closed.
