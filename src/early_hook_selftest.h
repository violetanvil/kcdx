#pragma once

// cap-80 self-test for the author-parameterized early-install primitive
// (early_hook.{h,cpp}). early_hook::Install is an engine-INTERNAL symbol — it is
// VM/boot plumbing, not a plugin export — so cap-80 self-reports from ENGINE
// code via kcdx::test::ReportResult, exactly like the prior-art engine
// self-tests cap-52 (record_synth) / cap-66 (ki0001 node classifier).
//
// The test proves the GENERALIZED, author-parameterized path works — not just
// the one baked BugSplat target. It installs a detour by (module + export +
// signature + detour) on a KNOWN already-mapped module (kcdx's own engine DLL,
// always mapped) targeting a dedicated exported no-op, then CALLS that export
// and asserts the detour FIRED. This exercises GetModuleHandleW +
// GetProcAddress(export) + MH_CreateHook + MH_EnableHook end-to-end through the
// public early_hook::Install signature, with a fired-flag that can go red.
//
// No VM needed (MinHook is up in DllMain by the time the suite ticks), no
// player input, no game-state dependency — boot-only, mirroring cap-66.

namespace kcdx::early_hook_selftest {

// Run the cap-80 early-hook self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (function-static one-shot guard); safe to
// call every tick from the engine's per-tick self-report block. Boot-only — no
// hook-fire / "ready" dependency beyond MinHook being initialized, which the
// engine guarantees by the first suite tick.
void RunSelfTestOnce();

}  // namespace kcdx::early_hook_selftest
