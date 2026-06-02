## Repo additions — root-cause-verifier

- **Concrete evidence forms (§1 mechanism audit)** — a "direct observation" in kcdx is a structured-log KV value (`kcdx-dev.log`), a heap/crash dump (cdb), a Ghidra disassembly fact, a `data/seeds/` CSV row, or an Address Library record. A code comment, a `docs/known-issues/` trail line, or a matrix Note is a CLAIM, not evidence — verify it against the thing it describes.

- **Domain anti-pattern rows (§5)** — beyond the generic floor: raw-RVA / prologue-shape-ABI / canonical-header-vtable invention (AP1/AP2/AP3), theorizing a checkable unknown / theory-shaped probe (AP10), close-without-mechanism-paragraph (AP17) — the numbered rows in `.claude/rules/anti-patterns.md`. Cite the row.

- **Design-surface threshold (§4 design-fork check)** — mirrors the debug Gate-A threshold: a fix crosses it when it touches a hook surface / ABI signature / vtable slot / Address Library entry / save-cosave field / `[[...]]` schema, adds a `src/`|`include/` file, modifies >1 `src/` file, changes a signature, or touches a `.claude/rules/` file or `docs/design.md`.

- **Sibling-skill names** — defaults stand (`/code-review`, `senior-architect-consult`).
