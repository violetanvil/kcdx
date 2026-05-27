#pragma once

// cap-53 self-test for the mod_absorb mod.manifest reader (mod_manifest.{h,cpp})
// and the shared version-compat helper (version_compat.{h,cpp}) — STEP 2 of the
// mod-loader-absorb feature.
//
// Like cap-52, these surfaces are engine-INTERNAL (kcdx::mod_absorb /
// kcdx::version_compat), not plugin exports, so cap-53 self-reports from ENGINE
// code via kcdx::test::ReportResult — the cap-47 / cap-39 / cap-52 prior-art
// pattern. It feeds a LITERAL mod.manifest XML string (no file on disk) through
// the tag extractor + the equivalent of ReadModManifest, asserts each field +
// entity-decoding, and exercises the UNIFIED <supports> string-prefix-wildcard
// gate from both entry points — DecideGameVersionCompatString directly and
// DecideModCompat (the pak-mod gate, which parses <supports> + delegates to it).
//
// Sub-step 2.5a added: (1) the unified gate DecideGameVersionCompatString
// (prefix/exact/empty/unknown/multi cases, incl. the no-*-is-exact-not-prefix
// discriminator), and (2) the system.cfg wh_sys_version parser ExtractCfgValue
// fed a literal cfg string (case-insensitive key, whitespace-around-'=',
// quote-stripping, absent key -> ""). Sub-step 2.5c wired the pak-mod <supports>
// parse (ParseSupports — the two-context discriminator) + repointed DecideModCompat
// onto the string gate, so assertion 4 now asserts that pak-mod delegation.

namespace kcdx::mod_absorb {

// Run the cap-53 self-test exactly once and report via kcdx::test::ReportResult.
// Idempotent (function-static one-shot). No hook-fire / "ready" dependency — the
// parsing + the helper work at boot — so it reports on the first tick.
void RunManifestSelfTestOnce();

}  // namespace kcdx::mod_absorb
