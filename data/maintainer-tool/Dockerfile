# kcdx maintainer-tool -- single-image, multi-stage Docker packaging.
#
# SINGLE IMAGE: stage 1 (Node) builds the React SPA -> dist/; stage 2 (Python)
# runs the FastAPI backend, which serves the API AND the built dist/ same-origin.
# The image carries ONLY app code; the git checkout (the curated CSVs + the
# reference DBs) is a mounted volume at the KCDX_CHECKOUT path. Secrets
# (KCDX_PUSH_TOKEN) are env-injected at RUN, NEVER baked into a layer.
#
# Build context = this dir. frontend/ + backend/ are reachable as-is in the
# context; .dockerignore trims it.

# ---------------------------------------------------------------------------
# Stage 1 -- frontend build (Node LTS). Produces /fe/dist (the built SPA bundle).
# ---------------------------------------------------------------------------
# node:22.* is the active LTS line (pinned to a stable minor, not `latest`).
# bookworm-slim keeps the build image lean; only dist/ crosses to stage 2, so
# the Node toolchain never ships in the runtime image.
FROM node:22.12-bookworm-slim AS frontend-build

WORKDIR /fe

# Same-origin SPA: VITE_API_BASE="" makes apiBase() return relative URLs, so the
# bundled SPA calls the backend on its own origin (the single image). An
# empty string is preserved by the frontend's `?? DEV_DEFAULT` (only null/undefined
# fall back), so this MUST be set for the build, not left unset.
ENV VITE_API_BASE=""

# Layer-cache the dependency install independently of source: copy the lockfile +
# manifest FIRST, `npm ci` (clean, reproducible, from the lockfile; the
# .gitattributes `eol=lf` pin keeps the lockfile + sources LF so the linux build
# resolves), THEN copy the rest. A source
# edit invalidates only the COPY+build layers below, never the cached npm ci.
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci

# Now the rest of the frontend, and build (tsc -b && vite build -> dist/).
COPY frontend/ ./
RUN npm run build

# ---------------------------------------------------------------------------
# Stage 2 -- python runtime (slim). Runs uvicorn serving the API + the built SPA.
# ---------------------------------------------------------------------------
# python:3.12-slim-bookworm: 3.12 matches the backend's supported Python (fastapi
# 0.136 / uvicorn 0.48 run on 3.12); pinned to a stable minor (not `latest`).
FROM python:3.12-slim-bookworm AS runtime

# git AND git-lfs: the backend shells `git -C <checkout>` to stage/commit/push the
# mounted checkout, and the bulk corpus is Git LFS.
# --no-install-recommends keeps the layer lean; clean the apt lists in the same layer.
RUN apt-get update \
    && apt-get install -y --no-install-recommends git git-lfs \
    && git lfs install --system \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/backend

# Layer-cache the pip install independently of the app source: requirements FIRST,
# pip install, THEN the app. An app edit never invalidates the dependency layer.
# --no-cache-dir keeps the pip download cache out of the image layer.
COPY backend/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

# The backend app (lands at /app/backend/app/ -- so `app.main:app` resolves from
# this WORKDIR, the same module path the dev `uvicorn app.main:app` uses).
COPY backend/ ./

# The built SPA from stage 1 -> the static dir the backend serves. KCDX_STATIC_DIR
# is set EXPLICITLY (below) to this exact path, so config.static_dir() resolves here
# with no dependence on _default_static_dir()'s <app>/static computation -- the COPY
# target and the env agree by construction, the less-fragile choice.
COPY --from=frontend-build /fe/dist /app/static
ENV KCDX_STATIC_DIR=/app/static

# The backend serves on 8000 (the dev default origin the FE config documents). Bind
# 0.0.0.0 so the container accepts connections from outside (NOT 127.0.0.1, which
# would only answer inside the container). This is the PRODUCTION launch of the BUILT
# artifact -- no --reload (this is the production launch, not a dev launcher).
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
