#pragma once

// cap-59 self-test for the BLAKE3 wrapper (blake3.{h,cpp}) over the vendored
// portable BLAKE3.
//
// This is the falsifiable proof that the engine's BLAKE3 produces byte-identical
// output to the canonical algorithm: it hashes all 35 official BLAKE3 test-vector
// inputs (the repeating 251-byte cycle 0,1,...,250,0,1,...) and asserts the first
// 32 bytes of each match the vector's expected default-length digest. The
// expected digests are embedded in the .cpp (not read from a file) so the check
// runs self-contained at engine boot in any install.
//
// Why it lives in engine code (like the mod_absorb cap-5x self-tests):
// kcdx::blake3::Hash256 is an engine-internal symbol, not a plugin export — so
// cap-59 self-reports from ENGINE code via kcdx::test::ReportResult.
//
// The survival check (kcdx::survival) trusts this wrapper. If these 35 vectors
// fail, the port is wrong and every survival comparison would be meaningless —
// so this gates trust in the on-disk content_hash comparison.

namespace kcdx::blake3 {

// Run the cap-59 BLAKE3 35-vector self-test exactly once and report its result
// via kcdx::test::ReportResult. Idempotent — one-shot guarded with a
// function-static bool, safe to call every tick from the engine's per-tick
// self-report block. No dependency on a hook firing or on "ready": the hash
// works as soon as the engine is loaded, so it reports on the first tick.
//
// It also writes a loud dev-log line: "PASS 35/35" on success, or a FAIL line
// naming the FIRST failing input_len (computed vs expected) on any mismatch.
void RunSelfTestOnce();

}  // namespace kcdx::blake3
