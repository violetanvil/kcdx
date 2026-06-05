"""seeds_shared.survival_checker_primitives -- the low-level byte/PE/decode
primitives the per-kind survival reference checker (survival_checker.py) is built on.

ONE CONCERN: turning a DLL's on-disk bytes into the spans + derivations the per-kind
checks operate over -- PE section mapping, an AOB (bytes + `?` mask) scanner, the
minimal RIP-relative disp32 decoder, and the qword reader. NO per-kind verdict logic
lives here (that is survival_checker.py); these are the reusable byte tools the kind
checks call. Split out per no-monolith.md (primitives here, dispatch+DAG there).

ON-DISK, NOT LIVE (corrected D25 / cross_impl_fixture.py header): every primitive
reads RAW ON-DISK bytes -- a `.text`/`.rdata`/`.data` section's on-disk span, the
on-disk bytes at an RVA. This is the STATIC checker; the live loaded-image survival
pass is engine-only (NOT this module's job).

REUSE: pefile (already a repo dependency -- version_resolver.py + import_to_sqlite.py
use it) for the section table. The minimal disp32 decoder is built to the probe-0.2
recipe (_research/maintainer-tool-verification-engine/probe-0.2-x86-decoder-finding.md):
REX.W 0x48 | MOV 0x8B / LEA 0x8D | RIP-relative ModRM (mod=00, rm=101) | signed
little-endian disp32; target = rva + 7 + disp32. The forward-direction follower the
0.3 probe named as gap G1 (probe-0.3-pe-helpers-surface-finding.md).
"""


# ---------------------------------------------------------------------------
# Section view -- one PE section's name + its on-disk span + its RVA window. The
# fixture (cross_impl_fixture.py) hands the check a BARE byte block (no PE
# scaffolding) for the in-memory path; the real path maps a DLL's sections via
# pefile. Both feed the same per-kind check, so the check sees a uniform
# {name, data, rva, size} view in either mode.
# ---------------------------------------------------------------------------
class SectionView:
    """One section: name (e.g. '.text'), its on-disk bytes, and its RVA window.

    data  -- the section's raw on-disk bytes (what an AOB scan / literal search /
             qword read operates over).
    rva   -- the section's RVA (VirtualAddress); maps an in-section offset to an RVA.
    size  -- len(data).
    """
    __slots__ = ("name", "data", "rva", "size")

    def __init__(self, name, data, rva):
        self.name = name
        self.data = bytes(data)
        self.rva = int(rva)
        self.size = len(self.data)

    def contains_rva(self, rva):
        """Does `rva` fall inside this section's RVA window?"""
        return self.rva <= rva < (self.rva + self.size)

    def offset_of_rva(self, rva):
        """In-section byte offset of `rva` (caller ensures contains_rva)."""
        return rva - self.rva


def sections_from_dll(dll_path):
    """Parse `dll_path` with pefile and return a list of SectionView over its RAW
    ON-DISK section bytes. REUSE: pefile (already a repo dependency).

    On-disk, NOT the loaded image: `section.get_data()` returns the file's raw
    section bytes (the static check's input), and the RVA is the section's
    VirtualAddress. The function-kind hash, the callsite AOB scan, the string
    search, the qword read all operate over these on-disk spans.
    """
    import pefile  # local import -- only the real-DLL path needs it (mirrors
                   # version_resolver._scan_rdata_matches' local pefile import).

    pe = pefile.PE(dll_path, fast_load=True)
    try:
        out = []
        for section in pe.sections:
            name = section.Name.rstrip(b"\x00").decode("latin-1")
            out.append(SectionView(name, section.get_data(), section.VirtualAddress))
        return out
    finally:
        pe.close()


# ---------------------------------------------------------------------------
# AOB (array-of-bytes) pattern -- bytes + a `?`/`??` wildcard mask. The stored
# survival_aob is the exact pattern the resolver matches (callsite +
# instruction_anchor shape). Parse "48 8B 41 08 ?? 3C 02" into (bytes, mask).
# ---------------------------------------------------------------------------
def parse_aob(pattern):
    """Parse an AOB pattern string ('48 8B 0D ?? ?? ?? ??') into (values, mask).

    values -- list[int 0..255]; the byte at a wildcard position is 0 (ignored via mask).
    mask   -- list[bool]; True = this position must match `values[i]`, False = wildcard.

    Accepts `?` or `??` as a wildcard token. Whitespace-separated hex bytes. A
    malformed token raises ValueError (fail loud -- a bad stored AOB is a data
    defect, never a silent skip).
    """
    values = []
    mask = []
    for tok in pattern.split():
        if tok in ("?", "??"):
            values.append(0)
            mask.append(False)
            continue
        if len(tok) != 2:
            raise ValueError("AOB token %r is not a 2-hex-digit byte or a wildcard" % (tok,))
        values.append(int(tok, 16))   # raises ValueError on a non-hex token
        mask.append(True)
    if not values:
        raise ValueError("empty AOB pattern")
    return values, mask


def aob_find_all(haystack, values, mask):
    """Return every offset in `haystack` (bytes) where the AOB (values + mask)
    matches. A wildcard position (mask[i] False) matches any byte.

    Plain forward scan -- the count of hits is what the callsite check needs
    (1 = unique, 0 = gone, >1 = ambiguous). NOT a hot path (a one-shot
    maintainer-tool / test scan over a bounded section); a straightforward scan,
    no allocation discipline owed.
    """
    n = len(values)
    hits = []
    if n == 0 or len(haystack) < n:
        return hits
    last = len(haystack) - n
    i = 0
    while i <= last:
        ok = True
        for j in range(n):
            if mask[j] and haystack[i + j] != values[j]:
                ok = False
                break
        if ok:
            hits.append(i)
        i += 1
    return hits


def aob_matches_at(buf, values, mask, offset=0):
    """Does the AOB (values + mask) match `buf` at `offset`? Used by the
    instruction_anchor SHAPE assert (the stored AOB shape at the anchor site)."""
    n = len(values)
    if offset < 0 or offset + n > len(buf):
        return False
    for j in range(n):
        if mask[j] and buf[offset + j] != values[j]:
            return False
    return True


# ---------------------------------------------------------------------------
# The minimal RIP-relative disp32 decoder (probe 0.2 recipe). At an
# instruction-byte buffer, decode just enough to FOLLOW the displacement:
#   REX.W (0x48) | opcode (0x8B MOV | 0x8D LEA) | ModRM (mod=00, rm=101)
#   | signed little-endian disp32
#   target = instr_rva + 7 + disp32   (RIP-relative is relative to the NEXT instr)
# This is the FORWARD follower (gap G1 in probe 0.3): instruction-RVA known ->
# read disp32 -> compute the target. 7 bytes fixed (REX+op+ModRM+disp32).
# ---------------------------------------------------------------------------
_REX_W = 0x48
_OP_MOV = 0x8B
_OP_LEA = 0x8D
RIP_INSTR_LEN = 7  # REX(1) + opcode(1) + ModRM(1) + disp32(4); no SIB, no immediate.


class DecodeError(Exception):
    """The instruction bytes are not a minimal REX.W MOV/LEA RIP-relative form.

    Carries the reason (which byte failed the recipe) so a Changed verdict /
    derivation failure names WHY the shape did not match (per probe 0.2: an
    out-of-scope encoding is reported, never guessed)."""


def decode_rip_disp32(instr_bytes):
    """Decode a minimal REX.W MOV/LEA RIP-relative instruction at the START of
    `instr_bytes`; return (opcode, disp32_signed). Raises DecodeError if the bytes
    are not that exact form (the probe-0.2 minimal scope -- a wider encoding is a
    DecodeError, never a silent wrong-follow).

    opcode      -- 0x8B (MOV) or 0x8D (LEA), the matched opcode.
    disp32_signed -- the displacement as a SIGNED little-endian int32.
    """
    if len(instr_bytes) < RIP_INSTR_LEN:
        raise DecodeError(
            "need %d bytes to decode a RIP-relative disp32; got %d"
            % (RIP_INSTR_LEN, len(instr_bytes)))
    rex, opcode, modrm = instr_bytes[0], instr_bytes[1], instr_bytes[2]
    if rex != _REX_W:
        raise DecodeError("REX byte 0x%02X != 0x48 (REX.W); minimal scope is REX.W only" % rex)
    if opcode not in (_OP_MOV, _OP_LEA):
        raise DecodeError("opcode 0x%02X is not MOV(0x8B) or LEA(0x8D)" % opcode)
    mod = (modrm >> 6) & 0x3
    rm = modrm & 0x7
    if mod != 0 or rm != 5:
        raise DecodeError(
            "ModRM 0x%02X is not RIP-relative (need mod=00, rm=101)" % modrm)
    disp32 = int.from_bytes(instr_bytes[3:7], "little", signed=True)
    return opcode, disp32


def follow_rip_disp32(instr_rva, instr_bytes):
    """FORWARD follow: at instruction RVA `instr_rva` with `instr_bytes`, decode the
    RIP-relative disp32 and return the target RVA = instr_rva + 7 + disp32.

    The exact arithmetic probe 0.2 confirmed on the real binary
    (0x0086AD99 -> disp32 0x040C0B08 -> 0x0492B8A8, EXACT). Raises DecodeError if
    the bytes are not the minimal MOV/LEA RIP-relative form (shape mismatch ->
    the derivation cannot complete -> the caller maps that to Changed)."""
    _opcode, disp32 = decode_rip_disp32(instr_bytes)
    return (instr_rva + RIP_INSTR_LEN + disp32) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# qword reader (vtable_base table-shape) -- read N little-endian u64 at an offset.
# ---------------------------------------------------------------------------
def read_qwords(buf, offset, count):
    """Read `count` little-endian u64 starting at `buf[offset]`. Raises ValueError
    if the span runs past the buffer (a shrunk table -- the caller maps a short
    read to Changed)."""
    end = offset + count * 8
    if offset < 0 or end > len(buf):
        raise ValueError(
            "qword span [%d, %d) runs past buffer of len %d" % (offset, end, len(buf)))
    return [int.from_bytes(buf[offset + i * 8: offset + i * 8 + 8], "little")
            for i in range(count)]
