# Step 2.3 — DEFLATE read path (kcdx CRT)

**What.** Build kcdx's pak-entry read path (design §6): given an index entry's
`{offset, size, method}`, kcdx opens the pak file (`_wfopen`, kcdx CRT), seeks to
the entry offset (kcdx `fseek`, kcdx CRT), reads the compressed bytes (kcdx
`fread`, kcdx CRT), and inflates via the step-2.1 inflater (kcdx's allocator) —
or copies directly for a STORED entry. Every byte on kcdx's CRT, cradle-to-grave;
no engine `ucrtbase` touches a pak read. This is the byte-delivery half the read
family (Phase 3) calls into for a Pak byte-source.

**Scope.** The pak-entry read function(s): open+seek+read+inflate for a DEFLATE
entry, open+seek+read for a STORED entry, all on kcdx's CRT. The output is the
decompressed bytes for a vpath's pak byte-source. One commit. Pairs with the CDR
parser (2.2) — that locates the entry, this reads it.

**Test bar.** A test that reads a known STORED entry AND a known DEFLATE entry
from a real pak and asserts the output bytes match the known plaintext (a
falsifiable comparison, `.claude/rules/test-suite.md`). The cross-CRT invariant is
asserted structurally: the read uses kcdx's CRT calls only (reviewed — no engine
read leaf in the path). Build green.

**Dependencies.** Step 2.1 (the inflater) + step 2.2 (the CDR parser supplies the
entry coordinates). Both land before this; this is independently verifiable on a
real pak the moment it lands.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §6 (every byte on kcdx
CRT), §4.4 (the cross-CRT invariant); `_research/phase8.5-pak-resolver/front3-handle-consume-read-path.md`
(the read-path mechanism the kcdx reader replaces).

**Disassembler-test / author-burden.** N/A — no author-facing surface.
