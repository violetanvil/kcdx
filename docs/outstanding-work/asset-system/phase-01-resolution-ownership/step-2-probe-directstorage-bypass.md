# Phase 1 step 2 — probe: does DirectStorage bypass the seam for textures?

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 2.

## What

The seam's one flagged-unverified coverage gap (design §7 caveat). KCD2 has an
OPTIONAL DirectStorage texture path (`dstorage.dll`, gated by
`wh_sys_streaming_directstorage_enabled`, default **0/OFF**) that MAY open its own
file handles outside `CCryPak::FOpen` — which would mean a texture served via
DirectStorage bypasses both seam hooks. The load-path map confirmed every NORMAL
class opens via FOpen; DirectStorage is the one arm it flagged unread. This probe
confirms — before the seam ships — whether DS bypasses the seam, so "the seam
serves all textures" is grounded, not assumed (`results-driven.md`). User-chosen as
a v1 probe over a deferral.

## Scope

- A throwaway probe (a `// === DIAGNOSTIC (PROBE …)` site and/or a read-only static
  read of the DS texture path) — agent-written/built/deployed, user-launched,
  agent-read (`agent-builds-and-deploys.md`). One variable; captured + removed.
- Reuse-first: `_research/asset-loadpath-map-recon/F1-texture-dds-loadpath-finding.md`
  already flagged the DS arm (`FUN_180d2ad38`, "Fallbacking to normal stream engine
  instead.") and read `wh_sys_streaming_directstorage_enabled` default 0. Start
  there (static read of the DS open path) before any live launch.
- The probe answers: (a) is DS enabled by default in the verified build (re-confirm
  the CVar default)? (b) if a texture loads via DS, does DS open its own handle
  (Win32 `CreateFile`/the DStorage API) WITHOUT calling `CCryPak::FOpen`?

## Outcome → meaning map (pre-committed, theory-independent)

- **DS default-OFF AND a texture's normal load goes through FOpen** (the common
  case) → the seam covers textures; DS is a non-default arm → its handling is a
  documented v1 limitation, not a gap on the common path. No seam change.
- **DS opens its own handle bypassing FOpen** → a texture served via DS is NOT
  reached by the seam → SURFACE a fork (`design-authority.md`): scope DS-textures
  out of v1 (documented limitation, default-off) OR own a DS seam (a separate
  mechanism). Do NOT silently ship "all textures covered."
- **DS is default-ON in the verified build** (contradicts the prior read) → a
  surfaced finding: textures are the common case AND may bypass — the seam's
  texture coverage is in question; re-weigh before shipping.

## Test bar

The probe IS its own verification — the pre-committed outcome map read against the
static DS-path read + (if needed) the live log (`acceptance-signal.md` — the agent
reads). No permanent test plugin (that is step 10). Finding captured to
`_research/`; any in-source probe removed (no residue, `working-artifacts.md`).

## Dependencies

None blocking (independent of step 1's ordering probe). Ordered in Phase 1 before
the hooks (s3/s4) so a DS-bypass finding can reshape the seam's texture-coverage
claim before the hooks are built (`incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§7 (the DirectStorage caveat) + §9 (build-gated probe 2). Shared spec:
[`../plan-spec.md`](../plan-spec.md) §"Build-gated probes". Prior read:
`_research/asset-loadpath-map-recon/F1-texture-dds-loadpath-finding.md`.

## Disassembler-test / author-burden

None — a probe adds no author-facing surface.
