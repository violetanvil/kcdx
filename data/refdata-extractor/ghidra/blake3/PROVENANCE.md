# Vendored BLAKE3 — provenance

`org/apache/commons/codec/digest/Blake3.java` is the **Apache Commons Codec**
`org.apache.commons.codec.digest.Blake3` class, vendored verbatim.

- **Origin:** Apache Commons Codec, version **1.16.0** (the class is `@since 1.16`).
- **Exact source:** extracted from the official Maven Central sources jar
  `commons-codec-1.16.0-sources.jar`
  (`https://repo1.maven.org/maven2/commons-codec/commons-codec/1.16.0/commons-codec-1.16.0-sources.jar`)
  — the genuine upstream distribution artifact, not a hand-copy.
- **License:** Apache-2.0 (the license header is retained verbatim at the top of
  the file).
- **Algorithm lineage (per the upstream Javadoc):** adapted from the
  ISC-licensed *O(1) Cryptography* library by Matt Sicker, ported from the
  reference public-domain BLAKE3 implementation by Jack O'Connor. It descends
  from the official BLAKE3 reference — it is NOT a from-spec hand-roll.

## Why this port

- **Single self-contained file, JDK-only dependencies** (`java.util.Arrays`,
  `java.util.Objects`) — drops into the headless Ghidra script environment with
  no Maven/Gradle/per-machine install (the same reusability principle as the
  other extractor scripts).
- **Strongest provenance of the single-file options** (ASF-maintained, the
  reference lineage above) vs. a single-author port.
- **Exposes both the 32-byte default digest and the XOF**, so the self-test can
  check the official vectors' full answers, not just a 32-byte prefix.
- A from-spec hand-roll was rejected: a subtly-wrong crypto primitive silently
  corrupts every `content_hash` (an AP-class correctness foot-gun). A verified
  port + a known-answer self-test is the safe choice.

## Re-vendoring recipe (if this file is ever lost again)

```bash
curl -sL -o cc-sources.jar \
  https://repo1.maven.org/maven2/commons-codec/commons-codec/1.16.0/commons-codec-1.16.0-sources.jar
unzip -o cc-sources.jar org/apache/commons/codec/digest/Blake3.java
# -> org/apache/commons/codec/digest/Blake3.java
```

Verify canonicality afterwards: `Blake3SelfTest.java` asserts the 35 official
BLAKE3 test vectors (35/35 PASS). A quick smoke check: BLAKE3 of the empty input
begins `af1349b9f5f9a1a6`.

## Files in this directory

- `org/apache/commons/codec/digest/Blake3.java` — the vendored upstream class.
- `Blake3SelfTest.java` — known-answer self-test over the 35 official vectors
  (the falsifiability gate for the `content_hash` primitive).
- `Blake3Hex.java` — a stdin→lowercase-hex filter over the vetted `Blake3`,
  used by the Python output-validation harness as the INDEPENDENT hash oracle
  (it hashes bytes the harness reads itself via pefile — not the extractor's
  Ghidra `ContentHash` path).
