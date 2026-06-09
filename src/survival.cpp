#include "survival.h"

#include <windows.h>

#include <algorithm>   // std::max
#include <cstdio>      // FILE i/o
#include <cstdlib>     // strtoull
#include <cstring>     // memcmp
#include <filesystem>  // std::filesystem::path
#include <string>
#include <unordered_map>
#include <vector>

#include "blake3.h"
#include "log.h"
#include "patch_engine.h"  // patch::Pattern + FindAllInBuffer (the wildcard AOB matcher — G3 reuse)
#include "paths.h"     // ToUtf8 (path -> std::string under C++20+ char8_t)
#include "pe_helpers.h"

namespace kcdx::survival {

namespace {

const char* kCategory = "SURVIVAL";

// Encode a raw digest as lowercase hex (for log lines that show the mismatch).
std::string ToHex(const uint8_t* p, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(kHex[(p[i] >> 4) & 0xF]);
        out.push_back(kHex[p[i] & 0xF]);
    }
    return out;
}

// Build a CannotCheck result with the given reason token (already logged).
Result CannotCheck(const char* reason) {
    Result r;
    r.status = Status::CannotCheck;
    r.reason = reason;
    return r;
}

// Resolve WHGame.dll's on-disk path. Returns false if the module is not loaded
// (handle null) or the path could not be retrieved.
bool ResolveModulePath(std::wstring& pathOut, bool& moduleLoaded) {
    moduleLoaded = false;
    HMODULE h = GetModuleHandleW(L"WHGame.dll");
    if (!h) {
        return false;  // not mapped
    }
    moduleLoaded = true;

    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(h, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return false;  // path not resolvable / truncated
    }
    pathOut.assign(buf, n);
    return true;
}

// Read the whole on-disk file into `out`. Returns false on open/read failure.
bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hf, &size) || size.QuadPart <= 0) {
        CloseHandle(hf);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));

    size_t total = 0;
    while (total < out.size()) {
        DWORD want = static_cast<DWORD>(
            (out.size() - total) > 0x40000000u ? 0x40000000u
                                                : (out.size() - total));
        DWORD got = 0;
        if (!ReadFile(hf, out.data() + total, want, &got, nullptr) || got == 0) {
            CloseHandle(hf);
            return false;
        }
        total += got;
    }
    CloseHandle(hf);
    return true;
}

const char* KindName(Kind k) {
    switch (k) {
        case Kind::Function:           return "function";
        case Kind::FunctionNoSig:      return "function_no_sig";
        case Kind::FunctionVariadic:   return "function_variadic";
        case Kind::Callsite:           return "callsite";
        case Kind::StringAnchor:       return "string_anchor";
        case Kind::InstructionAnchor:  return "instruction_anchor";
        case Kind::DataSlot:           return "data_slot";
        case Kind::VtableBase:         return "vtable_base";
        case Kind::VtableIndex:        return "vtable_index";
    }
    return "unknown";
}

// Build the kind's Result helpers for a definite (non-CannotCheck) verdict.
Result Unchanged() { Result r; r.status = Status::Unchanged; return r; }
Result Changed()   { Result r; r.status = Status::Changed;   return r; }
Result Ambiguous() { Result r; r.status = Status::Ambiguous; return r; }

// Read WHGame.dll's ON-DISK backing file into `out` (D25 — the static checks
// read the on-disk file, never live memory; the recorded survival data was
// produced from the on-disk bytes). Returns a CannotCheck reason token on any
// failure (already logged), or nullptr on success. The `dll` selector is
// reserved for the per-module read step (3.3) — today every check reads
// WHGame.dll's on-disk file, exactly like the function-hash path.
const char* ReadOnDiskModule(std::vector<uint8_t>& out) {
    std::wstring modulePath;
    bool moduleLoaded = false;
    if (!ResolveModulePath(modulePath, moduleLoaded)) {
        if (!moduleLoaded) {
            LOG_ERROR_KV(kCategory, "module_not_mapped",
                ::kcdx::log::KV("reason", "module_not_mapped"),
                ::kcdx::log::KV("module", "WHGame.dll"));
            return "module_not_mapped";
        }
        LOG_ERROR_KV(kCategory, "file_not_found",
            ::kcdx::log::KV("reason", "file_not_found"),
            ::kcdx::log::KV("module", "WHGame.dll"));
        return "file_not_found";
    }
    if (!ReadWholeFile(modulePath, out)) {
        LOG_ERROR_KV(kCategory, "file_open_error",
            ::kcdx::log::KV("reason", "file_open_error"),
            ::kcdx::log::KV("module", "WHGame.dll"));
        return "file_open_error";
    }
    return nullptr;
}

// Build a patch::Pattern from a Payload's aob bytes + mask. The mask vector is
// 1=match / 0=wildcard (parallel to aob); patch::Pattern wants a bool mask
// (true=literal). An empty mask means "every byte literal".
patch::Pattern PatternFromPayload(const Payload& p) {
    patch::Pattern pat;
    pat.bytes = p.aob;
    pat.mask.resize(p.aob.size(), true);
    for (size_t i = 0; i < p.aob.size() && i < p.aobMask.size(); ++i) {
        pat.mask[i] = (p.aobMask[i] != 0);
    }
    return pat;
}

// Scan an on-disk executable (.text-class) section set for `pat`; return the
// RVA of every match. Reuses patch::FindAllInBuffer (the SAME wildcard matcher
// the live scan path uses — G3 confirmed reuse, patch::Pattern carries the
// mask).
std::vector<uint32_t> ScanOnDiskTextForPattern(const std::vector<uint8_t>& file,
                                               const patch::Pattern& pat) {
    std::vector<uint32_t> hits;
    if (pat.bytes.empty()) return hits;
    auto sections = pe::OnDiskExecutableSections(file.data(), file.size());
    for (const auto& sec : sections) {
        auto offs = patch::FindAllInBuffer(sec.data, sec.size, pat);
        for (size_t off : offs) hits.push_back(sec.rva + static_cast<uint32_t>(off));
    }
    return hits;
}

// Count on-disk .text 7-byte `lea r64,[rip+disp32]` xrefs that target `targetRva`.
// The on-disk analogue of pe::FindLeaXrefsTo (which takes a live ModuleView).
// Used for the string_anchor unique-xref assertion.
size_t CountOnDiskLeaXrefsTo(const std::vector<uint8_t>& file, uint32_t targetRva) {
    size_t count = 0;
    auto sections = pe::OnDiskExecutableSections(file.data(), file.size());
    for (const auto& sec : sections) {
        if (sec.size < 7) continue;
        for (size_t i = 0; i + 7 <= sec.size; ++i) {
            if (sec.data[i] != 0x48 || sec.data[i + 1] != 0x8D) continue;
            uint8_t modrm = sec.data[i + 2];
            if ((modrm & 0xC7) != 0x05) continue;  // mod=00, rm=101 → rip-relative
            int32_t rel = static_cast<int32_t>(
                sec.data[i + 3] | (sec.data[i + 4] << 8) |
                (sec.data[i + 5] << 16) | (sec.data[i + 6] << 24));
            int64_t instrEndRva = static_cast<int64_t>(sec.rva) + i + 7;
            int64_t targetOfThis = instrEndRva + rel;
            if (targetOfThis == static_cast<int64_t>(targetRva)) ++count;
        }
    }
    return count;
}

// Find the on-disk .rdata RVA of a null-terminated literal. Returns the count
// of occurrences via `count`, and the first occurrence's RVA via the return
// (0 + count==0 when absent). Searches the on-disk read-only-data sections (the
// on-disk analogue of pe::FindCStringsIn over a live ReadOnlyDataSections set).
uint32_t FindOnDiskRdataString(const std::vector<uint8_t>& file,
                               const std::string& literal, size_t& count) {
    count = 0;
    uint32_t firstRva = 0;
    if (literal.empty()) return 0;
    auto sections = pe::OnDiskReadOnlyDataSections(file.data(), file.size());
    const size_t litLen = literal.size();
    for (const auto& sec : sections) {
        if (sec.size < litLen + 1) continue;
        const size_t span = sec.size - litLen;
        for (size_t i = 0; i <= span; ++i) {
            if (sec.data[i] != static_cast<uint8_t>(literal[0])) continue;
            if (std::memcmp(sec.data + i, literal.data(), litLen) != 0) continue;
            // Must be null-terminated (exact-string match).
            if (i + litLen < sec.size && sec.data[i + litLen] != 0) continue;
            if (count == 0) firstRva = sec.rva + static_cast<uint32_t>(i);
            ++count;
        }
    }
    return firstRva;
}

// Is `rva` inside an on-disk writable-data (.data-class) section? The data_slot
// "did the derivation land in .data" check.
bool IsRvaInOnDiskData(const std::vector<uint8_t>& file, uint32_t rva) {
    auto sections = pe::OnDiskWritableDataSections(file.data(), file.size());
    for (const auto& sec : sections) {
        // Bound by the in-memory extent — VirtualSize where it exceeds the raw
        // extent (a .data slot can sit in the zero-initialized tail beyond
        // SizeOfRawData). Mirrors IsRvaInExecutableSection's
        // max(VirtualSize, SizeOfRawData) discipline.
        uint64_t extent = std::max<uint64_t>(sec.size, sec.virtualSize);
        uint64_t end = static_cast<uint64_t>(sec.rva) + extent;
        if (rva >= sec.rva && rva < end) return true;
    }
    return false;
}

// Parse a data_slot `rule` descriptor. Two forms today (fingerprint-per-kind.md
// §data_slot):
//   "disp32@<kid>"  — follow the disp32 from the anchor instruction.
//   "<kid>±0xNN"    — the anchor RVA plus/minus a fixed offset.
// The <kid> part is the anchor's identity; the survival check uses the anchor's
// already-RESOLVED rva (threaded in via Payload.anchorResolvedRva), so the <kid>
// is informational here — what this parses is the FORM (disp32-follow vs
// offset) and, for the offset form, the signed offset.
enum class RuleForm { Disp32Follow, Offset, Bad };
struct ParsedRule {
    RuleForm form = RuleForm::Bad;
    int64_t  offset = 0;  // for Offset form (signed).
};
ParsedRule ParseDataSlotRule(const std::string& rule) {
    ParsedRule pr;
    if (rule.rfind("disp32@", 0) == 0) {
        pr.form = RuleForm::Disp32Follow;
        return pr;
    }
    // "<kid><sign>0xNN" — find the +/- after the leading id digits.
    size_t sign = std::string::npos;
    for (size_t i = 1; i < rule.size(); ++i) {  // start at 1: id is at least one digit
        if (rule[i] == '+' || rule[i] == '-') { sign = i; break; }
    }
    if (sign == std::string::npos) return pr;  // RuleForm::Bad
    const std::string offStr = rule.substr(sign + 1);  // "0xA8" / "0x50"
    if (offStr.empty()) return pr;
    char* end = nullptr;
    unsigned long long mag = std::strtoull(offStr.c_str(), &end, 0);
    if (end == offStr.c_str() || (end && *end != '\0')) return pr;  // not fully numeric
    pr.form = RuleForm::Offset;
    pr.offset = (rule[sign] == '-') ? -static_cast<int64_t>(mag)
                                    : static_cast<int64_t>(mag);
    return pr;
}

// ---------------------------------------------------------------------------
// THE 5 STATIC NON-FUNCTION PER-KIND CHECKS (D25 — all on-disk).
// Each takes the already-read on-disk file buffer + the Payload + the stored
// rva, and returns a DEFINED verdict (fail-loud: Changed / Ambiguous on a real
// negative, CannotCheck-with-reason on a missing datum, never a false Unchanged
// or a silent empty).

// callsite — re-match the stored AOB in on-disk .text.
//   unique hit  → Unchanged (the site survives; relocated to the hit RVA).
//   zero hits   → Changed   (the site is gone).
//   multiple    → Ambiguous (no longer a unique locator).
Result CheckCallsite(const std::vector<uint8_t>& file, const Payload& p, uint32_t rva) {
    if (p.aob.empty()) {
        LOG_WARN_KV(kCategory, "no_aob",
            ::kcdx::log::KV("reason", "no_aob"),
            ::kcdx::log::KV("kind", "callsite"),
            ::kcdx::log::KV("rva", (unsigned long long)rva));
        return CannotCheck("no_aob");
    }
    patch::Pattern pat = PatternFromPayload(p);
    std::vector<uint32_t> hits = ScanOnDiskTextForPattern(file, pat);
    if (hits.size() == 1) {
        LOG_DEBUG_KV(kCategory, "callsite_unchanged",
            ::kcdx::log::KV("stored_rva", (unsigned long long)rva),
            ::kcdx::log::KV("hit_rva", (unsigned long long)hits[0]));
        return Unchanged();
    }
    if (hits.empty()) {
        LOG_WARN_KV(kCategory, "callsite_changed",
            ::kcdx::log::KV("kind", "callsite"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("note", "AOB no longer occurs in .text (site gone)"));
        return Changed();
    }
    LOG_WARN_KV(kCategory, "callsite_ambiguous",
        ::kcdx::log::KV("kind", "callsite"),
        ::kcdx::log::KV("rva", (unsigned long long)rva),
        ::kcdx::log::KV("hit_count", (unsigned long long)hits.size()),
        ::kcdx::log::KV("note", "AOB matches >1 .text site — no longer a unique locator; extend the pattern"));
    return Ambiguous();
}

// string_anchor — confirm the literal is present in on-disk .rdata; if
// expectUnique, also confirm exactly one .text LEA references it.
//   present (+ unique xref if asserted) → Unchanged.
//   absent                              → Changed (the anchor is gone).
//   present but wrong xref count        → Ambiguous (the unique property broke).
//
// FLAGGED edge case (spec silent): a present literal with expectUnique whose
// .text xref count is NOT 1. The spec (fingerprint-per-kind.md §string_anchor)
// says "absent → Changed" and "if expect_unique, also confirm exactly one xref"
// but does not pin the verdict when the literal is present yet the xref count is
// wrong. Per the brief's fail-toward-CannotCheck/Changed posture and the
// design's Ambiguous semantics (a locator that no longer resolves UNIQUELY), a
// broken-uniqueness signal is the textbook Ambiguous — chosen over Changed
// (the bytes ARE present) and over Unchanged (the unique property the resolver
// depends on broke). SURFACED in the report.
Result CheckStringAnchor(const std::vector<uint8_t>& file, const Payload& p, uint32_t rva) {
    if (p.anchorString.empty()) {
        LOG_WARN_KV(kCategory, "no_anchor_string",
            ::kcdx::log::KV("reason", "no_anchor_string"),
            ::kcdx::log::KV("kind", "string_anchor"),
            ::kcdx::log::KV("rva", (unsigned long long)rva));
        return CannotCheck("no_anchor_string");
    }
    size_t presentCount = 0;
    uint32_t strRva = FindOnDiskRdataString(file, p.anchorString, presentCount);
    if (presentCount == 0) {
        LOG_WARN_KV(kCategory, "string_anchor_changed",
            ::kcdx::log::KV("kind", "string_anchor"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("note", "literal absent from .rdata — anchor gone; dependents un-derivable"));
        return Changed();
    }
    if (!p.expectUnique) {
        LOG_DEBUG_KV(kCategory, "string_anchor_unchanged",
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("present_count", (unsigned long long)presentCount));
        return Unchanged();
    }
    // expectUnique: the literal must be present AND singly LEA-xref'd from .text.
    size_t xrefs = CountOnDiskLeaXrefsTo(file, strRva);
    if (xrefs == 1) {
        LOG_DEBUG_KV(kCategory, "string_anchor_unchanged",
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("str_rva", (unsigned long long)strRva),
            ::kcdx::log::KV("xrefs", (unsigned long long)xrefs));
        return Unchanged();
    }
    LOG_WARN_KV(kCategory, "string_anchor_ambiguous",
        ::kcdx::log::KV("kind", "string_anchor"),
        ::kcdx::log::KV("rva", (unsigned long long)rva),
        ::kcdx::log::KV("xrefs", (unsigned long long)xrefs),
        ::kcdx::log::KV("note", "expect_unique asserted but .text xref count != 1 — unique property broke"));
    return Ambiguous();
}

// instruction_anchor — re-run the resolver chain: find the .rdata string anchor,
// scan .text for the LEA whose RIP-relative target == the string, then confirm
// the resolved site matches the stored instruction-shape AOB. Depends on its
// string anchor (the DAG) — the ordered walk threads the anchor verdict in.
//   chain completes + shape matches → Unchanged.
//   string absent / no unique LEA / shape mismatch → Changed.
//   ambiguous string (multi .rdata hit) or multi-LEA → Ambiguous.
Result CheckInstructionAnchor(const std::vector<uint8_t>& file, const Payload& p, uint32_t rva) {
    if (p.anchorString.empty()) {
        LOG_WARN_KV(kCategory, "no_anchor_string",
            ::kcdx::log::KV("reason", "no_anchor_string"),
            ::kcdx::log::KV("kind", "instruction_anchor"),
            ::kcdx::log::KV("rva", (unsigned long long)rva));
        return CannotCheck("no_anchor_string");
    }
    // (1) find the .rdata string anchor (require unique presence).
    size_t strCount = 0;
    uint32_t strRva = FindOnDiskRdataString(file, p.anchorString, strCount);
    if (strCount == 0) {
        LOG_WARN_KV(kCategory, "instruction_anchor_changed",
            ::kcdx::log::KV("kind", "instruction_anchor"),
            ::kcdx::log::KV("note", "string anchor absent from .rdata — chain broken"));
        return Changed();
    }
    if (strCount > 1) {
        LOG_WARN_KV(kCategory, "instruction_anchor_ambiguous",
            ::kcdx::log::KV("kind", "instruction_anchor"),
            ::kcdx::log::KV("str_count", (unsigned long long)strCount),
            ::kcdx::log::KV("note", "string anchor matches >1 .rdata site"));
        return Ambiguous();
    }
    // (2) scan .text for the LEA whose RIP-relative disp32 targets the string.
    //     The instruction_anchor is the LEA that loads the string's address.
    //     Reuse the on-disk LEA-xref count to find the unique referencing site.
    std::vector<uint32_t> leaRvas;
    {
        auto sections = pe::OnDiskExecutableSections(file.data(), file.size());
        for (const auto& sec : sections) {
            if (sec.size < 7) continue;
            for (size_t i = 0; i + 7 <= sec.size; ++i) {
                if (sec.data[i] != 0x48 || sec.data[i + 1] != 0x8D) continue;
                uint8_t modrm = sec.data[i + 2];
                if ((modrm & 0xC7) != 0x05) continue;
                int32_t rel = static_cast<int32_t>(
                    sec.data[i + 3] | (sec.data[i + 4] << 8) |
                    (sec.data[i + 5] << 16) | (sec.data[i + 6] << 24));
                int64_t instrEndRva = static_cast<int64_t>(sec.rva) + i + 7;
                if (instrEndRva + rel == static_cast<int64_t>(strRva)) {
                    leaRvas.push_back(sec.rva + static_cast<uint32_t>(i));
                }
            }
        }
    }
    if (leaRvas.empty()) {
        LOG_WARN_KV(kCategory, "instruction_anchor_changed",
            ::kcdx::log::KV("kind", "instruction_anchor"),
            ::kcdx::log::KV("note", "no .text LEA references the string — chain broken"));
        return Changed();
    }
    if (leaRvas.size() > 1) {
        LOG_WARN_KV(kCategory, "instruction_anchor_ambiguous",
            ::kcdx::log::KV("kind", "instruction_anchor"),
            ::kcdx::log::KV("lea_count", (unsigned long long)leaRvas.size()),
            ::kcdx::log::KV("note", ">1 .text LEA references the string — not a unique anchor"));
        return Ambiguous();
    }
    // (3) confirm the final instruction-shape signature at the resolved site, if
    //     the row carries one (Payload.aob = the expected instruction-shape AOB).
    //     The resolver chain landed; match the stored shape at the LEA site.
    if (!p.aob.empty()) {
        patch::Pattern pat = PatternFromPayload(p);
        // The stored shape is asserted AT the resolved LEA site. Map the LEA's
        // RVA to its on-disk offset and compare the bytes there.
        size_t fileOff = 0;
        if (!pe::RvaToFileOffsetOnDisk(file.data(), file.size(), leaRvas[0],
                                       pat.bytes.size(), fileOff)) {
            LOG_WARN_KV(kCategory, "instruction_anchor_changed",
                ::kcdx::log::KV("kind", "instruction_anchor"),
                ::kcdx::log::KV("note", "resolved LEA site not on-disk-readable for the shape match"));
            return Changed();
        }
        auto offs = patch::FindAllInBuffer(file.data() + fileOff, pat.bytes.size(), pat);
        // A shape match means the pattern matches AT offset 0 of the resolved site.
        bool shapeMatch = !offs.empty() && offs[0] == 0;
        if (!shapeMatch) {
            LOG_WARN_KV(kCategory, "instruction_anchor_changed",
                ::kcdx::log::KV("kind", "instruction_anchor"),
                ::kcdx::log::KV("lea_rva", (unsigned long long)leaRvas[0]),
                ::kcdx::log::KV("note", "resolved instruction does not match the stored shape signature"));
            return Changed();
        }
    }
    LOG_DEBUG_KV(kCategory, "instruction_anchor_unchanged",
        ::kcdx::log::KV("stored_rva", (unsigned long long)rva),
        ::kcdx::log::KV("lea_rva", (unsigned long long)leaRvas[0]));
    return Unchanged();
}

// data_slot — re-run the derivation; Unchanged iff it still lands in .data at a
// consistent offset relative to its anchor. NO content hash (.data holds
// relocated pointers — a byte hash is an anti-signal). Needs the anchor's
// resolved RVA (threaded in via Payload.anchorResolvedRva by the ordered walk).
Result CheckDataSlot(const std::vector<uint8_t>& file, const Payload& p, uint32_t rva) {
    if (p.rule.empty()) {
        LOG_WARN_KV(kCategory, "bad_rule",
            ::kcdx::log::KV("reason", "bad_rule"),
            ::kcdx::log::KV("kind", "data_slot"),
            ::kcdx::log::KV("note", "empty derivation rule"));
        return CannotCheck("bad_rule");
    }
    ParsedRule pr = ParseDataSlotRule(p.rule);
    if (pr.form == RuleForm::Bad) {
        LOG_WARN_KV(kCategory, "bad_rule",
            ::kcdx::log::KV("reason", "bad_rule"),
            ::kcdx::log::KV("kind", "data_slot"),
            ::kcdx::log::KV("rule", p.rule.c_str()));
        return CannotCheck("bad_rule");
    }
    // Every data_slot rule derives THROUGH an anchor row — its resolved RVA must
    // have been threaded in. A derivation dispatched without it (single-row
    // entry, not the ordered walk) cannot run — fail loud, not a false verdict.
    if (!p.hasAnchor) {
        LOG_WARN_KV(kCategory, "anchor_unresolved",
            ::kcdx::log::KV("reason", "anchor_unresolved"),
            ::kcdx::log::KV("kind", "data_slot"),
            ::kcdx::log::KV("note", "data_slot dispatched without its anchor's resolved RVA (use CheckOrdered)"));
        return CannotCheck("anchor_unresolved");
    }

    uint32_t derivedRva = 0;
    if (pr.form == RuleForm::Disp32Follow) {
        // Follow the disp32 at the anchor instruction (the instruction_anchor's
        // resolved RVA) to the slot VA. The anchor is a `48 8B 0D <disp32>` MOV
        // by default (Payload.dispOffsetInAnchorInstr / anchorInstrLen).
        if (!pe::FindDisp32Forward(file.data(), file.size(), p.anchorResolvedRva,
                                   p.dispOffsetInAnchorInstr, p.anchorInstrLen,
                                   derivedRva)) {
            LOG_WARN_KV(kCategory, "data_slot_changed",
                ::kcdx::log::KV("kind", "data_slot"),
                ::kcdx::log::KV("anchor_rva", (unsigned long long)p.anchorResolvedRva),
                ::kcdx::log::KV("note", "disp32 follow from anchor failed (instruction shape changed?)"));
            return Changed();
        }
    } else {  // RuleForm::Offset — anchor RVA ± fixed offset.
        int64_t target = static_cast<int64_t>(p.anchorResolvedRva) + pr.offset;
        if (target < 0 || target > 0xFFFFFFFFLL) {
            LOG_WARN_KV(kCategory, "data_slot_changed",
                ::kcdx::log::KV("kind", "data_slot"),
                ::kcdx::log::KV("note", "anchor-relative offset derivation out of range"));
            return Changed();
        }
        derivedRva = static_cast<uint32_t>(target);
    }

    // Unchanged iff the derivation lands in .data. (The slot's CONTENTS are not
    // hashed — they are relocated pointers, legitimately different at rest.)
    if (!IsRvaInOnDiskData(file, derivedRva)) {
        LOG_WARN_KV(kCategory, "derivation_off_data",
            ::kcdx::log::KV("kind", "data_slot"),
            ::kcdx::log::KV("derived_rva", (unsigned long long)derivedRva),
            ::kcdx::log::KV("note", "derivation did not land in .data — slot moved / derivation broke"));
        return Changed();
    }
    LOG_DEBUG_KV(kCategory, "data_slot_unchanged",
        ::kcdx::log::KV("stored_rva", (unsigned long long)rva),
        ::kcdx::log::KV("derived_rva", (unsigned long long)derivedRva));
    return Unchanged();
}

// vtable_base — read N qwords (Payload.slotCount) at the stored RVA on-disk;
// Unchanged iff there are N readable qwords AND each resolves into .text (a
// plausible relocated code pointer). A shrunk/grown table or non-pointer
// contents → Changed.
Result CheckVtableBase(const std::vector<uint8_t>& file, const Payload& p, uint32_t rva) {
    if (p.slotCount == 0) {
        LOG_WARN_KV(kCategory, "no_slot_count",
            ::kcdx::log::KV("reason", "no_slot_count"),
            ::kcdx::log::KV("kind", "vtable_base"),
            ::kcdx::log::KV("rva", (unsigned long long)rva));
        return CannotCheck("no_slot_count");
    }
    const size_t tableBytes = static_cast<size_t>(p.slotCount) * 8;
    size_t fileOff = 0;
    if (!pe::RvaToFileOffsetOnDisk(file.data(), file.size(), rva, tableBytes, fileOff)) {
        // The N-qword span is not on-disk-readable at the stored RVA — the table
        // shrank/moved (or never had N slots). A definite negative, not a
        // CannotCheck: the stored shape no longer holds.
        LOG_WARN_KV(kCategory, "vtable_base_changed",
            ::kcdx::log::KV("kind", "vtable_base"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("slot_count", (unsigned long long)p.slotCount),
            ::kcdx::log::KV("note", "N-qword table span not on-disk-readable at the stored RVA"));
        return Changed();
    }
    const uint8_t* base = file.data() + fileOff;
    for (uint32_t i = 0; i < p.slotCount; ++i) {
        uint64_t slot = 0;
        std::memcpy(&slot, base + static_cast<size_t>(i) * 8, 8);
        if (!pe::IsTextPointerOnDisk(file.data(), file.size(), slot)) {
            LOG_WARN_KV(kCategory, "vtable_base_changed",
                ::kcdx::log::KV("kind", "vtable_base"),
                ::kcdx::log::KV("rva", (unsigned long long)rva),
                ::kcdx::log::KV("slot_index", (unsigned long long)i),
                ::kcdx::log::KV("slot_value", (unsigned long long)slot),
                ::kcdx::log::KV("note", "slot is not a plausible .text code pointer — table shape broke"));
            return Changed();
        }
    }
    LOG_DEBUG_KV(kCategory, "vtable_base_unchanged",
        ::kcdx::log::KV("rva", (unsigned long long)rva),
        ::kcdx::log::KV("slot_count", (unsigned long long)p.slotCount));
    return Unchanged();
}

}  // namespace

Result SurvivalCheck(const Payload& payload,
                     uint32_t rva,
                     const std::string& dll) {
    (void)dll;  // reserved for the per-module on-disk read (step 3.2/3.3 wires
                // the module selection); the function path reads WHGame.dll.
    switch (payload.kind) {
        // --- Function kinds: the EXISTING on-disk body-hash check, UNCHANGED. ---
        // Route through the legacy entry point so its verdict is byte-identical
        // to today (the test asserts this). An empty contentHash here is a
        // non-byte entity → the legacy path returns CannotCheck "not_applicable",
        // exactly as before.
        case Kind::Function:
        case Kind::FunctionNoSig:
        case Kind::FunctionVariadic: {
            return SurvivalCheck(
                rva, payload.length,
                payload.contentHash.empty() ? nullptr : payload.contentHash.data(),
                payload.contentHash.size());
        }

        // --- vtable_index: a DEFINED, longer-lived deferral. ------------------
        // Its survival datum (a slot-target body-hash) is design-defined but
        // population waits on the runtime-vtable verification path. CannotCheck
        // with its own token, never a false Unchanged or a 3.2-pending stub.
        case Kind::VtableIndex: {
            LOG_DEBUG_KV(kCategory, "vtable_index_deferred",
                ::kcdx::log::KV("reason", "vtable_index_deferred"),
                ::kcdx::log::KV("kind", "vtable_index"),
                ::kcdx::log::KV("rva", (unsigned long long)rva),
                ::kcdx::log::KV("note", "slot-target body-hash population waits on the runtime-vtable path"));
            return CannotCheck("vtable_index_deferred");
        }

        // --- The 5 STATIC non-function checks (D25 — on-disk). ----------------
        // A dependent row whose anchor came back Changed short-circuits to
        // CannotCheck/"anchor_changed" — never silently re-derived through a dead
        // anchor (the ordered walk sets payload.anchorChanged). Each check reads
        // the on-disk DLL and returns a DEFINED verdict.
        case Kind::Callsite:
        case Kind::StringAnchor:
        case Kind::InstructionAnchor:
        case Kind::DataSlot:
        case Kind::VtableBase: {
            if (payload.hasAnchor && payload.anchorChanged) {
                LOG_WARN_KV(kCategory, "anchor_changed",
                    ::kcdx::log::KV("reason", "anchor_changed"),
                    ::kcdx::log::KV("kind", KindName(payload.kind)),
                    ::kcdx::log::KV("rva", (unsigned long long)rva),
                    ::kcdx::log::KV("note", "anchor (derivesFrom) came back Changed — dependent transitively un-derivable"));
                return CannotCheck("anchor_changed");
            }
            std::vector<uint8_t> file;
            if (const char* reason = ReadOnDiskModule(file)) {
                return CannotCheck(reason);
            }
            switch (payload.kind) {
                case Kind::Callsite:          return CheckCallsite(file, payload, rva);
                case Kind::StringAnchor:      return CheckStringAnchor(file, payload, rva);
                case Kind::InstructionAnchor: return CheckInstructionAnchor(file, payload, rva);
                case Kind::DataSlot:          return CheckDataSlot(file, payload, rva);
                case Kind::VtableBase:        return CheckVtableBase(file, payload, rva);
                default: break;  // unreachable — outer switch already gated these.
            }
            return CannotCheck("on_disk_unreadable");  // unreachable.
        }
    }

    // Exhaustive above; an unmapped kind is a defect, not a silent pass.
    LOG_ERROR_KV(kCategory, "kind_unknown",
        ::kcdx::log::KV("reason", "on_disk_unreadable"),
        ::kcdx::log::KV("kind_value", (unsigned long long)static_cast<int>(payload.kind)),
        ::kcdx::log::KV("note", "unmapped survival kind reached the dispatch"));
    return CannotCheck("on_disk_unreadable");
}

Result SurvivalCheck(uint32_t rva, size_t length,
                     const uint8_t* expectedHash32, size_t expectedLen) {
    // --- Non-byte entity: an empty expected hash is "not applicable", NEVER a
    // "Changed". (A vtable slot / data offset carries no content_hash.) ------
    if (expectedHash32 == nullptr || expectedLen == 0) {
        LOG_DEBUG_KV(kCategory, "not_applicable",
            ::kcdx::log::KV("reason", "not_applicable"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("note", "empty expected hash — non-byte entity, no check"));
        return CannotCheck("not_applicable");
    }

    // A present-but-wrong-width expected hash is a malformed DB row, not a
    // mismatch — fail loud, do not compare a truncated digest.
    if (expectedLen != kHashLen) {
        LOG_ERROR_KV(kCategory, "expected_hash_bad_length",
            ::kcdx::log::KV("reason", "expected_hash_bad_length"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("expected_len", (unsigned long long)expectedLen),
            ::kcdx::log::KV("want_len", (unsigned long long)kHashLen));
        return CannotCheck("expected_hash_bad_length");
    }

    if (length == 0) {
        LOG_ERROR_KV(kCategory, "length_zero",
            ::kcdx::log::KV("reason", "length_zero"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("note", "zero-length span — nothing to hash"));
        return CannotCheck("length_zero");
    }

    // --- Locate WHGame.dll's on-disk path. ---------------------------------
    std::wstring modulePath;
    bool moduleLoaded = false;
    if (!ResolveModulePath(modulePath, moduleLoaded)) {
        if (!moduleLoaded) {
            LOG_ERROR_KV(kCategory, "module_not_mapped",
                ::kcdx::log::KV("reason", "module_not_mapped"),
                ::kcdx::log::KV("module", "WHGame.dll"),
                ::kcdx::log::KV("note", "module not loaded in process"));
            return CannotCheck("module_not_mapped");
        }
        LOG_ERROR_KV(kCategory, "file_not_found",
            ::kcdx::log::KV("reason", "file_not_found"),
            ::kcdx::log::KV("module", "WHGame.dll"),
            ::kcdx::log::KV("note", "on-disk path could not be resolved"));
        return CannotCheck("file_not_found");
    }

    // --- Read the ON-DISK backing file (NOT live memory — the crux). --------
    std::vector<uint8_t> fileData;
    if (!ReadWholeFile(modulePath, fileData)) {
        std::string path8 = kcdx::paths::ToUtf8(std::filesystem::path(modulePath));
        LOG_ERROR_KV(kCategory, "file_open_error",
            ::kcdx::log::KV("reason", "file_open_error"),
            ::kcdx::log::KV("path", path8),
            ::kcdx::log::KV("note", "could not open/read the module's on-disk file"));
        return CannotCheck("file_open_error");
    }

    // --- Map [rva, rva+length) to its on-disk file offset. ------------------
    size_t fileOffset = 0;
    if (!pe::RvaToFileOffsetOnDisk(fileData.data(), fileData.size(),
                                   rva, length, fileOffset)) {
        LOG_ERROR_KV(kCategory, "rva_out_of_range",
            ::kcdx::log::KV("reason", "rva_out_of_range"),
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("length", (unsigned long long)length),
            ::kcdx::log::KV("file_size", (unsigned long long)fileData.size()),
            ::kcdx::log::KV("note", "span maps to no on-disk section"));
        return CannotCheck("rva_out_of_range");
    }

    // Defensive: RvaToFileOffsetOnDisk already bounds the span to the buffer,
    // but re-check before the read so a future helper-change can never read OOB.
    if (fileOffset > fileData.size() || length > fileData.size() - fileOffset) {
        LOG_ERROR_KV(kCategory, "read_error",
            ::kcdx::log::KV("reason", "read_error"),
            ::kcdx::log::KV("file_offset", (unsigned long long)fileOffset),
            ::kcdx::log::KV("length", (unsigned long long)length),
            ::kcdx::log::KV("file_size", (unsigned long long)fileData.size()),
            ::kcdx::log::KV("note", "mapped span runs past the file buffer"));
        return CannotCheck("read_error");
    }

    // --- Hash the on-disk span and compare. ---------------------------------
    uint8_t got[blake3::kHashLen];
    blake3::Hash256(fileData.data() + fileOffset, length, got);

    if (std::memcmp(got, expectedHash32, kHashLen) == 0) {
        LOG_DEBUG_KV(kCategory, "unchanged",
            ::kcdx::log::KV("rva", (unsigned long long)rva),
            ::kcdx::log::KV("length", (unsigned long long)length),
            ::kcdx::log::KV("hash", ToHex(got, kHashLen)));
        Result r;
        r.status = Status::Unchanged;
        return r;
    }

    LOG_WARN_KV(kCategory, "changed",
        ::kcdx::log::KV("rva", (unsigned long long)rva),
        ::kcdx::log::KV("length", (unsigned long long)length),
        ::kcdx::log::KV("computed", ToHex(got, kHashLen)),
        ::kcdx::log::KV("expected", ToHex(expectedHash32, kHashLen)));
    Result r;
    r.status = Status::Changed;
    return r;
}

// ---------------------------------------------------------------------------
// THE DEPENDENCY-ORDERED WALK — check a row set anchors-first.
//
// Why the DAG lives here, not in the single-row entry: a dependent kind
// (data_slot through an instruction_anchor; instruction_anchor through a
// string_anchor; vtable_index through a vtable_base) re-derives THROUGH its
// anchor, so it needs the anchor's resolved address + verdict — which only
// exist after the anchor is checked. The single row has neither. This walk
// resolves anchors first, caches each row's verdict, threads the anchor's
// resolved RVA + Changed-flag into each dependent's Payload, then dispatches.
//
// FLAGGED — the threaded "resolved RVA" is the anchor's STORED rva when the
// anchor came back Unchanged. The survival check's scope is "still the thing it
// was verified to be at the CURRENT game version" (one version), within which
// an Unchanged anchor sits at its recorded RVA; the data_slot/instruction_anchor
// chains in the curated DB (gEnv → pConsole → instruction_anchor → string) all
// derive from a version-stable anchor RVA. A FULL relocation-tracking design
// (threading the anchor's *re-found* RVA when it relocated, e.g. a callsite
// anchor that moved) is a richer future capability — the single-row checks
// would need to RETURN their resolved RVA, not just a verdict. Surfaced.

namespace {

// Topologically order row indices: every anchor appears before each row that
// derives from it. Returns false on a cycle (the affected rows are reported
// CannotCheck by the caller, never a hang). `order` is filled with input
// indices in dependency order; rows whose anchor id is unknown are placed last
// (they will report CannotCheck/"anchor_changed" — a dangling edge is a
// missing/dead anchor, fail-loud not silent).
bool TopoOrder(const std::vector<Row>& rows,
               const std::unordered_map<uint64_t, size_t>& byId,
               std::vector<size_t>& order) {
    enum Mark { White, Grey, Black };
    std::vector<Mark> mark(rows.size(), White);
    bool cycle = false;
    // Iterative DFS (avoid deep recursion on a long chain).
    for (size_t start = 0; start < rows.size(); ++start) {
        if (mark[start] != White) continue;
        std::vector<size_t> stack;
        stack.push_back(start);
        while (!stack.empty()) {
            size_t cur = stack.back();
            if (mark[cur] == White) {
                mark[cur] = Grey;
                uint64_t dep = rows[cur].derivesFrom;
                if (dep != 0) {
                    auto it = byId.find(dep);
                    if (it != byId.end()) {
                        size_t anchorIdx = it->second;
                        if (mark[anchorIdx] == Grey) {
                            cycle = true;  // back-edge → cycle.
                        } else if (mark[anchorIdx] == White) {
                            stack.push_back(anchorIdx);
                            continue;  // visit the anchor before finishing cur.
                        }
                    }
                    // dep not in byId → dangling edge; cur still orders after
                    // (its anchorChanged path fires at check time).
                }
            }
            if (mark[cur] == Grey) {
                mark[cur] = Black;
                order.push_back(cur);
            }
            stack.pop_back();
        }
    }
    return !cycle;
}

}  // namespace

std::vector<RowResult> CheckOrdered(const std::vector<Row>& rows) {
    std::vector<RowResult> out;
    out.reserve(rows.size());

    // Index rows by their DAG id (duplicate ids: last wins — a defect, but the
    // walk stays deterministic rather than ambiguous).
    std::unordered_map<uint64_t, size_t> byId;
    byId.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id != 0) byId[rows[i].id] = i;
    }

    std::vector<size_t> order;
    order.reserve(rows.size());
    bool acyclic = TopoOrder(rows, byId, order);
    if (!acyclic) {
        LOG_WARN_KV(kCategory, "survival_dag_cycle",
            ::kcdx::log::KV("reason", "anchor_changed"),
            ::kcdx::log::KV("note", "survival-DAG cycle detected — affected rows reported CannotCheck"));
    }

    // Per-row resolved state, accumulated as the walk proceeds (anchors first).
    struct Resolved { bool done = false; Status status = Status::CannotCheck; uint32_t rva = 0; };
    std::vector<Resolved> resolved(rows.size());

    // A row whose anchor's verdict is not Unchanged is transitively un-derivable.
    auto anchorIsBad = [&](Status s) {
        return s != Status::Unchanged;  // Changed / Ambiguous / CannotCheck.
    };

    for (size_t idx : order) {
        const Row& row = rows[idx];
        Payload payload = row.payload;  // copy — we thread anchor results in.

        // Resolve the anchor's result (if any). On a cycle the anchor may not be
        // resolved yet (stack ordering broke) — treat unresolved-anchor as bad.
        if (row.derivesFrom != 0) {
            payload.hasAnchor = true;
            auto it = byId.find(row.derivesFrom);
            if (it == byId.end() || !resolved[it->second].done) {
                // Dangling or not-yet-resolved (cycle) anchor → transitively bad.
                payload.anchorChanged = true;
                payload.anchorResolvedRva = 0;
            } else {
                const Resolved& a = resolved[it->second];
                payload.anchorChanged = anchorIsBad(a.status);
                payload.anchorResolvedRva = a.rva;  // FLAGGED: anchor's stored rva when Unchanged.
            }
        }

        Result r = SurvivalCheck(payload, row.rva, row.dll);

        // Cache this row's verdict + its resolved RVA for any dependent. The
        // resolved RVA threaded down is this row's stored rva when it survived
        // (see the FLAGGED note above); a non-Unchanged row contributes a 0 RVA,
        // which its dependents never reach (they short-circuit on anchorChanged).
        Resolved& slot = resolved[idx];
        slot.done = true;
        slot.status = r.status;
        slot.rva = (r.status == Status::Unchanged) ? row.rva : 0;

        out.push_back(RowResult{row.id, r});
    }

    // Re-emit in INPUT order (the walk visited in topological order; `out[k]`
    // is the result for input index `order[k]`).
    std::vector<RowResult> ordered(rows.size());
    for (size_t k = 0; k < order.size(); ++k) {
        ordered[order[k]] = out[k];
    }
    return ordered;
}

}  // namespace kcdx::survival
