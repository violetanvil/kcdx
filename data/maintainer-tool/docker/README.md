# kcdx maintainer-tool — Docker operator contract

The maintainer tool ships as a **single Docker image** (design D14): one container
runs the FastAPI backend, which serves the API **and** the built React SPA
same-origin. The image carries **only app code** — the Address Library checkout
(the curated CSVs, the bulk corpus, the reference DBs) is a **mounted volume**, and
the push credential is **env-injected at run** (never baked into the image).

This doc is the operator contract: the env seams the backend already implements,
the volume mount, and the concrete build + run commands. Auth, login, hosting, the
reverse proxy, and TLS are **out of scope** (D17) — the operator wires those around
the container; the image only exposes the seams.

## Build

The build context is `data/maintainer-tool/` (the dir holding the `Dockerfile`).
Stage 1 builds the frontend (`npm ci && npm run build` → `dist/`); stage 2 is the
Python runtime that serves it. Build from that dir:

```sh
docker build -t kcdx-maintainer-tool data/maintainer-tool
```

The image is self-contained: it builds the SPA in-stage-1 from the nested
`frontend/` repo on disk, so there is no out-of-band pre-build to remember.

## The volume mount — the operator provides the checkout (D18)

The image carries no repo. The operator mounts their **git checkout** of kcdx at
the `KCDX_CHECKOUT` path inside the container. That checkout must carry:

- `data/db-export/` — the curated Address Library CSVs (git text).
- `data/db-export-bulk/` — the bulk corpus, under **Git LFS** (D38).
- `data/reference.sqlite` + `data/reference-dev.sqlite` — the reference DBs the
  backend reads and the save path amends (these are git-ignored locally; the
  operator's checkout must have them present — rebuilt from the CSV export if absent).

**`git lfs` is required on BOTH sides:** it is installed in the image (the backend
shells `git` against the checkout), AND the operator's mounted checkout must have
already pulled its LFS objects (`git lfs pull`) so the bulk corpus is real files,
not LFS pointer stubs.

A checkout that resolves no DB/seeds is **not** a crash — the backend's `/health`
endpoint reports the empty/error state and names where it looked.

## The env seams (the backend already implements these — this documents them)

| Env var | Required? | What it is |
|---|---|---|
| `KCDX_CHECKOUT` | **yes** (prod) | The mounted-volume checkout path the backend reads/writes/commits (D18). Unset → a dev default (the repo root inferred from the app location) so the app boots locally; in the container the operator wires it to the mount. |
| `KCDX_PUSH_TOKEN` | no | **SECRET** — the git push credential (a GitHub PAT / installation token), injected at RUN only (D17). **Absent → the push is skipped** and the Confirm commit stays local (the dev/test posture). Present → wired into the push as an ephemeral `http.extraheader` Authorization, never written to disk, never logged. **Never bake this into the image** (see "Secret handling" below). |
| `KCDX_MAINTAINER_NAME` | no | The commit-author + `verified_by` display **name** (D17 / FIX 3). A non-secret public author identity. Unset → a documented dev default. |
| `KCDX_MAINTAINER_EMAIL` | no | The commit-author **email** (D17 / FIX 3). Non-secret. Unset → a documented dev default. |
| `KCDX_CORS_ORIGINS` | no | Comma-separated production frontend origin allowlist (D17). For the **single-image same-origin** deployment this is typically **not needed** (the SPA calls the API on its own origin, D18); set it only if the frontend is served from a different origin. Never a wildcard. |
| `KCDX_STATIC_DIR` | no (set in image) | The built SPA `dist/` dir the backend serves. The image sets this to `/app/static` (where stage 2 COPYs `dist/`), so the operator does **not** set it. Documented here only because the image relies on it. |

## The run command

A concrete `docker run` — the operator's acceptance gesture (running the BUILT image):

```sh
docker run --rm \
  -p 8000:8000 \
  -v /path/to/your/kcdx-checkout:/checkout \
  -e KCDX_CHECKOUT=/checkout \
  -e KCDX_PUSH_TOKEN="$KCDX_PUSH_TOKEN" \
  -e KCDX_MAINTAINER_NAME="Your Name" \
  -e KCDX_MAINTAINER_EMAIL="you@example.com" \
  kcdx-maintainer-tool
```

- `-p 8000:8000` — the container serves on `0.0.0.0:8000`; publish it to the host.
- `-v <host-checkout>:/checkout` + `-e KCDX_CHECKOUT=/checkout` — the mounted volume
  IS the checkout the backend reads/writes/commits (D18).
- `-e KCDX_PUSH_TOKEN=...` — injected at run; omit it to commit locally without
  pushing (the dev/test posture).
- The maintainer identity envs default the commit author + the `verified_by` field.

Then open `http://localhost:8000/` — the SPA loads same-origin and `/health`,
`/entities`, the save/confirm endpoints answer on the same origin.

## Secret handling (security-invariants.md)

The **push token is a SECRET**. It is injected **at run** via `-e KCDX_PUSH_TOKEN`
(or a secrets manager / `--env-file` the operator controls) and **NEVER**:

- baked into an image layer (no `ENV KCDX_PUSH_TOKEN=...` in the `Dockerfile`, no
  `ARG` default, no COPYing a token file into the context — `.dockerignore` excludes
  `.env`/`*.pem`/`*.key`),
- written to the checkout's `.git/config` (the backend passes it as an ephemeral
  per-command `-c http.extraheader=...`, in-process only),
- logged (the backend redacts it from any git stderr).

The maintainer identity envs (`KCDX_MAINTAINER_NAME` / `_EMAIL`) are **non-secret**
public author identities — safe to surface; they are not credentials.

## Out of scope — the operator wires it (D17)

Auth / login, the web portal, the reverse proxy, TLS termination, and the hosting
itself are the operator's. The image exposes the seams (the checkout volume, the
env-injected credential, the request-context commit identity); the operator wires
login + provides the credential + hosts the container.
