# P2 step 2 — `Invoke` + the off-thread command queue

**What.** The last two §8 pieces: callable values invocable from C++, and the
command half of the thread contract — off-thread `Set` queues, never errors on
thread.

**Scope.** `Invoke(handle, args…)` riding the dispatch path P1 s1's read
verified (honors the thread guard — `.claude/rules/lua-callback-threading.md`,
no new bypass). The queued Set: off-thread post-load `Set` records + queues
(FIFO arrival order, each set its own toggle, no coalescing); payload staging
for off-thread construction (scalars/strings/fn-pointers direct; table payloads
as engine-side descriptions materialized at execution); execution at the next
apply point on the main thread; per-disposition attribution (misuse → setter;
declarer-code raise → declarer; all async); `get()` flips at execution. If P1
s1's read found the pump UNBOUNDED, the overflow disposition was escalated to
the user there — this step builds the decided shape. Doc increment: the thread
contract section in `docs/cpp/` + `docs/lua/behavior.md`.

**Test bar.** Cap fixtures: Invoke on a callable value (both a Lua-declared fn
and a C++-registered fn-pointer); off-thread set → queues, executes on main,
`get` flips after; queued misuse failure → attributed to the setter; queued
declarer-raise → attributed to the declarer; off-thread table-payload set →
implementation receives the table; the C++ early-stop window-law fixture in its
final interface-based form.

**Dependencies.** P2 s1 (the interface + handles + builders); P1 s1 (pump
boundedness + invoke-path reads); P1 s5 (the toggle contract the queue executes).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §5.4
(command semantics, queue contract, attribution), §8 (Invoke, staging), §10
(queued-path rows).

**Disassembler-test / author-burden.** Set-from-anywhere is the engine carrying
the scheduling burden (the design's own cornerstone argument); no new author
input shapes.
