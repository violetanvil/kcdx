// CAP-109 — miniz DEFLATE inflater links + inflates on kcdx's own CRT.
//
// The regression proof for the file-system-takeover DEFLATE dependency
// (design file-system-takeover.md §6: kcdx reads vanilla + mod pak bytes with
// its OWN PKZIP/DEFLATE reader, every byte on kcdx's CRT, no engine ZipDir in
// the path; §10: the inflater dependency). This plugin links the vendored miniz
// inflater and inflates a KNOWN raw-DEFLATE blob to its KNOWN plaintext at boot,
// proving the dependency links and decodes correctly — the prerequisite the
// Phase-2 pak reader (steps 2.2/2.3) builds on.
//
// === Shape =============================================================
//
// A standalone native C++ DLL plugin (the cap-36 shape): caches (api, self) at
// kcdxPlugin_Load, runs ONE pure-CPU assertion (inflate the embedded blob,
// compare to the known plaintext), and self-reports the verdict via
// api->ReportTestResult. No engine state is touched, so the check runs and
// reports entirely within kcdxPlugin_Load — no after-phase export needed (the
// inflate is deterministic CPU work, not gated on any game lifecycle).
//
// The plugin links miniz by compiling vendor/miniz/miniz.c into its own DLL
// (CMakeLists.txt) — a self-contained test artifact. The ENGINE links the same
// vendored miniz via the shared `miniz` static lib (root CMakeLists.txt); this
// plugin proves the inflate API works against the same vendored source.
//
// === The known DEFLATE blob (how it was produced — regenerate/verify) ==
//
// Plaintext (kKnownPlaintext below, 79 bytes):
//   "kcdx miniz inflate test: the quick brown fox jumps over the lazy dog
//    0123456789"
//
// Compressed bytes (kDeflateBlob below, 74 bytes) were produced deterministically
// with Python's zlib at level 9, RAW DEFLATE (no zlib header/trailer) — wbits=-15,
// which matches a PKZIP method-8 (DEFLATE) entry's stored stream:
//
//   import zlib
//   plaintext = b"kcdx miniz inflate test: the quick brown fox jumps over "
//               b"the lazy dog 0123456789"
//   co = zlib.compressobj(9, zlib.DEFLATED, -15)
//   comp = co.compress(plaintext) + co.flush()
//   # comp is the 74 bytes in kDeflateBlob; round-trips via
//   # zlib.decompress(comp, -15) == plaintext
//
// miniz inflates it via tinfl_decompress_mem_to_mem(flags=0): flags=0 means a
// RAW DEFLATE stream (TINFL_FLAG_PARSE_ZLIB_HEADER is OFF), matching the wbits=-15
// raw stream above. A future reader regenerates the blob with the recipe above
// and confirms byte-identity, or re-runs this test.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "miniz.h"

namespace {

// Manifest bare name — must match [plugin].name in kcdx.toml.
const char* kName = "cap_109_miniz_inflate";
const char* kRow  = "cap-109-miniz-inflate";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

// The known plaintext the blob below inflates to (79 bytes, NUL-terminated
// string literal — the inflated output must equal these 79 bytes).
const char kKnownPlaintext[] =
    "kcdx miniz inflate test: the quick brown fox jumps over the lazy dog "
    "0123456789";
const size_t kKnownPlaintextLen = sizeof(kKnownPlaintext) - 1;  // 79

// The raw-DEFLATE blob (74 bytes) — produced by the recipe in the file header.
const unsigned char kDeflateBlob[] = {
    0x1d, 0xc9, 0xc7, 0x11, 0x80, 0x20, 0x00, 0x04, 0xc0, 0x56, 0xae, 0x04,
    0x73, 0xea, 0x06, 0x09, 0x8a, 0x24, 0x25, 0x28, 0x52, 0xbd, 0x33, 0xee,
    0x77, 0x15, 0x65, 0x19, 0x46, 0x5a, 0x59, 0x20, 0xad, 0xd0, 0x24, 0x72,
    0x44, 0x1e, 0xe2, 0x82, 0xb8, 0x73, 0x5c, 0x49, 0x52, 0x85, 0xd5, 0xbb,
    0xc7, 0x42, 0xb8, 0x8c, 0x23, 0x99, 0x33, 0xc0, 0xdd, 0xdc, 0xff, 0xad,
    0x49, 0x79, 0xc1, 0xdc, 0x86, 0xaa, 0x6e, 0xda, 0xae, 0x1f, 0xc6, 0x69,
    0xfe, 0x00,
};

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP109", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP109", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ==================================================
//
// The inflate is deterministic CPU work — it neither reads nor depends on any
// game lifecycle state, so the row self-checks and reports here, at load.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    // Inflate the known blob into a fixed output buffer (sized to the known
    // plaintext length — the output must be exactly kKnownPlaintextLen bytes).
    unsigned char out[256] = {};
    size_t written = tinfl_decompress_mem_to_mem(
        out, sizeof(out),
        kDeflateBlob, sizeof(kDeflateBlob),
        /*flags=*/0);  // 0 = raw DEFLATE (no zlib header); matches wbits=-15

    char reason[400];

    if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        snprintf(reason, sizeof(reason),
            "tinfl_decompress_mem_to_mem returned FAILED on the known %zu-byte "
            "raw-DEFLATE blob — the vendored miniz inflater did not link or "
            "could not decode a valid stream (miniz version %s)",
            sizeof(kDeflateBlob), MZ_VERSION);
        Report(false, reason);
        return true;
    }

    const bool len_ok   = (written == kKnownPlaintextLen);
    const bool bytes_ok = len_ok &&
        (std::memcmp(out, kKnownPlaintext, kKnownPlaintextLen) == 0);
    const bool pass = bytes_ok;

    snprintf(reason, sizeof(reason),
        "%s — tinfl_decompress_mem_to_mem inflated %zu bytes (expected %zu) and "
        "the inflated bytes %s the known plaintext; miniz version %s. Proves the "
        "DEFLATE inflater links into a kcdx-CRT artifact and decodes a raw "
        "method-8 stream correctly (the file-system-takeover pak reader's "
        "dependency)",
        pass ? "miniz inflate PASS" : "miniz inflate FAIL",
        written, kKnownPlaintextLen,
        bytes_ok ? "EQUAL" : (len_ok ? "DIFFER from" : "are the wrong length vs"),
        MZ_VERSION);
    Report(pass, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
