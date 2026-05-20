#include "trampoline_engine.h"

#include <cstring>

#include "log.h"
#include "symbols.h"
#include "trampoline.h"

namespace kcdx::trampoline_engine {

std::vector<TrampolineEntry> g_trampolines;

size_t ApplyAll() {
    if (g_trampolines.empty()) return 0;

    log::InfoF("Trampoline engine: applying %zu trampoline(s)...", g_trampolines.size());
    size_t applied = 0;

    for (const TrampolineEntry& t : g_trampolines) {
        if (t.bytes.empty() && !t.size.has_value()) {
            log::ErrorF("[trampoline '%s'] aborted: no `bytes` and no `size`",
                        t.name.c_str());
            continue;
        }

        size_t totalSize = t.size.value_or(t.bytes.size());
        if (totalSize < t.bytes.size()) {
            log::ErrorF("[trampoline '%s'] aborted: declared size (%zu) is "
                        "smaller than bytes.length (%zu)",
                        t.name.c_str(), totalSize, t.bytes.size());
            continue;
        }

        // Choose pool. Owner=0 means engine-owned for now; future plugin-
        // declared trampolines will thread a real handle through.
        void* region = nullptr;
        if (t.pool == "local") {
            region = trampoline::AllocateLocal(/*owner=*/0, totalSize);
        } else {
            // Default "branch"; reject unknown values.
            if (t.pool != "branch") {
                log::WarnF("[trampoline '%s'] unknown pool '%s' — defaulting to 'branch'",
                           t.name.c_str(), t.pool.c_str());
            }
            region = trampoline::AllocateBranch(/*owner=*/0, totalSize);
        }
        if (!region) {
            log::ErrorF("[trampoline '%s'] aborted: trampoline pool '%s' "
                        "could not allocate %zu bytes",
                        t.name.c_str(), t.pool.c_str(), totalSize);
            continue;
        }

        // Copy the author's bytes into the head of the region.
        auto* dst = reinterpret_cast<uint8_t*>(region);
        if (!t.bytes.empty()) {
            std::memcpy(dst, t.bytes.data(), t.bytes.size());
        }
        // NOP-pad the tail so any plugin patching into the unused space
        // doesn't trip over zero-init bytes (which decode as `add [rax], al`
        // and could crash if executed).
        if (totalSize > t.bytes.size()) {
            std::memset(dst + t.bytes.size(),
                        0x90,  // x86 NOP
                        totalSize - t.bytes.size());
        }

        LOG_DEBUG("TRAMPOLINE", "[%s] allocated %zu bytes at 0x%p (pool=%s)",
                  t.name.c_str(),
                  totalSize,
                  region,
                  t.pool.c_str());

        // Register export if requested.
        if (!t.exportSymbol.empty()) {
            uintptr_t addr = reinterpret_cast<uintptr_t>(region);
            if (symbols::Register(t.exportSymbol, addr, t.name)) {
                LOG_DEBUG("TRAMPOLINE", "[%s] exported symbol '%s' -> 0x%p",
                          t.name.c_str(),
                          t.exportSymbol.c_str(),
                          reinterpret_cast<void*>(addr));
            } else {
                // Symbol already registered. We've already allocated and
                // filled the region; the alloc isn't freed (alloc-only),
                // but the trampoline is effectively orphaned — no other
                // plugin can reach it by symbol.
                std::string priorOwner = symbols::OwnerOf(t.exportSymbol);
                log::ErrorF("[trampoline '%s'] symbol export collision: '%s' "
                            "already registered by '%s' — this trampoline is "
                            "allocated but unreachable by symbol.",
                            t.name.c_str(),
                            t.exportSymbol.c_str(),
                            priorOwner.c_str());
                continue;  // don't count as "applied"
            }
        }

        ++applied;
    }

    log::InfoF("Trampoline engine: %zu of %zu trampoline(s) applied; "
               "symbol table size = %zu",
               applied, g_trampolines.size(), symbols::Count());
    return applied;
}

}  // namespace kcdx::trampoline_engine
