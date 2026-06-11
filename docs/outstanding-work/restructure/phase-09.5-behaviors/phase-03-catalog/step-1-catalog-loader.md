# P3 step 1 — the catalog loader path

**What.** The engine loads `data/behavior-catalog/` as a builtin behavior pack:
one `.lua` file per behavior, declares stamped under the reserved
`kcdx.behavior.*` root, ahead of every user plugin.

**Scope.** The catalog-aware loader path (an engine-stamped registration — a
manifest-fronted pack is structurally blocked by author validation, design §7):
pack discovery, per-file execution authored exactly as a plugin would write a
declare, the reserved-root stamping, the pin-ahead ordering (P1 s1's observed
path), the malformed-file builtin boot error, the repo→install deploy mapping
(zip + live-deploy per `.claude/rules/loader-architecture.md`), and the catalog
dir's index README (design §11's catalog-dir row — one line per entry). ONE
proving entry ships with the loader; its selection is surfaced for per-entry user
sign-off at this step's execution on BOTH branches (RE-backed → via step 2's
sign-off flow; a benign engine-value entry → a direct per-entry ask) — the
recorded selection deferral reserves every shipped entry to the user, this one
included.
Replaces P1 s4's stub engine-declared fixture name with the real catalog row.
Doc increment: the catalog/promotion section of `docs/lua/behavior.md`
(promotion = move the file).

**Test bar.** Cap fixtures: the pack loads ahead of user plugins (ordering
asserted); the proving entry stamps `kcdx.behavior.<bare>` + is settable from an
early stop AND the main stop; malformed-file fixture → builtin boot error;
`list("kcdx.")` filter returns the catalog tier.

**Dependencies.** P1 complete (registry/boundary/window law); P1 s1 (pin-ahead
path observed). P2 not required (catalog is Lua-side) — ordered after P2 only
because parity-complete machinery is the stabler base; the step is verifiable
independently of P2.

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §7
(minus the entry roster), §10 (malformed-file row), §11 (catalog dir + index
README).

**Disassembler-test / author-burden.** A catalog author writes a plain declare
file — the same zero-hex surface as any declarer; promotion is a file move.
