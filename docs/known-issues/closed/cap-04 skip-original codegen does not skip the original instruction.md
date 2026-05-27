# cap-04 skip-original codegen does not skip the original instruction

**Status: FIXED 2026-05-20.** Full implementation per the original
`docs/outstanding-work/midhook-skip-original.md` plan. All four
CAP-04 sub-tests now return the expected values. See **Resolution**
section below for the implementation summary.

This bug is INDEPENDENT of the dummynode heap-corruption issue.
The corruption was masking it; once FIX C let save-load and cap-04
actually run to completion, the test-suite caught two pre-existing
mid-hook codegen failures.

## Symptom

cap-04's mid-hook test plugin has four sub-tests:

| Sub-test | Mode                              | Expected | Observed |
|----------|-----------------------------------|----------|----------|
| CAP-04a  | call_original = true              | 110      | 110 ✓    |
| CAP-04b  | call_original = false             | 10       | 110 ✗    |
| CAP-04c  | call_original = "auto" + `_skip=true` | 10   | 110 ✗    |
| CAP-04d  | call_original = "auto" (no `_skip`) | 110    | 110 ✓    |

The pattern: whichever sub-tests should END WITH the original
instruction not executing return 110 (with the `add rax, 0x64`
having executed). The "should call original" cases work.

Each test target is a 9-byte stub:

```
+0:  48 89 C8        mov rax, rcx          ; rax = seed (rcx=10)
+3:  48 83 C0 64     add rax, 0x64         ; HOOK HERE — adds 100
+7:  90              nop                   ; consumed by MinHook's 5-byte rel32 patch
+8:  C3              ret
```

The mid-hook lands at offset +3. The expected result for a
"skip original" run is `rax = 10` (the seed) because the
`add rax, 0x64` at +3 must NOT execute. Observed: rax = 110,
i.e. the add is still happening despite the skip-original
contract.

## Facts

- Specific to the **mid-hook** codegen path (`make_jit_midfunc`
  in `src/rom_borrowed/runtime_func_t.cpp`). Pre/post hooks have
  their own codegen and aren't exercised by cap-04.
- CAP-04b's `call_original=false` is supposed to be honored at
  **codegen time** — the JIT should emit no call to the
  original instruction at all. The observed behavior (original
  still runs) suggests the codegen branch for
  `call_original=false` is either not being taken or its
  emitted code path still falls through into the original.
- CAP-04c's `call_original="auto"` is supposed to be honored at
  **dispatch time** — the Lua callback can set `args._skip=true`
  on the args table and the dispatcher should observe it post-
  callback and skip the original. Observed behavior identical
  to CAP-04b (also returning 110) implies the dispatch path's
  `_skip` check is broken OR the codegen for `auto` mode falls
  through to the original regardless.
- These tests would also have failed in earlier runs of cap-04
  if save-load wasn't crashing first. The 15:36 PROBE O dev log
  showed the same FAIL lines:
  `FAIL CAP-04b: ... returned 110 (expected 10)`
  `FAIL CAP-04c: ... returned 110 (expected 10)`
  — so this is a long-latent bug that the heap-corruption crash
  masked from notice.
- The mid-hook *dispatch* flow itself works correctly: the
  Lua callback fires, the args table is populated, the C++
  `_skip` check executes (or should). PROBE O + PROBE P
  confirmed Table allocation + dispatch are sound after FIX C.

## Trail

| Date       | Action | Result |
|------------|--------|--------|
| 2026-05-20 | First clean cap-04 run after FIX C verified the heap-corruption fix | CAP-04b + CAP-04c fail (return 110); CAP-04a + CAP-04d pass (return 110 as expected). The two failing sub-tests are exactly the ones whose specification is "skip original." |
| 2026-05-20 | Land the full skip-original implementation per the `midhook-skip-original.md` plan: schema (`call_original = true \| false \| "auto"` in TOML), `MidHookEntry::CallOriginal` enum, hde64 auto-decode of `stack_restore_offset`, `kcdx::scripting::g_mid_skip_original` atomic flag, `dynamic_hook_mid` reads `args._skip` post-pcall, `make_jit_midfunc` three-mode codegen (push trampoline_ptr for True / push resume_addr for False / push trampoline_ptr + post-callback flag check for Auto), deleted the broken `sub rsp, K` block + retired the undocumented rax-return-as-resume-addr block. Build, deploy, run. First run: CAP-04b crashed with ACCESS_VIOLATION at `0x...99` — diagnosed: `stack_restore_offset=4` (hde64 length of `add rax,0x64`) but MinHook patches 5 bytes minimum on x64, so resume_addr landed mid-rel32-displacement. Fix: auto-decode now accumulates instruction lengths until ≥ 5 bytes (the MinHook patch size). | 1st failed run (4 → 5 byte fix needed): CAP-04a PASS, CAP-04b/c/d crashed Lua. 2nd run after stack_restore_offset bump to 5: CAP-04a PASS, CAP-04b PASS, CAP-04c **FAIL** (110 instead of 10), CAP-04d PASS. Diagnosed: dispatcher's pcall stack layout was wrong — `[func, args, args_dup]` put `args` (a Table) at pcall's `top-nargs` position instead of `func`, so all callbacks threw "attempt to call a table". Fix: `lua_insert(L,-2)` to move args BELOW func, then `lua_pushvalue(L,-2)` to push args as the arg, ending up with `[args, func, args]`. 3rd run: **ALL FOUR sub-tests PASS** — CAP-04a=110, CAP-04b=10, CAP-04c=10, CAP-04d=110. PROBE Q canary still logs zero kcdx-image-pointer frees (FIX C not regressed). Save-load completes cleanly. |

## Resolution

The skip-original codegen + auto-mode runtime decision works
end-to-end. Files changed:

- `src/hook_engine.h` — added `CallOriginalMode` enum (`True` / `False` / `Auto`) + `MidHookEntry::callOriginal` field.
- `src/config.cpp::ParseOneMidHook` — accepts `call_original = true | false | "auto"` from TOML.
- `src/hook_engine.cpp::ApplyOneMidHook` — auto-decodes `stack_restore_offset` via hde64 (accumulates instruction lengths until ≥ 5 bytes, the MinHook patch size); computes `resume_addr = targetAddr + stack_restore_offset`; passes mode + skip_flag_addr + resume_addr to `make_jit_midfunc`.
- `src/rom_borrowed/runtime_func_t.h/.cpp::make_jit_midfunc` — three-mode codegen:
  - True: push `*m_detour->original_` (MinHook trampoline) as ret target — original runs.
  - False: push `resume_addr` (immediate constant) as ret target — original skipped at codegen time.
  - Auto: push trampoline by default; after callback returns, read the skip-flag byte from `skip_flag_addr` and conditionally overwrite the slot with `resume_addr` if set.
  - Deleted the broken `if (stack_restore_offset != 0) cc.sub(rsp, K)` block (was moving rsp DOWN, making `ret` pop from wrong location).
  - Removed the undocumented Lua-return-as-resume-addr block; mid dispatcher always returns 0 now.
- `src/scripting.h/.cpp` — declared + defined `std::atomic<uint8_t> g_mid_skip_original` and `get_mid_skip_flag_address()` accessor.
- `src/scripting.cpp::dynamic_hook_mid` — cleared skip flag at dispatch entry; rebuilt pcall stack layout (`[args, func, args_dup]` so pcall sees func at `top-1` and args at top, leaving the first `args` ref on the stack post-pcall); reads `args._skip` post-pcall and sets the flag if true; removed the old "first non-zero numeric return wins" code; cleaned up the PROBE O + PROBE P diagnostic snapshots (they served their purpose during the heap-corruption investigation).
- `CMakeLists.txt` — added `vendor/minhook/src` to kcdx's include directories so `#include "hde/hde64.h"` resolves.

Two off-by-one issues caught during in-game iteration (recorded in the Trail above):
1. **5-byte MinHook patch minimum.** First implementation used hde64's instruction length directly (4 bytes for `add rax, 0x64`), but MinHook patches at least 5 bytes for the rel32 jmp it installs. Resume addr at target+4 landed mid-jmp-displacement → ACCESS_VIOLATION on jump-back. Fix: accumulate instruction lengths until ≥ 5.
2. **pcall stack layout.** First implementation pushed `[func, args, args_dup]` thinking pcall consumes the top — but pcall expects function at `top - nargs`, which puts `args` (a Table) at the function position. All callbacks threw "attempt to call a table". Fix: re-order to `[args, func, args_dup]` via `lua_insert(L, -2); lua_pushvalue(L, -2)`.

Both bugs were caught by CAP-04 sub-tests, which is why the
midhook-skip-original outstanding-work plan emphasized
"test-driven only — codegen has asm subtleties easy to get
wrong by speculation."

## Closed questions

- ~~For CAP-04b (`call_original=false`): is the codegen branch being taken?~~ Resolved: the codegen branch DID need adding (it didn't exist pre-fix). See `make_jit_midfunc` mode==1 path that pushes `resume_addr` as the immediate constant directly.
- ~~For CAP-04c (`auto`): how does the dispatcher read `_skip`?~~ Resolved: dispatcher reads `args._skip` post-pcall via `lua_getfield(L, -1, "_skip")` and sets `g_mid_skip_original` if true. JIT's mode==2 path checks the flag and conditionally overwrites the stack-top slot with `resume_addr`.
- ~~After FIX A lands, re-verify these tests.~~ FIX A is deferred to a future session (see [`docs/outstanding-work/fix-a-drop-static-lua.md`](../outstanding-work/fix-a-drop-static-lua.md)). When it lands, cap-04 stays in the test suite and re-verifies against the new Lua-routing path automatically.

## Why this is here and not in `design-gaps.md`

This is a regression of an existing capability (mid-hook
skip-original), not a deferred design decision. The capability
is documented in design.md and was claimed working at one point.
Fixing it is a blocker for advertising `[[mid_hook]]
call_original` semantics to plugin authors.

## Related

- `docs/known-issues/kcdx lua_newtable corrupts the process heap.md`
  — FIX C unblocked the test path that surfaced this bug.
- `kcdx/test-plugins/cap-04-midhook/kcdx.toml` — the test
  plugin's spec.
- `kcdx/test-plugins/cap-04-midhook/cap-04.cpp` — the C++
  callback logic.
- `kcdx/src/rom_borrowed/runtime_func_t.cpp::make_jit_midfunc`
  — the JIT codegen for mid-hooks.
- `kcdx/src/scripting.cpp::dynamic_hook_mid` — the runtime
  dispatcher (reads `_skip` post-callback).
