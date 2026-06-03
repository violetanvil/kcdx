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
