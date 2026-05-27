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
