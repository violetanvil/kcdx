## Repo additions — design

- **Design anchor = the cornerstones** — `.claude/rules/cornerstones.md` (UX > Capability >
  Performance, never traded for implementation effort; the disassembler test) IS this repo's design
  anchor. Every design the dialogue settles is checked against it, on two surfaces:
  - **Dialogue (§A)** — surface a **cornerstone tension** as a fork the user decides: when two
    cornerstones pull against each other, UX decides, but the trade is the user's call, not a silent
    pick (`.claude/rules/design-authority.md`). And run the **disassembler test on every
    author-facing input** the design introduces (a TOML key, a `kcdx.*` verb, a C++ interface, an
    error surface): could a competent modder who has never opened a disassembler accomplish this by
    declaring intent? A design making the author supply an address / offset / register / instruction
    length / hand-written signature for a common task is an AP12 UX defect — surface it ("this asks
    the author to hand-write a signature — expert-only form, or does the engine carry it?"), never
    settle it silently. **Every fork's options are run past the cornerstones BEFORE presenting:**
    each option's Pros/Cons states its cornerstone standing (+ the disassembler-test verdict where
    author-facing), and the Recommendation names the cornerstone it wins on (a fork no cornerstone
    bears on states so — never a fabricated standing).
  - **Soundness gate (§C.4)** — the gate's brief already carries "the repo's design anchor"; that
    anchor is the cornerstones. The §C.4 `architect-review` verifies the drafted design does not
    trade a cornerstone for implementation effort and does not put author hex/ABI burden on a common
    path (AP12) — a cornerstone sacrifice needs a **technical** justification (foot-gun, breaks an
    invariant, blocks a higher cornerstone), never effort or "punt because it's complex". A
    cornerstone-violating design is a §C.4 finding → HALT to the user.

- **Design-doc path + label** — `docs/design.md`, labelled "the authoritative design" (CLAUDE.md
  calls it the "Authoritative design"). §C's author/revise step writes here; the dialogue and the
  doc use this path + label.

- **Design-changelog** — no separate changelog file. Revisions edit the `docs/design.md` §body
  directly; the repo does not maintain a newest-first changelog doc for it.

- **Doc-structure additions** — kcdx is a reverse-engineering repo, so a design decision that rests
  on a game-binary fact (an address/RVA, ABI signature, vtable slot, struct offset) carries its
  **RE primary-sources / evidence** provenance: the verified fact + where it came from on the
  reuse-first ladder (existing `data/db-export/` rows → prior `_research/` dumps → predecessor sigs →
  the analyzed Ghidra project → fresh disassembly last), per `.claude/rules/reverse-engineering.md`
  and `/research-disassembly`. A design §section that turns on such a fact states the verified value
  AND its evidence tier, never a bare RVA.
