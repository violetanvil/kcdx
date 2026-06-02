## Repo additions — design

- **Design-doc path + label** — `docs/design.md`, labelled "the authoritative design" (CLAUDE.md
  calls it the "Authoritative design"). §C's author/revise step writes here; the dialogue and the
  doc use this path + label.

- **Design-changelog** — no separate changelog file. Revisions edit the `docs/design.md` §body
  directly; the repo does not maintain a newest-first changelog doc for it.

- **Doc-structure additions** — kcdx is a reverse-engineering repo, so a design decision that rests
  on a game-binary fact (an address/RVA, ABI signature, vtable slot, struct offset) carries its
  **RE primary-sources / evidence** provenance: the verified fact + where it came from on the
  reuse-first ladder (existing `data/seeds/` rows → prior `_research/` dumps → predecessor sigs →
  the analyzed Ghidra project → fresh disassembly last), per `.claude/rules/reverse-engineering.md`
  and `/research-disassembly`. A design §section that turns on such a fact states the verified value
  AND its evidence tier, never a bare RVA.
