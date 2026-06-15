# Phase 2 — pak reader + unified index

kcdx's own byte-source layer: a standard PKZIP/DEFLATE reader (every byte on
kcdx's CRT, no engine ZipDir) and the unified asset index (vpath → ByteSource,
built at load, one O(1) lookup per open). This is the data layer the real file
slots (Phase 3) read through.

Depends on Phase 1 (the seating mechanism is proven; the stub vtable is swapped
in, so this phase's index can be built and exercised through the spike).

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [2.1 — DEFLATE inflater dependency (zlib/miniz pick)](step-1-deflate-dependency.md) | DONE | eb42ee8 |
| [2.2 — PKZIP CDR parser + vanilla-pak format check](step-2-pkzip-parser.md) | DONE | 0c1b093 |
| [2.3 — DEFLATE read path (kcdx CRT)](step-3-deflate-read.md) | DONE | (landed) |
| [2.4 — unified asset index built at load](step-4-unified-index.md) | NOT STARTED | — |

## Verification gate (phase done when)

- 2.1: the inflater dependency is added, license-checked, and recorded
  (`.claude/rules/dependencies.md`); build green linking it.
- 2.2: kcdx parses a real vanilla `<game>/Data/*.pak` central directory + its
  test confirms entry `{offset,size,method,crc}` extraction; the format-uniformity
  check confirms standard PKZIP (or surfaces a deviation).
- 2.3: kcdx inflates a known STORED + a known DEFLATE entry to correct bytes (a
  test comparing kcdx's output to the known plaintext), every read on kcdx's CRT.
- 2.4: the unified index builds at load — a launch logs the index entry count +
  a sample vpath→ByteSource resolution (vanilla pak entry + a loose override),
  agent-read from `kcdx-dev.log`.
