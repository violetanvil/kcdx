# Step 11 — client-side JS `.rdata` resolver (D15) + the "check against a local DLL" control

**What.** Port the `.rdata` version scan to a small **JavaScript** function that runs in the
browser (D15): the maintainer picks a local DLL via the **File API** (no upload), the JS reads
its bytes in-page, scans `.rdata` for the `release_M_N_BUILD_SUB` intern (the hard
intern-agreement check — ≥2 agreeing interns), and resolves the version. Only the resolved
**version tag** is used (it sets the s02 version&verify surface's targeted version + marks the
matching row "checks out against your DLL"). Wire the "check against a local DLL" control in
s02 (placed in Phase 3 step 8) to this resolver. Advisory + overridable (law 4): a resolver
failure (`<2` / disagreeing interns) warns, never blocks.

**Scope.** The JS resolver module + the File-API DLL picker + the s02 "check against a local
DLL" control + the resolved/failure states. CONSUMES nothing server-side (the DLL never
leaves the browser). No new save path (the resolved tag flows into the existing save spine).

**Test bar.** A JS unit test (the frontend test convention) asserts the resolver on a known
DLL's `.rdata` bytes (a fixture byte buffer) resolves the expected tag + fails on `<2`/
disagreeing interns. **The cross-implementation agreement test** (D15): assert the JS port and
the Python `version_resolver.py` resolve the SAME tag on the same known DLL bytes (the
test-of-record agreement) — this is the load-bearing test that keeps the two implementations
in sync. Runnable now (the Python resolver is the landed test-of-record; the JS port + a DLL
byte fixture are this step's).

**Dependencies.** Phase 3 step 8 (the s02 version&verify surface the control lives in) + step
6 (the frontend). `version_resolver.py` (Phase 1 — the test-of-record). Sequenced first in
Phase 4 because the create flows (step 12) prefill from a resolved/picked tag — but advisory,
so the spine + dropdown already work without it.

**Design authority.** [`data/maintainer-tool/ui/screens/s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
(the version&verify surface — the "check against a local DLL" control + the verify states).
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §6 US-10 +
the current-version-row-resolution paragraph + §10 D15 (client-side, JS port, no upload,
Python = test-of-record). `requirements.md` R12 (the `.rdata` scan + intern-agreement).

**UX** (`.claude/rules/ux-first-class.md`, from s02):
- **Resolving** — pick a local DLL → the in-page scan runs → "checks out against your DLL:
  `<version>`" marks the matching row.
- **Resolver failure** — *"couldn't resolve a version from that DLL (interns disagree)."*
  (warning); version-stamping carries the override (law 4). System-caused copy naming the
  cause.
- **No DLL / phone** — the version dropdown remains the default; the control is meaningful
  only where the device has the game (D15/D10).
- **Responsive** — the File picker uses the device's native picker; on phone the control is
  present but the dropdown is the common path.
- **Accessibility + consistency:** the control keyboard/touch-operable; the verify state is
  glyph+text (law 7).

**Disassembler-test / author-burden.** This IS the engine carrying the version resolution —
it reads the game version FROM the binary's `.rdata` so the maintainer never hand-types it
(the disassembler-test's spirit; D15). No author hex burden added; it REMOVES one.
