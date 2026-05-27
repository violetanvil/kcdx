"""extend_genv_sig.py — derive a longer, unique AOB for the gEnv pConsole-MOV
site by dumping the actual instruction bytes around RVA 0x86ad99 and printing
progressively longer prefixes. We want the SHORTEST prefix that is unique in
.text.
"""

import sys
from pathlib import Path

import pefile

DEFAULT_DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
PCONSOLE_MOV_RVA = 0x86AD99
# We'll extract the bytes [pconsole_mov : pconsole_mov + 64] and check
# uniqueness for masked variants.


def find_count(data, pat, mask):
    n = len(pat)
    hits = 0
    for i in range(len(data) - n + 1):
        ok = True
        for j in range(n):
            if mask[j] and data[i + j] != pat[j]:
                ok = False
                break
        if ok:
            hits += 1
            if hits > 1:
                return hits  # short-circuit
    return hits


def main():
    dll = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL)
    pe = pefile.PE(str(dll), fast_load=True)

    text_sec = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    text_data = text_sec.get_data()
    text_rva_base = text_sec.VirtualAddress

    off = PCONSOLE_MOV_RVA - text_rva_base
    raw = text_data[off:off + 64]
    print("# raw bytes at pConsole_MOV_RVA:")
    print("  " + " ".join(f"{b:02X}" for b in raw))
    print()

    # The pConsole MOV is "48 8B 0D ?? ?? ?? ??" — 4 bytes are RIP-relative
    # disp32 (will change per build). Mask those four bytes.
    # Then continue greedily with non-masked bytes.
    # Try lengths from 8 up to 40.
    for n_bytes in range(8, 41):
        # Build pattern: first 3 bytes fixed (48 8B 0D), next 4 masked
        # (RIP+disp32), then the rest fixed up to n_bytes.
        if n_bytes <= 7:
            continue
        pat = bytearray(raw[:n_bytes])
        mask = [True] * n_bytes
        for i in range(3, 7):
            mask[i] = False  # disp32 of MOV
        # The LEA after the V1.4+ context (4C 8B 92 18 01 00 00 48 8D 15)
        # is at offset 23..29. Mask its disp32 (offset 26..29) when present.
        if n_bytes >= 30:
            for i in range(26, 30):
                mask[i] = False
        hits = find_count(text_data, pat, mask)
        if hits == 1:
            # Format
            tokens = []
            for i in range(n_bytes):
                tokens.append("??" if not mask[i] else f"{pat[i]:02X}")
            print(f"  n_bytes={n_bytes:2d}  hits=1  SIG: {' '.join(tokens)}")
            print(f"  (RVA={hex(PCONSOLE_MOV_RVA)})")
            return
    print("  no unique prefix found within 40 bytes")


if __name__ == "__main__":
    main()
