# `validate_extractor_output.py` — runbook + AP15 record

The output-validation harness is the **falsifiable regression gate** for the
entire reference-data extractor (Java passes + the two Python emit passes). It is
the agreed substitute for a `test-plugins/cap-NN` matrix row: headless tooling
has no in-game surface, so the "does it work" question is settled by an
independent-anchor harness instead of a game launch.

## Runbook

```
python tools/refdata-extractor/python/validate_extractor_output.py
```

No arguments. The harness:

1. Compiles the vetted `blake3/Blake3Hex.java` (+ `Blake3.java`) to a scratch
   classpath — the **independent** `content_hash` oracle.
2. Runs the Java launcher (`ghidra/produce-reference-data.ps1 -Limit 256`) →
   `functions/ statements/ referenced_vars/ call_edges/` into a scratch out dir.
3. Runs `produce_signatures.py` + `produce_caller_reg_args.py` (`-Limit 256`) →
   `signatures/ caller_reg_args/`.
4. Asserts the checks below, prints per-check `PASS/FAIL`, an overall
   `VERDICT: N/N checks PASS`, and `sys.exit(1)` on any FAIL.
5. Cleans up the scratch dir + `__pycache__` + the compiled `Blake3Hex.class`
   (all live under the scratch tree).

Prerequisites: `pwsh`, `javac`/`java` (JDK 21 present), `pefile` 2024.8.26 +
`capstone` 5.0.7, the pre-analyzed Ghidra project at
`third-party-ghidra/ghidra_project/`, and `WHGame.dll` at the game-bin path.

Expected current verdict: **26/26 PASS, exit 0.**

## Why 26 and not exactly 25

The spec's enumeration is "~25"; the requirement is that **every** check reads an
**independent anchor** and names a **broken state** (AP15), with no tautology
padding. The reconstruction lands on 26 (the `referenced_vars/` storage-kind
contract splits cleanly into three independent assertions). None is a tautology.

## AP15 record — each check's independent anchor + the extractor-broken state it catches

A check is only worth running if it can FAIL on a real bug. Each below reads an
answer the extractor did **not** produce.

| # | Check | Independent anchor | Broken state it catches |
|---|---|---|---|
| A1/A3/A5 | `functions/` 0x1020 / 0x1050 / 0x11d0 `auto_name` matches enum CSV | `WHGame.dll.functions.csv` (produced by EnumerateFunctions.java — a DIFFERENT tool) | FunctionPass mis-derives the auto-name / off-by-one RVA / wrong image-base subtraction |
| A2/A4/A6 | same anchors' `length` matches enum `size_bytes` | enum CSV | `getBody().getNumAddresses()` wrong → the hashed `[rva,rva+length)` span is wrong (corrupts every `content_hash`) |
| B1/B3/B5 | each anchor's `content_hash` is 64-char lowercase hex | the pinned BLAKE3 contract (encoding) | wrong digest length / uppercase / `0x` prefix → engine consumer can't compare |
| B2/B4/B6 | each anchor's `content_hash` == independent `Blake3Hex` recompute over pefile-read on-disk bytes | the vetted Apache Blake3 via `Blake3Hex.java` reading bytes the harness fetches itself | ContentHash hashes the wrong byte range / live-memory instead of on-disk / wrong algorithm |
| C | `statements/` 0x11d0 has a stmt at `byte_range_start` 0x1243 with `callee_rva` 0x1050 | the binary itself (`call 0x180001050` at 0x1243, verified by disasm) | StatementPass loses the machine-code range, mis-resolves the callee, or off-by-one `byte_range_start` |
| D1 | `call_edges/` has the resolved direct edge (0x11d0 → 0x1050 @ 0x1243) | the binary (`E8` direct call at 0x1243) | CallEdgePass drops the edge / wrong callsite / wrong callee resolution |
| D2 | `call_edges/` has ≥1 indirect edge (`edge_reason=indirect`, empty `callee_rva`) | AP14 contract (indirect calls must be VISIBLE) | indirect (`call rax`/`call [rip+x]`) calls silently dropped instead of emitted blind |
| E1 | `caller_reg_args/` 0x1050 `caller_reg_arg_count` == 3 | the binary (its one callsite sets rcx/rdx/r8) | backward register-scan attribution broken (sub-register map, window boundary) |
| E2 | NO `caller_reg_args/` row has `caller_reg_arg_count` > 4 | the §4e B design decision (register side capped at 4; stack side DROPPED) | the dropped noisy stack-arg side regresses back in |
| F1 | `referenced_vars/` 0x1050 has ≥1 `register` row naming reg **RCX** (arg1) | the MSVC x64 ABI (arg1 = rcx) + Ghidra HighVariable storage | StatementPass loses register storage detail / names the wrong reg |
| F2 | every 0x1050 row's `storage_kind` ∈ {register,stack,memory,unique,const,other} | the StatementPass storage-kind taxonomy contract | a varnode falls through to an undefined kind |
| F3 | every 0x1050 `register` row names a real x86-64 reg with positive `size_bytes` | the x86-64 register file | `program.getRegister(...)` returns null/garbage or zero size |
| G1 | NO `statements/` `callee_rva` is a >10-hex-digit underflow | the `inModuleImage` guard (an out-of-module callee must EMPTY the rva, not emit `getOffset()-imageBase` garbage) | the external-underflow bug regresses |
| G2 | NO `call_edges/` `callee_rva` is a >10-hex-digit underflow | same `inModuleImage` guard | same bug, edge table |
| H1 | every `functions/` rva exists in the enum CSV | enum CSV | a fabricated / mis-derived rva appears |
| H2 | `functions/` row count == 256 | the `-Limit 256` fixture bound | silent skip / over-emit (AP14 balance) |
| H3 | `signatures/` row count == 256 | the fixture bound | the signature pass silently drops functions |
| H4 | `signatures/` is 1:1 with `functions/` by rva | the merge contract (signatures merge OVER functions BY rva) | a misaligned shard / missing or duplicate signature row breaks the merge |

## The RCX-only caveat (the AP15 secondary trap)

Check **F1** asserts only that **RCX** (arg1) surfaces as a callee-side register
`HighVariable`. It does **NOT** assert RDX/R8 also appear. That is deliberate: on
the callee side Ghidra reliably materializes only the first integer-arg register
(rcx) as a register-storage HighVariable for many functions; the later arg
registers are frequently promoted to stack/unique varnodes by the decompiler.

Asserting "0x1050 also has an RDX and an R8 register row" would be an **inverted
check** that FAILs on *correct* extractor output — an AP15 trap (a check that
fires on the truth). The register-arity signal RDX/R8 lives on the **caller**
side (check E1, where `produce_caller_reg_args` reads rcx/rdx/r8 set at the
callsite), not the callee side. Keep F1 scoped to RCX.
