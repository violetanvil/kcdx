---
paths:
  - "**/*"
---

# Cornerstones

UX, Capability, Performance are **cornerstones**, not priorities to weigh against each other. When two pull against each other, **UX decides.** None is ever traded away for implementation effort.

1. **UX** — for mod authors AND mod users. **The engine does the heavy lifting; the author declares intent.** Author UX = TOML/API ergonomics, error messages that teach, doc clarity, time-to-first-working-plugin. User UX = install simplicity, no-crash, clear failure modes.
2. **Capability** — how flexible/powerful the engine and plugin surface are. Prefer the general mechanism over the special case.
3. **Performance** — how optimized the implementation is.

**Implementation effort is NEVER a valid reason to weaken a cornerstone.** "Hard to build", "needs more code", "punt to v0.2 because it's complex" do not justify cutting UX, narrowing capability, or skipping perf. Deferring to a later version is legitimate only for genuine scope-creep — never for the correct answer that just costs more effort.

---

## The disassembler test (the UX cornerstone's sharpest edge)

**The author declares WHAT they want; the engine resolves the WHERE and HOW.** A disassembler is an **expert-only escape hatch, never the common path.**

> **The disassembler test — apply to every author-facing design:**
> *Could a competent modder who has never opened a disassembler accomplish this by declaring what they want — an event to react to, a value to change, a named thing to hook?*
>
> If the design makes the author supply an **address, offset, register, instruction length, or hand-written signature/byte-pattern** for a common task, **the engine is shoving its own job onto the author — a UX defect in the ENGINE, not a config field.**

A modder hooking a game function writes a **name** and gets the address **and** the verified ABI for free. The engine already carries `address_id = "IsInCombat" → address`; it must likewise carry the signature, so the name supplies the ABI.

- ❌ `kcdx.hook{ address_id = "IsInCombat", signature = "i32 (i32)" }` — the author hand-writes the ABI. That is disassembler-tier reverse-engineering pushed onto everyone.
- ✅ `kcdx.hook{ target = "IsInCombat" }` — the name resolves to address **and** verified signature; the engine knows both.

**Hard rule — author hex/ABI burden is a defect, not a default (AP12).** Any capability that forces the author to supply an address / offset / register / instruction length / hand-written signature for a common task MUST have a name-based equivalent where the engine carries that detail. The author-supplies-hex path as the *only* path is a violation to fix at the source — not tracked debt, not "expert-only-for-now."

**Disassembler-tier inputs are EXPERT-ONLY and must be LABELED.** Raw byte patterns, mid-function callsite offsets, hand-written register captures, raw RVAs are legitimate for the rare case an expert authors something the engine cannot yet name. The surface must say so plainly ("expert/advanced form; the common path is `target = <name>`").

### The engine cannot pre-name everything — authors name targets themselves, and share them

No shipped table can cover every function and mid-function site ahead of time. The tenet does not promise an omniscient name table; it promises authors are never *stuck* and never do hex work *repeatedly*. Three guarantees for the un-named case:

1. **Author-declared targets are first-class.** No engine name → the author identifies it via the expert hatch (signature/AOB/RVA) **once**, names it, refers to it by that name thereafter. Hex authored once, not per hook.
2. **Author-declared targets are SHAREABLE.** A declared target packages and is consumed *by name* by another author who never touches the hex — one expert unblocks many non-experts (a community-authored Address Library entry). A hard requirement, not a nice-to-have.
3. **Manual and official names COEXIST — no silent clobber.** A later engine-shipped name does not override or break an author's pre-existing declaration. Both resolve; precedence is explicit (the author's own declaration is not silently displaced). Additive, never a regression for whoever named it first.

These three guarantees are IMPLEMENTED by the `<author>.<plugin>.<bare>` model and its self > engine > other precedence — see `naming-namespaces.md` for the binding mechanism (engine-derived author + plugin prefix, dot-as-canonical-separator, warn-once-per-bare-collision, reserved `kcdx.*` root). Guarantee 3 is precedence, not partition: an engine release adopting a name an author already used never displaces the author, because `self` resolves before `engine`.

**Burden-of-proof inversion.** The engine carrying address + ABI + resolution is the DEFAULT. A design requiring author hex-tier input for a common task is the EXCEPTION — surface it ("this asks the author to hand-write a signature — expert-only form, or should the engine carry it?"), don't choose it silently.

---

## How to apply

- **Designing a feature:** pick the option that wins on UX/Capability/Performance, then build it; don't pre-narrow. Run the disassembler test on every author-facing input.
- **Reviewing a proposal:** a cornerstone sacrifice needs a **technical** justification (foot-gun, breaks invariants, blocks a higher cornerstone), never effort. Author hex/ABI burden for a common task is AP12.
- **Trigger — about to write "the author can just provide the address/signature/offset":** STOP. That sentence is the engine failing the disassembler test. Resolve it from a name, or surface why it can't.
