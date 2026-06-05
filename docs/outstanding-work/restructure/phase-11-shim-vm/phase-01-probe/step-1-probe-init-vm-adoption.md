# P1 step 1 — probe Init + lua_newstate; settle intercept / boot-swap / slot

## What

Instrument `CScriptSystem::Init` (`0x1448F38`) and `lua_newstate` (`0x14492A8`) under
a WHGame force-load and observe ground truth for the four §4 questions, each against
its pre-committed, flat, theory-independent outcome→meaning map. The probe is the
keystone: its output is the decided intercept point, the boot-swap-reachability
verdict, and the early-slot shape that Phases 3–6 consume. Agent-written,
agent-built, agent-deployed; the user only launches.

## Scope

- A diagnostic observer (`// === DIAGNOSTIC (PROBE …)`) on `CScriptSystem::Init` +
  `lua_newstate` that logs: (a) whether Init reads any virgin-state field
  (`storedebug==1` @ g+0x22, gc fields) before overwriting; (b) the single-state
  invariant (`[L->l_G+0xB0]==L`, no second allocation) when kcdx feeds its state;
  (c) the early-slot register-vs-boot-open ordering + a `kcdx.assets.replace`
  `rt=HIT` for a boot asset (reuse `_research/probe-archive/ki0005-resolver-dds-observer.md`);
  (d) which early-Lua-body shape (a `before_game`-zoned `plugin.lua` vs a minimal
  dedicated body) runs cleanly that early.
- Each observation's outcome map is committed UP FRONT per design §4.1–§4.4.
- Capture the finding + the reusable wiring to `_research/probe-archive/`; remove the
  probe from source (no residue — `.claude/rules/working-artifacts.md`).
- Record the settled answers back into `lua-vm-design.md` §4/§5 + its changelog.

## Test bar

This step's deliverable IS a probe; its "test" is the live observation under the
user's launch, read by the agent from `kcdx-dev.log` against the pre-committed
outcome maps (`.claude/rules/results-driven.md`, `.claude/rules/agent-builds-and-deploys.md`).
No `test-plugins/` regression row (the probe is throwaway; its finding is the
durable artifact). The four outcome maps in design §4 are the falsifiable bar — each
question resolves to exactly one mapped outcome.

## Dependencies

None (step 1). The force-load harness it needs is the minimal `LoadLibraryW` the
probe sets up itself for observation; the production force-load is P3 step 2 (this
probe does not ship it).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §4 (the four probe questions + outcome
maps) + §4.1 (the intercept-point lean). The probe builds to those outcome maps; a
new observed shape that no map covers is re-observed, never theorized into a fix
(§4 "re-observe ground truth").

## RE / author-burden note

The probe resolves the game-function targets (`CScriptSystem::Init`, `lua_newstate`)
by NAME through the Address Library (seed ids 121, 114), never a hardcoded RVA in
source (`.claude/rules/no-hardcoded-addresses.md`, AP1). The facts are already
harvested ([`../../fix-a-drop-static-lua.md`](../../../fix-a-drop-static-lua.md)) — no
new disassembly pass owed.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E1–E4; design §4.
