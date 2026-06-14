# Step 2.1 — DEFLATE inflater dependency

**What.** Add a standard DEFLATE inflater so kcdx can read DEFLATE-compressed pak
entries itself (design §6). The design left the specific library an open pick
(§10): zlib (zlib license) and miniz (MIT, single-file) are the candidates. **This
step surfaces the zlib-vs-miniz pick to the user** for decision AT build time,
license-checked against the repo's distribution model and recorded per
`.claude/rules/dependencies.md` — the pick is the user's call
(`.claude/rules/design-authority.md`), not the plan's. First check whether kcdx
already vendors an inflater under `vendor/` before adding one.

**Scope.** Check `vendor/` for an existing inflater; if absent, surface the
zlib-vs-miniz pick (Pros/Cons + recommendation, cleared against the cornerstones —
neither library bears on a cornerstone; the decision is license + footprint),
get the user's pick, vendor it, license-check it (registry AND source), record the
license-manifest row in the same commit, pin the version. One commit.

**Test bar.** A unit test that inflates a known DEFLATE blob to its known
plaintext via the chosen library (proves the dependency links + works). Build
green linking it (`pwsh ./build.ps1`). The license-manifest row exists in the same
change (`.claude/rules/dependencies.md`).

**Dependencies.** None structurally; ordered before 2.2/2.3 (the pak reader builds
on the inflater) per `.claude/rules/incremental-delivery.md`.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §6 (kcdx's own reader),
§10 (the dependency is the user's pick); `.claude/rules/dependencies.md`.

**Disassembler-test / author-burden.** N/A — no author-facing surface (an internal
library dependency).
