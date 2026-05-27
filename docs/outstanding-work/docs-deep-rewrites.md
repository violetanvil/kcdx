# Outstanding — documentation deep rewrites (tracked, not buried)

Created 2026-05-22 by the docs-staleness audit (restructure docs reconciliation, step 3).

The audit's policy (locked): fix cheap/misleading staleness inline; for
anything needing a feature-sized rewrite, add a dated "as-built note:
superseded by X" pointer at the stale section head AND file a tracked
follow-up here. No silent stale doc, no buried someday-maybe — every gap
is either fixed or tracked with a real next step (no-deferred-correctness,
AP13).

Items fixed inline in the audit are NOT listed here (restructure-plan.md
line-40 summary, test-plugins/README.md roll-up header, design.md banner,
design-gaps.md gap statuses #1/#2/#12/#14, the VERIFY_PHASE2/3/4 archived
banners). What remains below is the deferred deep rewrites + cross-doc
follow-ups, each with its real next step.

---

## 1. `docs/design.md` body — full rewrite/retirement (feature-sized)

**What's stale:** the 58 KB v0.1 body still presents the seven TOML
behavior entry types (`[[patch]]`/`[[hook]]`/`[[mid_hook]]`/`[[trampoline]]`/
`[[scan]]`/`[[command]]`/`[[event]]`), the ASI-loader install model, and
the immediate-apply hook model as the design. The restructure replaced
the schema (manifest-only TOML), the install model (owned `kcdx.exe`
launcher), and the author surface (`kcdx.*` Lua/C++ API).

**Mitigated inline:** a dated SUPERSEDED banner at the top points readers
at `restructure-plan.md` (authoritative design) + `docs/lua/index.md` (current
author API). The banner already enumerates which engine-internals sections
remain accurate (patch_engine, conflict_engine, ldr_notify, trampoline
pools, messaging, serialization, address library, console).

**Why deferred:** a body rewrite is feature-sized (58 KB) and the
restructure plan defines its own end state: per `restructure-plan.md`'s
own banner, at restructure completion this doc is EITHER fully replaced by
the restructure plan as the live spec OR retained as the engine-internals
reference with the schema/author-surface sections deleted. That decision
is part of the restructure's late phases, not this docs pass.

**Real next step:** at the restructure's documentation-consolidation phase
(end of the phase sequence in `restructure-plan.md`), execute the
banner's stated fork — retire design.md to an engine-internals-only
reference (strip the `[[...]]` schema + install/lifecycle sections) or
fold it into the restructure plan. Until then the banner prevents a reader
from mistaking the v0.1 schema for current.

---

## 2. `docs/migration.md` — Phase 5 section is a live placeholder

**What's stale:** the "Phase 5 — manifest-only TOML (lands later)" section
is an explicit `[This section will be filled in when Phase 5 ships.
Currently placeholder.]`. The Phase 1 install-layout-flip section IS
accurate against the as-built loader layout (`kcdx.exe` + `kcdx-engine/kcdx.dll`
+ `kcdx-plugins/`, matches `loader-architecture.md` and CLAUDE.md), so no
inline fix was needed there.

**Why deferred:** the placeholder is honest (it is labeled a placeholder
and dated by phase), and the content it will hold is the Phase 5 schema-
migration steps — which do not exist until Phase 5 ships. Writing them now
would be inventing an unbuilt migration.

**Real next step:** when restructure Phase 5 (manifest-only TOML — removes
the seven behavior table-arrays) lands, fill in the Phase 5 section with
the concrete per-plugin migration steps in the SAME unit of work that lands
Phase 5 (per `restructure-plan.md` Phase 5: "Phase 5's deletion of the old
TOML behavior parsers updates the same doc with the schema-level migration
steps"). No action needed before then.

---

## 3. `.claude/rules/lua-api-surface.md` — stale claims (OUT OF SCOPE for this step; tracked here)

**What's stale (reported, not verified by this pass):** the authoring-
surface rule may carry claims that no longer match the as-built `kcdx.*`
surface (e.g. examples or capability statements that predate the sub-by-sub
build-out of `kcdx.hook` modes, `kcdx.command`, the per-entry-zone model).

**Why deferred:** `.claude/**` (the rule files) is explicitly out of scope
for the docs-reconciliation step that created this file — rule edits are a
separate concern with their own consent/governance path
(`/governance-architect`). Editing it here would cross the authorized
scope boundary.

**Real next step:** a separate `/governance-architect` (or `/execute`)
pass scoped to `.claude/rules/lua-api-surface.md` — audit its claims/
examples against the as-built Lua + C++ surface (`docs/lua/index.md` + the
`src/lua_bind_*.cpp` bindings + the CAP-20…29 / COMP-09/10/11 matrix rows)
and reconcile. Track separately from this docs pass; do not fold into it.

---

## 4. `examples/` — refreshed author starting points (a curated correct set)

**What's stale:** all 11 of the old `examples/` taught the v0.1 TOML-behavior
schema (`[[patch]]`/`[[hook]]`/etc.) — wrong post-restructure. They were
**archived 2026-05-22** to `examples/archive/v0.1-schema/` (history preserved
via `git mv`) with a README marking them do-not-copy and pointing at
`docs/lua/index.md` + `test-plugins/`. So nothing live teaches the dead schema
anymore — but there is currently **no** correct `examples/` starting set.

**Why deferred:** examples are the second-class teaching set (the user's call —
the reference docs were the priority, now done). The `test-plugins/` already
exercise every shipped capability with real working plugins, so they serve as
the de-facto correct reference in the interim; a curated `examples/` set is a
polish improvement, not a correctness gap.

**Real next step:** a focused cycle that adds a curated set of correct
`plugin.lua` + `kcdx.*` examples — a hello-plugin (`kcdx.log` + `kcdx.command`),
an outfit-swap hook (`kcdx.hook{ target=, replace= }`), a both-phase plugin
(`lua` + `lua_after`), a cross-plugin pub/sub pair (`kcdx.publish` + `kcdx.on`),
and a C++ DLL (`kcdxPlugin_Load` + `kcdxPlugin_PostGameLoad`) — each a correct
copy-paste starting point, verified against `docs/lua/index.md`. Do when examples
are wanted; the archive + the test-plugins cover the interim.
