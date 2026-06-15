// CAP-112 — standalone overlay-SEAM stub for compiling the engine asset_index.cpp
// into the test-plugin DLL.
//
// asset_index.cpp (the index under test) composes TWO things from the asset-
// overlay layer: asset_overlay::NormalizeVPath (the single key-normalization
// definition) and asset_overlay::GetOverlayMap (the loose-source + precedence
// authority — it ALREADY computed the §4.4 load-order winner / §5.3 cross-mod
// resolution). The real asset_overlay.cpp pulls in the whole engine — the hook
// chain, refdb, the namespace/sidecar layer — and cannot be linked standalone.
//
// This TU supplies trivial standalone definitions of EXACTLY those two seam
// symbols (analogous to the cap-111 log stub), so asset_index.cpp stays
// BYTE-IDENTICAL between the engine build and this test build — no #ifdef carve-
// out in production source. The stub supplies the overlay SEAM, NOT the index
// logic under test (asset_index.cpp is compiled as-is); this is the same
// legitimate seam as the log stub, not a cfg-test-shim of the thing under test.
//
//   - NormalizeVPath: the real, tiny free function (lowercase + '\\'->'/'), so
//     the test's keys collide with the index's keys exactly as in production.
//   - GetOverlayMap: returns a TEST-CONTROLLED OverlayMap the test populates via
//     SetTestOverlayMap below — letting the test DRIVE the overlay contents (a
//     synthetic loose override of a known vanilla vpath) to assert overlay-wins-
//     vanilla deterministically.
//
// The struct OverlayEntry / OverlayMap typedef come from the REAL asset_overlay.h
// (included via asset_index.cpp's `#include "../asset_overlay.h"` and here), so
// the test exercises the real composition shape.

#include "../../src/asset_overlay.h"

#include <string>

namespace kcdx::asset_overlay {

// The single key-normalization definition (lowercase + backslash->forwardslash).
// Byte-identical in intent to the engine's asset_overlay.cpp NormalizeVPath; the
// test relies only on this exact fold, which the index reuses for its pak keys.
std::string NormalizeVPath(const std::string& vpath) {
    std::string out = vpath;
    for (char& c : out) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

namespace {
// The test-controlled overlay map. Empty by default (assertion (a) builds the
// index with NO overlay); the test injects a synthetic override for (b).
OverlayMap g_testOverlay;
}  // namespace

const OverlayMap& GetOverlayMap() { return g_testOverlay; }

// Test-only seam control — NOT part of the engine asset_overlay surface. The
// cap-112 test calls this to drive the overlay contents that the index ingests,
// so it can assert overlay-wins-vanilla. Declared in this TU's own header
// fragment below (the test includes this .cpp's declaration via extern).
void SetTestOverlayMap(const OverlayMap& m) { g_testOverlay = m; }

}  // namespace kcdx::asset_overlay
