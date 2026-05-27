"""Phase 6 verifier: confirm existing predecessor sigs still resolve.

Runs the yobson1 update/pcall/loadfile sigs (already used by kcdx),
the muyuanjin gEnv anchor chain, and a sanity check on the new
phase6 candidate AOBs (Tier-2) — printing PASS/FAIL for each.

Usage:
    py phase6_verify_existing_sigs.py <path/to/WHGame.dll>
"""

import sys
from pathlib import Path

import capstone
import pefile


# Predecessor sigs we want to confirm still work.
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

# Phase 6 Tier-2 candidates from xref runs.
PHASE6_SIGS = {
    # SaveGameManager::LoadGame (anchor: "Loading saved game '%s' %s, ...")
    "C_SaveGameManager::LoadGame (Tier-2)":
        "48 89 5C 24 08 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC "
        "50 45 33 FF 41 8B D8 44 89 BC 24 A8 00 00 00 44",

    # SaveGameManager::PostLoadGame (anchor: "SaveGameManager::PostLoadGame")
    "C_SaveGameManager::PostLoadGame (Tier-2)":
        "48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC "
        "90 00 00 00 49 8B F8 8B DA 48 8B F1 E8 7F F1 35 FE 4C 8B B8",

    # LoadGame caller (entry 0x1825BCEEC) — the wrapper that calls LoadGame
    "C_SaveGameManager::LoadGame_wrapper (Tier-2)":
        "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B D8 8B FA 48 8B F1 E8 "
        "C0 F1 FF FF 48 8D 8E C8 00 00 00 66 C7 86 C0 00",

    # Orchestrator (entry 0x180FBEE78) — calls both the wrapper and the
    # PostLoadGame-string-bearing function 0x180FBE628
    "savemgr_orchestrator_0xFBEE78 (Tier-2)":
        "48 89 5C 24 10 48 89 7C 24 18 4C 89 74 24 20 55 48 8B EC 48 81 EC 80 00 "
        "00 00 83 B9 8C 00 00 00 FF 48 8B F9 0F 84 D0 00",

    # CryAction PostLoadGame internal (entry 0x180FBE628) — bears the
    # "PostLoadGame" string literal at xref 0x180FBE6D8
    "cryaction_postloadgame_string_owner_0xFBE628 (Tier-2)":
        "48 89 5C 24 18 55 56 57 41 54 41 56 48 8D 6C 24 C9 48 81 EC C0 00 00 00 "
        "4C 8B F1 E8 78 CE 40 01 4C 8D 25 99 CB B2 02 48",

    # C_UISaveLoad::LoadLastSave (entry 0x182BA7094) — bears
    # "Loading last saved game..." string xref
    "C_UISaveLoad::LoadLastSave (Tier-2)":
        "48 89 5C 24 08 57 48 83 EC 30 48 8B F9 48 8D 15 C0 E2 B7 01 48 8B 0D 31 "
        "48 D8 01 48 8B 01 FF 50 60 83 C8 FF 48 8D 54 24",

    # C_UISaveLoad::ExitGame (entry 0x182BA6894)
    "C_UISaveLoad::ExitGame (Tier-2)":
        "48 89 4C 24 08 48 83 EC 38 48 8B 0D 14 50 D8 01 4C 8D 44 24 40 48 8D 15 "
        "08 EB B7 01 48 8B 01 FF 50 38 E8 7D 58 D7 FD 41",

    # Tier-1 also unique candidates (for completeness)
    "C_UISaveLoad::LoadLastSave (Tier-1)":
        "48 89 5C 24 08 57 48 83 EC 30 48 8B F9 48 8D 15 C0 E2 B7 01 48 8B 0D 31",
    "C_UISaveLoad::ExitGame (Tier-1)":
        "48 89 4C 24 08 48 83 EC 38 48 8B 0D 14 50 D8 01 4C 8D 44 24 40 48 8D 15",
    "C_SaveGameManager::UpdateSaveGameDescriptions (Tier-1)":
        "48 89 5C 24 10 48 89 74 24 18 57 48 81 EC B0 01 00 00 48 8B 05 73 10 21",
    "savemgr_orchestrator_0xFBEE78 (Tier-1)":
        "48 89 5C 24 10 48 89 7C 24 18 4C 89 74 24 20 55 48 8B EC 48 81 EC 80 00",
    "cryaction_postloadgame_string_owner_0xFBE628 (Tier-1)":
        "48 89 5C 24 18 55 56 57 41 54 41 56 48 8D 6C 24 C9 48 81 EC C0 00 00 00",

    # ----- SAVE PATH (Tier-2 only; Tier-1 collides with one other function) -----
    # C_SaveGameManager::SaveGame (anchors: 'SaveGame: ...Duration=%.4f secs',
    # 'Saving failed : player is dead!', 'Saving')
    "C_SaveGameManager::SaveGame (Tier-2)":
        "4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57 "
        "48 8B EC 48 83 EC 50 40 8A 7D 58 48 8D 05 B2 A6",

    # ----- DELETE PATH -----
    # C_SaveGameManager::DeleteSavegame (anchor: 'Deleting savegame %d (%s) of playline %d')
    "C_SaveGameManager::DeleteSavegame (Tier-2)":
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 "
        "48 63 EA 45 8B F8 8B D5 48 8B F1 E8 40 19 42 FF",

    # C_SaveGameManager::DeleteAllSavegamesOfPlayline (anchor: 'Deleting all %d savegames of playline %d')
    "C_SaveGameManager::DeleteAllSavegamesOfPlayline (Tier-1)":
        "48 89 5C 24 10 48 89 74 24 20 57 48 83 EC 20 4C 8B 15 AE F4 36 02 48 63",

    # ----- LOAD CRY ENGINE DATA (interior step inside LoadGame) -----
    "C_SaveGameManager::LoadCryEngineData (Tier-1)":
        "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B DA 48 8B F1 48 8D 15",
}


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


def find_all(data, pat_bytes, pat_mask):
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
    if len(sys.argv) < 2:
        sys.exit("usage: phase6_verify_existing_sigs.py <WHGame.dll>")
    dll_path = Path(sys.argv[1])
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

    def check(label, sig, expect=1):
        pb, pm = parse_pattern(sig)
        hits = []
        for sec_va, sec_data, sec_name in exec_sections:
            for off in find_all(sec_data, pb, pm):
                hits.append((sec_va + off, sec_name))
        status = "PASS" if len(hits) == expect else "FAIL"
        print(f"  [{status}] {label}: {len(hits)} hit(s) (expect {expect})")
        for va, sec_name in hits[:3]:
            print(f"        @ 0x{va:016X} ({sec_name})")

    print("=" * 78)
    print("PREDECESSOR SIGS (already used by kcdx — confirm still unique)")
    print("=" * 78)
    check("yobson1 update", UPDATE_SIG)
    check("yobson1 lua_pcall", PCALL_SIG)
    check("yobson1 luaL_loadfile", LOADFILE_SIG)

    print()
    print("=" * 78)
    print("MUYUANJIN gEnv anchor chain")
    print("=" * 78)
    # Find "exec autoexec.cfg" string and the v1.4+ context byte sig that
    # precedes the LEA xref.
    gEnv_str = b"exec autoexec.cfg"
    rdata_va, rdata_data = sections[".rdata"]
    str_off = rdata_data.find(gEnv_str)
    if str_off == -1:
        print("  FAIL: 'exec autoexec.cfg' anchor not found in .rdata")
    else:
        str_va = rdata_va + str_off
        print(f"  Anchor string @ 0x{str_va:016X}")
        # Find LEA xrefs to it in .text
        from_text = []
        for i in range(len(text_data) - 7):
            if text_data[i] in (0x48, 0x4C) and text_data[i + 1] == 0x8D:
                modrm = text_data[i + 2]
                if (modrm & 0xC7) == 0x05:
                    disp = int.from_bytes(text_data[i + 3:i + 7],
                                          "little", signed=True)
                    if text_va + i + 7 + disp == str_va:
                        from_text.append(text_va + i)
        print(f"  LEA xrefs from .text: {len(from_text)}")
        for x in from_text:
            print(f"    @ 0x{x:016X}")
            # Check the 7 bytes BEFORE the LEA for the v1.4+ context
            # sig (4C 8B 92 18 01 00 00) or the v1.2/3 sig (48 8B 0D ? ? ? ?)
            ctx_off = x - text_va - 7
            if ctx_off >= 0 and ctx_off + 7 <= len(text_data):
                ctx = text_data[ctx_off:ctx_off + 7]
                if ctx == b"\x4C\x8B\x92\x18\x01\x00\x00":
                    print(f"      v1.4+ context byte sig: PRESENT")
                elif ctx[:3] == b"\x48\x8B\x0D":
                    print(f"      v1.2/3 'mov rcx, [rip+...]' "
                          f"context PRESENT (likely v1.3 layout)")
                else:
                    print(f"      context bytes: {ctx.hex(' ')}  (UNKNOWN)")

    print()
    print("=" * 78)
    print("PHASE 6 NEW CANDIDATE SIGS (uniqueness only)")
    print("=" * 78)
    for label, sig in PHASE6_SIGS.items():
        check(label, sig)


if __name__ == "__main__":
    main()
