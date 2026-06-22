// === DIAGNOSTIC (PROBE R) — KI-0028 shader-cache VALIDATION gate ===============
#pragma once

namespace kcdx::fs_takeover {

// Arm PROBE R: the shader-cache-validation dispatch tracer. Idempotent; the first
// call installs the hooks, later calls no-op.
//
// WHY (KI-0028, the CShaderMan RE — _research/ki0028-cshaderman-pso-consumer-recon):
// the engine reads the FULL compiled shader cache correctly (574 .cfxb + 588 .cfib
// served), yet builds gfx_calls=1 (PROBE P) and idles — black screen. The RE
// REFRAMED the gate: the engine never reaches PSO creation because shader-cache
// VALIDATION rejects the cache under the swap and DISABLES the read-only cache,
// falling into the source-.ext-enumeration path (the swap-on-induced 36
// data/gameshaders/*.ext probes) that never completes the pipeline build.
//
// The gate is two WHGame functions (body-read, image base 0x180000000):
//   - lookupdata.bin LOADER  FUN_180b04984 (RVA 0xb04984): reads the 4-byte magic
//     + 0x14-byte header via the FReadRaw slot WITHOUT checking bytes-read, tests
//     magic 0x4b435043("CPCK") + version on the buffer; mismatch => RETURN 0.
//   - validate DRIVER        FUN_180b04478 (RVA 0xb04478): calls the loader on the
//     %ENGINE% copy; loader-returns-0 => "Disabling read-only shader cache!".
//
// PROBE R hooks BOTH as after-hooks (call original, log its return). The loader's
// return is the decisive gate bool: 0 = cache REJECTED = the wedge; 1 = ACCEPTED.
// Armed BEFORE the PROBE F noswap early-return in seating_hook.cpp, so it fires
// swap-ON and swap-OFF IDENTICALLY — the A/B diff that isolates the divergence.
//
// PRE-COMMITTED OUTCOME->MEANING MAP (theory-independent; the loader's return is
// the ground-truth fact, read regardless of theory):
//   loader returns 0 swap-ON, 1 swap-OFF  -> CONFIRMS the validation-reject
//     mechanism: the swap makes the cache fail validation. Next: read WHY the
//     header read diverges (the FReadRaw return-contract on the lookupdata.bin
//     pak handle) — a kcdx read-slot fix.
//   loader returns 0 BOTH modes             -> the cache is rejected even unswapped
//     (a cold/version-stale cache, NORMAL first-run) — the reject is NOT the
//     swap differentiator. Widen: the divergence is elsewhere (the validate
//     driver's %USER% arm, or downstream of validation). FALSIFIES "the loader
//     reject is the wedge".
//   loader returns 1 swap-ON too            -> the cache VALIDATES under the swap;
//     the gate is PAST validation (the precache submit / job-dispatch). FALSIFIES
//     the validation-reject theory entirely; re-frame to _PrecacheShaderList.
//   loader NEVER called swap-ON             -> the engine does not reach cache
//     validation under the swap; an earlier shader-init step early-exited. Widen
//     UP (mfInit / the cache-setup caller FUN_180b033a0).
//
// agent-builds-and-deploys: the agent builds/deploys/hash-verifies/reads the log;
// the user only launches. NO-RESIDUE: on retire, remove this file + its
// seating_hook.cpp arm + the CMakeLists entry (capture finding+wiring to
// _research/probe-archive first).
void DispatchProbeStart();

}  // namespace kcdx::fs_takeover
