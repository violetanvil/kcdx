#include "blake3.h"

// The vendored portable BLAKE3 C header. Included by an explicit relative path
// (not a bare "blake3.h") because the engine's include search puts src/ ahead
// of vendor/blake3/, so a bare include would resolve to this module's own
// wrapper header above. The relative path pins it to the vendored C API.
#include "../vendor/blake3/blake3.h"

namespace kcdx::blake3 {

void Hash256(const void* data, size_t len, uint8_t out[kHashLen]) {
    blake3_hasher h;
    blake3_hasher_init(&h);
    blake3_hasher_update(&h, data, len);
    blake3_hasher_finalize(&h, out, BLAKE3_OUT_LEN);
    static_assert(kHashLen == BLAKE3_OUT_LEN,
                  "kcdx::blake3::kHashLen must equal the vendored BLAKE3_OUT_LEN");
}

}  // namespace kcdx::blake3
