# P2 step 1 — `kcdxBehaviorInterface` + the value-handle model

**What.** The C++ mirror of the four verbs over the engine-owned value-handle
model — values never marshalled out of the one VM.

**Scope.** `src/behavior_interface.cpp` + the `include/kcdx/Interfaces.h`
append (append-only ABI per `.claude/rules/skse-parity.md` / AP11):
`Declare(name, desc, default, impl_fn, revert_fn /*nullable*/, user_ctx)` ·
`Set(name, value)` · `Get(name, out_handle)` · `List(prefix, callback)`. The
handle model: engine-pinned opaque handles, generation-checked staleness (a
stale accessor errors, never dangles), coercion accessors
(`AsBool/AsInt64/AsDouble/AsString`), table traversal, typed builders +
C-function-pointer callable registration. The QUERY thread wall (load-wave +
main-thread-only post-load; off-thread query → teaching error with the two
pattern pointers). `Invoke` and the queued Set are step 2. Doc increment: the
`docs/cpp/` interface doc.

**Test bar.** C++ leg in the cap plugin: all four verbs at main-stop surfaces;
C++-declared behavior set from Lua and Lua-declared set from C++ (main stops);
coercion-mismatch fixture; stale-handle fixture; off-thread post-load query →
teaching error, no crash. Replaces step 4's thin C harness for the early-stop
out-of-window fixture... (that swap completes in step 2 with the queue; this
step carries the interface-based re-issue of the early-stop SET fixture).

**Dependencies.** P1 complete (the surface being mirrored); P1 s1 (VM-access
window + C++ stop observations).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §8
(minus Invoke/queue), §10 (query-wall, coercion, stale-handle rows).

**Disassembler-test / author-burden.** The C++ author writes names + typed
values; the handle accessors carry all VM mechanics; zero hex.
