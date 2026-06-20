"""KI-0026 — read the NGX/FSR2 assertion message strings the raise reporter emits.

The raising function (0x2459810) is an FSR2 fatal-assert reporter: it calls
FUN_1804d4510(rcx=r14, edx=<msg-id>, r8=<string ptr>, ...) for a series of
message lines, then raises 0xC8. The string pointers (RVAs below) NAME the
resource/condition that was null — the exact detail P-live would otherwise need
a live hook to capture. Read them straight from .rdata.

String-pointer RVAs seen in the reporter body (the r8 operands), in order:
  0x3a66ac0 (r14 base, the category/file string)
  0x3dbb300, 0x3dc3a74, 0x3dbb300 (msg 0xf6b/0xf6c/0xf6d lines)
  0x3a6c49c (msg 0xf6f — also loaded by the CreateGameStartup caller frame)
  0x3dc3a50 (msg 0xf74 — the rdi!=0 arm)
Also dump a neighborhood of .rdata around each so adjacent format strings show.
"""
import pefile

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase


def read_cstr(rva, maxlen=300):
    data = pe.get_data(rva, maxlen)
    end = data.find(b"\x00")
    if end < 0:
        end = maxlen
    try:
        return data[:end].decode("utf-8", errors="replace")
    except Exception:
        return repr(data[:end])


def neighborhood(rva, span=0x140):
    """Print printable ASCII runs (>=4) in [rva-span, rva+span) so adjacent
    format strings are visible even if the exact pointer lands mid-record."""
    lo = rva - span
    data = pe.get_data(lo, span * 2)
    runs = []
    cur = bytearray()
    cur_start = lo
    for i, b in enumerate(data):
        if 0x20 <= b < 0x7f:
            if not cur:
                cur_start = lo + i
            cur.append(b)
        else:
            if len(cur) >= 4:
                runs.append((cur_start, cur.decode("ascii", "replace")))
            cur = bytearray()
    if len(cur) >= 4:
        runs.append((cur_start, cur.decode("ascii", "replace")))
    return runs


PTRS = [
    ("r14/category (0xf6b base)", 0x3a66ac0),
    ("msg 0xf6b/0xf6d line", 0x3dbb300),
    ("msg 0xf6c line", 0x3dc3a74),
    ("msg 0xf6f line (shared w/ caller frame)", 0x3a6c49c),
    ("msg 0xf74 line (rdi!=0 arm)", 0x3dc3a50),
]

for label, rva in PTRS:
    print("=" * 78)
    print(f"== {label}   RVA={rva:#x}")
    print("=" * 78)
    print(f"  AT PTR: {read_cstr(rva)!r}")
    print("  neighborhood ASCII runs:")
    for r_rva, s in neighborhood(rva):
        mark = " <==" if r_rva == rva else ""
        print(f"    {r_rva:#x}  {s!r}{mark}")
    print()
