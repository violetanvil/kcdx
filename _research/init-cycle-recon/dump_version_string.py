"""Dump the raw bytes around both candidate version strings.

Confirm the cleaner RVA 0x3dba258 is a self-contained null-terminated string
(so it can be read by GetProcAddress-style logic) — not embedded in a larger
sentence.
"""
import pefile

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

CANDIDATES = [
    ("first_occurrence", 0x3c3edef),
    ("clean_standalone", 0x3dba258),
]

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase

    for label, rva in CANDIDATES:
        # Find which section contains this RVA.
        for section in pe.sections:
            sec_rva = section.VirtualAddress
            sec_size = section.Misc_VirtualSize
            if sec_rva <= rva < sec_rva + sec_size:
                data = section.get_data()
                offset = rva - sec_rva
                # Show 32 bytes BEFORE + 64 bytes AT the RVA.
                ctx_start = max(0, offset - 32)
                ctx_end = min(len(data), offset + 80)
                window = data[ctx_start:ctx_end]
                print(f"=== {label} ===")
                print(f"  RVA={rva:#x} VA={base+rva:#x} section={section.Name.rstrip(b'\\x00').decode()}")
                print(f"  bytes_before(32): {data[ctx_start:offset].hex()}")
                print(f"  bytes_at(80):     {data[offset:ctx_end].hex()}")
                # Try to read as a C-string starting AT the RVA.
                end_at = offset
                while end_at < len(data) and data[end_at] != 0 and (data[end_at] >= 0x20 and data[end_at] <= 0x7E):
                    end_at += 1
                c_str = data[offset:end_at].decode("latin-1", errors="replace")
                print(f"  c_string: {c_str!r}  (len={end_at-offset})")
                # Check if it's NUL-terminated immediately after.
                if end_at < len(data) and data[end_at] == 0:
                    print(f"  next byte: NUL (terminator) -> self-contained")
                else:
                    next_byte = data[end_at] if end_at < len(data) else None
                    print(f"  next byte: {next_byte} -> NOT a terminator")
                break
        print()

if __name__ == "__main__":
    main()
