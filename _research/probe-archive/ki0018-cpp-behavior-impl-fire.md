# KI-0018 probe archive — C++-declared behavior impl-not-fired (a fixture timing bug)

**Verdict:** NOT an engine defect. The C++ behavior interface fires a C-declared
behavior's impl correctly at the apply boundary. The failing cap-102 rows read
the impl-fire flag at `kcdxPlugin_PostGameLoad`, which runs BEFORE
`RunApplyBoundary`. Fix = move the boundary-dependent rows to the `InputLoaded`
handler (post-boundary), the cap-100 / s5 / s6 pattern.

**Root cause:** observation-timing in the test, not the engine. The apply
boundary (`behavior_registry::RunApplyBoundary`, hooks.cpp) fires after
`RunPostGameLoad` and before `kcdxMessage_InputLoaded`; a row that reads a
boundary effect at PostGameLoad reads it one stop too early.

## Reusable probe wiring (reconstruct from here, never from source)

Two read-only probes settled it; both are removed from live source (no residue).

**PROBE B — boundary reaches the behavior, on which VM, with what ref.** In
`behavior_registry.cpp` `RunApplyBoundary`, just before the impl pcall:

```cpp
// after `b->applied = true;`, before `lua_rawgeti(L,..,implementationRef)`:
lua_rawgeti(L, LUA_REGISTRYINDEX, b->implementationRef);
LOG_DEBUG_KV("BEHAVIOR_PROBE", "boundary_invoke",
    ::kcdx::log::KV("behavior", b->fullName),
    ::kcdx::log::KV("declarer", b->DeclarerLabel()),
    ::kcdx::log::KV("boundary_vm", reinterpret_cast<uintptr_t>(L)),
    ::kcdx::log::KV("implRef", b->implementationRef),
    ::kcdx::log::KV("recordedRef", b->recordedRef),
    ::kcdx::log::KV("implRef_type", lua_typename(L, lua_type(L, -1))));
lua_pop(L, 1);
```
Result: cpp_scalar/cpp_crosslang enter the pass on the same `boundary_vm` as all
behaviors; `implRef_type="function"`. No VM-identity mismatch. (The C++ refs are
low ints — 1, 5 — because they were the first refs built on kcdx's freshly-built
VM at the early `kcdxPlugin_Load` stop; that is benign, the same registry.)

**PROBE C — does the trampoline actually fire, with the right fn.** In
`behavior_interface.cpp` `CImplClosure`, first line after the `lua_touserdata`:

```cpp
LOG_DEBUG_KV("BEHAVIOR_PROBE", "cimpl_closure_fired",
    ::kcdx::log::KV("bundle", reinterpret_cast<uintptr_t>(b)),
    ::kcdx::log::KV("fn", b ? reinterpret_cast<uintptr_t>(b->fn) : 0),
    ::kcdx::log::KV("isRevert", b ? (b->isRevert ? 1 : 0) : -1),
    ::kcdx::log::KV("arg1_type", lua_typename(L, lua_type(L, 1))));
```
Result: exactly 2 firings (boolean + number, real fn ptrs) — the trampoline
fires and forwards the correct value. The engine works; the fixture's PostGameLoad
read is premature.

## Read recipe

`grep BEHAVIOR_PROBE kcdx-dev_<ts>.log` — `boundary_invoke` (one per behavior the
boundary walks) + `cimpl_closure_fired` (one per C-impl invocation). The
discriminator was: closure fires with the right fn+value → engine OK → the test
reads the flag too early.
