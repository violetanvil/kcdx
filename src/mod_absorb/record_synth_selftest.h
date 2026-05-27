#pragma once

// cap-52 self-test for the mod_absorb record-synthesis module
// (record_synth.{h,cpp}). BuildRecord is an engine-INTERNAL symbol
// (kcdx::mod_absorb) — correctly NOT exposed through the plugin interface (it
// drives an internal MOUNT-list mechanism, not an author surface) — so cap-52
// self-reports from ENGINE code via kcdx::test::ReportResult, exactly like the
// prior-art engine self-tests cap-47 / cap-39 in src/hooks.cpp.
//
// The test proves the load-bearing concern of the absorb design: a synthesized
// I_Mod record's string buffers stay address-stable for process lifetime and
// across later BuildRecord calls (the container-stability proof — see the .cpp
// assertions). It also proves the vtables resolve via the Address Library
// (id 3105/3106), the field offsets are correct, and the scalar tail is zeroed.

namespace kcdx::mod_absorb {

// Run the cap-52 record-synthesis self-test exactly once and report its result
// via kcdx::test::ReportResult. Idempotent — internally one-shot guarded with a
// function-static bool, so it is safe to call every tick from the engine's
// per-tick self-report block. Unlike cap-47, this has NO dependency on a hook
// firing or on "ready": BuildRecord works as soon as the Address Library
// resolves (available at boot), so it reports on the first tick.
void RunSelfTestOnce();

}  // namespace kcdx::mod_absorb
