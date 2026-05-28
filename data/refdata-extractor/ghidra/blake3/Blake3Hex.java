// Blake3Hex.java -- a stdin -> lowercase-hex filter over the VETTED Apache
// Commons Codec Blake3 (the same class Blake3SelfTest proves canonical against
// the 35 official vectors).
//
// PURPOSE: the INDEPENDENT content_hash oracle for the output-validation harness
// (validate_extractor_output.py). The harness reads a function's on-disk bytes
// ITSELF (via pefile) and pipes them to this filter, which prints the BLAKE3
// 32-byte digest as 64 lowercase hex chars. This is INDEPENDENT of the
// extractor's Ghidra-side ContentHash path -- the two share only the pinned
// BLAKE3 algorithm + this vetted class, so a wrong byte-range or wrong
// invocation in the extractor mismatches the oracle and the harness FAILs
// (AP15: the check reads an independent answer, not the extractor's own output).
//
// It is a thin filter, NOT a re-implementation: it calls Blake3.hash(bytes) and
// only adds stdin-read + hex-encode. No algorithm drift risk.
//
// Run: java -cp <blake3-root> Blake3Hex   (reads stdin to EOF, prints one hex line)

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

import org.apache.commons.codec.digest.Blake3;

public final class Blake3Hex {

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    public static void main(String[] args) throws Exception {
        // Read all of stdin into a byte[].
        ByteArrayOutputStream buf = new ByteArrayOutputStream();
        InputStream in = System.in;
        byte[] chunk = new byte[8192];
        int n;
        while ((n = in.read(chunk)) != -1) {
            buf.write(chunk, 0, n);
        }
        byte[] data = buf.toByteArray();

        // The EXACT call the contract pins + ContentHash uses: 32-byte default digest.
        byte[] h = Blake3.hash(data);

        StringBuilder sb = new StringBuilder(h.length * 2);
        for (byte b : h) {
            int v = b & 0xff;
            sb.append(HEX[v >>> 4]).append(HEX[v & 0x0f]);
        }
        System.out.println(sb.toString());
    }
}
