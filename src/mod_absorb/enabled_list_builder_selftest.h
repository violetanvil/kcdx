#pragma once

// cap-55 self-test — the enabled-list builder (src/mod_absorb/
// enabled_list_builder.cpp), STEP 4 of the mod-loader absorb. Lives in engine
// code (like cap-52/cap-54) because BuildEnabledList + NormalizeToNativeRecordForm
// are engine-internal, not a plugin export.
//
// Asserts the rebuilt list's ORDER (resolved load order), COUNT (disabled mods
// excluded), and per-record PATH FIELD (native form) against a synthetic
// resolved state, plus the path-normalization. The live MOUNT end-to-end (every
// enabled mod mounts, in kcdx order, no crash) is the batched verification
// checkpoint, not a boot self-test. One-shot guarded internally; reports on the
// first update tick (BuildEnabledList works at boot — no hook-fire dependency).

namespace kcdx::mod_absorb {

void RunEnabledListSelfTestOnce();

}  // namespace kcdx::mod_absorb
