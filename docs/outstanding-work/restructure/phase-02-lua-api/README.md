# Phase 2 — Lua API skeleton

**Status: DONE.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 2" + the "As-built status + governance" block at the top of that file.

All seven core verbs (`kcdx.hook`/`bytes`/`code`/`on`/`command`/`publish`/`scan`)
plus the grouped `kcdx.*` domains, the `docs/lua/` reference, `zone_gate`
capability gating, and the `kcdx.plugin.*` introspection domain. The `kcdx.hook`
surface ships all six modes (before/after/around/replace + mid + callsite) with
chaining, locators (incl. address_id-by-name), and the name-carries-the-ABI
`target=` form. Multi-file plugins + complete source attribution, `kcdx.on`
lifecycle bridge + `ready`, `kcdx.publish` cross-plugin pub/sub, `kcdx.cosave.*`
persistence — all live.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| Phase 2 subs 1–9 + AP12 batch + multi-file + command + per-entry-zone + code + cosave + scan + zone_gate | DONE | — |

Per-row live evidence is `../../../../test-plugins/README.md`; chronology is `git log`.
