# Step 3.1 — probe P3: off-vtable raw-handle access + handle representation

**What.** Settle the kcdx handle representation (design §4.4) by probing P3 (design
§8): does any engine code BYPASS the CCryPak vtable and operate a handle directly
(a streamer / DirectStorage path holding a raw `FILE*` and `fseek`-ing it
off-vtable)? The answer decides whether a kcdx handle can be a lightweight kcdx
handle-id (the engine treats it as opaque, only the vtable read slots operate it)
or must be a real `FILE*`-shaped object operable on kcdx's CRT off-vtable. This
gates the open/read slots (3.2/3.3) — they must mint the representation P3
settles.

**Scope.** First the static leads (read
`_research/asset-loadpath-map-recon/F5-streaming-engine-bypass.md` +
`step2-directstorage-bypass-finding.md`), then a live probe: does any asset read
reach bytes WITHOUT going through a vtable read slot? Probe (read-only,
captured+removed, no residue). One commit's worth of probe + the
representation-decision record. Agent writes/builds/deploys; user launches; agent
reads the log.

**Outcome→meaning map** (pre-committed, design §8 P3):
- no off-vtable raw-handle access → a kcdx handle-id representation is safe → 3.2
  mints handle-ids.
- an off-vtable streamer operates a raw handle → kcdx's handle must be a real
  `FILE*`-shaped object operable on kcdx's CRT off-vtable → 3.2 mints that.

**Test bar.** A probe, not a feature — the "test" is P3 firing with a falsifiable
reading matching one outcome (agent-read, `PROBE P3` category). The representation
decision it produces is captured durably (a decision record / the plan-spec
invariant). The slots that consume it (3.2/3.3) carry the permanent regression
rows.

**Dependencies.** Phase 1 (the swap is proven — the probe runs against the
swapped-in vtable) + Phase 2 (the index + reader exist, so a real read can be
exercised). Ordered before 3.2 per `.claude/rules/incremental-delivery.md` (the
open slots rest on the representation P3 settles).

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §4.4 (handle
representation), §8 P3; `_research/asset-loadpath-map-recon/F5-streaming-engine-bypass.md`
+ `step2-directstorage-bypass-finding.md` (the static leads).

**Disassembler-test / author-burden.** N/A — engine-internal probe.
