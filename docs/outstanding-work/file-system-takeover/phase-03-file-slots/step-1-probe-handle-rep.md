# Step 3.1 — probe P3: off-vtable raw-handle access + handle representation

**What.** Settle the kcdx handle representation (design §4.4) by probing P3 (design
§8): does any engine code BYPASS the CCryPak vtable and operate a handle directly
(a streamer / DirectStorage path holding a raw `FILE*` and `fseek`-ing it
off-vtable)? The answer decides whether a kcdx handle can be a lightweight kcdx
handle-id (the engine treats it as opaque, only the vtable read slots operate it)
or must be a real `FILE*`-shaped object operable on kcdx's CRT off-vtable. This
gates the open/read cutover (the merged step 3.2) — it must mint the
representation P3 settles.

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
invariant). The slots that consume it (the merged step 3.2 open+read cutover)
carry the permanent regression rows.

**Dependencies.** Phase 1 (the swap is proven — the probe runs against the
swapped-in vtable) + Phase 2 (the index + reader exist, so a real read can be
exercised). Ordered before 3.2 per `.claude/rules/incremental-delivery.md` (the
open slots rest on the representation P3 settles).

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §4.4 (handle
representation), §8 P3; `_research/asset-loadpath-map-recon/F5-streaming-engine-bypass.md`
+ `step2-directstorage-bypass-finding.md` (the static leads).

**Disassembler-test / author-burden.** N/A — engine-internal probe.

## Resolution

**RESOLVED 2026-06-15 — outcome 1, static binary read (no live launch).** P3 is a
static call-graph question, settled from the primary-evidence asset-resolution recon
already on disk; static evidence settles it and precedes a live probe
(`.claude/rules/results-driven.md` §4), so the Scope's "then a live probe" was not
needed. No probe code was written into `src/` — no residue to remove.

**Outcome that held — outcome 1: no off-vtable raw-handle access to a `FOpen`-class
(slot 36) handle.**
- The read family (FReadRaw 38/39, FGetCachedFileData 40, FWrite 41, FSeek 53,
  FClose 55, FEof 56 — roles per the later slot-map reconciliation `4ca0bae`,
  `_research/fs-takeover-slot35-recon/FINDINGS.md`; an earlier draft here carried
  front3's superseded labels) dispatches purely on the handle tag THROUGH the
  vtable; the loose-vs-pak decision bites at `FOpen`-time, never off-vtable
  (`_research/phase8.5-pak-resolver/front3-handle-consume-read-path.md`).
- The ONE off-vtable raw-handle operation — the streaming engine's
  `SetFilePointer`/`ReadFile` on `m_zipFile` — operates an ENGINE-minted pak-MOUNT
  handle (`CreateFileA`, archive factory slot 72), NEVER a `FOpen` per-file handle
  (`_research/asset-loadpath-map-recon/F5-streaming-engine-bypass.md`).
- DirectStorage (the only other off-vtable open candidate) is default-OFF and dead at
  the shipped default
  (`_research/asset-loadpath-map-recon/step2-directstorage-bypass-finding.md`).

Outcome 2 (forced to a real `FILE*`-shaped object) is FALSIFIED.

**Decision (design §4.4 settled).** A kcdx `FOpen` handle is a lightweight **kcdx
handle-id** — opaque to the engine, operated only by kcdx's own read slots. It honors
the engine's tagged-union contract (`index+1` = pak entry; else = real-`FILE*`-class)
so any reused/thunked read slot dispatches it correctly. **Load-bearing constraint
for 3.2/3.4:** the read family is **kcdx-owned, never thunked** — a thunked read
slot's OS arm would `fread` the kcdx handle-id on the ENGINE's CRT (the cross-CRT
straddle §9 removes). The merged step 3.2 mints the handle-id AND builds the
kcdx-owned read family in one atomic cutover (open + read flip together — a
handle-id reachable by a thunked read slot is exactly the straddle); 3.4's
per-slot table keeps every handle-operating slot `KCDX`. (This constraint is WHY
the original 3.2-mints / 3.3-reads split was merged into one step.)

**Capture:** `_research/probe-archive/p3-off-vtable-handle-rep.md` (the finding +
the cited recon).
