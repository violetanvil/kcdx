"""Search WHGame.dll .rdata for console-related strings and locate their LEA xrefs."""
import re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

# Patterns we want
PATTERNS = [
    b"too many",
    b"too few",
    b"unknown command",
    b"wrong number",
    b"expected ",
    b"argument",
    b"AddCommand",
    b"RegisterCommand",
    b"RegisterAutoComplete",
    b"command not found",
    b"console command",
    b"CommandFunc",
    b"cant be executed",
    b"can't be executed",
    b"recursion limit",
    b"Console command",
    b"executestring",
    b"ExecuteString",
    b"only %d arg",
    b"Unknown",
    b"Function ",
]

def find_strings(data, sec_va_image, image_base):
    """Yield (va, bytes) for each null-terminated ASCII string containing any pattern."""
    hits = {}
    # Find all C-strings
    i = 0
    n = len(data)
    while i < n:
        # find next printable
        j = i
        while j < n and 0x20 <= data[j] < 0x7F:
            j += 1
        if j - i >= 5 and j < n and data[j] == 0:
            s = bytes(data[i:j])
            for p in PATTERNS:
                if p in s.lower() or p in s:
                    va = image_base + sec_va_image + i
                    hits.setdefault(p, []).append((va, s))
                    break
            i = j + 1
        else:
            i = j + 1 if j == i else j
    return hits

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    rdata = None
    text = None
    for s in pe.sections:
        nm = s.Name.rstrip(b"\x00")
        if nm == b".rdata": rdata = s
        if nm == b".text": text = s
    if rdata is None:
        print("no .rdata"); return
    rdata_data = rdata.get_data()
    rdata_va = rdata.VirtualAddress

    hits = find_strings(rdata_data, rdata_va, base)
    for p, lst in sorted(hits.items()):
        print("=" * 78)
        print(f"PATTERN: {p!r}   {len(lst)} hits")
        print("=" * 78)
        for va, s in lst[:30]:
            try:
                txt = s.decode("utf-8", errors="replace")
            except Exception:
                txt = repr(s)
            print(f"  {va:#012x}  {txt!r}")
        if len(lst) > 30:
            print(f"  ... ({len(lst) - 30} more)")
        print()

if __name__ == "__main__":
    main()
