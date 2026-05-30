#pragma once

// cap-66 self-test — the permanent regression guard for KI-0001
// (save-load STATUS_HEAP_CORRUPTION 0xC0000374 on the chain-mediated lua_pcall
// path). See docs/known-issues/KI-0001-save-load-heap-corruption-on-chain-
// mediated-lua_pcall.md.
//
// Root cause: kcdx and WHGame.dll each statically embed Lua 5.1 with their own
// `static const Node dummynode_` at different .rdata addresses, both driving one
// shared global_State. kcdx's GC sweep called luaH_free on a WHGame-allocated
// Table whose t->node is WHGame's dummynode; the `t->node != dummynode` guard
// (kcdx's) was TRUE, so luaM_freearray freed WHGame's .rdata sentinel ->
// RtlSizeHeap faulted.
//
// Fix (vendor/lua/ltable.c): kcdx_node_freeable() — a node is freeable only if
// it is NOT a loaded-module-image (.rdata) sentinel (VirtualQuery MEM_IMAGE).
// kcdx's own dummynode and any foreign dummynode are skipped.
//
// This self-test exercises that discriminator directly and at boot (no save-load
// gesture needed), so the regression is caught by the standard launch-to-menu
// run. The live save-load repro is the cap-66 [manual] companion row.
//
// FALSIFIABLE: the row goes red if the guard classifies a .rdata/module-image
// node as freeable (the exact regression that reintroduces the crash), or if it
// classifies a heap node as non-freeable (which would leak every table's hash
// part). Both directions are asserted.

namespace kcdx::ki0001 {

// Run the cap-66 node-classifier self-test exactly once and report via
// kcdx::test::ReportResult. Idempotent (one-shot guarded); safe to call every
// tick from the engine's per-tick self-report block. No hook/"ready" dependency.
void RunSelfTestOnce();

}  // namespace kcdx::ki0001
