// ContentHash.java -- the content_hash helper for the production reference-data
// extractor (parallel-ghidra-research.md §4a; BLAKE3-HASH-CONTRACT.md).
//
// ONE CONCERN: turn a function/statement byte range into the BLAKE3 content_hash
// the engine survival-check reproduces. Wraps the vendored, vector-verified
// pure-Java Apache Commons Codec Blake3 (blake3/, self-test 35/35 official
// vectors PASS). The per-function pass and the future per-statement pass both
// call this -- one primitive, one identity definition, no second hash notion
// (BLAKE3-HASH-CONTRACT.md §2b).
//
// THE CONTRACT THIS HONORS (BLAKE3-HASH-CONTRACT.md, verbatim):
//   - BLAKE3, 256-bit / 32-byte default output (NOT XOF).
//   - lowercase hex, 64 chars, no 0x prefix, no separators.
//   - RAW ON-DISK bytes over the CONTIGUOUS span [rva, rva+length). Ghidra
//     imports WHGame.dll at its preferred image base and applies no ASLR
//     relocations, so getMemory().getBytes(entry, buf) returns the on-disk
//     bytes. NO normalization of any kind -- bytes hashed as-is.
//   - length = (max body address) - entry + 1, the full single-span extent
//     (== Function.getBody().getNumAddresses() for a contiguous body; for a
//     rare non-contiguous body the span INCLUDES inter-range gap bytes, which
//     is correct -- the engine reads those same [rva, rva+length) on-disk bytes).
//
// This is a plain helper class (not a GhidraScript) on the -scriptPath; the
// extractor imports it.

package refdata;

import org.apache.commons.codec.digest.Blake3;

import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryAccessException;

public final class ContentHash {

    private ContentHash() {}

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    /** 32 raw bytes -> 64-char lowercase hex, no prefix/separators (the contract encoding). */
    public static String toHex(byte[] b) {
        char[] out = new char[b.length * 2];
        for (int i = 0; i < b.length; i++) {
            int v = b[i] & 0xff;
            out[i * 2]     = HEX[v >>> 4];
            out[i * 2 + 1] = HEX[v & 0x0f];
        }
        return new String(out);
    }

    /**
     * Read the contiguous on-disk span [entry, entry+length) and return its
     * lowercase-hex BLAKE3 content_hash.
     *
     * <p>The read is the SAME call shape the engine reproduces
     * (seek(rva); read(length); blake3) and the SAME the producer peer uses.
     * A failed read is NOT swallowed: it throws MemoryAccessException so the
     * caller can record the edge state (decompile_quality=unanalyzable + empty
     * hash + reason) per AP14 -- never a silent skip.
     *
     * @throws MemoryAccessException if any byte in [entry, entry+length) is
     *         not backed on disk (the caller MUST surface this as a visible
     *         edge row, not drop the function).
     */
    public static String ofRange(Memory memory, Address entry, long length)
            throws MemoryAccessException {
        if (length <= 0) {
            // A zero/negative span is itself an edge state, not a normal hash.
            // Caller decides the row; we refuse to fabricate a hash of nothing.
            throw new MemoryAccessException(
                    "non-positive length " + length + " at " + entry);
        }
        if (length > Integer.MAX_VALUE) {
            throw new MemoryAccessException(
                    "length " + length + " exceeds addressable buffer at " + entry);
        }
        byte[] buf = new byte[(int) length];
        // getBytes throws MemoryAccessException if the FULL range is not
        // readable (partial reads return a short count); request the full
        // length and verify the count so a short read is also an edge state.
        int read = memory.getBytes(entry, buf);
        if (read != buf.length) {
            throw new MemoryAccessException(
                    "short read " + read + "/" + buf.length + " at " + entry);
        }
        return toHex(Blake3.hash(buf));
    }
}
