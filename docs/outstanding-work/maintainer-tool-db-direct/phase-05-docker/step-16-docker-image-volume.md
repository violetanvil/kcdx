# Step 16 — Docker image + volume layout + env-credential seam (D14/D18)

**What.** Package the working app (Phases 2–4) as a **Docker image**: the Python backend
serving the API + the **built React frontend** static files, over the data-core (D14). The
image carries only app code; the git checkout (`data/seeds/` + the reference DB) is a
**mounted volume at a configured path** the container reads/writes/commits (D18); the push
credential is **env-injected** (the seam the backend's commit+push reads — Phase 2 step 5 /
D17). Resolve the image-packaging sub-decision here: a single image (backend serves the built
frontend) vs a `docker-compose` backend+frontend split (D14). Auth / login / hosting / the
proxy / TLS are the operator's (D17) — the image exposes the seams, not the deployment.

**Scope.** The Dockerfile (+ `docker-compose.yml` if the split is chosen) + the frontend
production build step + the served-build wiring + the volume-mount + env-var contract
(documented: the checkout path var, the push-credential var, the commit-identity seam). No
auth/login/hosting (the operator's). The `.exe`/PyInstaller path is GONE (superseded by D14).

**Test bar.** The image builds; a smoke test runs the container with a **mounted test
checkout** (the mini-dump-derived seeds + DB) + a **test push remote** (a throwaway local bare
remote via the env seam, NOT real GitHub) and asserts: the app serves (the API + the frontend
respond), a read endpoint returns the curated set from the volume, and a save commits+pushes
to the test remote via the env credential. The container-run smoke test is the phase's
user-facing acceptance (the operator runs the built image). Runnable when this step lands
(Phases 2–4 give a working app to package).

**Dependencies.** Phases 2–4 complete (the working backend + frontend to package). Phase 2
step 5 (the env-credential + commit-identity seams the image's env contract exposes). Phase 2
step 1 / D18 (the configured-checkout-path the volume mounts to). Sequenced LAST — there is no
app to package before it (`.claude/rules/incremental-delivery.md`: the dependency, a working
app, lands before its consumer, the image).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§8 (Distribution R9 — the Docker image, the mounted-checkout, gitignored build artifacts) +
§10 D14 (the web app + the image-packaging sub-decision) + D17 (auth out of scope, the
env-credential seam) + D18 (the volume-mounted checkout at a configured path). `requirements.md`
R9/R10.

**UX** (`.claude/rules/ux-first-class.md`): no NEW user-facing surface — the container serves
the same app Phases 3–4 verified. The acceptance is the BUILT image running against the volume
+ committing via the env seam (an operator launch gate). A misconfigured checkout (wrong
volume path) surfaces the s01 empty-state the read API + s01 already define (names the cause).

**Disassembler-test / author-burden.** N/A — packaging step; no author-facing input.

## Resolved 2026-06-15 (`/feature` audit — the two open forks + the verification handoff)

Vision-preserving build; D14/D17/D18 settled the scope. The audit surfaced exactly the forks
the design left open or did not pin, screened against `.claude/rules/cornerstones.md`, decided
by the user:

- **Image packaging (D14's reserved sub-decision) → SINGLE IMAGE.** The backend's uvicorn
  serves the API AND the built frontend `dist/` static files from one container (same-origin —
  D18's "CORS may not apply" note holds). Wins on operator-UX (one `docker run`, one volume,
  one env set); Capability identical; no cornerstone traded. Rejected: a `docker-compose`
  backend+frontend split (a second service + cross-origin CORS for no benefit a private
  few-maintainer tool uses).
- **Frontend bundle source (D23 makes the FE a gitignored nested repo) → MULTI-STAGE,
  built in-image.** Stage 1 `npm ci && npm run build` the nested `frontend/` (on disk at
  `data/maintainer-tool/frontend/` — gitignored-from-kcdx ≠ excluded-from-build-context, the
  build context is a filesystem read) → `dist/`; stage 2 = python runtime + backend + copied
  `dist/`. D14's by-construction Linux-compat (`.gitattributes` `* text=auto eol=lf`,
  `package-lock.json` present) is what makes `npm ci` resolve in the Linux build stage —
  verified present. Wins on operator-UX (one build command, self-contained + reproducible);
  no cornerstone traded. Rejected: a pre-built `dist/` COPYed in (a forgettable out-of-band
  manual pre-build → silent stale-bundle foot-gun).
- **Verification handoff (Docker absent on the build machine — no `docker` on PATH, no Docker
  Desktop).** Step 16 (backend static-serving) is pytest-gated by the agent now. Steps 16b/16c
  need a Docker daemon the agent does not have → the user runs the one irreducible gesture (the
  documented `docker build` + the smoke-test script), the agent reads the `ACCEPT-SUITE` result
  (`.claude/rules/acceptance-signal.md` / `headless-testable.md` / `agent-builds-and-deploys.md`
  — the same shape as the game-launch gate). NOT a self-deferral: the user chose build-all-now
  with the user-run Docker gate (`.claude/rules/deferral-authority.md`).

**Decomposition (each its own commit):** 16 backend static-serving (pytest) · 16b the
multi-stage Dockerfile + `.dockerignore` + env/volume/`git lfs` contract · 16c the
container smoke test (emits `ACCEPT-RESULT`/`ACCEPT-SUITE`; user-run). Ledger:
[README.md](README.md).
