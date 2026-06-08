# Step 9 — backend-layer subsystem/reference doc

**What.** Write the reference / subsystem doc for the detour-backend layer the
prior phases built (design §8: a new responsibility unit gets its reference doc,
`structure-by-responsibility.md` §6). The doc a future maintainer reads to
understand the layer cover-to-cover — not a grep of the source. Covers E19
(`../context.md`).

**Scope (commit-grain).**
- A reference doc at the repo's reference-docs home for engine internals (the
  `docs/` engine-reference area — mirror where existing engine-subsystem docs
  live; if none exists for the hook engine, place it alongside the hook-engine
  reference material). One concern: the detour-backend layer.
- Cover:
  - **The `IDetourBackend` contract** — the four methods, the get_original
    guarantee (a stable callable trampoline-original pointer for the hook's
    lifetime), and that the chain + JIT marshaling sit unchanged above it.
  - **The two backends** — `MinHookBackend` (when + why: loader-lock + bootstrap
    paths) and `SafetyhookBackend` (the bulk: thread-safe install, E9→FF
    far-target reach, the mid-hook `MidHook`).
  - **The routing predicate** — the §4.2 table + the impossible-misroute mechanism
    Step 5 chose; the loader-lock constraint (WHY MinHook is permanent on those
    paths).
  - **The mid-hook path** — `safetyhook::MidHook` + the `Context64`-based
    `MidDispatch` adapter; the three call-original modes via `ctx.rip`.
  - **Foreign-hook detection + chaining** — the prologue classifier + the
    follow-the-jump capture; the v1 scope boundaries (chain-always; foreign
    unhook/install-later out of scope).
  - **How to add a third backend** — implement `IDetourBackend`, add a routing-table
    row; the interface is the future-proofing (design §10).
- Cross-link the settled design (`docs/design/hook-backend-marriage.md`) as the
  authority; the reference doc is the HOW-IT-WORKS, the design is the WHY-IT-IS.
- This is a PUBLIC-tree doc (`docs/` is allowlisted) → it references nothing
  private: no `.claude/` path, no `AP<n>` citation, no governance slash-command
  (`public-private-boundary.md` / AP16). State facts self-contained.

**Test bar.** Docs-only → no code build, no cap-NN row. Acceptance: the doc covers
every bullet above and a maintainer unfamiliar with the layer can (a) explain why
two backends exist, (b) name which path uses which engine and why, and (c)
describe how to add a third backend — from the doc alone. (The `/code-review` /
`step-review` doc-completeness check is the gate; no launch.)

**Dependencies.** Phases 1–4 (the doc describes the built layer — the interface,
both backends, routing, the mid-hook path, foreign-hook chaining all exist). It is
last because it documents what was built; writing it earlier would document an
intended layer, not the as-built one.

**Design authority.** [`hook-backend-marriage.md §8`](../../../design/hook-backend-marriage.md)
+ the whole doc (the reference doc summarizes the built layer the design
specified).

**Disassembler-test / author-burden note.** None — a maintainer-facing reference
doc; no author-facing surface, no game-address resolution.

**Reference.** [`../context.md`](../context.md) E19;
`structure-by-responsibility.md` §6 (a new unit ships its reference doc).
