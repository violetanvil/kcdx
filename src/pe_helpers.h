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

// Resolve x64 function bounds via .pdata RUNTIME_FUNCTION entries.
// Returns true and fills [beginVA, endVA) if addressVA is inside any function.
bool FindFunctionBoundsViaPdata(const ModuleView& m,
                                uintptr_t addressVA,
                                uintptr_t& beginVA,
                                uintptr_t& endVA);

}  // namespace kcdx::pe
