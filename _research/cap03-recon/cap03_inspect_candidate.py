"""Inspect a specific CAP-03 candidate: read 64 bytes at the VA, confirm
uniqueness of 24/32/40-byte prefixes, and dump nearby strings/xrefs."""

import sys
from pathlib import Path
import pefile
import capstone


def find_all_in_bytes(data, pat_bytes, pat_mask):
    hits = []
    n = len(pat_bytes)
    for i in range(len(data) - n + 1):
        ok = True
        for j in range(n):
            if pat_mask[j] and data[i + j] != pat_bytes[j]:
                ok = False
                break
        if ok:
            hits.append(i)
    return hits


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: cap03_inspect_candidate.py <WHGame.dll> <hex_va>")
    dll_path = Path(sys.argv[1])
    target_va = int(sys.argv[2], 16)

    pe = pefile.PE(str(dll_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    exec_sections = []
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        chars = sec.Characteristics
        if not (chars & 0x20000000):
            continue
        sec_va = image_base + sec.VirtualAddress
        sec_data = sec.get_data()
        exec_sections.append((sec_va, sec_data, name))

    # Find the section containing the target.
    sec = next(
        (s for s in exec_sections if s[0] <= target_va < s[0] + len(s[1])),
        None
    )
    if sec is None:
        sys.exit("VA not in any exec section")
    sec_va, sec_data, sec_name = sec
    off = target_va - sec_va

    n = 96
    if off + n > len(sec_data):
        n = len(sec_data) - off
    head = sec_data[off:off + n]

    print(f"VA:          0x{target_va:016X} (section {sec_name})")
    print(f"head 64B:    " + " ".join("%02X" % b for b in head[:64]))

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    print("\nDisassembly (first 24 insns):")
    insn_count = 0
    for ins in md.disasm(head, target_va):
        print(f"  0x{ins.address:016X}: {ins.mnemonic:<7} {ins.op_str}")
        insn_count += 1
        if insn_count >= 24:
            break
        if ins.mnemonic in ("ret", "retn"):
            break

    # Uniqueness check for prefix lengths.
    print("\nUniqueness of prefix lengths:")
    for ln in (16, 20, 24, 28, 32, 40, 48):
        prefix = head[:ln]
        mask = [True] * len(prefix)
        total = 0
        for sec_va2, sec_data2, _ in exec_sections:
            total += len(find_all_in_bytes(sec_data2, prefix, mask))
        print(f"  {ln}B: {total} match(es) across exec sections")


if __name__ == "__main__":
    main()
