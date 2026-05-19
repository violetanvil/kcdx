#include "patch_engine.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "log.h"
#include "pe_helpers.h"
#include "symbols.h"

namespace kcdx::patch {

std::vector<PatchEntry> g_patches;
std::vector<Conflict>   g_conflicts;
bool g_dryRun = false;

// Pre-flight resolutions, parallel to g_patches by index. Populated by
// PreFlightAll(); consumed by ApplyAll() so plugins benefit from the
// "incidental overlap is OK" property of working against the pristine DLL.
static std::vector<ResolvedPatch> g_resolved;

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

    // Locator path A: target_symbol — resolves via the global symbol table.
    // Used when a patch wants to write into another plugin's [[trampoline]]
    // region. No module / pattern / context / anchor needed.
    if (!p.targetSymbol.empty()) {
        auto addr = symbols::Lookup(p.targetSymbol);
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

// Look up the FIRST write-on-original conflict where the named patch is the
// reader. Used to enrich the reader's failure message when its verify fails.
const Conflict* FindWriteOnOriginalReader(const std::string& readerName) {
    for (const auto& c : g_conflicts) {
        if (c.kind == ConflictKind::WriteOnOriginal && c.readerName == readerName) {
            return &c;
        }
    }
    return nullptr;
}

// Look up write-on-write conflicts where the named patch is the WRITER
// landing on bytes a previous plugin already wrote. Used to log "you just
// clobbered plugin X's bytes" at the point of write.
std::vector<const Conflict*> FindWriteOnWriteForLaterWriter(const std::string& laterWriterName) {
    std::vector<const Conflict*> out;
    for (const auto& c : g_conflicts) {
        if (c.kind != ConflictKind::WriteOnOriginal && c.readerName == laterWriterName) {
            out.push_back(&c);
        }
    }
    return out;
}

// Plain-English explanation, addressed to a player reading the log to figure
// out which mod is interacting with which other mod.
std::string ConflictExplanation(const Conflict& c) {
    char buf[640];
    switch (c.kind) {
        case ConflictKind::WriteOnOriginal:
            snprintf(buf, sizeof(buf),
                     "Plugin '%s' modified bytes that plugin '%s' needs to verify before patching "
                     "(overlap at 0x%p..0x%p). The earlier mod stopped the later one from applying. "
                     "Try removing or reordering one of them.",
                     c.writerName.c_str(), c.readerName.c_str(),
                     reinterpret_cast<void*>(c.overlap.begin),
                     reinterpret_cast<void*>(c.overlap.end));
            break;
        case ConflictKind::WriteOnWriteFull:
            snprintf(buf, sizeof(buf),
                     "Plugin '%s' fully overwrote bytes already written by plugin '%s' "
                     "(at 0x%p..0x%p). Both mods applied; '%s' wins because it ran later. "
                     "If you wanted plugin '%s' to take effect at this address instead, "
                     "give it a lower 'priority' number in its kcdx.toml.",
                     c.readerName.c_str(), c.writerName.c_str(),
                     reinterpret_cast<void*>(c.overlap.begin),
                     reinterpret_cast<void*>(c.overlap.end),
                     c.readerName.c_str(),
                     c.writerName.c_str());
            break;
        case ConflictKind::WriteOnWritePartial:
            snprintf(buf, sizeof(buf),
                     "Plugin '%s' partially overwrote bytes already written by plugin '%s' "
                     "(overlap at 0x%p..0x%p). Both mods applied, but the result is a MIX of "
                     "their bytes -- this may produce invalid instructions and crash the game. "
                     "If the game becomes unstable, remove one of the two conflicting mods.",
                     c.readerName.c_str(), c.writerName.c_str(),
                     reinterpret_cast<void*>(c.overlap.begin),
                     reinterpret_cast<void*>(c.overlap.end));
            break;
    }
    return std::string(buf);
}

// Compute intersection of two ByteRanges. Caller must verify they overlap.
ByteRange Intersect(const ByteRange& a, const ByteRange& b) {
    return ByteRange{ std::max(a.begin, b.begin), std::min(a.end, b.end) };
}

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
    g_conflicts.clear();
    g_resolved.clear();
    g_resolved.resize(g_patches.size());

    log::InfoF("Pre-flight: resolving %zu patch(es) against pristine module...",
               g_patches.size());

    for (size_t i = 0; i < g_patches.size(); ++i) {
        g_resolved[i] = Resolve(g_patches[i]);
    }

    // Pairwise overlap check. g_patches is sorted by (priority, name) at
    // config load time, so for any pair (i, j) with i < j, patch i applies
    // first and is the "writer" relative to patch j's view.
    for (size_t i = 0; i < g_patches.size(); ++i) {
        if (!g_resolved[i].ok) continue;
        const auto& w = g_resolved[i];
        const auto& wp = g_patches[i];

        for (size_t j = i + 1; j < g_patches.size(); ++j) {
            if (!g_resolved[j].ok) continue;
            const auto& r = g_resolved[j];
            const auto& rp = g_patches[j];

            // (a) Write-on-original: writer's bytes overlap reader's verify
            //     target. The reader will fail its verify check at apply time.
            if (w.writeRange.overlaps(r.originalRange)) {
                Conflict c;
                c.kind = ConflictKind::WriteOnOriginal;
                c.writerName = wp.name;
                c.readerName = rp.name;
                c.overlap = Intersect(w.writeRange, r.originalRange);
                g_conflicts.push_back(c);
                log::Warn(ConflictExplanation(c));
                continue;  // don't also report write-on-write for this pair
            }

            // (b) Write-on-write: both patches target the same bytes. The
            //     later one will land its full write on top of the earlier
            //     one's bytes. Categorize as full (write ranges identical)
            //     or partial (overlapping but not equal).
            if (w.writeRange.overlaps(r.writeRange)) {
                bool fullOverlap = (w.writeRange.begin == r.writeRange.begin &&
                                    w.writeRange.end   == r.writeRange.end);
                Conflict c;
                c.kind = fullOverlap ? ConflictKind::WriteOnWriteFull
                                     : ConflictKind::WriteOnWritePartial;
                c.writerName = wp.name;
                c.readerName = rp.name;
                c.overlap = Intersect(w.writeRange, r.writeRange);
                g_conflicts.push_back(c);
                if (fullOverlap) {
                    log::Info(ConflictExplanation(c));
                } else {
                    log::Warn(ConflictExplanation(c));
                }
                continue;
            }

            // (c) Pattern / context overlap is INCIDENTAL. Pre-flight resolved
            //     the reader against the pristine DLL, so its patchAddr is
            //     correct regardless of subsequent writes. No log entry.
        }
    }

    if (g_conflicts.empty()) {
        log::Info("Pre-flight: no conflicts detected");
    } else {
        size_t wOnO = 0, wOnW = 0;
        for (const auto& c : g_conflicts) {
            if (c.kind == ConflictKind::WriteOnOriginal) ++wOnO;
            else ++wOnW;
        }
        log::InfoF("Pre-flight: %zu conflict(s) recorded (%zu write-on-original, %zu write-on-write).",
                   g_conflicts.size(), wOnO, wOnW);
    }
}

bool ApplyResolvedPatch(const PatchEntry& p, const ResolvedPatch& r) {
    if (!r.ok) {
        // Resolution failed during pre-flight. Surface conflict context if
        // any was recorded against this patch.
        if (const Conflict* c = FindWriteOnOriginalReader(p.name)) {
            log::Warn(std::string("[") + p.name + "] " + ConflictExplanation(*c));
        }
        return false;
    }

    // Verify against the *current* DLL bytes (which may have been modified by
    // earlier writes in this same ApplyAll loop).
    int verdict = VerifyOriginalAtAddr(r.patchAddr, p);

    if (verdict == 0) {
        log::InfoF("[%s] patch already applied; skipping (site addr 0x%p)",
                   p.name.c_str(), reinterpret_cast<void*>(r.patchAddr));
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
        if (const Conflict* c = FindWriteOnOriginalReader(p.name)) {
            log::Warn(std::string("[") + p.name + "] " + ConflictExplanation(*c));
        }
        return false;
    }

    if (g_dryRun) {
        log::InfoF("[%s] dry_run: would write %s at 0x%p (skipped)",
                   p.name.c_str(),
                   HexBytes(p.replacement).c_str(),
                   reinterpret_cast<void*>(r.patchAddr));
        return true;
    }

    if (!WriteBytesAtAddr(r.patchAddr, p)) {
        return false;
    }

    log::InfoF("[%s] applied successfully at 0x%p: %s -> %s",
               p.name.c_str(),
               reinterpret_cast<void*>(r.patchAddr),
               HexBytes(p.original).c_str(),
               HexBytes(p.replacement).c_str());

    // If this patch's write landed on bytes a previous plugin already wrote,
    // log the clobber so the user can see exactly what happened.
    auto clobbers = FindWriteOnWriteForLaterWriter(p.name);
    for (const Conflict* c : clobbers) {
        if (c->kind == ConflictKind::WriteOnWriteFull) {
            log::Info(ConflictExplanation(*c));
        } else {
            log::Warn(ConflictExplanation(*c));
        }
    }
    return true;
}

bool ApplyPatch(const PatchEntry& p) {
    // Lua-runtime entry point: no pre-flight context, re-resolve against the
    // current DLL state. Patches called this way do NOT benefit from the
    // "incidental overlap is OK" property of pre-flight; if an earlier TOML
    // patch wrote into this patch's pattern bytes, this resolve will fail.
    ResolvedPatch r = Resolve(p);
    return ApplyResolvedPatch(p, r);
}

void ApplyAll() {
    PreFlightAll();

    log::InfoF("Applying %zu patch(es)%s",
               g_patches.size(),
               g_dryRun ? " [dry_run=true]" : "");
    size_t ok = 0, fail = 0;
    for (size_t i = 0; i < g_patches.size(); ++i) {
        if (ApplyResolvedPatch(g_patches[i], g_resolved[i])) ++ok;
        else ++fail;
    }
    log::InfoF("Patch summary: %zu applied, %zu aborted", ok, fail);
}

}  // namespace kcdx::patch
