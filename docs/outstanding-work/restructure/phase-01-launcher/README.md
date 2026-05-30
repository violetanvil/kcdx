# Phase 1 — launcher exe + drop the .asi extension

**Status: DONE** (live-verified). Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 1".

The install-layout migration: launch model flipped from "Ultimate ASI Loader +
kcdx.asi" to "`kcdx.exe` injects `kcdx.dll`". Shipped the launcher exe, the
`kcdx-engine/` → `engine/` move, the `kcdx --init-plugin <name>` scaffolder, and
the `docs/migration.md` install-layout guide.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| launcher exe + .asi→.dll + path rework + scaffolder | DONE | live-verified |
