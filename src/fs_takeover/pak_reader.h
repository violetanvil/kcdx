#pragma once

// kcdx's own PKZIP central-directory reader.
//
// kcdx serves pak bytes through its OWN PKZIP reader so that every byte of a
// pak read happens on kcdx's CRT, with the engine's ZipDir out of the path
// (file-system-takeover design §6). This file is the directory-parsing half:
// given a pak path, it locates the End-Of-Central-Directory record at the file
// tail, walks the central-directory file headers, and extracts each entry's
// {name(=vpath), local-header offset, compressed/uncompressed size, compression
// method, crc32}. It does NOT inflate any entry bytes — the DEFLATE read path
// (the inflater) is a separate concern; this reader only reads the table that
// tells the read path WHERE each entry's bytes are.
//
// The per-entry fields feed the unified asset index's Pak byte-source
// (design §5: ByteSource{ pak: { pakFile, offset, size, method, crc } }) — the
// index records one PakEntry per vpath at load-time, then resolves a vpath to
// its entry with one lookup per open.
//
// A .pak is untrusted external file data: every field crossing in from disk is
// validated before it is trusted or allocated for (cap before alloc, signature
// check per record, zip64 rejected loud, every offset/length bounds-checked).
// A malformed or hostile pak yields a logged failure + a false return, never a
// crash, a silent empty list, or an over-allocation.

#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::fs_takeover {

// One central-directory entry. Field names + semantics mirror the design §5
// ByteSource pak shape ({ offset, size, method, crc } + the entry name = vpath)
// so the unified index (step 2.4) consumes a PakEntry without translation.
struct PakEntry {
    // The entry name as stored in the central directory. For a vanilla/mod pak
    // this IS the vpath — forward-slashed, root-relative (design §5/§6).
    std::string name;
    // Offset of this entry's local file header from the start of the pak file.
    // The read path seeks here, parses the local header, and reads the bytes
    // that follow it.
    uint64_t local_header_offset = 0;
    // Size of the entry's stored (possibly DEFLATE-compressed) bytes.
    uint64_t compressed_size = 0;
    // Size of the entry's bytes after inflation. Equal to compressed_size for a
    // STORED (method 0) entry.
    uint64_t uncompressed_size = 0;
    // PKZIP compression method: 0 = STORED, 8 = DEFLATE (the only two vanilla
    // paks use — design §6). The read path picks STORED-copy vs inflate on this.
    uint16_t method = 0;
    // CRC-32 of the uncompressed bytes (the read path may verify against it).
    uint32_t crc32 = 0;
};

// Parse a pak's central directory into outEntries.
//
// Opens pakPath on kcdx's own CRT (kcdx _wfopen/fread/fseek, never the engine),
// locates the EOCD at the tail (scanning back past a trailing comment up to the
// PKZIP 64 KiB comment max), reads the total-entry-count + CDR offset/size,
// then walks each central-directory file header recording a PakEntry.
//
// Returns true with outEntries populated on success. On any failure (no EOCD,
// a corrupt/absurd entry count, a zip64 marker, a bad record signature, an
// out-of-bounds offset/length) it logs the situation + context and returns
// false with outError set to a human-readable reason; outEntries is cleared.
// Never throws, never crashes on malformed input, never returns a partial list
// as if it were whole.
//
// Cold path (index build at load) — normal allocation is fine here.
bool ParsePakCentralDirectory(const std::wstring& pakPath,
                              std::vector<PakEntry>& outEntries,
                              std::string& outError);

// Read one pak entry's UNCOMPRESSED bytes, entirely on kcdx's own CRT.
//
// Given a PakEntry (as ParsePakCentralDirectory produced) and the pak path,
// this opens the pak (kcdx _wfopen), parses the entry's LOCAL FILE HEADER at
// entry.local_header_offset (the LFH carries its OWN name/extra lengths, which
// can differ from the central-directory record's — the compressed data starts
// at offset + 30 + local-name-len + local-extra-len, NEVER the CDR's lengths),
// reads entry.compressed_size bytes (kcdx fread), and delivers the uncompressed
// bytes into outBytes:
//   - STORED (method 0): copied directly (compressed_size must equal
//     uncompressed_size).
//   - DEFLATE (method 8): inflated via the vendored miniz inflater
//     (tinfl_decompress_mem_to_mem, raw DEFLATE) into a buffer sized to the
//     DECLARED uncompressed_size; the written length must equal it.
//   - any other method: a loud failure (design §6 — only 0 and 8 occur).
// The uncompressed bytes' CRC-32 is verified against entry.crc32 — a mismatch
// fails loud. This is the strongest end-to-end correctness check (a wrong
// local-header parse, seek, inflate flag, or off-by-N yields a wrong CRC); it
// runs on every read (cold path — the cost is irrelevant, the correctness is
// not). See file-system-takeover design §6 (every byte on kcdx's CRT) + §4.4
// (no engine read leaf in the path).
//
// A .pak is untrusted external data: compressed_size and uncompressed_size are
// capped against a sane maximum BEFORE any read/inflate buffer is allocated,
// and every offset+length (LFH, data start, compressed extent) is bounds-
// checked against the file size. A corrupt entry claiming a multi-GB size, or
// one pointing past EOF, is a logged failure + a false return, never an
// over-allocation or a crash.
//
// Returns true with outBytes holding exactly uncompressed_size bytes on
// success. On any failure it logs the situation + context (kcdx::log) and
// returns false with outError set; outBytes is cleared. Never throws, never
// returns partial/wrong-length bytes as if whole.
//
// Cold path (a read at index-build / open time, not a hot loop) — normal
// allocation is fine here.
bool ReadPakEntry(const std::wstring& pakPath,
                  const PakEntry& entry,
                  std::vector<uint8_t>& outBytes,
                  std::string& outError);

}  // namespace kcdx::fs_takeover
