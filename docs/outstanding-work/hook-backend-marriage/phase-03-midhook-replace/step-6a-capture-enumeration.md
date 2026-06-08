# Step 6a — capture-form enumeration + Context64 mapping (resolves U4)

**What.** Before the mid-hook rewrite (6b) touches the capture machinery, enumerate
EVERY named-capture form the current `make_jit_midfunc` supports AND every form the
mid-hook tests (cap-21 + cap-04 + any other mid plugin) actually exercise, and map
each to its `safetyhook::Context64` equivalent. A form with NO `Context64`
equivalent is SURFACED (U4), not silently dropped — it becomes a design decision
for the user, not a guess the rewrite makes. The spike (Phase 1 step 2) proved the
register-READ half on `ctx.rax`; it never enumerated the stack-expression forms or
exercised writeback. This step closes that gap on paper before 6b builds on it.
Covers U4 (`../context.md`); the durable map de-risks E9 (capture writeback) for 6b.

**Scope (commit-grain).**
- **Enumerate the capture forms `make_jit_midfunc` supports** — read
  `src/rom_borrowed/runtime_func_t.cpp` (`make_jit_midfunc`, from ~:357) + the
  `CaptureHandle` machinery (`PushCaptureValue`/`WriteCaptureValue` + the Lua/C++
  capture-binding surface). The forms to enumerate, each with how the current code
  reads/writes it from the JIT 16-byte slot payload:
  - **Register captures** (`rax`, `rdx`, `rcx`, the GPRs; the XMM registers `xmm0`…).
  - **Stack-expression captures** (`[rbp+0x10]`, `[rsp+N]`, other base+offset forms)
    — enumerate the exact expression grammar the parser accepts.
  - Any other capture kind the code supports (a by-name capture, a typed capture).
- **Map each form to a `Context64` field** (design §5.2): a register capture →
  `ctx.<reg>` (read AND writeback — `Context64` is by-value-with-writeback);
  a stack-expression capture → read/write through `ctx.trampoline_rsp` + the
  computed offset (NOT `ctx.rsp`, which is read-only — design §2 `Context64`).
  Cite the `Context64` field for each, read from
  `vendor/safetyhook/include/safetyhook/context.hpp` THIS step (not recall —
  `dependencies.md`).
- **Surface any form with NO `Context64` equivalent (U4).** A capture form
  `make_jit_midfunc` supports that `Context64` cannot express is a design gap —
  surface it to the user as a decision (drop-with-approval / a workaround /
  reconsider the scope), NEVER silently dropped (`deferral-authority.md` — the
  deferral is the user's call). Most forms map (register + base-offset); a form
  that doesn't is the finding this step exists to catch.
- **Cross-check against the tests that exercise captures** — which forms do cap-21,
  cap-04, and any other mid-hook `test-plugins/` plugin actually USE? A form a test
  exercises MUST map (it's live-tested in 6b); a form supported-but-untested is
  noted (6b preserves it or surfaces it).
- **Capture the map as a durable `_research/` finding** (`working-artifacts.md` —
  durable process-output, reuse-first): a table `capture form | current JIT-slot
  mechanism | Context64 field | tested-by | maps? (Y / N-surface)`. This is the
  authority 6b builds the `Context64` rewire against.

**Test bar.** This step ships NO live code change to the mid path — its deliverable
is the capture map, and its verification is the map's COMPLETENESS: every form
`make_jit_midfunc` supports AND every form a test exercises is in the table, each
mapped to a `Context64` field OR surfaced as a U4 gap. A form omitted from the table
is the defect (the same class as a missing test bar). No build/launch needed (the
map is read-only analysis + a `_research/` doc). Build-green is trivially held (no
source change); the map's correctness is the gate, verified by the 6b rewrite
landing on it without a surprise.

**Dependencies.** Phase 1 step 2 (the spike — the register-read half proven, the
mechanism confirmed). NONE on safetyhook beyond the `context.hpp` header read. Blocks
6b (the rewrite builds the `Context64` capture wire against THIS map).

**Design authority.** [`hook-backend-marriage.md §5.2, §2 (Context64), §9.4 (U4)`](../../../design/hook-backend-marriage.md)
— the capture-writeback model + the `Context64` field semantics + the
surface-don't-drop discipline for an unmapped form are built to the design.

**Disassembler-test / author-burden note.** None — read-only enumeration of an
existing internal surface; no author-facing change, no game-address resolution.

**Reference.** [`../context.md`](../context.md) U4 (stack-expression capture
coverage) + E9 (named captures → Context64 writeback, de-risked here for 6b).
`vendor/safetyhook/include/safetyhook/context.hpp` (the `Context64` field set).
