#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kcdx::pe {

struct ModuleView {
    HMODULE base = nullptr;
    const uint8_t* baseBytes = nullptr;
    size_t size = 0;
    PIMAGE_NT_HEADERS nt = nullptr;
};

// Resolves a module by name (e.g. "WHGame.dll") and fills the view. Returns false
// if the module isn't loaded.
bool OpenModule(const wchar_t* moduleName, ModuleView& out);

struct SectionView {
    std::string name;     // e.g. ".text", ".rdata"
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t rva = 0;
    uint32_t characteristics = 0;
};

std::vector<SectionView> Sections(const ModuleView& m);
// Subset of sections matching predicate(s).
std::vector<SectionView> ExecutableSections(const ModuleView& m);   // IMAGE_SCN_MEM_EXECUTE
std::vector<SectionView> ReadOnlyDataSections(const ModuleView& m); // .rdata-ish: read but not execute

// Find a null-terminated literal in any of the given sections.
// Returns absolute VA of every occurrence (string must be 0-terminated at the match).
std::vector<uintptr_t> FindCStringsIn(const std::vector<SectionView>& sections,
                                      std::string_view literal);

// Scan executable sections for LEA-rip-relative references to targetVA.
// Matches the 7-byte form `48 8D ?? rel32` (typical for `lea r64, [rip + disp]`).
// Returns absolute VAs of the LEA instruction starts.
std::vector<uintptr_t> FindLeaXrefsTo(const ModuleView& m, uintptr_t targetVA);

// Map an RVA span to its file offset in a RAW ON-DISK PE buffer.
//
// `fileData`/`fileSize` are the bytes of a PE file as it sits on disk (e.g. the
// whole WHGame.dll read from its backing file) — NOT a loaded/relocated image.
// The function parses the file's OWN section headers (DOS + NT headers in the
// buffer) and finds the section whose VirtualAddress range covers
// [rva, rva+length), then maps it via the section's PointerToRawData.
//
// Returns true and writes the file offset to fileOffsetOut iff the WHOLE span
// [rva, rva+length) lies within one section's raw on-disk data AND the resulting
// file range fits inside fileData. Any failure (truncated headers, no covering
// section, span crosses the section's raw-data end, or the file offset would run
// past fileSize) returns false WITHOUT writing fileOffsetOut — the caller treats
// a false return as a fail-loud "this RVA span is not on-disk-readable here",
// never a silent zero. `length` must be > 0.
bool RvaToFileOffsetOnDisk(const uint8_t* fileData, size_t fileSize,
                           uint32_t rva, size_t length, size_t& fileOffsetOut);

// Resolve x64 function bounds via .pdata RUNTIME_FUNCTION entries.
// Returns true and fills [beginVA, endVA) if addressVA is inside any function.
bool FindFunctionBoundsViaPdata(const ModuleView& m,
                                uintptr_t addressVA,
                                uintptr_t& beginVA,
                                uintptr_t& endVA);

}  // namespace kcdx::pe
