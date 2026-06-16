// CAP-113 — standalone overlay-SEAM stub for compiling the engine open_slots.cpp
// + asset_index.cpp into the test-plugin DLL.
//
// asset_index.cpp + open_slots.cpp compose TWO things from the asset-overlay
// layer: asset_overlay::NormalizeVPath (the single key-normalization definition,
// used to key the index and the FOpen lookup) and asset_overlay::GetOverlayMap
// (the loose-source + precedence authority — it ALREADY computed the §4.4 load-
// order winner / §5.3 cross-mod resolution). The real asset_overlay.cpp pulls in
// the whole engine — the hook chain, refdb, the namespace/sidecar layer — and
// cannot be linked standalone.
//
// This TU supplies trivial standalone definitions of EXACTLY those two seam
// symbols (analogous to the cap-112 stub), so asset_index.cpp + open_slots.cpp
// stay BYTE-IDENTICAL between the engine build and this test build — no test
// carve-out in production source. The stub supplies the overlay SEAM, NOT the
// slot/index logic under test (those TUs are compiled as-is); this is the same
// legitimate seam as the log stub, not a cfg-test-shim of the thing under test.
//
//   - NormalizeVPath: the real, tiny free function (lowercase + '\\'->'/'), so
//     the test's keys collide with the index's + FOpen's keys exactly as in
//     production.
//   - GetOverlayMap: returns a TEST-CONTROLLED OverlayMap the test populates via
//     SetTestOverlayMap below — letting the test DRIVE the overlay contents (a
//     synthetic loose override of a known vpath) to assert the Loose-arm open+read
//     lifecycle deterministically.
//
// The struct OverlayEntry / OverlayMap typedef come from the REAL asset_overlay.h
// (included via asset_index.cpp's `#include "../asset_overlay.h"` and here), so
// the test exercises the real composition shape.

#include "../../src/asset_overlay.h"

#include <string>

namespace kcdx::asset_overlay {

// The single key-normalization definition (lowercase + backslash->forwardslash).
// Byte-identical in intent to the engine's asset_overlay.cpp NormalizeVPath; the
// test relies only on this exact fold, which the index + FOpen reuse for their
// keys.
std::string NormalizeVPath(const std::string& vpath) {
    std::string out = vpath;
    for (char& c : out) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

namespace {
// The test-controlled overlay map. Empty by default (the pak-lifecycle + FOpenRaw
// + bad-handle checks build the index with NO overlay); the test injects a
// synthetic override for the Loose-arm lifecycle check.
OverlayMap g_testOverlay;
}  // namespace

const OverlayMap& GetOverlayMap() { return g_testOverlay; }

// Test-only seam control — NOT part of the engine asset_overlay surface. The
// cap-113 test calls this to drive the overlay contents the index ingests, so it
// can assert the Loose arm opens + reads on kcdx's CRT. Declared in the test via
// an extern.
void SetTestOverlayMap(const OverlayMap& m) { g_testOverlay = m; }

}  // namespace kcdx::asset_overlay
