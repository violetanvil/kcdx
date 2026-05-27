// Blake3SelfTest.java -- known-answer self-test for the vendored Apache Commons
// Codec Blake3, asserting the 35 OFFICIAL BLAKE3 test vectors
// (test_vectors.json, vendored verbatim from the BLAKE3 reference repo).
//
// THE FALSIFIABILITY GATE for the content_hash primitive: a wrong / wrong-endian
// / broken port fails the known-answer assertion. The vectors come from an
// INDEPENDENT source (the official BLAKE3 team's repo), so this measures
// canonicality, not the port hashing itself. The engine's BLAKE3 (a different
// language/library) MUST pass these SAME vectors before it is trusted -- that is
// what guarantees producer and consumer content_hash values agree.
//
// Each official case: the input is the repeating byte pattern 0,1,...,250,0,1,...
// (a 251-byte cycle -- the BLAKE3 standard test input) of length input_len. The
// `hash` field is the extended (XOF) output; its first 64 hex chars are the
// 32-byte default digest (the exact Blake3.hash(byte[]) call content_hash uses).
// We check FOUR things per case: the 32-byte default digest, and the hash /
// keyed_hash / derive_key XOF outputs at their full vendored length.
//
// Run: javac org/apache/commons/codec/digest/Blake3.java Blake3SelfTest.java
//      java -cp . Blake3SelfTest
// Exit 0 = all cases PASS; non-zero = at least one FAIL. Standalone main()
// harness (pure computation over fixed vectors; no Ghidra context needed).

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import org.apache.commons.codec.digest.Blake3;

public final class Blake3SelfTest {

    // The canonical key + context from the official test_vectors.json header.
    private static final byte[] KEY = "whats the Elvish word for friend".getBytes();
    private static final String CONTEXT = "BLAKE3 2019-12-27 16:29:52 test vectors context";

    private static final char[] HEX = "0123456789abcdef".toCharArray();

    private static String toHex(byte[] b) {
        char[] out = new char[b.length * 2];
        for (int i = 0; i < b.length; i++) {
            int v = b[i] & 0xff;
            out[i * 2] = HEX[v >>> 4];
            out[i * 2 + 1] = HEX[v & 0x0f];
        }
        return new String(out);
    }

    /** The BLAKE3 standard test input: byte i = i % 251, length n. */
    private static byte[] testInput(int n) {
        byte[] in = new byte[n];
        for (int i = 0; i < n; i++) {
            in[i] = (byte) (i % 251);
        }
        return in;
    }

    private static final class Case {
        int inputLen;
        String hash;       // expected XOF output, lowercase hex
        String keyedHash;
        String deriveKey;
    }

    // Minimal hand-parser for the official test_vectors.json `cases` array
    // (no external JSON lib in the Ghidra env). The file is well-formed + stable;
    // we extract input_len + the three hex fields per case object.
    private static List<Case> parseCases(String json) {
        List<Case> cases = new ArrayList<>();
        int i = json.indexOf("\"cases\"");
        if (i < 0) throw new IllegalStateException("no cases array");
        while (true) {
            int il = json.indexOf("\"input_len\"", i);
            if (il < 0) break;
            Case c = new Case();
            c.inputLen = (int) readNumber(json, il);
            c.hash = readString(json, json.indexOf("\"hash\"", il));
            c.keyedHash = readString(json, json.indexOf("\"keyed_hash\"", il));
            c.deriveKey = readString(json, json.indexOf("\"derive_key\"", il));
            cases.add(c);
            i = json.indexOf("\"derive_key\"", il) + 1;
        }
        return cases;
    }

    private static long readNumber(String json, int keyPos) {
        int colon = json.indexOf(':', keyPos);
        int p = colon + 1;
        while (p < json.length() && (json.charAt(p) == ' ' || json.charAt(p) == '\n')) p++;
        int start = p;
        while (p < json.length() && (Character.isDigit(json.charAt(p)))) p++;
        return Long.parseLong(json.substring(start, p));
    }

    private static String readString(String json, int keyPos) {
        int colon = json.indexOf(':', keyPos);
        int q1 = json.indexOf('"', colon + 1);
        int q2 = json.indexOf('"', q1 + 1);
        return json.substring(q1 + 1, q2);
    }

    public static void main(String[] args) throws IOException {
        // test_vectors.json sits beside this class's source root.
        Path vectors = Path.of(args.length > 0 ? args[0] : "test_vectors.json");
        if (!Files.exists(vectors)) {
            // also try relative to the blake3 dir if run from elsewhere
            Path alt = Path.of("blake3", "test_vectors.json");
            if (Files.exists(alt)) vectors = alt;
        }
        String json = Files.readString(vectors);
        List<Case> cases = parseCases(json);

        int pass = 0;
        int total = 0;
        boolean allOk = true;

        for (Case c : cases) {
            byte[] input = testInput(c.inputLen);
            int xofLen = c.hash.length() / 2;

            // (1) the 32-byte default digest -- the exact content_hash call.
            String def32 = toHex(Blake3.hash(input));
            boolean ok1 = def32.equals(c.hash.substring(0, 64));

            // (2) hash XOF at the vendored length.
            Blake3 h = Blake3.initHash();
            h.update(input);
            String hashXof = toHex(h.doFinalize(xofLen));
            boolean ok2 = hashXof.equals(c.hash);

            // (3) keyed_hash XOF.
            Blake3 kh = Blake3.initKeyedHash(KEY);
            kh.update(input);
            String keyedXof = toHex(kh.doFinalize(c.keyedHash.length() / 2));
            boolean ok3 = keyedXof.equals(c.keyedHash);

            // (4) derive_key XOF.
            Blake3 dk = Blake3.initKeyDerivationFunction(CONTEXT.getBytes());
            dk.update(input);
            String dkXof = toHex(dk.doFinalize(c.deriveKey.length() / 2));
            boolean ok4 = dkXof.equals(c.deriveKey);

            total++;
            boolean ok = ok1 && ok2 && ok3 && ok4;
            if (ok) {
                pass++;
            } else {
                allOk = false;
                System.out.printf(
                    "FAIL  in_len=%-7d  hash|keyed|derive|def32 = %b|%b|%b|%b%n",
                    c.inputLen, ok2, ok3, ok4, ok1);
            }
            System.out.printf("%s  in_len=%-7d  hash|keyed|derive|def32 all match%n",
                ok ? "PASS" : "FAIL", c.inputLen);
        }

        String bar = "----------------------------------------------------------------------";
        System.out.println(bar);
        System.out.printf("VERDICT: %d/%d cases PASS%n", pass, total);
        if (allOk && total == 35) {
            System.out.println("RESULT: PASS -- vendored BLAKE3 produces canonical output for "
                + "all official vectors (all four checks). content_hash primitive verified.");
            System.exit(0);
        } else {
            System.out.printf("RESULT: FAIL -- %d/%d (expected 35/35).%n", pass, total);
            System.exit(1);
        }
    }
}
