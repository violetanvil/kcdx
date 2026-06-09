#include "pe_helpers.h"

#include <algorithm>
#include <cstring>
#include <limits>

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

std::vector<SectionView> WritableDataSections(const ModuleView& m) {
    // G2 — writable + not executable (= .data-class). The mirror of
    // ReadOnlyDataSections with the write bit required, so .data (filtered OUT
    // of both Executable and ReadOnly because it is writable) gets a predicate.
    auto all = Sections(m);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const SectionView& s) {
                                 bool writable = s.characteristics & IMAGE_SCN_MEM_WRITE;
                                 bool exec = s.characteristics & IMAGE_SCN_MEM_EXECUTE;
                                 return !(writable && !exec);
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

// ---------------------------------------------------------------------------
// ON-DISK section span access (D25 — the survival static checks read the
// on-disk file, not the live relocated image). Shares the DOS+NT header parse
// RvaToFileOffsetOnDisk uses; returns spans bounded by SizeOfRawData.

namespace {

// Validate the on-disk PE headers and return the NT header pointer, or nullptr
// on any malformed/truncated buffer. Mirrors RvaToFileOffsetOnDisk's header
// checks so the on-disk accessors fail loud exactly where the offset mapper
// does. The caller separately bounds the section table against the buffer
// before walking it.
const IMAGE_NT_HEADERS* ParseOnDiskNt(const uint8_t* fileData, size_t fileSize) {
    if (!fileData) return nullptr;
    if (fileSize < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(fileData);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if (dos->e_lfanew < 0) return nullptr;
    auto ntOff = static_cast<size_t>(dos->e_lfanew);
    if (ntOff > fileSize || fileSize - ntOff < sizeof(IMAGE_NT_HEADERS)) return nullptr;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(fileData + ntOff);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

}  // namespace

std::vector<OnDiskSection> OnDiskSections(const uint8_t* fileData, size_t fileSize) {
    std::vector<OnDiskSection> result;
    const IMAGE_NT_HEADERS* nt = ParseOnDiskNt(fileData, fileSize);
    if (!nt) return result;  // fail-loud empty — the caller treats it as unparseable.

    const WORD numSections = nt->FileHeader.NumberOfSections;
    auto firstSec = IMAGE_FIRST_SECTION(nt);
    auto secTableOff =
        static_cast<size_t>(reinterpret_cast<const uint8_t*>(firstSec) - fileData);
    if (secTableOff > fileSize) return result;
    if ((fileSize - secTableOff) / sizeof(IMAGE_SECTION_HEADER) < numSections) {
        return result;  // section table runs past the buffer — malformed.
    }

    for (WORD i = 0; i < numSections; ++i) {
        const auto& sh = firstSec[i];
        const uint64_t rawBase = sh.PointerToRawData;
        const uint64_t rawSize = sh.SizeOfRawData;
        const uint64_t rawEnd = rawBase + rawSize;
        // Drop a section whose raw span overflows or runs past the buffer — its
        // bytes are not on-disk-readable, so a scan over it would read OOB.
        if (rawEnd < rawBase) continue;
        if (rawEnd > fileSize) continue;

        OnDiskSection v;
        v.name.assign(reinterpret_cast<const char*>(sh.Name),
                      strnlen(reinterpret_cast<const char*>(sh.Name), 8));
        v.data = fileData + rawBase;
        v.size = static_cast<size_t>(rawSize);
        v.rva = sh.VirtualAddress;
        v.virtualSize = sh.Misc.VirtualSize;
        v.characteristics = sh.Characteristics;
        result.push_back(std::move(v));
    }
    return result;
}

std::vector<OnDiskSection> OnDiskExecutableSections(const uint8_t* fileData, size_t fileSize) {
    auto all = OnDiskSections(fileData, fileSize);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const OnDiskSection& s) {
                                 return !(s.characteristics & IMAGE_SCN_MEM_EXECUTE);
                             }),
              all.end());
    return all;
}

std::vector<OnDiskSection> OnDiskReadOnlyDataSections(const uint8_t* fileData, size_t fileSize) {
    auto all = OnDiskSections(fileData, fileSize);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const OnDiskSection& s) {
                                 bool readable = s.characteristics & IMAGE_SCN_MEM_READ;
                                 bool exec = s.characteristics & IMAGE_SCN_MEM_EXECUTE;
                                 return !(readable && !exec);
                             }),
              all.end());
    return all;
}

std::vector<OnDiskSection> OnDiskWritableDataSections(const uint8_t* fileData, size_t fileSize) {
    auto all = OnDiskSections(fileData, fileSize);
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const OnDiskSection& s) {
                                 bool writable = s.characteristics & IMAGE_SCN_MEM_WRITE;
                                 bool exec = s.characteristics & IMAGE_SCN_MEM_EXECUTE;
                                 return !(writable && !exec);
                             }),
              all.end());
    return all;
}

bool FindDisp32Forward(const uint8_t* fileData, size_t fileSize,
                       uint32_t instrRva, uint32_t dispOffsetInInstr,
                       uint32_t instrLen, uint32_t& targetRvaOut) {
    // The disp32 field must fit within the instruction.
    if (instrLen < 4) return false;
    if (dispOffsetInInstr > instrLen - 4) return false;

    // Map the 4-byte disp32 field's RVA to its on-disk file offset (fail-loud
    // false if it lies in no on-disk section).
    const uint64_t dispRva64 = static_cast<uint64_t>(instrRva) + dispOffsetInInstr;
    if (dispRva64 > std::numeric_limits<uint32_t>::max()) return false;
    size_t dispFileOff = 0;
    if (!RvaToFileOffsetOnDisk(fileData, fileSize,
                               static_cast<uint32_t>(dispRva64), 4, dispFileOff)) {
        return false;
    }

    // SOURCE: x64 RIP-relative addressing — the displacement is relative to the
    // address of the NEXT instruction (instrEnd = instrRva + instrLen). Same
    // little-endian decode + instrEnd-relative arithmetic as FindLeaXrefsTo
    // (.cpp:96-100), applied FORWARD (instruction-known → target).
    const uint8_t* p = fileData + dispFileOff;
    int32_t disp = static_cast<int32_t>(
        p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    const int64_t instrEnd = static_cast<int64_t>(instrRva) + instrLen;
    const int64_t target = instrEnd + disp;
    if (target < 0 || target > std::numeric_limits<uint32_t>::max()) return false;
    targetRvaOut = static_cast<uint32_t>(target);
    return true;
}

bool IsRvaInExecutableSection(const uint8_t* fileData, size_t fileSize, uint32_t rva) {
    // VirtualSize bounds the in-memory extent (the SizeOfRawData on-disk extent
    // can be smaller, padded by FileAlignment) — use the larger of the two so a
    // code pointer into a section's zero-padded tail still classifies as .text.
    const IMAGE_NT_HEADERS* nt = ParseOnDiskNt(fileData, fileSize);
    if (!nt) return false;
    const WORD numSections = nt->FileHeader.NumberOfSections;
    auto firstSec = IMAGE_FIRST_SECTION(nt);
    auto secTableOff =
        static_cast<size_t>(reinterpret_cast<const uint8_t*>(firstSec) - fileData);
    if (secTableOff > fileSize) return false;
    if ((fileSize - secTableOff) / sizeof(IMAGE_SECTION_HEADER) < numSections) return false;

    for (WORD i = 0; i < numSections; ++i) {
        const auto& sh = firstSec[i];
        if (!(sh.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint64_t secVa = sh.VirtualAddress;
        uint64_t extent = sh.Misc.VirtualSize;
        if (extent < sh.SizeOfRawData) extent = sh.SizeOfRawData;
        const uint64_t secEnd = secVa + extent;
        if (secEnd < secVa) continue;  // malformed
        if (rva >= secVa && rva < secEnd) return true;
    }
    return false;
}

bool IsTextPointerOnDisk(const uint8_t* fileData, size_t fileSize, uint64_t slotValue) {
    // An on-disk vtable slot holds the PREFERRED-base absolute (no relocations
    // applied on disk). Convert to an RVA by subtracting ImageBase, then test
    // against the executable sections. A 0/below-base slot (a load-time reloc
    // placeholder or an import thunk) is not a plausible .text pointer.
    const IMAGE_NT_HEADERS* nt = ParseOnDiskNt(fileData, fileSize);
    if (!nt) return false;
    const uint64_t imageBase = nt->OptionalHeader.ImageBase;
    if (slotValue < imageBase) return false;
    const uint64_t rva64 = slotValue - imageBase;
    if (rva64 > std::numeric_limits<uint32_t>::max()) return false;
    return IsRvaInExecutableSection(fileData, fileSize, static_cast<uint32_t>(rva64));
}

bool IsVaInLiveText(const ModuleView& m, uintptr_t va) {
    // The reachability range test (D25 step 3.3): does `va` land in a live
    // executable section? A live SectionView spans [base+rva, base+rva+size)
    // where size is the section's VirtualSize (the in-memory extent). va==0 or
    // any VA outside every executable section → false (fail toward "dead").
    if (va == 0 || m.base == nullptr) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(m.base);
    for (const auto& sec : ExecutableSections(m)) {
        const uintptr_t secStart = base + sec.rva;
        const uintptr_t secEnd = secStart + sec.size;
        if (secEnd < secStart) continue;  // overflow guard (malformed extent).
        if (va >= secStart && va < secEnd) return true;
    }
    return false;
}

}  // namespace kcdx::pe
