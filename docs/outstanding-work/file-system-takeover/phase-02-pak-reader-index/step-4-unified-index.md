# Step 2.4 — unified asset index built at load

**What.** Build the unified asset index (design §5): one in-memory map `vpath →
ByteSource { Loose{diskPath} | Pak{pakFile, offset, size, method, crc} }`,
populated at load by walking every byte-source kcdx serves — vanilla paks
(discover `<game>/Data/*.pak`, CDR-parse each via 2.2 to record Pak sources),
kcdx-mounted mod paks, and loose mod-override files (carried from the existing
overlay map / sidecar declarations). Precedence (overlay wins vanilla; load-order;
cross-mod resolution from `asset-replacement.md` §4.4/§5.3) is decided ONCE here.
Resolution becomes a single O(1) lookup per open — the "no extra hotpath checks"
property.

**Scope.** The index data structure + its load-time builder: vanilla-pak
discovery + CDR population (E7), loose-override + mod-pak source ingestion carried
from the asset-overlay map (E8), and the precedence resolution at build time. One
commit. Built at load (cold path); the lookup it serves is O(1) (hot path). This
step builds the index + its build; the SLOTS that consult it are Phase 3.

**Design authority.** Built to `docs/design/file-system-takeover.md` §5 (the index
shape, the O(1)-lookup property, build-time precedence) and §7 (the precedence is
the `asset-replacement.md` §4.4/§5.3 precedence, now computed once). The executor
builds to those sections (`.claude/rules/spec-conformance.md`).

**Test bar.** A test that builds the index over a fixture (a vanilla pak + a loose
override + a mod pak) and asserts: each vpath resolves to the correct ByteSource,
the override wins the vanilla path, and a cross-mod reference resolves
(`.claude/rules/test-suite.md`). A launch logs the built index entry count + a
sample resolution (agent-read, `kcdx-dev.log`). Falsifiable: FAILS if an override
does not win, or a vpath resolves to the wrong source.

**Dependencies.** Step 2.2 (CDR parser — to populate Pak sources) + step 2.3 (the
read path the index's Pak sources point into). The loose/overlay precedence reuses
the existing asset-overlay map logic (still live until step 3.6).

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §5 (the index), §7
(precedence carried from asset-replacement); [`docs/design/asset-replacement.md`](../../../design/asset-replacement.md)
§4.4/§5.3 (the precedence rules).

**Disassembler-test / author-burden.** N/A — engine-internal. (The author-facing
contract this index serves is unchanged from asset-replacement — design §7 — so no
new author burden is introduced.)
