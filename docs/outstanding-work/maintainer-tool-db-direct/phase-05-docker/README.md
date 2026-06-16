# Phase 5 — Docker packaging

**Intent.** Package the working app as a **Docker image** (D14/D18): the Python backend
serving the API + the built React frontend's static files, over the data-core, with the git
checkout (`data/seeds/` + the reference DB) on a **mounted volume at a configured path** and
the push credential **env-injected**. Lands last — nothing to package until the app works
(Phases 2–4). Auth / login / hosting / the reverse proxy / TLS / the portal are the
**operator's** (D17), explicitly out of scope; the image exposes the seams, the operator
wires them.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §8 + §10
D14/D17/D18.

## Step ledger

Step 16 decomposes into three commit-grain sub-steps (settled 2026-06-15 via `/feature`; the
two open forks + the verification handoff resolved by the user — see step-16 doc §"Resolved
2026-06-15"). 16 (backend static-serving) is pytest-gated by the agent now; 16b/16c need a
Docker daemon (absent on this machine) → the user runs the `docker build` + smoke gate, the
agent reads the `ACCEPT-SUITE` result (`.claude/rules/acceptance-signal.md` / `headless-testable.md`).

| Step | Status | Commit |
|---|---|---|
| 16 backend static-serving — uvicorn serves the built frontend `dist/` as the SPA + the API (single image, same-origin); API routes keep priority, deep-link fallback to `index.html`. pytest-gated (agent-run) | DONE | (pending) |
| 16b Docker image + env/volume contract — multi-stage Dockerfile (in-image `npm ci && npm run build` of the nested frontend → backend runtime + copied `dist/`) + `.dockerignore` + the documented env-var/volume/`git lfs` contract (D14/D18) | NOT STARTED | — |
| 16c container smoke test — `docker build` + run with a mounted test checkout + a throwaway local bare remote (the `KCDX_PUSH_TOKEN` seam); asserts API+frontend serve, a read returns the curated set, a save commits+pushes; emits `ACCEPT-RESULT`/`ACCEPT-SUITE` (user-run gate, agent reads) | NOT STARTED | — |

A top-level Step 5 / the sequence-ledger row flips DONE when 16 + 16b + 16c are DONE (16c's
DONE is the user's smoke-gate `ACCEPT-SUITE: N/N passing`).

## Step docs

16. [step-16-docker-image-volume.md](step-16-docker-image-volume.md) (covers all three sub-steps)

## Verification gate (phase end)

- A Docker build produces the image: the backend serves the API + the built frontend; the
  app runs against a **mounted-volume checkout** at the configured path (the reference DB +
  `data/seeds/`); the push credential is read from the env-injected seam (D17/D18).
- The maintainer (operator) runs the container behind their own auth/proxy and reaches the
  full six-job app from a browser (including a phone) — the same flows Phases 3–4 verified,
  now from the built container.
- The image carries only app code (no repo baked in — the checkout is the volume, D18). The
  image-packaging sub-decision (single image vs a `docker-compose` backend+frontend split,
  D14) is resolved here.
- The built artifacts (the frontend build output, the image) are gitignored where
  appropriate; the Dockerfile / compose / the served-build config + the volume + env contract
  are what land in the repo.
- **User-facing acceptance:** the operator builds + runs the image with a mounted checkout +
  the env credential, opens the app in a browser, and runs the catalog — the acceptance is
  running the BUILT image (not a dev launcher, `.claude/rules/acceptance-signal.md`). Auth/
  hosting are the operator's; the build's gate is "the container runs the app against the
  volume + commits/pushes via the env seam."
