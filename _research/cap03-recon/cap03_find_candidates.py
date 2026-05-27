"""CAP-03 hook-target finder.

Disassembles WHGame.dll, finds the function whose prologue matches the
yobson1 `update` sig, walks its direct CALL targets, and for each callee
emits a 24-byte function-entry sig + uniqueness check.

The goal is to find a function called every tick from `update`, that has
a unique function-entry pattern in .text, suitable as a kcdx [[hook]]
target for CAP-03.

Usage:
    py cap03_find_candidates.py <path/to/WHGame.dll>
"""

import sys
from pathlib import Path

import capstone
import pefile


UPDATE_SIG = (
    "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 "
    "48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? "
    "44 0F 29 48 ? 44 0F 29 50 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1"
)
PCALL_SIG = "48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8"
LOADFILE_SIG = (
    "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8D AC 24 ? ? ? ? "
    "48 81 EC 40 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 79 10"
)

PROLOGUE_BYTES = 24
MAX_INSNS_IN_UPDATE = 200000  # safety cap
MAX_CANDIDATES = 40


def parse_pattern(sig):
    bytes_ = []
    mask = []
    for tok in sig.split():
        if tok in ("?", "??"):
            bytes_.append(0)
            mask.append(False)
        else:
            bytes_.append(int(tok, 16))
            mask.append(True)
    return bytes(bytes_), mask


def find_all_in_bytes(data, base_va, pat_bytes, pat_mask):
    """Search a buffer for masked pattern. Return list of file-offsets."""
    hits = []
    n = len(pat_bytes)
    if n == 0:
        return hits
    for i in range(len(data) - n + 1):
        ok = True
        for j in range(n):
            if pat_mask[j] and data[i + j] != pat_bytes[j]:
                ok = False
                break
        if ok:
            hits.append(i)
    return hits


def find_all_in_sections(executable_sections, pat_bytes, pat_mask):
    """Search executable sections; return list of (va, file_offset_in_section, section)."""
    hits = []
    for sec_va, sec_data, sec_name in executable_sections:
        offs = find_all_in_bytes(sec_data, sec_va, pat_bytes, pat_mask)
        for off in offs:
            hits.append((sec_va + off, off, sec_name))
    return hits


def looks_like_prologue(buf):
    if len(buf) < 3:
        return False
    b0, b1 = buf[0], buf[1]
    if b0 == 0xFF and b1 == 0x25:
        return False  # JMP [rip+...] thunk
    if b0 == 0xE9:
        return False  # JMP rel32 thunk
    return (
        (b0 == 0x48 and b1 in (0x89, 0x81, 0x83, 0x8B, 0x8D)) or
        (b0 == 0x40 and b1 in (0x53, 0x55, 0x56, 0x57)) or
        (b0 in (0x53, 0x55, 0x56, 0x57)) or
        (b0 == 0x41 and b1 in (0x54, 0x55, 0x56, 0x57)) or
        (b0 == 0x4C and b1 in (0x89, 0x8B))
    )


def bytes_to_sig(buf):
    return " ".join("%02X" % b for b in buf)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: cap03_find_candidates.py <WHGame.dll>")
    dll_path = Path(sys.argv[1])
    if not dll_path.exists():
        sys.exit(f"not found: {dll_path}")

    pe = pefile.PE(str(dll_path), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    print(f"Image: {dll_path}")
    print(f"ImageBase: 0x{image_base:016X}")

    # Collect executable section blobs in VA space.
    exec_sections = []
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        chars = sec.Characteristics
        if not (chars & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
            continue
        sec_va = image_base + sec.VirtualAddress
        sec_data = sec.get_data()
        exec_sections.append((sec_va, sec_data, name))
        print(f"  exec sec: {name} VA=0x{sec_va:016X} size=0x{len(sec_data):X}")
    if not exec_sections:
        sys.exit("no executable sections found")

    # Locate `update`.
    pb, pm = parse_pattern(UPDATE_SIG)
    hits = find_all_in_sections(exec_sections, pb, pm)
    print(f"update sig hits: {len(hits)}")
    if len(hits) != 1:
        sys.exit("update sig not unique — abort")
    update_va, update_off_in_sec, update_sec = hits[0]
    print(f"update VA: 0x{update_va:016X} in section {update_sec}")

    # Locate pcall and loadfile for exclusion.
    excluded_vas = {update_va}
    for label, sig in [("pcall", PCALL_SIG), ("loadfile", LOADFILE_SIG)]:
        pb_e, pm_e = parse_pattern(sig)
        h = find_all_in_sections(exec_sections, pb_e, pm_e)
        if len(h) == 1:
            print(f"excluded {label} @ 0x{h[0][0]:016X}")
            excluded_vas.add(h[0][0])

    # Disassemble from update's entry until we exit the function.
    # We don't have function boundaries, so we trace flow: walk linearly,
    # stop on RET / JMP-out / INT3. Recurse through internal jumps.
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    # Build a flat blob keyed by VA for the section update lives in.
    # That's simplest; CGame::Update should be one contiguous fn in .text.
    update_sec_va, update_sec_data, _ = next(
        (s for s in exec_sections if s[0] <= update_va < s[0] + len(s[1])),
        (None, None, None)
    )

    def disasm_at(va, max_bytes=None):
        offset = va - update_sec_va
        end = len(update_sec_data) if max_bytes is None else offset + max_bytes
        return md.disasm(update_sec_data[offset:end], va)

    # Trace `update` with worklist of pending VAs to follow.
    visited = set()
    pending = [update_va]
    call_targets = []
    insn_count = 0

    while pending and insn_count < MAX_INSNS_IN_UPDATE:
        va = pending.pop()
        if va in visited:
            continue
        if va < update_sec_va or va >= update_sec_va + len(update_sec_data):
            continue
        # Walk linearly from va.
        for insn in disasm_at(va):
            if insn_count >= MAX_INSNS_IN_UPDATE:
                break
            if insn.address in visited:
                break
            visited.add(insn.address)
            insn_count += 1
            mnem = insn.mnemonic

            if mnem == "call":
                op = insn.operands[0]
                if op.type == capstone.x86.X86_OP_IMM:
                    call_targets.append(op.imm)
                # don't follow into callees — fall through past the call

            elif mnem == "ret" or mnem.startswith("retn"):
                break

            elif mnem == "jmp":
                op = insn.operands[0]
                if op.type == capstone.x86.X86_OP_IMM:
                    target = op.imm
                    # Stay inside the function: continue walking from target.
                    if target not in visited:
                        pending.append(target)
                break  # unconditional jmp — stop linear walk

            elif mnem.startswith("j"):  # conditional jump
                op = insn.operands[0]
                if op.type == capstone.x86.X86_OP_IMM:
                    target = op.imm
                    if target not in visited:
                        pending.append(target)
                # fall through to next instruction (continue linear walk)

            elif mnem == "int3":
                break

    print(f"insns walked in update: {insn_count}")
    print(f"unique call targets from update: {len(set(call_targets))}")

    # For each call target, get 24 bytes and check uniqueness in .text.
    seen_targets = set()
    candidates = []
    for tgt in call_targets:
        if tgt in seen_targets:
            continue
        seen_targets.add(tgt)
        if tgt in excluded_vas:
            continue
        # Map target VA to section data.
        sec = next(
            (s for s in exec_sections if s[0] <= tgt < s[0] + len(s[1])),
            None
        )
        if sec is None:
            continue
        sec_va, sec_data, sec_name = sec
        off = tgt - sec_va
        if off + PROLOGUE_BYTES > len(sec_data):
            continue
        prolog = sec_data[off:off + PROLOGUE_BYTES]
        if not looks_like_prologue(prolog):
            continue
        # Uniqueness check across all exec sections.
        pat_b = bytes(prolog)
        pat_m = [True] * len(pat_b)
        all_hits = find_all_in_sections(exec_sections, pat_b, pat_m)
        if len(all_hits) != 1:
            continue
        candidates.append((tgt, prolog, sec_name))

    print(f"unique-prologue candidates: {len(candidates)}")
    print()

    # For each candidate, estimate body size by linear-walking to first ret,
    # and count inner calls + count distinct prologue bytes.
    enriched = []
    for tgt, prolog, sec_name in candidates:
        sec = next(
            (s for s in exec_sections if s[0] <= tgt < s[0] + len(s[1])),
            None
        )
        if sec is None:
            continue
        sec_va, sec_data, _ = sec
        off = tgt - sec_va
        body_max = min(off + 8192, len(sec_data))
        sub = sec_data[off:body_max]
        body_insns = 0
        body_calls = 0
        body_size_bytes = 0
        for ins in md.disasm(sub, tgt):
            body_insns += 1
            body_size_bytes = (ins.address + ins.size) - tgt
            if ins.mnemonic == "call":
                body_calls += 1
            if ins.mnemonic in ("ret", "retn"):
                break
            if body_insns > 2000:
                break
        # distinct-byte score for the prologue
        distinct = len(set(prolog))
        enriched.append({
            "va": tgt,
            "prolog": prolog,
            "sec_name": sec_name,
            "body_bytes": body_size_bytes,
            "body_insns": body_insns,
            "body_calls": body_calls,
            "distinct": distinct,
        })

    # Rank: prefer substantial fns (>= 40 bytes body, >= 1 inner call,
    # distinct prologue bytes >= 12) — these are real functions, less
    # likely inlined.
    def score(c):
        # Smaller body penalty, fewer calls penalty, but cap rewards.
        body = min(c["body_bytes"], 2000)
        calls = min(c["body_calls"], 10)
        return body * 0.5 + calls * 100 + c["distinct"] * 5

    enriched.sort(key=score, reverse=True)

    for i, c in enumerate(enriched[:MAX_CANDIDATES], 1):
        print(f"CANDIDATE #{i}  score={int(score(c))}")
        print(f"  va:           0x{c['va']:016X}")
        print(f"  sec:          {c['sec_name']}")
        print(f"  body_bytes:   {c['body_bytes']}")
        print(f"  body_insns:   {c['body_insns']}")
        print(f"  inner_calls:  {c['body_calls']}")
        print(f"  distinct:     {c['distinct']}/24")
        print(f"  sig:          {bytes_to_sig(c['prolog'])}")
        print()


if __name__ == "__main__":
    main()
