#pragma once

// kcdx BLAKE3 wrapper — a thin C++ surface over the vendored portable BLAKE3
// (vendor/blake3). It exposes exactly the one operation the engine needs: a
// default-length (256-bit / 32-byte) hash of a contiguous byte span.
//
// The vendored C is wrapped, never reimplemented. The wrapper exists so the
// rest of the engine includes ONE header (this one) instead of the vendored
// blake3.h directly — and so the engine can stay on the official, vector-proven
// implementation while presenting a small, idiomatic call shape.
//
// Output encoding/contract: 32 raw bytes, default-length digest (no XOF). This
// matches the content_hash wire format the reference database carries (a 32-byte
// blob) — the survival check hashes an on-disk span and compares the 32 bytes
// directly. See kcdx::survival.

#include <cstddef>
#include <cstdint>

namespace kcdx::blake3 {

// The default BLAKE3 digest length in bytes (256-bit). Mirrors the vendored
// BLAKE3_OUT_LEN; restated here so callers/tests don't pull in the vendored
// header.
constexpr size_t kHashLen = 32;

// Hash [data, data+len) with default-length BLAKE3, writing exactly kHashLen
// (32) raw bytes into out. A zero-length input is valid (BLAKE3 of the empty
// string is well-defined — vector input_len=0). `data` may be null only when
// len == 0.
void Hash256(const void* data, size_t len, uint8_t out[kHashLen]);

}  // namespace kcdx::blake3
