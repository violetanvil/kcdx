#include "patch_engine.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "address_library.h"
#include "conflict_engine.h"
#include "load_order.h"
#include "log.h"
#include "lua_registry.h"  // ForEachEntryOfKind — enumerate Kind::Bytes
#include "modification_inventory.h"  // RegisterModification (Bytes — fault-time owner record)
                           // entries for GetAppliedBytesPatchesAtTarget
                           // (COMP-15). The patch engine casts the
                           // type-erased payload; the registry stays
                           // payload-agnostic.
#include "pe_helpers.h"
#include "symbols.h"

namespace kcdx::patch {

std::vector<PatchEntry> g_patches;
bool g_dryRun = false;

namespace {

std::string HexBytes(const uint8_t* data, size_t n) {
    std::ostringstream os;
    for (size_t i = 0; i < n; ++i) {
        if (i) os << ' ';
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        os << buf;
    }
    return os.str();
}

std::string HexBytes(const std::vector<uint8_t>& v) {
    return HexBytes(v.data(), v.size());
}

bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

uint8_t HexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
    throw std::runtime_error("not a hex digit");
}

}  // namespace

Pattern ParsePattern(const std::string& s) {
    Pattern p;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        if (token == "?" || token == "??") {
            p.bytes.push_back(0);
            p.mask.push_back(false);
        } else if (token.size() == 2 && IsHexDigit(token[0]) && IsHexDigit(token[1])) {
            uint8_t b = static_cast<uint8_t>((HexNibble(token[0]) << 4) | HexNibble(token[1]));
            p.bytes.push_back(b);
            p.mask.push_back(true);
        } else {
            throw std::runtime_error("invalid token in pattern: '" + token + "'");
        }
        token.clear();
    };

    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
        } else {
            token.push_back(c);
        }
    }
    flush();
    if (p.bytes.empty()) throw std::runtime_error("empty pattern");
    return p;
}

std::vector<uint8_t> ParseBytes(const std::string& s) {
    std::vector<uint8_t> out;
    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;
        if (token.size() == 2 && IsHexDigit(token[0]) && IsHexDigit(token[1])) {
            out.push_back(static_cast<uint8_t>((HexNibble(token[0]) << 4) | HexNibble(token[1])));
        } else {
            throw std::runtime_error("invalid byte token: '" + token + "'");
        }
        token.clear();
    };
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
        } else {
            token.push_back(c);
        }
    }
    flush();
    if (out.empty()) throw std::runtime_error("empty byte sequence");
    return out;
}

std::vector<size_t> FindAllInBuffer(const uint8_t* data, size_t size, const Pattern& pat) {
    std::vector<size_t> hits;
    if (pat.bytes.empty() || size < pat.bytes.size()) return hits;
    const size_t span = size - pat.bytes.size();
    for (size_t i = 0; i <= span; ++i) {
        bool match = true;
        for (size_t j = 0; j < pat.bytes.size(); ++j) {
            if (pat.mask[j] && data[i + j] != pat.bytes[j]) {
                match = false;
                break;
            }
        }
        if (match) hits.push_back(i);
    }
    return hits;
}

// Important caveat for callers: this scans the LIVE in-memory image,
// which means any patches kcdx has already applied (TOML [[patch]] /
// [[hook]] entries that ran during first-update-tick) are visible. An
// AOB that matched the pristine binary may no longer match if the bytes
// it covers were rewritten. Pak Lua callers calling kcdx.memory.scan_pattern
// after game-load are scanning post-patch state — pick patterns that
// don't overlap with any active patch.
std::optional<uintptr_t> ScanModuleFirst(const std::wstring& module_name_wide,
                                         const std::string&  pattern) {
    pe::ModuleView mv;
    if (!pe::OpenModule(module_name_wide.c_str(), mv)) {
        return std::nullopt;
    }
    Pattern pat;
    try {
        pat = ParsePattern(pattern);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const auto sections = pe::ExecutableSections(mv);
    for (const auto& sec : sections) {
        const auto offs = FindAllInBuffer(sec.data, sec.size, pat);
        if (!offs.empty()) {
            return reinterpret_cast<uintptr_t>(sec.data) + offs[0];
        }
    }
    return std::nullopt;
}

namespace {

// Resolve `pattern` against executable sections of `module`. Returns the absolute VA
// of the unique match, or {} if 0 or >1 matches. Logs the match count regardless.
std::optional<uintptr_t> ResolveUniquePatternMatch(const PatchEntry& p,
                                                  const pe::ModuleView& module,
                                                  const Pattern& pat,
                                                  const char* label) {
    auto sections = pe::ExecutableSections(module);
    std::vector<uintptr_t> all;
    for (const auto& sec : sections) {
        auto offs = FindAllInBuffer(sec.data, sec.size, pat);
        for (size_t off : offs) {
            all.push_back(reinterpret_cast<uintptr_t>(sec.data + off));
        }
    }
    log::InfoF("[%s] %s matches: %zu", p.name.c_str(), label, all.size());
    if (all.size() != 1) return std::nullopt;
    return all[0];
}

// Resolve a Tier-3 anchor. On success, fills [beginVA, endVA) with the function
// bounds the patch must fall within (per anchor type semantics).
bool ResolveAnchor(const PatchEntry& p,
                   const pe::ModuleView& module,
                   uintptr_t& beginVA,
                   uintptr_t& endVA) {
    if (std::holds_alternative<std::monostate>(p.anchor)) {
        beginVA = endVA = 0;
        return true;
    }

    if (auto* a = std::get_if<AnchorString>(&p.anchor)) {
        auto rodata = pe::ReadOnlyDataSections(module);
        auto strHits = pe::FindCStringsIn(rodata, a->literal);
        log::InfoF("[%s] anchor_string '%s' matches: %zu",
                   p.name.c_str(), a->literal.c_str(), strHits.size());
        if (strHits.size() != 1) return false;

        auto leaHits = pe::FindLeaXrefsTo(module, strHits[0]);
        log::InfoF("[%s] anchor_string xrefs: %zu", p.name.c_str(), leaHits.size());
        if (leaHits.size() != 1) return false;

        if (!pe::FindFunctionBoundsViaPdata(module, leaHits[0], beginVA, endVA)) {
            log::ErrorF("[%s] anchor_string xref not inside any .pdata function",
                        p.name.c_str());
            return false;
        }
        log::InfoF("[%s] anchor function: [0x%p, 0x%p)",
                   p.name.c_str(),
                   reinterpret_cast<void*>(beginVA),
                   reinterpret_cast<void*>(endVA));
        return true;
    }

    if (auto* a = std::get_if<AnchorFunctionByExport>(&p.anchor)) {
        auto proc = GetProcAddress(module.base, a->name.c_str());
        log::InfoF("[%s] anchor_function_by_export '%s': %s",
                   p.name.c_str(), a->name.c_str(),
                   proc ? "found" : "not found");
        if (!proc) return false;
        if (!pe::FindFunctionBoundsViaPdata(module,
                                            reinterpret_cast<uintptr_t>(proc),
                                            beginVA, endVA)) {
            log::ErrorF("[%s] exported function not in .pdata", p.name.c_str());
            return false;
        }
        return true;
    }

    if (std::holds_alternative<AnchorSymbol>(p.anchor)) {
        // gEnv-chain symbol resolution. Not implemented yet — would need the
        // sig-scan recipe from muyuanjin/DISASSEMBLY.md. Patches that need it
        // can declare it; the engine refuses to apply rather than guessing.
        log::ErrorF("[%s] anchor_symbol resolution not yet implemented",
                    p.name.c_str());
        return false;
    }

    return false;
}

}  // namespace

// Core locator resolution. Read-only — does not write any bytes. Populates
// every field of ResolvedPatch including writeRange and readRanges. Logs the
// per-locator match counts (same lines the previous ApplyPatch used to emit,
// so log behavior is unchanged for the happy path).
ResolvedPatch Resolve(const PatchEntry& p) {
    ResolvedPatch r;

    if (p.original.size() != p.replacement.size()) {
        r.reason = "original/replacement length mismatch";
        log::ErrorF("[%s] aborted: %s (%zu vs %zu)",
                    p.name.c_str(), r.reason.c_str(),
                    p.original.size(), p.replacement.size());
        return r;
    }

    // Locator path 0: pre-resolved VA — the opt-in carrier. Set ONLY by the
    // kcdx.bytes `target = "<name>"` path when the name resolved to a bare VA
    // (engine-seed name or Rva author-target). When present, the WHERE is
    // already known: use it directly and skip pattern / target_symbol /
    // address_id resolution entirely. Mirrors the address_id path exactly —
    // patchAddr = VA + offset, empty patternRange, originalRange == verify
    // range — so a `target=name` site behaves IDENTICALLY to the address_id
    // for that same site. Every existing entry leaves resolvedVa == 0 and
    // falls through to the unchanged paths below.
    if (p.resolvedVa != 0) {
        r.patchAddr = p.resolvedVa + p.offset;
        r.writeRange = { r.patchAddr, r.patchAddr + p.replacement.size() };
        r.patternRange  = { 0, 0 };
        r.originalRange = { r.patchAddr, r.patchAddr + p.original.size() };
        log::InfoF("[%s] resolved target= -> 0x%p (patchAddr 0x%p)",
                   p.name.c_str(),
                   reinterpret_cast<void*>(p.resolvedVa),
                   reinterpret_cast<void*>(r.patchAddr));
        r.ok = true;
        return r;
    }

    // Locator path A: target_symbol — resolves via the global symbol table.
    // Used when a patch wants to write into another plugin's [[trampoline]]
    // region. No module / pattern / context / anchor needed.
    if (!p.targetSymbol.empty()) {
        // Pass the consuming plugin's full (author, plugin) identity so
        // the symbol resolves by the namespace model (alias substitution
        // + self > other precedence + shared warn-once) — a bare
        // target_symbol resolves to this plugin's own export first, an
        // explicit "<author>.<plugin>.<name>" or "<plugin>.<name>"
        // directly (naming-namespaces.md).
        //
        // Step 4 of the 2-dot namespace refactor: PatchEntry now carries
        // pluginAuthor alongside pluginName, threaded in by lua_bind_bytes
        // (kcdx.bytes calls) or by config.cpp's LoadOneFile (TOML [[patch]]
        // rows). When pluginAuthor is empty (the corpus state before
        // step 6) the symbol table walks the legacy 1-dot scope under
        // (pluginName, bareName), identical to today's observable
        // behavior.
        auto addr = symbols::Lookup(p.targetSymbol,
                                    p.pluginAuthor, p.pluginName);
        if (!addr) {
            r.reason = "target_symbol '" + p.targetSymbol + "' not registered "
                       "(producer plugin may be missing or failed to apply)";
            log::ErrorF("[%s] aborted: %s", p.name.c_str(), r.reason.c_str());
            return r;
        }
        r.patchAddr = *addr + p.offset;
        r.writeRange = { r.patchAddr, r.patchAddr + p.replacement.size() };
        // patternRange / originalRange behave as for the pattern path —
        // originalRange tracks what verify will read, patternRange is empty
        // since there was no pattern scan.
        r.patternRange  = { 0, 0 };
        r.originalRange = { r.patchAddr, r.patchAddr + p.original.size() };
        log::InfoF("[%s] resolved target_symbol '%s' -> 0x%p (patchAddr 0x%p)",
                   p.name.c_str(),
                   p.targetSymbol.c_str(),
                   reinterpret_cast<void*>(*addr),
                   reinterpret_cast<void*>(r.patchAddr));
        r.ok = true;
        return r;
    }

    // Locator path A2: address_id — resolves via kcdx's Address Library.
    // Stable across game patches as long as the Library row matches the
    // running game build.
    if (p.addressId != 0) {
        uintptr_t addr = kcdx::address_library::Resolve(p.addressId);
        if (!addr) {
            char idBuf[32];
            snprintf(idBuf, sizeof(idBuf), "%llu",
                     static_cast<unsigned long long>(p.addressId));
            r.reason = "address_id " + std::string(idBuf) +
                       " did not resolve (unknown id, game-version "
                       "mismatch, or row status != verified)";
            log::ErrorF("[%s] aborted: %s", p.name.c_str(), r.reason.c_str());
            return r;
        }
        r.patchAddr = addr + p.offset;
        r.writeRange = { r.patchAddr, r.patchAddr + p.replacement.size() };
        r.patternRange  = { 0, 0 };
        r.originalRange = { r.patchAddr, r.patchAddr + p.original.size() };
        log::InfoF("[%s] resolved address_id %llu -> 0x%p (patchAddr 0x%p)",
                   p.name.c_str(),
                   static_cast<unsigned long long>(p.addressId),
                   reinterpret_cast<void*>(addr),
                   reinterpret_cast<void*>(r.patchAddr));
        r.ok = true;
        return r;
    }

    std::wstring wmod(p.module.begin(), p.module.end());
    pe::ModuleView module;
    if (!pe::OpenModule(wmod.c_str(), module)) {
        r.reason = "module '" + p.module + "' not loaded";
        log::ErrorF("[%s] aborted: %s", p.name.c_str(), r.reason.c_str());
        return r;
    }

    // Tier 1 — required.
    auto t1 = ResolveUniquePatternMatch(p, module, p.pattern, "pattern");
    if (!t1) {
        r.reason = "pattern did not produce exactly 1 match";
        log::ErrorF("[%s] aborted: %s", p.name.c_str(), r.reason.c_str());
        return r;
    }
    r.patchAddr = *t1 + p.offset;
    r.writeRange = { r.patchAddr, r.patchAddr + p.replacement.size() };

    // Tier 1 read footprint: the pattern bytes scanned + the `original`
    // bytes the verify check will compare.
    r.patternRange  = { *t1, *t1 + p.pattern.bytes.size() };
    r.originalRange = { r.patchAddr, r.patchAddr + p.original.size() };

    // Tier 2 — optional context.
    if (p.context) {
        auto t2 = ResolveUniquePatternMatch(p, module, *p.context, "context");
        if (!t2) {
            r.reason = "context did not produce exactly 1 match";
            log::ErrorF("[%s] aborted: %s", p.name.c_str(), r.reason.c_str());
            return r;
        }
        uintptr_t ctxStart = *t2;
        uintptr_t ctxEnd = ctxStart + p.context->bytes.size();
        if (*t1 < ctxStart || (*t1 + p.pattern.bytes.size()) > ctxEnd) {
            r.reason = "locator disagreement: pattern not inside context";
            log::ErrorF("[%s] aborted: locator disagreement — pattern (0x%p) not inside context [0x%p, 0x%p)",
                        p.name.c_str(),
                        reinterpret_cast<void*>(*t1),
                        reinterpret_cast<void*>(ctxStart),
                        reinterpret_cast<void*>(ctxEnd));
            return r;
        }
        r.contextRange = ByteRange{ ctxStart, ctxEnd };
    }

    // Tier 3 — optional anchor.
    if (!std::holds_alternative<std::monostate>(p.anchor)) {
        uintptr_t anchorBegin = 0, anchorEnd = 0;
        if (!ResolveAnchor(p, module, anchorBegin, anchorEnd)) {
            r.reason = "anchor resolution failed";
            log::ErrorF("[%s] aborted: %s", p.name.c_str(), r.reason.c_str());
            return r;
        }
        if (r.patchAddr < anchorBegin || r.patchAddr >= anchorEnd) {
            r.reason = "patch addr outside anchor function";
            log::ErrorF("[%s] aborted: locator disagreement — patch addr (0x%p) outside anchor function [0x%p, 0x%p)",
                        p.name.c_str(),
                        reinterpret_cast<void*>(r.patchAddr),
                        reinterpret_cast<void*>(anchorBegin),
                        reinterpret_cast<void*>(anchorEnd));
            return r;
        }
        uintptr_t dist = (r.patchAddr > anchorBegin) ? (r.patchAddr - anchorBegin)
                                                     : (anchorBegin - r.patchAddr);
        if (dist > p.maxAnchorDistance) {
            r.reason = "patch addr too far from anchor";
            log::ErrorF("[%s] aborted: patch addr too far from anchor (%llu > %u)",
                        p.name.c_str(),
                        static_cast<unsigned long long>(dist),
                        p.maxAnchorDistance);
            return r;
        }
    }

    r.ok = true;
    return r;
}

namespace {

// Verify `original` bytes at addr. Returns:
//   1  — original matches, write should proceed
//   0  — bytes already equal `replacement` (idempotent skip)
//  -1  — neither matches (genuine mismatch)
int VerifyOriginalAtAddr(uintptr_t addr, const PatchEntry& p) {
    auto* siteBytes = reinterpret_cast<const uint8_t*>(addr);
    if (std::memcmp(siteBytes, p.original.data(), p.original.size()) == 0) return 1;
    if (p.idempotent &&
        std::memcmp(siteBytes, p.replacement.data(), p.replacement.size()) == 0) {
        return 0;
    }
    return -1;
}

// Do the actual byte write at addr with VirtualProtect dance. Returns true on
// success. Logs a clear abort line on failure.
bool WriteBytesAtAddr(uintptr_t addr, const PatchEntry& p) {
    DWORD oldProt = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(addr),
                        p.replacement.size(),
                        PAGE_EXECUTE_READWRITE,
                        &oldProt)) {
        log::ErrorF("[%s] aborted: VirtualProtect failed (err=%lu)",
                    p.name.c_str(), GetLastError());
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(addr), p.replacement.data(), p.replacement.size());
    DWORD restoreOld = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(addr), p.replacement.size(), oldProt, &restoreOld);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(addr),
                          p.replacement.size());
    return true;
}

}  // namespace

void PreFlightAll() {
    // Phase 4b.3 refactor: resolution + conflict detection live in
    // conflict_engine now. patch_engine's PreFlightAll is retained as a
    // thin shim so the ApplyAll() entry point (and the Lua-runtime
    // ApplyPatch path) still has a way to populate g_resolved if the
    // unified conflict_engine::RunPreFlight wasn't called externally.
    //
    // In the hooks.cpp orchestration path, conflict_engine::RunPreFlight
    // is called first, which populates conflict_engine::g_resolvedPatches.
    // ApplyAll below reads from that. If for some reason it wasn't called
    // (e.g. tests, or future runtime patches), fall back to resolving here.
    if (conflict_engine::g_resolvedPatches.size() != g_patches.size()) {
        conflict_engine::g_resolvedPatches.clear();
        conflict_engine::g_resolvedPatches.resize(g_patches.size());
        for (size_t i = 0; i < g_patches.size(); ++i) {
            conflict_engine::g_resolvedPatches[i] = Resolve(g_patches[i]);
        }
        log::InfoF("Pre-flight (patch_engine fallback): resolved %zu patch(es) — "
                   "no cross-engine conflict matrix available on this path",
                   g_patches.size());
    }
}

bool ApplyResolvedPatch(PatchEntry& p, const ResolvedPatch& r) {
    // load_order.toml disabled gate. The production orchestration in
    // hooks.cpp filters disabled-plugin entries out of g_applyOrder
    // before reaching this function, but the ldr_notify before_game
    // path, the patch::ApplyAll fallback, and the Lua-runtime
    // ApplyPatch path all reach ApplyResolvedPatch directly — they
    // need their own gate.
    if (!load_order::IsPluginEnabled(p.pluginName)) {
        log::InfoF("[%s] skipping patch '%s' (plugin disabled via load_order.toml)",
                   p.pluginName.c_str(), p.name.c_str());
        return false;
    }

    if (!r.ok) {
        // Resolution failed during pre-flight. Surface conflict context if
        // any was recorded against this patch.
        if (auto* c = conflict_engine::FindWriteOnOriginalAffecting(p.name)) {
            log::WarnF("[%s] note: pre-flight predicted this — earlier entry "
                       "'%s' modified bytes inside this patch's verify range.",
                       p.name.c_str(), c->earlier.name.c_str());
        }
        return false;
    }

    // Verify against the *current* DLL bytes (which may have been modified by
    // earlier writes in this same ApplyAll loop).
    int verdict = VerifyOriginalAtAddr(r.patchAddr, p);

    if (verdict == 0) {
        // Verified idempotent-skip: the replacement bytes are already at the
        // site, so this patch IS effectively applied. Cache the write-range +
        // mark applied so it reports in GetConflictReport (COMP-15) just like
        // a fresh write — an idempotent-skipped bytes-Register entry is a real
        // winner at this VA.
        p.appliedPatchAddr = r.patchAddr;
        p.appliedOK = true;
        log::InfoF("[%s] patch already applied; skipping (site addr 0x%p)",
                   p.name.c_str(), reinterpret_cast<void*>(r.patchAddr));
        // FAULT-TIME TRACE (fail-state-logging.md / finding #15): a byte
        // patch made INVISIBLE to the crash-guard modification inventory left
        // a crash at/after a byte-patched site with no owner record. An
        // idempotent-skip is STILL a live byte mod at this VA (the replacement
        // bytes are present, regardless of whether THIS apply wrote them), so
        // it belongs in the inventory. `p.name.c_str()` is process-lifetime
        // stable: the PatchEntry lives either in patch::g_patches (sorted once
        // before apply, never push_back'd after — interfaces.cpp's
        // GetConflictReport already borrows these c_str()s for process life)
        // or in the lua_registry deque-backed shared_ptr (append-only,
        // node-stable, never freed — patch_engine.h AppliedBytesPatch lifetime
        // note). Idempotent per (va, category) inside RegisterModification, so
        // a re-apply on the load path does not double-count.
        modification_inventory::RegisterModification(
            r.patchAddr, modification_inventory::Category::Bytes,
            p.name.c_str());
        return true;
    }
    if (verdict == -1) {
        auto* siteBytes = reinterpret_cast<const uint8_t*>(r.patchAddr);
        std::string actual = HexBytes(siteBytes, p.original.size());
        std::string expected = HexBytes(p.original);
        log::ErrorF("[%s] aborted: bytes at patch site don't match expected original (got %s, expected %s)",
                    p.name.c_str(), actual.c_str(), expected.c_str());
        // If another loaded patch's `replacement` matches the bytes we found,
        // call it out by name — most user-friendly explanation.
        for (const auto& other : g_patches) {
            if (other.name == p.name) continue;
            if (other.replacement.size() != p.original.size()) continue;
            if (std::memcmp(siteBytes, other.replacement.data(), other.replacement.size()) == 0) {
                log::WarnF("[%s] the bytes currently at this site (%s) match plugin '%s' replacement bytes — "
                           "that mod probably patched this same location first.",
                           p.name.c_str(), actual.c_str(), other.name.c_str());
                break;
            }
        }
        // Enriched diagnostic: did a conflict_engine pre-flight pass
        // predict this verify-failure? If so, surface which other entry
        // (patch or hook) is responsible.
        if (auto* c = conflict_engine::FindWriteOnOriginalAffecting(p.name)) {
            log::Warn(std::string("[") + p.name + "] note: " +
                      "earlier entry '" + c->earlier.name +
                      "' modified bytes inside this patch's verify range "
                      "(see Conflict engine WARN above for details).");
        }
        return false;
    }

    if (g_dryRun) {
        // dry_run returns "applied" without writing (test-only path). Cache
        // the would-be write-range + mark applied so the conflict report
        // treats it as a participant — consistent with the true return.
        p.appliedPatchAddr = r.patchAddr;
        p.appliedOK = true;
        log::InfoF("[%s] dry_run: would write %s at 0x%p (skipped)",
                   p.name.c_str(),
                   HexBytes(p.replacement).c_str(),
                   reinterpret_cast<void*>(r.patchAddr));
        return true;
    }

    if (!WriteBytesAtAddr(r.patchAddr, p)) {
        return false;
    }

    // Successful write — cache the final write-range + mark applied. For the
    // bytes-Register path (ApplyBytesEntry) this is the ONLY appliedOK write;
    // for the legacy g_patches path the callers also assign appliedOK post-
    // call (now redundant). appliedPatchAddr stays 0 on every !ok return
    // above, so an unapplied entry can never false-match a target query.
    p.appliedPatchAddr = r.patchAddr;
    p.appliedOK = true;

    log::InfoF("[%s] applied successfully at 0x%p: %s -> %s",
               p.name.c_str(),
               reinterpret_cast<void*>(r.patchAddr),
               HexBytes(p.original).c_str(),
               HexBytes(p.replacement).c_str());

    // FAULT-TIME TRACE (fail-state-logging.md / finding #15): record this
    // byte mod in the crash-guard inventory so a crash at/after the patched
    // VA is attributable to its owner. `p.name.c_str()` is process-lifetime
    // stable (see the idempotent-skip path above for the lifetime proof).
    modification_inventory::RegisterModification(
        r.patchAddr, modification_inventory::Category::Bytes,
        p.name.c_str());

    // If this patch's write landed on bytes a previous plugin already wrote,
    // log the clobber so the user can see exactly what happened. conflict_engine
    // already emitted the human-readable Explain() at pre-flight time; here we
    // just emit a brief reminder at the moment of write so the log reads
    // chronologically.
    auto clobbers = conflict_engine::FindWriteOnWriteAffecting(p.name);
    for (const auto* c : clobbers) {
        log::InfoF("[%s] note: this write landed on top of earlier writer '%s' "
                   "(see Conflict engine log line above for full explanation).",
                   p.name.c_str(), c->earlier.name.c_str());
    }
    return true;
}

bool ApplyPatch(PatchEntry& p) {
    // Lua-runtime entry point: no pre-flight context, re-resolve against the
    // current DLL state. Patches called this way do NOT benefit from the
    // "incidental overlap is OK" property of pre-flight; if an earlier TOML
    // patch wrote into this patch's pattern bytes, this resolve will fail.
    ResolvedPatch r = Resolve(p);
    return ApplyResolvedPatch(p, r);
}

void ApplyAll() {
    // PreFlightAll is a no-op shim if conflict_engine::RunPreFlight already
    // ran (the normal path from hooks.cpp). It re-resolves only when called
    // outside that orchestration (e.g., tests). Either way, when we exit
    // PreFlightAll, conflict_engine::g_resolvedPatches is sized and populated.
    PreFlightAll();

    log::InfoF("Applying %zu patch(es)%s",
               g_patches.size(),
               g_dryRun ? " [dry_run=true]" : "");
    size_t ok = 0, fail = 0;
    for (size_t i = 0; i < g_patches.size(); ++i) {
        if (ApplyResolvedPatch(g_patches[i], conflict_engine::g_resolvedPatches[i])) ++ok;
        else ++fail;
    }
    log::InfoF("Patch summary: %zu applied, %zu aborted", ok, fail);
}

std::vector<AppliedBytesPatch> GetAppliedBytesPatchesAtTarget(uintptr_t targetVa) {
    std::vector<AppliedBytesPatch> out;
    // Enumerate the registry's Kind::Bytes entries. The registry hands us the
    // const Entry&; WE own PatchEntry, so WE do the payload cast (DECISION A:
    // registry payload-agnostic, patch engine interprets, interfaces.cpp
    // blind). The whole walk runs under the registry mutex inside
    // ForEachEntryOfKind, so the callback stays a cheap read-only test and
    // never re-enters the registry.
    kcdx::lua_registry::ForEachEntryOfKind(
        kcdx::lua_registry::Kind::Bytes,
        [&](const kcdx::lua_registry::Entry& e) {
            const auto* p =
                std::static_pointer_cast<PatchEntry>(e.payload).get();
            if (!p) return;  // defensive — a Bytes payload is always a PatchEntry
            // An unapplied entry has appliedPatchAddr == 0; its [0, size)
            // range can't contain a real targetVa, so it self-skips here
            // (mirrors the legacy g_patches loop skipping resolve-failures).
            uintptr_t begin = p->appliedPatchAddr;
            uintptr_t end   = begin + p->replacement.size();
            if (targetVa >= begin && targetVa < end) {
                out.push_back({ p->name.c_str(), p->priority, p->appliedOK });
            }
        });
    return out;
}

}  // namespace kcdx::patch
