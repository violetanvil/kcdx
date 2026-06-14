# Step 2.2 — PKZIP central-directory parser + vanilla-pak format check

**What.** Build kcdx's own PKZIP central-directory parser (design §6) and run the
vanilla-pak format-uniformity check first (design §27/§6): read a real
`<game>/Data/*.pak`'s first bytes + central directory to confirm it is standard
PKZIP (`PK\x03\x04`, STORED+DEFLATE, no zip64, unsigned CDR) — the recon verified
2 Nexus MOD paks but not a vanilla pak's bytes. The parser extracts every entry's
`{name(=vpath), offset, size, method, crc}` for the index (step 2.4) to consume.

**Scope.** The format check (a static read of a real vanilla pak — cheap, not a
live probe) + the CDR parser (locate EOCD, walk central-directory records, extract
per-entry fields). Pure kcdx code on kcdx's CRT — no engine ZipDir. One commit. If
the vanilla format check surfaces a deviation (a proprietary header, zip64,
encryption), STOP and surface it — the design's standard-PKZIP assumption would be
falsified (design §6 marks this as a probe-before-building check).

**Test bar.** A test that parses a real vanilla pak's CDR and asserts the expected
entry count + a known entry's `{offset,size,method}` against the pak's actual
bytes (a falsifiable fixture, `.claude/rules/test-suite.md`). The format check is
a named assertion: the vanilla pak IS standard PKZIP (FAILS loud if not). Build
green.

**Dependencies.** None structurally (CDR parse needs no inflater); ordered after
2.1 only for phase coherence. Independently verifiable: the CDR parse is testable
on a real pak the moment it lands.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §6 (the PKZIP reader +
the format-uniformity check); `_research/phase8.5-pak-resolver/RESOLUTION-OWNERSHIP-synthesis.md`
§4 + front-5 (the verified-on-real-files PKZIP format facts).

**Disassembler-test / author-burden.** N/A — no author-facing surface.
