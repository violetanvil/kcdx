---
paths:
  - "**/*"
---

# UX is a first-class concern — UI work is incomplete without it

A cross-cutting governance law for every skill that designs, plans, builds, or reviews user-facing work (`/design`, `/plan`, `execute`, the review skills). When a change touches UI, the end user's experience is a load-bearing part of the work — not a polish pass deferred to "later." This rule is the single canonical statement; skills cite it, they do not restate it.

## The law

**When UI is involved, UX is mandatory, not optional.** A design or a step that produces, changes, or removes anything a user sees or interacts with is **NOT complete** until it has diligently addressed what the end user will see and experience. "No UX consideration" is never an acceptable answer for UI work — it carries the same completeness standard a test bar does: a UI deliverable without its UX is as incomplete as an implemented item without its test.

"UI involved" means the change touches any user-facing surface: a screen, a view, a component, a control, a flow, a message the user reads, a visual/auditory state, a piece of copy, an interaction, or a layout. A purely internal change (a refactor with no user-visible delta) does not trigger this rule — but if behavior the user perceives changes at all, it does.

## What "addressed UX" requires (the checkable bar)

A UI-touching design section or step doc must speak to each of these that applies — concretely, not "TBD":

1. **What the user sees** — the visible states of the surface: the default/populated state, and every other state it can be in.
2. **The non-happy-path states** — the ones routinely forgotten: **empty** (no data yet), **loading** (in flight), **error** (it failed — what the user sees and how they recover), **disabled / no-permission**, and **edge content** (long text, overflow, zero/one/many).
3. **What the user experiences** — the flow: how the user gets here, what they do, what feedback confirms their action landed (success/failure signaling), and where they go next. No silent success, no dead-end error.
4. **Accessibility + clarity** — keyboard/focus reachability and labels where the platform expects them; copy the user can actually understand; nothing conveyed by color/visual alone.
5. **Consistency** — the surface matches the app's existing patterns (the established components, layout, and interaction conventions) rather than inventing a one-off.

A repo names its concrete UX standards (its design-system / component library, its accessibility target, its copy/voice guide) in the relevant append; this rule is the language-agnostic floor every UI change clears.

## How it applies per stage

- **Design** — when the design covers UI, the UX is developed diligently as part of the design dialogue and captured in the design doc (the visible states, the flows, the non-happy-path experience). A thin-on-UX UI design is an unsettled design — surface the gap as a decision for the user, never paper over it. (Every UX choice is still a design decision the user owns, per [`design-authority.md`](design-authority.md).)
- **Plan** — a step that touches UI carries its UX acceptance in the step doc (what the user sees/experiences for that step), and the phase's verification gate includes the user-facing acceptance, not only build/test green. A UI step doc with no UX section is incomplete — the same class as a missing test bar.
- **Build / Review** — a UI change is not done when it merely compiles and the happy path works; the non-happy-path states and the experience must be present and correct. Build-green is necessary, not sufficient, for UI work.

## What this is NOT

- NOT a mandate to gold-plate. The bar is *diligent consideration of what the user sees and experiences*, scaled to the surface — a one-line status label needs its empty/error states thought through, not a research study.
- NOT a license to decide UX autonomously. UX choices are design decisions the user owns ([`design-authority.md`](design-authority.md)); surface options + a recommendation, never pick silently.
- NOT triggered by non-user-facing work. A pure internal refactor with no perceptible change does not invoke this rule.
