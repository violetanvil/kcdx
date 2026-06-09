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
// G2 — .data-ish: writable AND not executable (the `data_slot` "did we land in
// .data" check). The mirror of ReadOnlyDataSections, opposite write bit:
// .rdata is read+not-write+not-exec; .data is read+write+not-exec. `.data`
// is filtered OUT of both Executable and ReadOnly accessors (it is writable),
// so it had no convenience predicate before. SOURCE: IMAGE_SCN_MEM_WRITE /
// IMAGE_SCN_MEM_EXECUTE section characteristics (winnt.h).
std::vector<SectionView> WritableDataSections(const ModuleView& m);

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

// ---------------------------------------------------------------------------
// ON-DISK section span access — the on-disk analogue of Sections/Executable/
// ReadOnly/Writable above. The ModuleView variants take a LIVE relocated image
// (GetModuleHandleW). The survival static checks (D25 — on-disk, not live)
// need section spans over the RAW on-disk file buffer instead, the same buffer
// + header-parse RvaToFileOffsetOnDisk already walks. Each on-disk section span
// is described RVA-relative so a scan over it can attribute a hit back to an
// RVA; `data` points into the raw `fileData` buffer at the section's
// PointerToRawData. A section's on-disk span is bounded by SizeOfRawData (the
// uninitialized tail beyond it has no on-disk backing).

// One on-disk section's raw-data span. `data`/`size` cover [PointerToRawData,
// +SizeOfRawData) in the supplied fileData; `rva` is the section RVA so a hit
// at `data+k` maps to `rva + k`.
struct OnDiskSection {
    std::string    name;            // ".text" / ".rdata" / ".data" / …
    const uint8_t* data = nullptr;  // into fileData at PointerToRawData
    size_t         size = 0;        // SizeOfRawData (on-disk extent)
    uint32_t       rva = 0;         // VirtualAddress
    uint32_t       virtualSize = 0; // Misc.VirtualSize (in-memory extent; can
                                    // exceed SizeOfRawData via a zero-init tail)
    uint32_t       characteristics = 0;
};

// Enumerate every section's on-disk raw-data span from a raw PE file buffer.
// Parses the buffer's OWN DOS+NT+section headers (the same parse
// RvaToFileOffsetOnDisk does). Returns empty on a truncated/malformed buffer —
// the caller treats empty as a fail-loud "the on-disk file is not parseable",
// never a silent skip. A section whose [PointerToRawData, +SizeOfRawData) would
// run past `fileSize` is dropped (it cannot be read on-disk).
std::vector<OnDiskSection> OnDiskSections(const uint8_t* fileData, size_t fileSize);

// Filtered on-disk section sets — the on-disk mirrors of the ModuleView
// accessors, same characteristic predicates.
std::vector<OnDiskSection> OnDiskExecutableSections(const uint8_t* fileData, size_t fileSize);
std::vector<OnDiskSection> OnDiskReadOnlyDataSections(const uint8_t* fileData, size_t fileSize);
std::vector<OnDiskSection> OnDiskWritableDataSections(const uint8_t* fileData, size_t fileSize);

// G1 — FORWARD RIP-relative disp32 follower (on-disk). Given the RVA of an
// instruction with a 4-byte RIP-relative displacement, read the disp32 AT a
// known displacement-offset within the instruction and compute the target VA:
// `target = (instrRva + instrLen) + disp32`. The FORWARD direction of the
// arithmetic FindLeaXrefsTo does in reverse (.cpp:96-100). For `data_slot`
// (follow disp32 from an instruction_anchor → the .data slot) and the
// `instruction_anchor` final-MOV step.
//
//   fileData/fileSize  — the raw on-disk PE buffer.
//   instrRva           — RVA of the instruction start.
//   dispOffsetInInstr  — byte offset of the disp32 field within the instruction
//                        (e.g. 3 for `48 8B 0D <disp32>` — REX + opcode + modrm).
//   instrLen           — total instruction length (the disp32 is relative to the
//                        instruction's END, so instrEnd = instrRva + instrLen).
//
// Returns true and writes the FORWARD target RVA to targetRvaOut iff the disp32
// field lies wholly within an on-disk section (readable). Fail-loud false (no
// write) on any out-of-range / unparseable input — never a silent zero.
bool FindDisp32Forward(const uint8_t* fileData, size_t fileSize,
                       uint32_t instrRva, uint32_t dispOffsetInInstr,
                       uint32_t instrLen, uint32_t& targetRvaOut);

// G4 — is `rva` inside any executable (.text-class) on-disk section? The
// vtable_base "is this slot a plausible .text code pointer" classifier: a
// vtable slot holds an absolute VA at runtime; on-disk it holds either the
// preferred-base absolute (image_base + rva) or 0/a relocation placeholder.
// The stable, relocation-independent property to check is "the slot's RVA
// (value − image_base) lands in an executable section." This predicate answers
// the RVA half; IsTextPointerOnDisk (below) does the value→RVA→.text test.
bool IsRvaInExecutableSection(const uint8_t* fileData, size_t fileSize, uint32_t rva);

// G4 — classify an on-disk vtable-slot qword as a plausible relocated .text
// code pointer. An on-disk vtable slot holds the PREFERRED-base absolute
// address (imageBase + targetRva) because the on-disk image has no relocations
// applied (the loader fixes these up at load). So: subtract the file's own
// ImageBase, and test the resulting RVA against the executable sections. A slot
// of 0 (a relocation that targets an import / is patched at load) is NOT a
// plausible .text pointer → false (fail toward Changed, never a false
// Unchanged). Reads ImageBase from the on-disk OptionalHeader.
bool IsTextPointerOnDisk(const uint8_t* fileData, size_t fileSize, uint64_t slotValue);

}  // namespace kcdx::pe
