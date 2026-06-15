# Vanilla pak format — confirmed standard PKZIP (static on-disk read)

**Captured 2026-06-15** during file-system-takeover step 2.2. Primary evidence
(a fresh static read of real on-disk vanilla game paks), discharging the design's
`assumes — vanilla pak format uniformity` clause (`docs/design/file-system-takeover.md` §6).

## The question

The recon (`_research/phase8.5-pak-resolver/RESOLUTION-OWNERSHIP-synthesis.md` §4 /
front-5) verified **2 Nexus MOD paks** are standard PKZIP. Design §6 ASSERTED that
VANILLA game paks (`<game>/Data/*.pak`) are the same standard PKZIP — but inferred
it from the engine's ZipDir being a standard-PKZIP parser, NOT from a vanilla pak's
actual on-disk bytes. The build of kcdx's own PKZIP reader rests on that assumption.
If vanilla paks carry a proprietary CryPak header, zip64, or encryption, the reader
design is falsified.

## Outcome → meaning map (pre-committed, theory-independent)

- First 4 bytes `50 4B 03 04` (`PK\x03\x04`) → standard PKZIP local file header, no
  proprietary header prepended → design holds.
- First 4 bytes anything else (a CryPak magic) → proprietary container → **design
  §6 FALSIFIED**, STOP and surface.
- Tail has `PK\x05\x06` (EOCD) and NO `PK\x06\x06`/`PK\x06\x07` (zip64 EOCD/locator)
  → not zip64 → design holds.
- Tail has a zip64 EOCD/locator → zip64 → design's "no zip64" FALSIFIED, surface.

## Ground truth observed

**Signature scan — 8 real vanilla `<game>/Data/*.pak`, every one `PK\x03\x04`:**

```
Animations.pak             head=504b0304
Characters.pak             head=504b0304
Cinematics.pak             head=504b0304
GeomCaches.pak             head=504b0304
Heads.pak                  head=504b0304
IPL_Characters-part0.pak   head=504b0304
IPL_Characters-part1.pak   head=504b0304
IPL_Characters-part2.pak   head=504b0304
```
(Tables.pak, Scripts.pak also confirmed `504b0304` in an earlier scan.)

**Full head + EOCD read of `GeomCaches.pak` (10,553,477 bytes, the smallest — a
clean 8-entry fixture):**

- **Head (offset 0):** `50 4b 03 04 14 00 00 00 00 00 …` — local file header sig
  `PK\x03\x04`; version-to-extract `0x0014` (20, standard); general-purpose flags
  `0x0000` (no encryption bit set); compression method `0x0000` = **STORED** (this
  first entry uncompressed); compressed size `0x0005613b` (352,571) == uncompressed
  size → STORED confirmed.
- **EOCD (near offset 0x00a10871):** `50 4b 05 06` (`PK\x05\x06`, standard EOCD) —
  disk `0x0000`, CDR-start disk `0x0000`, entries-this-disk `0x0008` (8),
  total entries `0x0008` (8), CDR size `0x00000390` (912 bytes), CDR offset
  `0x00a104df`, comment length `0x0000` (no trailing comment).
- **No zip64:** no `PK\x06\x06` (zip64 EOCD) or `PK\x06\x07` (zip64 EOCD locator)
  in the tail.

## Verdict — design §6 CONFIRMED (not falsified)

Vanilla game paks are **standard PKZIP**: `PK\x03\x04` local headers (no proprietary
CryPak header prepended), `PK\x05\x06` EOCD, no zip64, no encryption flag, STORED
(method 0) + DEFLATE (method 8) per entry, unsigned CDR — the same format the recon
verified on Nexus mod paks. kcdx's own PKZIP CDR parser (built in step 2.2) is a
standard zip decoder; no engine ZipDir or proprietary-format handling is needed.

## Reusable wiring (reconstruct without re-reading)

The check is a static head+tail byte read on disk — no game launch, no engine. To
re-verify on a new game version:
- Head signature: read first 4 bytes of `<game>/Data/<name>.pak`, expect `50 4b 03 04`.
- EOCD: scan the last ~64 bytes (+ any comment) backward for `50 4b 05 06`; the 22
  bytes after it are the EOCD record (entry counts, CDR size, CDR offset, comment len).
- zip64 negative check: confirm no `50 4b 06 06` / `50 4b 06 07` in the tail.

This finding is made a **standing falsifiable assertion** by the `cap-110-pak-cdr-parse`
test plugin's format-uniformity check (scans several vanilla paks' sig + EOCD at boot,
FAILs loud on any deviation) — so a future game version that changes the pak format
trips the regression row rather than silently corrupting a read.
