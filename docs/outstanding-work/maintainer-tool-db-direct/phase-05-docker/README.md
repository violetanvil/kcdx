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

| Step | Status | Commit |
|---|---|---|
| 16 Docker image + volume layout + env-credential seam (D14/D18) | NOT STARTED | — |

## Step docs

16. [step-16-docker-image-volume.md](step-16-docker-image-volume.md)

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
