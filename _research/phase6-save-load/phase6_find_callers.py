"""Phase 6 caller scanner.

Given a function VA, finds all `call rel32` instructions in .text that
target it, then walks each caller back to its function entry. Useful
for finding wrappers around an internal function.

Usage:
    py phase6_find_callers.py <path/to/WHGame.dll> <hex_va> [<hex_va> ...]
"""

import sys
from pathlib import Path

import capstone
import pefile

PROLOGUE_BYTES = 24
WALKBACK_MAX = 0x4000


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


def find_function_entry(text_data, text_va, xref_va):
    rel = xref_va - text_va
    if rel < 0 or rel >= len(text_data):
        return None
    start = max(0, rel - WALKBACK_MAX)
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
    return text_va + max(candidate_offsets)


def find_call_to_va(text_data, text_va, target_va):
    """Find all E8 <imm32> CALL instructions that target target_va."""
    callers = []
    n = len(text_data)
    for i in range(n - 5):
        if text_data[i] != 0xE8:
            continue
        disp = int.from_bytes(text_data[i + 1:i + 5], "little", signed=True)
        next_ip = text_va + i + 5
        if next_ip + disp == target_va:
            callers.append(text_va + i)
    return callers


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: phase6_find_callers.py <WHGame.dll> <hex_va> ...")
    dll_path = Path(sys.argv[1])
    targets = [int(v, 16) for v in sys.argv[2:]]

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

    for target in targets:
        print(f"\n{'=' * 78}")
        print(f"Target: 0x{target:016X} (RVA 0x{target - image_base:X})")
        print("=" * 78)
        callers = find_call_to_va(text_data, text_va, target)
        print(f"  {len(callers)} CALL site(s):")
        seen = set()
        for c in callers:
            entry = find_function_entry(text_data, text_va, c)
            if entry is None:
                print(f"    call @ 0x{c:016X}  (entry: NOT FOUND)")
                continue
            if entry in seen:
                print(f"    call @ 0x{c:016X}  (entry: 0x{entry:016X} [dup])")
                continue
            seen.add(entry)
            entry_off = entry - text_va
            prolog = text_data[entry_off:entry_off + PROLOGUE_BYTES]
            prolog40 = text_data[entry_off:entry_off + 40]
            sig24 = " ".join("%02X" % b for b in prolog)
            sig40 = " ".join("%02X" % b for b in prolog40)
            # uniqueness check
            hits24 = []
            for sec_va, sec_data, _ in exec_sections:
                hits24.extend(find_all_in_buf(sec_data, bytes(prolog)))
            hits40 = []
            for sec_va, sec_data, _ in exec_sections:
                hits40.extend(find_all_in_buf(sec_data, bytes(prolog40)))
            insn_text = []
            for insn in md.disasm(prolog40, entry):
                insn_text.append(f"      {insn.mnemonic} {insn.op_str}")
                if len(insn_text) >= 6:
                    break
            print(f"\n    call @ 0x{c:016X}")
            print(f"    -> entry: 0x{entry:016X} (RVA 0x{entry - image_base:X})")
            print(f"       Tier-1 (24B): {sig24}  [{len(hits24)} hit(s)]")
            print(f"       Tier-2 (40B): {sig40}  [{len(hits40)} hit(s)]")
            print(f"       Decoded:")
            for line in insn_text:
                print(line)


if __name__ == "__main__":
    main()
