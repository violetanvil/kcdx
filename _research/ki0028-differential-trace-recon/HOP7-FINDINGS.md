# KI-0028 differential trace — HOP 7: the render-item ENQUEUE side

**Date:** 2026-07-03 · **Method:** static Ghidra (dispatcher-caller decomp + module append-scan).
**Trust:** primary (body reads).

## What HOP 6 handed us

Swap-ON the pass-A dispatcher `FUN_180779534` runs every frame (6722×, pass object non-null)
but its render-item vector `[obj+0x298(begin) .. +0x2a0(end)]` (obj = `[dispatcher.param_1 +
0x378]`; the vector lives on `passA_ctx = obj+0x70`) is EMPTY on every entry. The submit
machinery is intact; **nothing appends items to the pass swap-ON.**

## Static so far

- **`FUN_1804e8d88`** (the dispatcher's only caller, 70 bytes) is a thin wrapper: it calls the
  dispatcher then a cleanup (`FUN_1804e8dd0` / `FUN_180397308`). It does NOT fill the list —
  the pass object was populated earlier in the frame. Its caller: `FUN_18251bb1c`.
- **`FUN_180501694`** (called on passA_ctx right after pass A) is a resource-transition op
  (walks `+0xd8` handles, `FUN_18042c4b0` barrier calls), NOT the list clear. The dispatcher
  clears the vector inline (`*(obj+0x310) = *(obj+0x308)`).
- **append-scan** (`_hop7b_append_scan.txt`) — module-wide (0x4e0000..0x900000) hunt for the
  push-back idiom (writes `+0x2a0` end-ptr near `+0x298` reads) to name the append leaf
  mechanically, no caller-chain guessing (AP19). [result pending]

## The REFRAME worth surfacing (not yet decided)

The compiled render objects EXIST on both arms (CCRO compile pass fired swap-ON, Step 1 /
Z8 geo_buf=262). The gap is between "compiled objects exist" and "items enqueued into the
pass." That enqueue is normally driven by **scene traversal / visibility culling** — it walks
the visible render nodes and appends their compiled objects as pass items. An empty item list
every frame, with the machinery intact, is the signature of **the scene having no visible
render nodes** — which points UPSTREAM of the renderer entirely (world/level population, the
view/camera/visibility set, or the scene-graph the FS-takeover swap perturbs), not a render
bug. The divergence may be jumping subsystems here: from "render submission" to "is there a
populated, visible scene at all swap-ON."

## RESULT — the driver is a render COMMAND-STREAM INTERPRETER; pass A is opcode 4

`FUN_18251bb1c(param_1, param_2)` (0x18251bb1c, 1676 bytes, caller `FUN_18252a228`) is a
**typed-opcode command-buffer interpreter**. It walks a byte stream:
- cursor `[param_2+0x8]`, length `[param_2+0x10]`, buffer base `[param_2+0x18]`;
- reads a 4-byte opcode `iVar4` each step and `switch`es on it (cases 1..0x1d).

**Opcode 4 → `FUN_1804e8d88` → the pass-A dispatcher → pass A submit.** So a render frame is
a sequence of these opcodes; the pass-A item list is filled by OTHER opcodes earlier in the
SAME stream (e.g. 0x10→`FUN_1807798e8`, 2→`FUN_182517d4c`, and the set-state/bind ops), then
opcode 4 submits what was built. The whole stream is produced upstream and is exactly what the
FS-takeover swap perturbs.

Method note (process defect, corrected): the 7b module-wide append-scan
(`getFunctions` + decompiler over 0x4e0000..0x900000) HUNG twice — never wrote a byte, two
java procs stuck for hours; killed. A whole-module iterate-with-decompiler is the wrong tool
headless. The targeted single-function decomp (7c) ran clean in seconds. Also: the bash tool
mangles the spaced Windows analyzeHeadless path (`'C:\Users\Michael\Documents\KCD2' is not
recognized`) — run Ghidra via PowerShell `Start-Process ... -PassThru` + `WaitForExit`,
foreground, so a hang is caught by the timeout instead of stalling on a notification that
never comes.

## Next probe (HOP 8) — is the command STREAM empty swap-ON, or the same stream w/o items?

One site, both arms: hook the interpreter `FUN_18251bb1c` at entry; log the stream length
`[param_2+0x10]` and tally the opcode histogram (esp. count of opcode-4 = pass submits, and
the item-build opcodes 0x10/2). Pre-committed map:
- **length≈0 / few opcodes swap-ON** → the render command stream itself is empty/truncated →
  frontier jumps to the stream PRODUCER (`FUN_18252a228` up), i.e. the scene→command-buffer
  build — the upstream subsystem the empty-list already implicated.
- **same length + same opcode-4 count swap-ON, but the item-build opcodes absent/reduced** →
  the build opcodes are being dropped → decompile the specific build opcode's handler.
- **same stream + same opcodes both arms** → the items ARE referenced but resolve empty →
  back to the per-opcode item source (the opcode-4 handler already traced; re-read the build
  opcode's data payload).
