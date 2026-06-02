# Step 12 — s07 DLL link (verification context, advisory/overridable)

**What.** Build the status-bar DLL-link (s07): a `[Link DLL…]` control that points the
tool at a game DLL; the existing `.rdata` version resolver (`version_resolver.py`, R12
— CONSUME, do not rebuild) scans it for the `release_M_N_BUILD_SUB` intern (≥2 agreeing
interns) and resolves the version. The resolved version (a) marks the matching
`address_versions` row as "current / matches linked DLL" in s02/s03 (in place — law 3),
and (b) becomes the prefill source for a new version's `valid_from_version` (step 13).
**The link is advisory, never required** (law 4, D9): unlinked is a normal state (s02
default-selects newest — D10); a resolver failure (`<2` or disagreeing interns) is
shown as an advisory warning, and any version-stamping action carries the "I accept —
save anyway" override (wired into the step-11 confirm). Status-bar segments: DLL-link
state, the last save result (step 11), transient notices.

**Scope.** The s07 DLL-link control + the resolver binding + the resolved-version
markers in s02/s03 + the advisory-warning / override plumbing into the confirm. CONSUMES
`version_resolver.py` (the scan + intern-agreement exist). The save-result segment was
built in step 11; this completes s07's DLL/verification part.

**Test bar.** The resolver is oracle-tested already (`test_version_resolver.py`); the
intern-agreement + interval-contains-ordinal current-row filter logic is headless
(extend that test for the interval filter if not already covered). The GUI link control
+ the "matches linked DLL" marker + the advisory-warning/override rendering are verified
at the phase's user-facing acceptance gate.

**Test bar runnable now?** Yes — the resolver/interval test runs now; the link UI is an
eyeball gate (the resolver + the version table from step 9 are its inputs).

**Dependencies.** Step 9 (the version table the "current" marker decorates) + step 11
(the confirm the override plugs into). The existing `version_resolver.py`. Sequenced
first in Phase 3 because steps 13 (create version, prefill from resolved version) + the
"current" marker reference it — but advisory, so the spine already works without it.

**Design authority.** [`data/maintainer-tool/ui/screens/s07-status-dll-link.md`](../../../../data/maintainer-tool/ui/screens/s07-status-dll-link.md)
(the full segment set + states). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-10 + the current-version-row-resolution paragraph + §8 (R12 constraints) + §10
D9/D10. `requirements.md` R12 (the `.rdata` scan + intern-agreement + interval filter).

**UX** (`.claude/rules/ux-first-class.md`, from s07):
- **Unlinked (default)** — *"No DLL linked"* + `[Link DLL…]`, neutral (not an error —
  the normal state; the tool works).
- **Linked** — *"Linked: `<dll>` — version `<M.N.BUILD>`"* (mono) + `[Unlink]`; the
  resolved row gets the "matches linked DLL" marker in s02/s03.
- **Resolver failure** — *"Linked DLL — couldn't resolve version (interns disagree)."*
  (warning); version-stamping carries the override (law 4). System-caused copy naming
  the specific cause.
- **Edge content** — a long DLL path truncates (full on hover); the version tag is
  fixed mono width.
- **Flow + feedback:** Link DLL → resolved version shown + markers update in place
  (law 3 — never re-selects a row or navigates) → create flows prefill from it.
- **Accessibility + consistency:** link/unlink keyboard-reachable; the state is
  glyph+text (law 7); the resolver is bound, not reimplemented (law 6).

**Disassembler-test / author-burden.** This step is the engine carrying the version
resolution — it reads the game version FROM the binary's `.rdata` so the maintainer
never hand-types it (the disassembler-test's spirit: the engine does the heavy lifting,
the author declares intent). No author hex burden added; this REMOVES one.
