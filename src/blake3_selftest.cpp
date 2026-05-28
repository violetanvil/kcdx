#include "blake3_selftest.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>   // snprintf
#include <cstring>  // memcmp
#include <string>
#include <vector>

#include "blake3.h"
#include "log.h"
#include "test.h"

// cap-59 self-test — see blake3_selftest.h for why this lives in engine code.
// The 35 expected digests below are the first 32 bytes (64 hex chars) of each
// official BLAKE3 test vector's default-length `hash` output, embedded so the
// check is self-contained at boot (no file read). The full vector set + the
// 251-byte input definition live with the reference-data extractor tooling.

namespace kcdx::blake3 {

namespace {

constexpr const char* kRow = "cap-59-blake3-vectors";
constexpr const char* kCategory = "BLAKE3";

// One official vector: the input length and the expected first-32-bytes digest
// as 64 lowercase hex chars.
struct Vector {
    size_t      input_len;
    const char* expected_hex;  // 64 chars
};

// All 35 official vectors (input_len + first 32 bytes of the default-length
// hash). Order matches the canonical vector file.
constexpr Vector kVectors[] = {
    { 0,      "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262" },
    { 1,      "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213" },
    { 2,      "7b7015bb92cf0b318037702a6cdd81dee41224f734684c2c122cd6359cb1ee63" },
    { 3,      "e1be4d7a8ab5560aa4199eea339849ba8e293d55ca0a81006726d184519e647f" },
    { 4,      "f30f5ab28fe047904037f77b6da4fea1e27241c5d132638d8bedce9d40494f32" },
    { 5,      "b40b44dfd97e7a84a996a91af8b85188c66c126940ba7aad2e7ae6b385402aa2" },
    { 6,      "06c4e8ffb6872fad96f9aaca5eee1553eb62aed0ad7198cef42e87f6a616c844" },
    { 7,      "3f8770f387faad08faa9d8414e9f449ac68e6ff0417f673f602a646a891419fe" },
    { 8,      "2351207d04fc16ade43ccab08600939c7c1fa70a5c0aaca76063d04c3228eaeb" },
    { 63,     "e9bc37a594daad83be9470df7f7b3798297c3d834ce80ba85d6e207627b7db7b" },
    { 64,     "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98" },
    { 65,     "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee" },
    { 127,    "d81293fda863f008c09e92fc382a81f5a0b4a1251cba1634016a0f86a6bd640d" },
    { 128,    "f17e570564b26578c33bb7f44643f539624b05df1a76c81f30acd548c44b45ef" },
    { 129,    "683aaae9f3c5ba37eaaf072aed0f9e30bac0865137bae68b1fde4ca2aebdcb12" },
    { 1023,   "10108970eeda3eb932baac1428c7a2163b0e924c9a9e25b35bba72b28f70bd11" },
    { 1024,   "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7" },
    { 1025,   "d00278ae47eb27b34faecf67b4fe263f82d5412916c1ffd97c8cb7fb814b8444" },
    { 2048,   "e776b6028c7cd22a4d0ba182a8bf62205d2ef576467e838ed6f2529b85fba24a" },
    { 2049,   "5f4d72f40d7a5f82b15ca2b2e44b1de3c2ef86c426c95c1af0b6879522563030" },
    { 3072,   "b98cb0ff3623be03326b373de6b9095218513e64f1ee2edd2525c7ad1e5cffd2" },
    { 3073,   "7124b49501012f81cc7f11ca069ec9226cecb8a2c850cfe644e327d22d3e1cd3" },
    { 4096,   "015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e969" },
    { 4097,   "9b4052b38f1c5fc8b1f9ff7ac7b27cd242487b3d890d15c96a1c25b8aa0fb995" },
    { 5120,   "9cadc15fed8b5d854562b26a9536d9707cadeda9b143978f319ab34230535833" },
    { 5121,   "628bd2cb2004694adaab7bbd778a25df25c47b9d4155a55f8fbd79f2fe154cff" },
    { 6144,   "3e2e5b74e048f3add6d21faab3f83aa44d3b2278afb83b80b3c35164ebeca205" },
    { 6145,   "f1323a8631446cc50536a9f705ee5cb619424d46887f3c376c695b70e0f0507f" },
    { 7168,   "61da957ec2499a95d6b8023e2b0e604ec7f6b50e80a9678b89d2628e99ada77a" },
    { 7169,   "a003fc7a51754a9b3c7fae0367ab3d782dccf28855a03d435f8cfe74605e7817" },
    { 8192,   "aae792484c8efe4f19e2ca7d371d8c467ffb10748d8a5a1ae579948f718a2a63" },
    { 8193,   "bab6c09cb8ce8cf459261398d2e7aef35700bf488116ceb94a36d0f5f1b7bc3b" },
    { 16384,  "f875d6646de28985646f34ee13be9a576fd515f76b5b0a26bb324735041ddde4" },
    { 31744,  "62b6960e1a44bcc1eb1a611a8d6235b6b4b78f32e7abc4fb4c6cdcce94895c47" },
    { 102400, "bc3e3d41a1146b069abffad3c0d44860cf664390afce4d9661f7902e7943e085" },
};

constexpr size_t kVectorCount = sizeof(kVectors) / sizeof(kVectors[0]);

// Build the official vector input: `len` bytes of the repeating 251-byte cycle
// 0,1,2,...,249,250,0,1,... (byte value = index mod 251).
std::vector<uint8_t> MakeVectorInput(size_t len) {
    std::vector<uint8_t> v(len);
    for (size_t i = 0; i < len; ++i) {
        v[i] = static_cast<uint8_t>(i % 251);
    }
    return v;
}

// Lowercase-hex-encode a digest into a 65-char buffer (64 chars + NUL).
void ToHex(const uint8_t* digest, size_t n, char* out) {
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i * 2]     = kHex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[digest[i] & 0xF];
    }
    out[n * 2] = '\0';
}

}  // namespace

void RunSelfTestOnce() {
    static bool s_reported = false;
    if (s_reported) {
        return;
    }
    s_reported = true;  // one-shot — the hash is deterministic, no retry needed.

    char reason[512];

    for (size_t i = 0; i < kVectorCount; ++i) {
        const Vector& vec = kVectors[i];

        std::vector<uint8_t> input = MakeVectorInput(vec.input_len);
        uint8_t digest[kHashLen];
        Hash256(input.empty() ? nullptr : input.data(), input.size(), digest);

        char gotHex[kHashLen * 2 + 1];
        ToHex(digest, kHashLen, gotHex);

        // FAILS if the engine's hash of vector `input_len`'s 251-byte-cycle
        // input does not byte-match the canonical first-32-bytes digest — i.e.
        // the port is wrong. Names the FIRST failing input_len (computed vs
        // expected) loud, then stops (one mismatch falsifies the port).
        if (std::strcmp(gotHex, vec.expected_hex) != 0) {
            std::snprintf(reason, sizeof(reason),
                "FAIL: BLAKE3 vector mismatch at input_len=%zu (vector %zu/%zu) — "
                "computed %s, expected %s. The engine's BLAKE3 does NOT match the "
                "canonical algorithm; the survival-check content_hash comparison "
                "would be meaningless. (Wrong port / wrong SIMD fallback / wrong "
                "input construction.)",
                vec.input_len, i + 1, kVectorCount, gotHex, vec.expected_hex);
            LOG_ERROR_KV(kCategory, "selftest_fail",
                ::kcdx::log::KV("input_len", (unsigned long long)vec.input_len),
                ::kcdx::log::KV("computed", gotHex),
                ::kcdx::log::KV("expected", vec.expected_hex),
                ::kcdx::log::KV("vector_index", (unsigned long long)(i + 1)),
                ::kcdx::log::KV("vector_count", (unsigned long long)kVectorCount));
            kcdx::test::ReportResult(kRow, false, reason);
            kcdx::test::EmitSummaryIfChanged("cap-59 blake3-vectors");
            return;
        }
    }

    // All 35 held — the engine's BLAKE3 is byte-identical to the canonical
    // algorithm; the survival check's content_hash comparison is trustworthy.
    std::snprintf(reason, sizeof(reason),
        "PASS %zu/%zu BLAKE3 official vectors — first 32 bytes match for every "
        "input_len (0..102400, 251-byte-cycle input); the engine's BLAKE3 is "
        "byte-identical to the canonical algorithm",
        kVectorCount, kVectorCount);
    LOG_INFO_KV(kCategory, "selftest_pass",
        ::kcdx::log::KV("vectors", (unsigned long long)kVectorCount),
        ::kcdx::log::KV("result", "35/35"));
    kcdx::test::ReportResult(kRow, true, reason);
    kcdx::test::EmitSummaryIfChanged("cap-59 blake3-vectors");
}

}  // namespace kcdx::blake3
