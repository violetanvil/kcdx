#pragma once

// cap-101 self-test — behavior dependency edge persistence + launch re-check +
// prune (Phase 9.5 s6). One-shot guarded; called from the engine per-tick
// update (hooks.cpp), the cap-52..59 prior-art pattern. The PURE serializer /
// parser / prune assertions touch no global state; the reorder-recognition
// assertion drives the global load_order state in isolation and RESTORES it
// verbatim (the cap-54/55 snapshot/restore pattern). The LIVE on-disk
// write/read + the cross-launch up-front warn + the second-launch error upgrade
// are TWO-LAUNCH matrix rows (a single boot run cannot span two launches).

namespace kcdx::load_order {

void RunEdgePersistSelfTestOnce();

}  // namespace kcdx::load_order
