"""Find the canonical KCD2 build-version string inside WHGame.dll.

The launcher log shows "1.5.1164953" (major.minor.build). VS_VERSIONINFO does
NOT carry it on this build. The engine MUST hold this string somewhere
internally (it surfaces in crash dumps, internal logging, save-file headers).

Strategy:
  1. Scan .rdata for ASCII strings matching the shape:
       - exactly "<major>.<minor>.<build>"        e.g. "1.5.1164953"
       - or "release_<major>_<minor>_<build>_<minor>" e.g. "release_1_5_1164953_841"
       - or "build <build>"
  2. Print each candidate with its RVA + neighbor strings (gives context).
  3. Look for any global pointer that points at one of these strings so the
     engine has a stable callable entry.

Output: human-readable listing -> stdout, redirect to a file.
"""
import re
import pefile

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

# Conservative regex for build strings — we want SEMVER-shaped with a build
# field big enough to be a real KCD2 build number (>= 5 digits typically).
PATTERN_SEMVER = re.compile(rb"\b(\d{1,3})\.(\d{1,3})\.(\d{4,8})\b")
PATTERN_RELEASE = re.compile(rb"\brelease_(\d{1,3})_(\d{1,3})_(\d{4,8})_(\d{1,4})\b")
PATTERN_BUILD_TAG = re.compile(rb"\bbuild[_\s]?(\d{4,8})\b", re.IGNORECASE)
# CryEngine often uses "GameDLL Version" or similar
PATTERN_GAMEDLL = re.compile(rb"GameDLL Version", re.IGNORECASE)

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase

    # Walk every section and look for matches in section bodies.
    for section in pe.sections:
        name = section.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        if name not in (".rdata", ".data", ".text"):
            continue
        data = section.get_data()
        section_rva = section.VirtualAddress

        for m in PATTERN_RELEASE.finditer(data):
            offset = m.start()
            rva = section_rva + offset
            va = base + rva
            # Print full match + a window of context (32 bytes before, 32 after).
            ctx_start = max(0, offset - 32)
            ctx_end = min(len(data), offset + len(m.group(0)) + 32)
            ctx = data[ctx_start:ctx_end].decode("latin-1", errors="replace")
            ctx = ctx.replace("\x00", ".")
            print(f"[RELEASE] section={name} rva={rva:#x} va={va:#x}")
            print(f"  match = {m.group(0)!r}")
            print(f"  ctx   = {ctx!r}")
            print()

        for m in PATTERN_SEMVER.finditer(data):
            offset = m.start()
            rva = section_rva + offset
            va = base + rva
            # Print full match + a window of context.
            ctx_start = max(0, offset - 32)
            ctx_end = min(len(data), offset + len(m.group(0)) + 32)
            ctx = data[ctx_start:ctx_end].decode("latin-1", errors="replace")
            ctx = ctx.replace("\x00", ".")
            print(f"[SEMVER] section={name} rva={rva:#x} va={va:#x}")
            print(f"  match = {m.group(0)!r}")
            print(f"  ctx   = {ctx!r}")
            print()

        for m in PATTERN_BUILD_TAG.finditer(data):
            offset = m.start()
            rva = section_rva + offset
            va = base + rva
            ctx_start = max(0, offset - 32)
            ctx_end = min(len(data), offset + len(m.group(0)) + 32)
            ctx = data[ctx_start:ctx_end].decode("latin-1", errors="replace")
            ctx = ctx.replace("\x00", ".")
            print(f"[BUILD] section={name} rva={rva:#x} va={va:#x}")
            print(f"  match = {m.group(0)!r}")
            print(f"  ctx   = {ctx!r}")
            print()

        for m in PATTERN_GAMEDLL.finditer(data):
            offset = m.start()
            rva = section_rva + offset
            va = base + rva
            ctx_start = max(0, offset - 32)
            ctx_end = min(len(data), offset + len(m.group(0)) + 64)
            ctx = data[ctx_start:ctx_end].decode("latin-1", errors="replace")
            ctx = ctx.replace("\x00", ".")
            print(f"[GAMEDLL] section={name} rva={rva:#x} va={va:#x}")
            print(f"  match = {m.group(0)!r}")
            print(f"  ctx   = {ctx!r}")
            print()

if __name__ == "__main__":
    main()
