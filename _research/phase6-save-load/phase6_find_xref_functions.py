"""Phase 6 xref-to-function resolver.

Given a string VA, finds LEA xrefs to that VA in .text, then for each
xref walks backward to the function prologue. Emits a 24-byte function-
entry AOB candidate per unique function found, and verifies its .text
uniqueness.

Used to resolve CCryAction::SaveGame / LoadGame entry points from
string anchors discovered in phase6_dump_string_anchors.py.

Usage:
    py phase6_find_xref_functions.py <path/to/WHGame.dll> <hex_va_of_string>
    py phase6_find_xref_functions.py <path/to/WHGame.dll> <hex_va_of_string> ...

Multiple VAs may be passed and will all be scanned.

Walk-back heuristic: from the LEA xref instruction, scan backward
looking for a function-prologue byte pattern (INT3 padding followed by
a prologue, OR known prologue opcodes preceded by an aligned address).
"""

import sys
from pathlib import Path

import capstone
import pefile


PROLOGUE_BYTES = 24
WALKBACK_MAX = 0x4000  # 16 KiB max walk-back from xref to function start


# Same prologue heuristic as cap03_find_candidates.py:looks_like_prologue
def looks_like_prologue(buf):
    if len(buf) < 3:
        return False
    b0, b1 = buf[0], buf[1]
    if b0 == 0xFF and b1 == 0x25:
        return False
    if b0 == 0xE9:
        return False
    return (
        (b0 == 0x48 and b1 in (0x89, 0x81, 0x83, 0x8B, 0x8D)) or
        (b0 == 0x40 and b1 in (0x53, 0x55, 0x56, 0x57)) or
        (b0 in (0x53, 0x55, 0x56, 0x57)) or
        (b0 == 0x41 and b1 in (0x54, 0x55, 0x56, 0x57)) or
        (b0 == 0x4C and b1 in (0x89, 0x8B))
    )


def find_all_in_buf(data, pat_bytes):
    n = len(pat_bytes)
    out = []
    for i in range(len(data) - n + 1):
        if data[i:i + n] == pat_bytes:
            out.append(i)
    return out


def find_xrefs_to_va(text_data, text_va, target_va):
    """Find all 7-byte LEA RIP-relative xrefs to target_va.

    Encodings considered:
      48 8D ?? <imm32>      (REX.W LEA with modR/M selecting RIP-rel)
      4C 8D ?? <imm32>      (REX.WR LEA, for r8/r9 etc.)
    The modR/M byte must have mod=00 and rm=101 (RIP-relative).
    """
    xrefs = []
    n = len(text_data)
    for i in range(n - 7):
        b0 = text_data[i]
        if b0 not in (0x48, 0x4C):
            continue
        if text_data[i + 1] != 0x8D:
            continue
        modrm = text_data[i + 2]
        # mod must be 00 (top two bits 00), rm must be 101
        if (modrm & 0xC7) != 0x05:
            continue
        disp = int.from_bytes(
            text_data[i + 3:i + 7], "little", signed=True
        )
        next_ip = text_va + i + 7
        target = next_ip + disp
        if target == target_va:
            xrefs.append(text_va + i)
    return xrefs


def find_function_entry(text_data, text_va, xref_va, image_base):
    """From an xref VA, walk backward to the nearest function start.

    Heuristic: scan backward looking for an INT3 byte (0xCC) followed by
    a prologue-like byte sequence. Most MSVC-compiled functions are
    INT3-padded at their start.
    """
    rel = xref_va - text_va
    if rel < 0 or rel >= len(text_data):
        return None
    start = max(0, rel - WALKBACK_MAX)
    # Scan backwards from xref looking for an INT3 padding boundary.
    # The byte AT a prologue start is preceded by 0xCC (single int3) or
    # 0x90 (nop padding) in MSVC output. Sometimes 0xC3 (ret of previous
    # function). Walk backwards looking for one of those, then check
    # whether the following bytes look like a prologue.
    candidate_offsets = []
    for off in range(rel - 1, start, -1):
        b = text_data[off]
        if b in (0xCC, 0xC3) and off + 1 <= len(text_data) - 16:
            if looks_like_prologue(text_data[off + 1:off + 17]):
                candidate_offsets.append(off + 1)
                if len(candidate_offsets) >= 4:
                    break
    if not candidate_offsets:
        return None
    # Take the candidate CLOSEST to the xref site (largest offset).
    closest = max(candidate_offsets)
    return text_va + closest


def main():
    if len(sys.argv) < 3:
        sys.exit(
            "usage: phase6_find_xref_functions.py <WHGame.dll> "
            "<hex_va> [<hex_va> ...]"
        )
    dll_path = Path(sys.argv[1])
    target_vas = [int(v, 16) for v in sys.argv[2:]]

    pe = pefile.PE(str(dll_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    sections = {}
    exec_sections = []
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        sec_va = image_base + sec.VirtualAddress
        sec_data = bytes(sec.get_data())
        sections[name] = (sec_va, sec_data)
        if sec.Characteristics & 0x20000000:
            exec_sections.append((sec_va, sec_data, name))

    text_va, text_data = sections[".text"]

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    for target_va in target_vas:
        print(f"\n{'=' * 78}")
        print(f"Target VA: 0x{target_va:016X}")
        print("=" * 78)

        xrefs = find_xrefs_to_va(text_data, text_va, target_va)
        print(f"  {len(xrefs)} LEA xref(s) in .text:")
        for x in xrefs:
            print(f"    xref @ VA=0x{x:016X}")

        # For each xref, find function entry and emit 24-byte sig.
        unique_entries = {}  # entry_va -> first xref that found it
        for x in xrefs:
            entry = find_function_entry(text_data, text_va, x, image_base)
            if entry is None:
                print(f"    xref @ 0x{x:016X}: NO PROLOGUE FOUND in 16KB walkback")
                continue
            if entry in unique_entries:
                continue
            unique_entries[entry] = x

        if not unique_entries:
            continue

        print(f"\n  {len(unique_entries)} unique function entry(ies) found:")
        for entry_va, xref_va in unique_entries.items():
            entry_off = entry_va - text_va
            prolog = text_data[entry_off:entry_off + PROLOGUE_BYTES]
            sig = " ".join("%02X" % b for b in prolog)

            # Verify .text-uniqueness of 24-byte prolog.
            hits = []
            for sec_va, sec_data, sec_name in exec_sections:
                hits.extend(
                    (sec_va + i, sec_name)
                    for i in find_all_in_buf(sec_data, bytes(prolog))
                )

            # Also try a longer (40-byte) pattern as Tier-2.
            prolog40 = text_data[entry_off:entry_off + 40]
            sig40 = " ".join("%02X" % b for b in prolog40)
            hits40 = []
            for sec_va, sec_data, sec_name in exec_sections:
                hits40.extend(
                    (sec_va + i, sec_name)
                    for i in find_all_in_buf(sec_data, bytes(prolog40))
                )

            # Decode first 8 instructions for human review.
            insn_text = []
            for insn in md.disasm(prolog40, entry_va):
                insn_text.append(f"      {insn.mnemonic} {insn.op_str}")
                if len(insn_text) >= 8:
                    break

            rva = entry_va - image_base
            print(f"\n  Function entry @ VA=0x{entry_va:016X} "
                  f"(RVA=0x{rva:X})")
            print(f"    via xref @ 0x{xref_va:016X}")
            print(f"    Tier-1 sig (24 B): {sig}")
            print(f"      .text uniqueness: {len(hits)} hit(s)")
            print(f"    Tier-2 sig (40 B): {sig40}")
            print(f"      .text uniqueness: {len(hits40)} hit(s)")
            print(f"    Decoded prologue:")
            for line in insn_text:
                print(line)


if __name__ == "__main__":
    main()
