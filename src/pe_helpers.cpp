#include "pe_helpers.h"

#include <algorithm>
#include <cstring>

namespace kcdx::pe {

bool OpenModule(const wchar_t* moduleName, ModuleView& out) {
    HMODULE h = GetModuleHandleW(moduleName);
    if (!h) return false;
    auto base = reinterpret_cast<const uint8_t*>(h);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(
        const_cast<uint8_t*>(base) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.base = h;
    out.baseBytes = base;
    out.size = nt->OptionalHeader.SizeOfImage;
    out.nt = nt;
    return true;
}

std::vector<SectionView> Sections(const ModuleView& m) {
    std::vector<SectionView> result;
    if (!m.nt) return result;
    auto firstSec = IMAGE_FIRST_SECTION(m.nt);
    for (WORD i = 0; i < m.nt->FileHeader.NumberOfSections; ++i) {
        auto& sh = firstSec[i];
        SectionView v;
        v.name.assign(reinterpret_cast<const char*>(sh.Name),
                      strnlen(reinterpret_cast<const char*>(sh.Name), 8));
        v.data = m.baseBytes + sh.VirtualAddress;
        v.size = sh.Misc.VirtualSize;
        v.rva = sh.VirtualAddress;
        v.characteristics = sh.Characteristics;
        result.push_back(std::move(v));
    }
    return result;
}

std::vector<SectionView> ExecutableSections(const ModuleView& m) {
    auto all = Sections(m);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const SectionView& s) {
                                 return !(s.characteristics & IMAGE_SCN_MEM_EXECUTE);
                             }),
              all.end());
    return all;
}

std::vector<SectionView> ReadOnlyDataSections(const ModuleView& m) {
    auto all = Sections(m);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const SectionView& s) {
                                 bool readable = s.characteristics & IMAGE_SCN_MEM_READ;
                                 bool exec = s.characteristics & IMAGE_SCN_MEM_EXECUTE;
                                 return !(readable && !exec);
                             }),
              all.end());
    return all;
}

std::vector<uintptr_t> FindCStringsIn(const std::vector<SectionView>& sections,
                                     std::string_view literal) {
    std::vector<uintptr_t> hits;
    if (literal.empty()) return hits;

    for (const auto& sec : sections) {
        if (sec.size < literal.size() + 1) continue;
        const uint8_t* base = sec.data;
        const size_t span = sec.size - literal.size();
        for (size_t i = 0; i <= span; ++i) {
            if (base[i] != static_cast<uint8_t>(literal[0])) continue;
            if (std::memcmp(base + i, literal.data(), literal.size()) != 0) continue;
            // Must be null-terminated to match exact-string semantics.
            if (i + literal.size() < sec.size && base[i + literal.size()] != 0) continue;
            hits.push_back(reinterpret_cast<uintptr_t>(base + i));
        }
    }
    return hits;
}

std::vector<uintptr_t> FindLeaXrefsTo(const ModuleView& m, uintptr_t targetVA) {
    std::vector<uintptr_t> hits;
    auto sections = ExecutableSections(m);
    for (const auto& sec : sections) {
        if (sec.size < 7) continue;
        const uint8_t* base = sec.data;
        // Scan for `48 8D <modrm> <rel32>` — 7-byte LEA rip-relative with REX.W
        for (size_t i = 0; i + 7 <= sec.size; ++i) {
            if (base[i] != 0x48 || base[i + 1] != 0x8D) continue;
            uint8_t modrm = base[i + 2];
            // mod == 00, rm == 101  → rip-relative addressing (any reg field)
            if ((modrm & 0xC7) != 0x05) continue;
            int32_t rel = static_cast<int32_t>(
                base[i + 3] | (base[i + 4] << 8) |
                (base[i + 5] << 16) | (base[i + 6] << 24));
            uintptr_t instrEnd = reinterpret_cast<uintptr_t>(base + i + 7);
            uintptr_t target = instrEnd + static_cast<intptr_t>(rel);
            if (target == targetVA) {
                hits.push_back(reinterpret_cast<uintptr_t>(base + i));
            }
        }
    }
    return hits;
}

bool RvaToFileOffsetOnDisk(const uint8_t* fileData, size_t fileSize,
                           uint32_t rva, size_t length, size_t& fileOffsetOut) {
    if (!fileData || length == 0) return false;

    // DOS header must fit and carry the MZ magic + a sane e_lfanew.
    if (fileSize < sizeof(IMAGE_DOS_HEADER)) return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(fileData);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (dos->e_lfanew < 0) return false;
    auto ntOff = static_cast<size_t>(dos->e_lfanew);

    // NT headers must fit at e_lfanew and carry the PE signature.
    if (ntOff > fileSize || fileSize - ntOff < sizeof(IMAGE_NT_HEADERS)) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(fileData + ntOff);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    // Section headers follow the optional header; bound the table against the
    // buffer before walking it.
    const WORD numSections = nt->FileHeader.NumberOfSections;
    auto firstSec = IMAGE_FIRST_SECTION(nt);
    auto secTableOff =
        static_cast<size_t>(reinterpret_cast<const uint8_t*>(firstSec) - fileData);
    if (secTableOff > fileSize) return false;
    if ((fileSize - secTableOff) / sizeof(IMAGE_SECTION_HEADER) < numSections) {
        return false;
    }

    // The span's end (exclusive). Guard the rva + length addition against
    // overflow before forming the half-open interval.
    const uint64_t spanBegin = rva;
    const uint64_t spanEnd = spanBegin + length;
    if (spanEnd < spanBegin) return false;  // overflow

    for (WORD i = 0; i < numSections; ++i) {
        const auto& sh = firstSec[i];
        const uint64_t secVa = sh.VirtualAddress;
        const uint64_t secRawSize = sh.SizeOfRawData;
        const uint64_t secVaEnd = secVa + secRawSize;
        if (secVaEnd < secVa) continue;  // malformed section, skip

        // The WHOLE span must sit within this section's RAW on-disk data
        // (bounded by SizeOfRawData, not VirtualSize — uninitialized tail bytes
        // have no on-disk backing).
        if (spanBegin < secVa || spanEnd > secVaEnd) continue;

        const uint64_t rawBase = sh.PointerToRawData;
        const uint64_t fileOff = rawBase + (spanBegin - secVa);
        const uint64_t fileEnd = fileOff + length;
        if (fileEnd < fileOff) return false;             // overflow
        if (fileEnd > fileSize) return false;            // span runs past the buffer

        fileOffsetOut = static_cast<size_t>(fileOff);
        return true;
    }

    return false;  // no section covers the span
}

bool FindFunctionBoundsViaPdata(const ModuleView& m,
                                uintptr_t addressVA,
                                uintptr_t& beginVA,
                                uintptr_t& endVA) {
    if (!m.nt) return false;
    auto& exc = m.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exc.VirtualAddress == 0 || exc.Size == 0) return false;

    auto* table = reinterpret_cast<const RUNTIME_FUNCTION*>(m.baseBytes + exc.VirtualAddress);
    size_t count = exc.Size / sizeof(RUNTIME_FUNCTION);

    uintptr_t targetRVA = addressVA - reinterpret_cast<uintptr_t>(m.baseBytes);
    if (targetRVA > std::numeric_limits<uint32_t>::max()) return false;
    auto rva = static_cast<uint32_t>(targetRVA);

    // .pdata entries are sorted by BeginAddress — binary search.
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const auto& rf = table[mid];
        if (rva < rf.BeginAddress) {
            hi = mid;
        } else if (rva >= rf.EndAddress) {
            lo = mid + 1;
        } else {
            beginVA = reinterpret_cast<uintptr_t>(m.baseBytes) + rf.BeginAddress;
            endVA   = reinterpret_cast<uintptr_t>(m.baseBytes) + rf.EndAddress;
            return true;
        }
    }
    return false;
}

}  // namespace kcdx::pe
