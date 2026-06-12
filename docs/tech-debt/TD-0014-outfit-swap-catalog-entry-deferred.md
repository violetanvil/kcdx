---
id: TD-0014
opened: 2026-06-12
status: Open
area: behavior catalog (Phase 9.5 P3 — shipped entries)
closure_gate: a future cycle builds the address-backed catalog entry `kcdx.behavior.outfit_swap_in_combat` (a `kcdx.bytes` patch at the verified `outfit_swap_callsite_aob`, DB id 5 — already verified, no new DB row / no AP18)
owner: behavior-catalog lane
commit_at_filing: HEAD (P3 s2 — the shipped console-driven catalog entries)
related:
  - the Phase 9.5 behavior catalog (design §7 — the shipped entries; the canonical case study is `kcdx.behavior.outfit_swap_in_combat`)
---

# TD-0014 — the canonical address-backed catalog case study (`outfit_swap_in_combat`) is deferred out of the v1 roster

## Context

Phase 9.5 P3 s2 shipped the launch behavior-catalog roster. The user decided
(at the P3 s2 step head, after a live CVar-existence probe) that the v1 roster
is **console-driven**: six entries, each toggling a verified game console
variable via `kcdx.console.execute` (`motion_blur` / `depth_of_field` /
`display_info` / `chromatic_aberration` / `show_compass` / `skip_intro_logos`).

The Phase 9.5 design (§7) names the **canonical case study** for an
address-backed catalog behavior as `kcdx.behavior.outfit_swap_in_combat` — a
behavior whose `implementation` installs a `kcdx.bytes` patch at the verified
game-binary site `outfit_swap_callsite_aob` (Address Library **id 5**, already
verified — no new DB row, no `/research-disassembly`, no `AP18`). That entry is
**not in the v1 catalog**.

This is an **approved deferral** (the user's explicit call), recorded here per
the deferral discipline (a deferred item lands a durable artifact). It is NOT an
engine or data defect: the byte-patch fact is verified and the catalog loader +
the behavior model fully support an address-backed entry; the entry simply was
not built this cycle.

## Why deferred

P3 s2's roster was scoped to a **console-driven** set (the user's decision) so
the launch catalog demonstrates the simplest author path (a name → a verified
CVar toggle, the engine doing the work). The address-backed case study is its
own focused build: the entry's `implementation` installs a byte patch rather
than running a console command, exercising the catalog model against the
`kcdx.bytes` surface — a distinct, more involved entry that deserves its own
cycle rather than being bundled into the launch roster.

## Closure blocker

A future cycle builds the address-backed catalog entry
`kcdx.behavior.outfit_swap_in_combat`:

- a new `data/behavior-catalog/outfit_swap_in_combat.lua` whose `implementation`
  installs a `kcdx.bytes` patch at the verified `outfit_swap_callsite_aob`
  (Address Library id 5);
- **no new DB row and no AP18** — id 5 is already verified, so no
  `/research-disassembly` evidence sub-task and no seed addition are needed;
- its public-safe header (the catalog ships on the public allowlist) +
  catalog-README index row + a `cap-NN` regression row exercising it (the same
  same-change test/doc bar every catalog entry carries).

Closes when that entry ships, is exercised by a suite row reporting PASS on a
launch, and is indexed in `data/behavior-catalog/README.md`.

## What this entry does NOT do

- Does not double as a bug report — there is no defect; the fact (id 5) is
  verified and the catalog model supports the entry. The debt is a
  deliberately-deferred capability with a named, already-de-risked build.
- Closure is appended by the skill that lands the entry, which then moves this
  file to `closed/` + reindexes per `.claude/rules/doc-organization.md` — never
  at filing time.
