# `content_hash` — the BLAKE3 interoperability contract

**Status:** PINNED 2026-05-26. The binding contract that the reference-data
extractor (`parallel-ghidra-research.md` §4a/§4b) and the engine survival check
(`restructure-plan.md` Phase 9.1/9.2) BOTH conform to. The extractor is the data
*producer*; the engine is the *consumer*; this document is the wire format
between them. Decided by the user + senior architect; not to be relitigated.

**Location:** kept beside the extractor (the data-producer code). The plan's §9
points here.

---

## 1. Algorithm + encoding (exact)

- **Algorithm:** BLAKE3, **256-bit / 32-byte default output** (the standard
  digest length; XOF is *not* used for `content_hash`).
- **Output encoding:** **lowercase hex**, 64 characters, no `0x` prefix, no
  separators (e.g. `af1349b9f5f9a1a6...`).
- **Reference implementation + canonicality:** the producer uses the vendored
  pure-Java Apache Commons Codec `Blake3` (see `blake3/PROVENANCE.md`), verified
  byte-identical to the official BLAKE3 test vectors by `blake3/Blake3SelfTest.java`
  (35/35 PASS). The engine side (a DIFFERENT language/library) MUST pass the
  SAME official vectors before it is trusted — that is what guarantees producer
  and consumer hashes agree.

## 2. Input — what bytes are hashed (the load-bearing definition)

### 2a. Function-level

`content_hash` covers the **RAW ON-DISK bytes** of the function, **exactly as
they sit in the module's backing file** (`WHGame.dll` on disk), over the
**contiguous span `[rva, rva + length)`**, where `length` =
`Function.getBody().getNumAddresses()` (the column the extractor emits alongside
`rva` so the engine can reproduce the exact hashed range).

- **NO normalization of any kind.** No relocation normalization, no
  un-relocation, no rel32-target canonicalization, no masking. Bytes hashed as-is.
- **Why on-disk, not live memory:** Ghidra imports a DLL at its preferred image
  base (0x180000000 for WHGame.dll) and does NOT apply ASLR relocations, so
  Ghidra's `getMemory().getBytes(...)` returns the on-disk bytes. The engine's
  survival check MUST read the module's **on-disk backing file** (NOT live
  process memory) and hash the same `[rva, rva+length)` span — so the
  static-dump hash and the engine's check-time hash are trivially comparable
  with zero normalization. (This is the Option-A decision from the
  senior-architect consult: both sides read the on-disk file; the relocation
  problem never arises.)

### 2b. Per-statement

The per-statement `content_hash` uses the **same definition scoped to the
statement's byte sub-range** `[byte_range_start, byte_range_start + byte_range_len)`
— same source (on-disk bytes), same algorithm, no separate identity definition.
`byte_range_start` is an RVA; `byte_range_len` is the statement's machine-code
byte length.

## 3. The producer/consumer agreement

- **Producer (this extractor):** `refdata/ContentHash.java` reads
  `[rva, rva+length)` via `getMemory().getBytes(...)` and hashes with the
  vendored `Blake3.hash(byte[])` → 32 bytes → lowercase hex.
- **Consumer (future Phase 9.1/9.2 engine):** reads the same span from the
  on-disk backing file and hashes with its own BLAKE3, which MUST first pass the
  35 official vectors (`blake3/test_vectors.json`).
- **The harness oracle** (`validate_extractor_output.py` via `blake3/Blake3Hex.java`)
  independently recomputes the hash from on-disk bytes (read by pefile) — a
  third path that proves the producer's emitted `content_hash` is correct.

## 4. Schema requirement

The `functions` row MUST persist the function body `length` alongside `rva` +
`content_hash` (the engine needs `length` to reproduce the hashed `[rva,rva+length)`
range). The extractor emits a `length` column for exactly this reason. (The
`statements` row already carries `byte_range_len`.)
