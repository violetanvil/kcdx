# safetyhook MidHook ctx.rip spike — findings

hook-backend-marriage Phase 1 step 2 (the keystone spike). Proved whether
`safetyhook::MidHook`'s `Context64.rip` carries the call-original modes the
production `make_jit_midfunc` implements, on the cap-04 stub, before Phase 3
rewrites the mid-hook path. **Verdict: PASS** — `ctx.rip` carries both run and
skip; the design's §5.1 mechanism stands (resume target corrected to observed).

## The result (live, kcdx-dev_2026-06-08_10-23-05.log)

```
SPIKE-PATCH bytes@+3 = E9 92 FD FE FF 90 90 90 90 90 90 90 90 90
SPIKE-CAPTURE rax=10      (expect 10)   PASS
SPIKE-RUN    fn(10)=110   (expect 110)  PASS  — ctx.rip left alone -> add ran
SPIKE-SKIP   fn(10)=10    (expect 10)   PASS  — ctx.rip = ret -> add skipped
```

Game booted clean afterward (suite 187/218, no crash). The spike ran once at
first-tick, dev-gated.

## What was proven (the design's build-gated unknowns)

- **U1 — ctx.rip carries run + skip.** Confirmed. Leaving `ctx.rip` runs the
  captured instruction (safetyhook's trampoline re-executes it); writing
  `ctx.rip` to a resume VA past the instruction skips it. This is the supported
  mechanism — the maintainer built it in **PR #39** (issue #38, "Is it possible
  to skip the original instruction in midhook?"): *"allows you to change the
  instruction pointer, effectively letting you bypass the trampoline that would
  execute the original instruction."*
- **U3 — resume offset.** Observed: safetyhook patches the captured-instruction
  site with a **5-byte E9 rel32 jmp** (`E9 92 FD FE FF` here), then nops. The
  skip resume target is the address of the genuinely-next original instruction
  past the captured one — NOT a fixed `+5`/`+7` guess. The maintainer's own hint
  (issue #38): skip by resuming at "the next instruction."
- **Register capture (read half of §5.2).** `ctx.rax` read the pre-add seed (10)
  correctly. (Writeback was not exercised by this spike; the run/skip + read are
  the load-bearing proofs.)

## The pass-1 crash — diagnosed, not a mechanism defect

The first pass crashed (ACCESS_VIOLATION at stub+9) because:
1. The resume offset was wrong (`ctx.rip = base+7`, the nop), AND
2. The stub was too small (9 bytes) — a patch at +3 left no clean resume past it.

Both are spike bugs, not safetyhook defects. The FOpen `FAULTED_FIRE` storm in
that log was the crash handler dumping the detour fire-ring (the prior clean run
had 0 such faults), NOT an FOpen regression. Fixed in pass 2 with a roomy 24-byte
stub (mov / add / nop×16 / ret) and `ctx.rip = the ret`.

## Implications for Phase 3 (the make_jit_midfunc retirement)

- The §5.1 three-mode mapping is feasible as designed:
  - **True** (run) = leave `ctx.rip` alone.
  - **False** (skip) = `ctx.rip = resume_addr` where `resume_addr` = the
    captured instruction's VA + its length (the next original instruction).
  - **Auto** = the callback conditionally sets `ctx.rip` from callback state.
- **resume_addr ownership (U3 detail):** safetyhook does NOT hand back the
  resume address; kcdx computes it as `target + instruction_length`. The existing
  hde64 length-decode (or the new Zydis decoder) supplies the length — kcdx keeps
  a small length-decode helper, not the whole `make_jit_midfunc` codegen.
- **Stub/patch caution for the production path:** safetyhook used a 5-byte E9
  here; for a real game target near a far module it may use the 14-byte FF
  absolute (the E9->FF fallback). The resume-offset computation is independent of
  the patch width (it's `captured-instruction VA + its length`), so this does not
  change the mode mapping — but the production mid-hook adapter must compute
  resume from the instruction length, never from the patch width.

## The spike code (removed from source after capture)

The probe was `src/safetyhook_midhook_spike.{h,cpp}` + a one-shot dev-gated call
at the first-tick seam in `src/hooks.cpp` (after `hook_chain::SetLuaState`). It
allocated a roomy cap-04-shaped stub via `trampoline::AllocateBranch`, installed
a `safetyhook::MidHook` at the `add`, and called the stub in run + skip modes.
Reconstruct from this finding if the mechanism ever needs re-observing; the
production adapter (Phase 3) is the durable consumer.
