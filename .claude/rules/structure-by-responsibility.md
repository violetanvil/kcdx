---
paths:
  - "src/**"
  - "lib/**"
  - "crates/**"
  - "packages/**"
  - "app/**"
  - "apps/**"
  - "services/**"
  - "internal/**"
  - "pkg/**"
  - "cmd/**"
  - "Documentation/**"
  - "docs/**"
  - "doc/**"
---

# Structure by responsibility — code, docs, and everything else

A cross-cutting governance law: every unit a repo contains — a directory, a package/crate/module, a file, a documentation tree — is carved by the **single responsibility it owns**, enforced at boundaries (dependency direction, public surface) and signaled by naming. Organization is first-class; a thing's place tells you its job. This rule owns the ARCHITECTURE layer; it cites two siblings rather than restating them.

## Relationship to the sibling rules (no overlap)

This rule owns the units ABOVE the file; file granularity is `.claude/rules/no-monolith.md`; lifecycle-artifact trees are `.claude/rules/doc-organization.md`. Documentation splits three ways, no overlap: **reference docs** (by responsibility-unit — this rule) · **lifecycle docs** (bugs / debt / plans — `doc-organization.md`) · **planning docs** (the plan trees `/plan` writes).

## The law

Carve every unit by ONE responsibility. The unit's NAME declares it; its location places it; its dependencies enforce it. A unit that owns two responsibilities is split; a unit that owns none (a junk-drawer) is dissolved.

## The principles

### 1. One unit = one responsibility (no junk-drawers)

Each package / crate / module / directory answers "what is my ONE job?" Its name states it; its manifest/header documents it. **No catch-all units** — no `utils`, `common`, `helpers`, `misc`, `shared`-grab-bag. A genuinely shared helper that's used twice gets extracted to a NAMED unit by its responsibility (a `clock` module, a `retry` module), never to a `misc` pile. If you can't name a unit's single responsibility in a short phrase, it owns too many.

### 2. Core has no upward dependencies (dependency inversion)

Establish a **core** layer that holds the shared contracts — the interfaces/traits, the shared types, the schema, the wire protocol — and depends on NOTHING else in the app (external libraries only). Every other unit depends on core; core depends on nothing in-app. Platform- or environment-specific code lives in **leaf** units that implement the core's contracts; cross-cutting code depends on the contract, never on a concrete leaf. A new platform/backend is a new leaf, core untouched.

### 3. Coordinator / worker layering (a downward DAG)

Every file is one of two roles:
- **A worker** does exactly one thing, exposes a clean API, and is unaware of its callers. Named by what it does.
- **A coordinator** wires workers and sequences them; it carries no business logic of its own. Named by its role.

Coordinators form a strict **downward DAG**: a coordinator may call a lower coordinator or a worker; a worker NEVER calls upward; no peer-to-peer calls across the same layer; no cycles. Cross-unit composition happens at the top (the orchestrator/binary/entry point), not smuggled sideways between leaves.

### 4. Name signals responsibility

A reader infers a unit's role from its name without opening it. Workers are named by **what they do** (`frame-capture`, `watchdog`, `retry`, `handshake-io`); coordinators by their **role** (`session`, `orchestrator`, `router`, `pipeline`, the entry point). Do NOT organize by TYPE (all-interfaces-here, all-structs-there, all-models-there) — that scatters one responsibility across type-buckets. Organize by **responsibility/feature**, so everything one concern needs sits together.

### 5. Bounded public surface

A unit's boundary is strict; its internals are free. Expose the minimum: the public API a consumer needs, and ONE bounded error/failure type per unit's public surface (not error-type sprawl, not stringly-typed failures at the boundary). The entry/wiring file (the unit's `lib`/`index`/`mod` root) is a coordinator — re-exports + wiring only; if it grows logic, extract that logic to a named worker.

### 6. Documentation mirrors the code's responsibility structure

A repo's **reference documentation** is organized by the SAME responsibility units as its code — a permanent reference library parallel to the source, NOT a flat pile and NOT only timeline/phase docs. Mandatory, scaled to the repo:

```
<docs-root>/
  README.md            index of the reference docs
  architecture/        cross-unit patterns (the dependency hierarchy, the coordinator/worker
                       contract, error propagation, the system-wide design laws)
  subsystems/          ONE doc per responsibility-unit, mirroring the code's units
    <unit-a>.md        what this unit does, its public surface, how to extend it
    <unit-b>.md
  <other by-responsibility groupings as the domain needs>  e.g. protocols/, interfaces/
```

The reference tree is the third docs category from the boundary block above — distinct from the lifecycle and planning trees. A developer asking "how does <unit> work?" reads its subsystem doc cover-to-cover, not a grep of a monolithic spec. When a new responsibility-unit is added to the code, its reference doc is added in the same change. (A tiny single-purpose repo satisfies this with one `README.md`; a multi-subsystem app needs the parallel tree.)

### 7. Everything else, organized the same way

The principle is not code-only. Tests follow the unit they exercise (co-located with the unit or in a tests tree mirroring it, named by scenario — never a junk-drawer test pile). Config, scripts, fixtures, and assets are grouped by the responsibility they serve, not dumped in a flat `scripts/` or `misc/`. (A PRODUCED non-source artifact — a probe, a research dump, a generated fixture — has a lifecycle + provenance beyond placement: `.claude/rules/working-artifacts.md`.) The governance tree itself (`.claude/{rules,skills,hooks}/`) is organized by responsibility. If it's a thing the repo contains, it has a responsibility and a place that declares it.

## What's the repo's, not this rule's (the seam)

This rule is the language-agnostic principle. A repo names its concrete instances in the relevant append / its own layout rule: the exact core unit + its name, the package/crate naming scheme, the platform-gating mechanism, the boundary-error idiom, the docs-root path, and the unit list. The PRINCIPLE (one-responsibility, core-no-upward-deps, coordinator/worker DAG, name-signals-role, bounded surface, docs-mirror-code) is the system floor; the INSTANCES are the repo's.

## What this is NOT

- NOT a mandate to over-split a small repo. Scale to size — a single-purpose library is one well-named unit with one README; the principle is "by responsibility," not "maximize directories."
- NOT a reason to reorganize working code on sight. It governs how new units + docs are CREATED and where things MOVE; it is not a license to churn a settled, well-placed layout. A reorganization of existing structure is a design decision the user owns (`design-authority.md`).
- NOT file-size/split (that's `.claude/rules/no-monolith.md`) and NOT lifecycle-artifact trees (that's `doc-organization.md`). This rule is the unit-and-dependency architecture + the reference-docs mirror.
