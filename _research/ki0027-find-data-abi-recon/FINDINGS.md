# KI-0027 / Probe P5 — the CCryPak find-data buffer ABI (slots 63/64/65)

**Question (design §8 P5, §5.1):** kcdx's `FindFirst`/`FindNext` (slots 63/64,
+0x1F8/+0x200) must fill a caller-provided find-data buffer the engine consumer
reads correctly. What is that buffer's field layout — the attribute word (the
`& 0x10` directory bit), the entry-name offset, the total size?

**Verdict — RESOLVED, outcome A (design §8 P5): the field offsets are read from the
CONSUMER side and CROSS-CHECKED across two independent consumers. The layout is
unambiguous; the producer-fallback decompile was NOT needed.** Evidence tier:
Ghidra decompile of two engine consumers, already captured in
`_research/ki0027-table-glob-dispatch-recon/`. HIGH confidence (two independent
callers agree byte-for-byte on the layout).

## The find-data buffer layout

| Field | Offset | Type / size | Evidence |
|---|---|---|---|
| **Attribute / flags word** | **0x00** | byte (at least; tested as a byte) | `(local_158[0] & 0x10) == 0` — bit `0x10` is the **directory** attr; a set bit skips the entry (the loader wants files). |
| **Entry name** | **0x24 (36)** | inline C-string (NUL-terminated, fixed in-buffer, NOT a pointer) | `local_134` sits `0x158 - 0x134 = 0x24` ABOVE the buffer base (locals grow down → smaller Ghidra local-number = higher address). The loader takes `&local_134` as a `char*`: `FUN_1806a1fe4(&local_134)` (strlen-class), an inline `strcmp` loop, `strstr(&local_134, …)`. |
| **(bytes 0x01–0x23)** | 0x01–0x23 | opaque header (size/time/reserved — not read by these consumers) | The 35 bytes between the attr byte and the name are part of the engine's find-data header; the table-glob consumers read only the attr byte + the name, so the intermediate fields' exact split (size, write-time, etc.) is not load-bearing for kcdx's impl — kcdx zero-fills them or mirrors the engine's writes. |
| **Total declared buffer** | — | **≥ 0x24 + a MAX_PATH-class name region** | The caller declares `byte local_158[36]` for the HEADER, with the name region (`local_134…`) contiguous immediately after at +0x24. The full find-data struct = 36-byte header + the inline name buffer. |

## The two corroborating consumers (read from the binary)

**Consumer 1 — the table-DB override-glob loader `FUN_180974484`**
(`_research/ki0027-table-glob-dispatch-recon/_ghidra_globconsumer.txt`):

```c
  byte local_158 [36];      // the find-data buffer (header), passed to FindFirst/FindNext
  char local_134;           // name byte [0]  — at local_158 + 0x24
  char local_133;           // name byte [1]
  char local_132;           // name byte [2]
  ...
  lVar5 = (**(code **)(*DAT_18492b850 + 0x1f8))(DAT_18492b850, local_1c0, local_158, 0); // FindFirst(pattern, &finddata, 0)
  if (-1 < lVar5) {
    do {
      if (((local_158[0] & 0x10) == 0) &&                  // attr byte @0, 0x10 = dir-skip
         ((local_134 != '.' || (...))) {                   // name @0x24, the './..' skip
        lVar6 = FUN_1806a1fe4(&local_134);                 // strlen(name)
        ... inline strcmp over &local_134 ...
          pcVar8 = strstr(&local_134, *(char **)(local_1d0 + 0x20));   // name is a C-string
          FUN_1804f692c(local_1c8, &local_134);            // copy the matched name
      }
      iVar3 = (**(code **)(*plVar7 + 0x200))(plVar7, lVar5, local_158); // FindNext(handle, &finddata)
    } while (-1 < iVar3);
    (**(code **)(*plVar7 + 0x208))(plVar7, lVar5);         // FindClose(handle)
  }
```

**Consumer 2 — the general directory listing `FUN_18041d238`**
(`_research/ki0027-table-glob-dispatch-recon/_ghidra_confirmglobal.txt`):

```c
      if ((local_144[0] != '.') && ((local_168[0] & 0x10) == 0)) {   // attr @0 (0x10=dir), name @ +0x24
```

Here the buffer base is `local_168` and the name is `local_144` — `0x168 - 0x144 =
0x24 = 36`, the **identical** attr@0 / name@0x24 layout, from a second, unrelated
consumer. Two independent callers agreeing is the cross-check; the layout is not an
artifact of one decompile.

## What this means for kcdx's slot-63/64/65 impl (step 5.2)

kcdx's `FindFirst`/`FindNext` fill the caller's find-data so that:
- **byte at offset 0** carries the attribute word; **bit 0x10 set ⇔ the entry is a
  directory** (kcdx sets it for a directory entry, clears it for a file — kcdx's
  unified-set entries are files/vpaths, so kcdx clears 0x10 for a served pak/loose
  file and sets it only for a synthesized directory entry if it emits one).
- **the entry name is written as a NUL-terminated C-string starting at offset 0x24**,
  inline in the caller's buffer (NOT a pointer kcdx allocates — the engine consumer
  reads `&buffer[0x24]` directly and runs strlen/strcmp/strstr over it). kcdx copies
  the entry's base name (not the full path — the consumer matches the glob's base
  pattern against the name) into `buffer + 0x24`, NUL-terminated, bounded to the
  caller's buffer (a MAX_PATH-class region).
- bytes 0x01–0x23 are the engine header's reserved/size/time region the table-glob
  consumers do not read; kcdx zero-fills them (a future consumer that reads a size
  field would need that field's offset read — out of scope for the table-DB glob,
  noted as a follow-up if a size-reading consumer surfaces).

## Caveats / unread edges

- **The PRODUCER-side STORE offsets were not separately decompiled.** The engine
  `FindFirst` body (`0x180973058`) was targeted for a confirming dump, but the
  `analyzeHeadless.bat` headless invocation did not cooperate this session (it
  requires `JAVA_HOME` and detaches java in a way the run capture missed). It was
  NOT needed: the design §8 P5 outcome map makes the producer decompile the
  FALLBACK for when "the layout is ambiguous from the consumer alone" — and the
  consumer side is doubly-corroborated and unambiguous (outcome A held). A future
  agent wanting the producer-side confirmation runs `dump_finddata_abi.java` (in
  this dir, already targets `0x180973058`) with `JAVA_HOME` set
  (`C:\Program Files\Microsoft\jdk-21.0.11.10-hotspot`).
- The exact split of bytes 0x01–0x23 (size vs time vs reserved) is unread because no
  table-glob consumer reads it; kcdx zero-fills. If a size-reading consumer of slot
  63/64 is later found, read that field's offset before relying on it.

## Reuse-ladder tier

Tier 2 — the layout is read from the prior `_research/ki0027-table-glob-dispatch-recon/`
consumer decompiles already on disk (the reuse-first ladder answered without a fresh
producer disassembly). The `dump_finddata_abi.java` script here is the
producer-side cross-check tooling, kept for the next investigation.
