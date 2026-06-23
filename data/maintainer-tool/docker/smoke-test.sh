#!/usr/bin/env bash
# kcdx maintainer-tool — single-image Docker container smoke test.
#
# WHAT THIS PROVES (one falsifiable claim per assertion):
#   The single multi-stage image builds, runs, and end-to-end:
#     api-serves          — the FastAPI backend answers GET /health 200 with a JSON state.
#     frontend-serves     — uvicorn serves the built React SPA same-origin (GET / -> HTML).
#     read-returns-curated— GET /entities returns the curated entity set from the mounted
#                           checkout's reference DB (PASS), or SKIP when no local DB exists.
#     save-commits        — a save->confirm (a `notes` re-edit) lands a NEW git commit in
#                           the mounted checkout (push skipped: hermetic, no real token).
#
# THE USER RUNS ONE COMMAND (Docker is required):
#     bash docker/smoke-test.sh
# It builds its OWN fixtures (a temp checkout + a throwaway bare remote), builds + runs the
# image, asserts, and prints a machine-readable result summary to STDOUT.
#
# The result grammar — these tokens are emitted verbatim:
#     ACCEPT-RESULT: <PASS|FAIL|SKIP> <id> [— <detail>]
#     ACCEPT-SUITE: <passed>/<total> passing
#
# HERMETIC PUSH SEAM (the dev/test posture): the container runs WITHOUT a real
# KCDX_PUSH_TOKEN, so the backend SKIPS the push and the Confirm commit stays
# LOCAL in the mounted checkout. save-commits asserts that local commit exists — the
# push-to-real-GitHub path is not hermetically testable and that is correct. A throwaway local
# bare remote is still wired as `private` so the checkout's remote topology matches production
# (a no-token run never reaches it; this keeps the fixture faithful without exercising auth).

set -euo pipefail

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
IMAGE_TAG="kcdx-maintainer-smoke"
CONTAINER_NAME="kcdx-maintainer-smoke-$$"
HOST_PORT="${KCDX_SMOKE_PORT:-18000}"      # host port -> container 8000 (override if 18000 is taken)
HEALTH_MAX_ATTEMPTS=60                      # readiness wait: 60 attempts x ~1s = ~60s cap
HEALTH_SLEEP_SECONDS=1
MAINTAINER_NAME="Smoke Test"
MAINTAINER_EMAIL="smoke@kcdx.local"

# Resolve paths relative to THIS script's own dir (never cd-into the tree).
# This script:           docker/smoke-test.sh
# Build context:         the repo root                        (../  from here)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
BUILD_CONTEXT="$(cd "${SCRIPT_DIR}/.." >/dev/null 2>&1 && pwd)"
REPO_DB_EXPORT="$(cd "${SCRIPT_DIR}/../../.." >/dev/null 2>&1 && pwd)/data/db-export"
REPO_REFERENCE_DB="$(cd "${SCRIPT_DIR}/../../.." >/dev/null 2>&1 && pwd)/data/reference.sqlite"

# The three curated CSV filenames the backend reads (config.py _SEED_FILES).
SEED_FILES=(module_seed.csv address_names_seed.csv address_versions_seed.csv)

# Mutable run state (set as we go; the trap cleans up whatever exists).
TMP_CHECKOUT=""
TMP_BARE_REMOTE=""
CONTAINER_ID=""
DB_PROVISIONED=0                            # 1 if the mounted checkout has reference.sqlite

# Result accounting.
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
TOTAL_COUNT=0

# ---------------------------------------------------------------------------
# Result emitters — the result grammar, verbatim to STDOUT.
# ---------------------------------------------------------------------------
emit_pass() {  # emit_pass <id>
    PASS_COUNT=$((PASS_COUNT + 1)); TOTAL_COUNT=$((TOTAL_COUNT + 1))
    echo "ACCEPT-RESULT: PASS $1"
}
emit_fail() {  # emit_fail <id> <detail>
    FAIL_COUNT=$((FAIL_COUNT + 1)); TOTAL_COUNT=$((TOTAL_COUNT + 1))
    echo "ACCEPT-RESULT: FAIL $1 — $2"
}
emit_skip() {  # emit_skip <id> <detail>
    SKIP_COUNT=$((SKIP_COUNT + 1)); TOTAL_COUNT=$((TOTAL_COUNT + 1))
    echo "ACCEPT-RESULT: SKIP $1 — $2"
}
emit_suite() {
    # passed = PASS only (a SKIP is neither pass nor fail). Any FAIL denies acceptance.
    # The SKIP count is surfaced on its own line so the agent sees GREEN vs DENIED vs partial.
    echo "ACCEPT-SUITE: ${PASS_COUNT}/${TOTAL_COUNT} passing"
    if [ "${SKIP_COUNT}" -gt 0 ]; then
        echo "ACCEPT-SKIPPED: ${SKIP_COUNT} skipped (environment gaps, not failures)"
    fi
}

# A hard prerequisite failure: emit one FAIL + a 0/1 suite and exit (the trap still cleans up).
die_prereq() {  # die_prereq <detail>
    emit_fail prereq "$1"
    emit_suite
    exit 1
}

# ---------------------------------------------------------------------------
# Cleanup trap — stop+rm the container and remove the temp dirs, ALWAYS (even on failure).
# ---------------------------------------------------------------------------
cleanup() {
    set +e
    if [ -n "${CONTAINER_ID}" ]; then
        docker stop "${CONTAINER_ID}" >/dev/null 2>&1
        docker rm "${CONTAINER_ID}" >/dev/null 2>&1
    else
        # Fall back to the name in case `docker run` started it but we never captured the id.
        docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1
    fi
    [ -n "${TMP_CHECKOUT}" ] && rm -rf "${TMP_CHECKOUT}"
    [ -n "${TMP_BARE_REMOTE}" ] && rm -rf "${TMP_BARE_REMOTE}"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# HTTP helper — curl preferred, python3 fallback. Picked once at startup.
# get_status_body <url> -> sets HTTP_STATUS + HTTP_BODY (body in a temp file path HTTP_BODY).
# ---------------------------------------------------------------------------
HTTP_CLIENT=""
pick_http_client() {
    if command -v curl >/dev/null 2>&1; then
        HTTP_CLIENT="curl"
    elif command -v python3 >/dev/null 2>&1; then
        HTTP_CLIENT="python3"
    else
        die_prereq "no HTTP client found — install curl or python3"
    fi
}

# http_get <url> — sets globals: HTTP_STATUS (int, 000 on connect failure),
#                                 HTTP_CTYPE (lowercased content-type),
#                                 HTTP_BODY_FILE (path to the response body on disk).
HTTP_STATUS=""
HTTP_CTYPE=""
HTTP_BODY_FILE=""
http_get() {
    local url="$1"
    HTTP_BODY_FILE="$(mktemp)"
    if [ "${HTTP_CLIENT}" = "curl" ]; then
        local meta
        # -s silent, -S show errors, -o body file, write status + content-type to stdout.
        meta="$(curl -sS -o "${HTTP_BODY_FILE}" -w '%{http_code} %{content_type}' "${url}" 2>/dev/null || echo '000 ')"
        HTTP_STATUS="${meta%% *}"
        HTTP_CTYPE="$(echo "${meta#* }" | tr '[:upper:]' '[:lower:]')"
    else
        # python3 fallback: write status + content-type to stdout, body to the file.
        local meta
        meta="$(python3 - "$url" "$HTTP_BODY_FILE" <<'PY' 2>/dev/null || echo '000 '
import sys, urllib.request
url, body_path = sys.argv[1], sys.argv[2]
try:
    with urllib.request.urlopen(url, timeout=10) as r:
        data = r.read()
        status = r.status
        ctype = r.headers.get("Content-Type", "")
except urllib.error.HTTPError as e:
    data = e.read()
    status = e.code
    ctype = e.headers.get("Content-Type", "") if e.headers else ""
except Exception:
    print("000 ")
    sys.exit(0)
with open(body_path, "wb") as f:
    f.write(data)
print(f"{status} {ctype}")
PY
)"
        HTTP_STATUS="${meta%% *}"
        HTTP_CTYPE="$(echo "${meta#* }" | tr '[:upper:]' '[:lower:]')"
    fi
}

# http_post_json <url> <json-string> — same globals as http_get.
http_post_json() {
    local url="$1" json="$2"
    HTTP_BODY_FILE="$(mktemp)"
    if [ "${HTTP_CLIENT}" = "curl" ]; then
        local meta
        meta="$(curl -sS -o "${HTTP_BODY_FILE}" -w '%{http_code} %{content_type}' \
            -H 'Content-Type: application/json' -X POST --data "${json}" "${url}" 2>/dev/null || echo '000 ')"
        HTTP_STATUS="${meta%% *}"
        HTTP_CTYPE="$(echo "${meta#* }" | tr '[:upper:]' '[:lower:]')"
    else
        local meta
        meta="$(python3 - "$url" "$HTTP_BODY_FILE" "$json" <<'PY' 2>/dev/null || echo '000 '
import sys, urllib.request
url, body_path, payload = sys.argv[1], sys.argv[2], sys.argv[3]
req = urllib.request.Request(url, data=payload.encode("utf-8"),
                             headers={"Content-Type": "application/json"}, method="POST")
try:
    with urllib.request.urlopen(req, timeout=30) as r:
        data, status, ctype = r.read(), r.status, r.headers.get("Content-Type", "")
except urllib.error.HTTPError as e:
    data, status = e.read(), e.code
    ctype = e.headers.get("Content-Type", "") if e.headers else ""
except Exception:
    print("000 "); sys.exit(0)
with open(body_path, "wb") as f:
    f.write(data)
print(f"{status} {ctype}")
PY
)"
        HTTP_STATUS="${meta%% *}"
        HTTP_CTYPE="$(echo "${meta#* }" | tr '[:upper:]' '[:lower:]')"
    fi
}

# json_extract <body-file> <python-expr-over-`d`> — print a value parsed from a JSON body.
# Uses python3 for robust JSON (no jq dependency); empty string on any failure.
json_extract() {
    local body_file="$1" expr="$2"
    python3 - "$body_file" "$expr" <<'PY' 2>/dev/null || true
import sys, json
body_path, expr = sys.argv[1], sys.argv[2]
try:
    with open(body_path, "rb") as f:
        d = json.load(f)
    val = eval(expr, {"__builtins__": {}}, {"d": d})
    if val is None:
        print("")
    else:
        print(val)
except Exception:
    print("")
PY
}

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
require_cmd() {  # require_cmd <cmd> <hint>
    command -v "$1" >/dev/null 2>&1 || die_prereq "missing required command '$1' ($2)"
}

# ===========================================================================
# 0. PREREQUISITES + HTTP client
# ===========================================================================
require_cmd docker "the container runtime — install Docker"
require_cmd git    "needed to build the test checkout + bare remote"
pick_http_client   # picks curl or python3, or die_prereq

# python3 is also needed for JSON parsing of the assertions (curl alone can't parse JSON).
command -v python3 >/dev/null 2>&1 || die_prereq "python3 is required to parse JSON assertions"

# The build context + curated export must exist where we expect them.
[ -f "${BUILD_CONTEXT}/Dockerfile" ] || die_prereq "no Dockerfile at the build context ${BUILD_CONTEXT}"
[ -d "${REPO_DB_EXPORT}" ] || die_prereq "no curated export dir at ${REPO_DB_EXPORT}"
for f in "${SEED_FILES[@]}"; do
    [ -f "${REPO_DB_EXPORT}/${f}" ] || die_prereq "curated CSV missing: ${REPO_DB_EXPORT}/${f}"
done

echo "smoke-test: building fixtures + image (this takes a few minutes on first run)..." >&2

# ===========================================================================
# 1. BUILD FIXTURES — a temp git checkout + a throwaway bare remote
# ===========================================================================
TMP_CHECKOUT="$(mktemp -d -t kcdx-smoke-checkout-XXXXXX)"
TMP_BARE_REMOTE="$(mktemp -d -t kcdx-smoke-remote-XXXXXX)"

# 1a. Lay out what the backend reads inside the mounted checkout:
#     <checkout>/data/db-export/<the three CSVs>   (curated; git text)
#     <checkout>/data/reference.sqlite             (the user DB — copied if present)
mkdir -p "${TMP_CHECKOUT}/data/db-export"
for f in "${SEED_FILES[@]}"; do
    cp "${REPO_DB_EXPORT}/${f}" "${TMP_CHECKOUT}/data/db-export/${f}"
done

# 1b. The reference DB. PREFER copying the real one (data/reference.sqlite, gitignored locally
#     — present on a maintainer's checkout). The documented rebuild
#     (import_to_sqlite.py --rebuild) is NOT hermetically runnable here: it needs the bulk
#     corpus under Git LFS (data/db-export-bulk/) which this fixture does not provision. So:
#     copy the real DB if present; ELSE SKIP the DB-read assertion (a missing local DB is an
#     ENVIRONMENT gap, not a test failure — build+serve+commit still run).
if [ -f "${REPO_REFERENCE_DB}" ]; then
    cp "${REPO_REFERENCE_DB}" "${TMP_CHECKOUT}/data/reference.sqlite"
    DB_PROVISIONED=1
else
    DB_PROVISIONED=0
    echo "smoke-test: no local data/reference.sqlite — DB-dependent assertions will SKIP" >&2
fi

# 1c. git init the checkout, configure a LOCAL user, add + commit the fixtures so the
#     container's commit path has a HEAD to build on. -C targets the dir; never cd.
#     The DB is gitignored in production, but in this throwaway
#     fixture we DO commit it (it is part of the fixture state, not the real repo) — except
#     we keep the production posture of committing only the db-export CSVs as the tracked
#     record, mirroring what the backend stages. We git-ignore the .sqlite so the
#     fixture's first commit matches what the backend will later stage (the 3 CSVs).
git -C "${TMP_CHECKOUT}" init -q
git -C "${TMP_CHECKOUT}" config user.name "${MAINTAINER_NAME}"
git -C "${TMP_CHECKOUT}" config user.email "${MAINTAINER_EMAIL}"
git -C "${TMP_CHECKOUT}" config commit.gpgsign false
# Match production: the .sqlite is the local originator, never git-tracked.
printf 'data/reference.sqlite\ndata/reference-dev.sqlite\n' > "${TMP_CHECKOUT}/.gitignore"
git -C "${TMP_CHECKOUT}" add -- .gitignore data/db-export/module_seed.csv \
    data/db-export/address_names_seed.csv data/db-export/address_versions_seed.csv
git -C "${TMP_CHECKOUT}" commit -q -m "smoke-test fixture: curated export checkout"

# 1d. The throwaway bare remote, wired as `private` (the name git_commit pushes to). A
#     no-token run never pushes to it (push skipped), so this only makes the checkout's
#     remote topology faithful to production — it is never exercised by the hermetic run.
git -C "${TMP_BARE_REMOTE}" init --bare -q
git -C "${TMP_CHECKOUT}" remote add private "file://${TMP_BARE_REMOTE}"

# Record the fixture HEAD so save-commits can prove a NEW commit landed beyond it.
FIXTURE_HEAD="$(git -C "${TMP_CHECKOUT}" rev-parse HEAD)"

# ===========================================================================
# 2. BUILD + RUN THE IMAGE
# ===========================================================================
# 2a. Build the single multi-stage image from the build context (the repo root).
docker build -t "${IMAGE_TAG}" "${BUILD_CONTEXT}" >&2 \
    || die_prereq "docker build failed (see the build output above)"

# 2b. Run detached. Mount the temp checkout at /checkout; KCDX_CHECKOUT=/checkout; the
#     maintainer-identity envs; NO KCDX_PUSH_TOKEN (the hermetic dev posture -> push skipped,
#     commit stays local). The container serves 0.0.0.0:8000; publish it to HOST_PORT.
CONTAINER_ID="$(docker run -d --name "${CONTAINER_NAME}" \
    -p "${HOST_PORT}:8000" \
    -v "${TMP_CHECKOUT}:/checkout" \
    -e KCDX_CHECKOUT=/checkout \
    -e KCDX_MAINTAINER_NAME="${MAINTAINER_NAME}" \
    -e KCDX_MAINTAINER_EMAIL="${MAINTAINER_EMAIL}" \
    "${IMAGE_TAG}" 2>/dev/null)" \
    || die_prereq "docker run failed to start the container"

BASE_URL="http://127.0.0.1:${HOST_PORT}"

# 2c. READINESS WAIT — poll /health until the server answers, bounded by HEALTH_MAX_ATTEMPTS.
#     This is a test-harness readiness check (waiting for a freshly-started server to come up):
#     it is bounded (max attempts), fails loudly on timeout, and exists only because the
#     container start is async with no readiness signal a test can subscribe to.
ready=0
for _ in $(seq 1 "${HEALTH_MAX_ATTEMPTS}"); do
    http_get "${BASE_URL}/health"
    if [ "${HTTP_STATUS}" = "200" ]; then
        ready=1
        break
    fi
    sleep "${HEALTH_SLEEP_SECONDS}"
done
if [ "${ready}" -ne 1 ]; then
    # Surface the container's own logs to the operator's stderr to diagnose a boot failure.
    echo "smoke-test: container did not become ready; recent container logs:" >&2
    docker logs --tail 50 "${CONTAINER_ID}" >&2 2>/dev/null || true
    die_prereq "the container /health did not return 200 within ${HEALTH_MAX_ATTEMPTS} attempts (~$((HEALTH_MAX_ATTEMPTS * HEALTH_SLEEP_SECONDS))s)"
fi

# ===========================================================================
# 3. ASSERTIONS — each emits exactly one ACCEPT-RESULT line
# ===========================================================================

# --- api-serves: GET /health returns 200 with a JSON `state` field ---
http_get "${BASE_URL}/health"
HEALTH_STATE="$(json_extract "${HTTP_BODY_FILE}" "d.get('state')")"
if [ "${HTTP_STATUS}" = "200" ] && [ -n "${HEALTH_STATE}" ]; then
    emit_pass api-serves
else
    emit_fail api-serves "expected 200 + JSON state; observed status=${HTTP_STATUS} state='${HEALTH_STATE}'"
fi

# --- frontend-serves: GET / returns 200 + an HTML body (the built SPA index.html) ---
#     The container sets KCDX_STATIC_DIR=/app/static where stage-2 COPYs dist/, so / -> the
#     SPA index.html (text/html). A 503 here means the bundle is missing from the image.
http_get "${BASE_URL}/"
IS_HTML=0
if [ "${HTTP_STATUS}" = "200" ]; then
    case "${HTTP_CTYPE}" in
        *text/html*) IS_HTML=1 ;;
    esac
    # Belt-and-suspenders: also accept if the body looks like HTML (doctype / <html / root div).
    if [ "${IS_HTML}" -eq 0 ] && grep -qiE '<!doctype html|<html|<div id="root"' "${HTTP_BODY_FILE}" 2>/dev/null; then
        IS_HTML=1
    fi
fi
BODY_BYTES="$(wc -c < "${HTTP_BODY_FILE}" 2>/dev/null | tr -d '[:space:]')"
if [ "${HTTP_STATUS}" = "200" ] && [ "${IS_HTML}" -eq 1 ] && [ "${BODY_BYTES:-0}" -gt 0 ]; then
    emit_pass frontend-serves
else
    emit_fail frontend-serves "expected 200 + non-empty text/html SPA; observed status=${HTTP_STATUS} content-type='${HTTP_CTYPE}' bytes=${BODY_BYTES:-0}"
fi

# --- read-returns-curated: GET /entities returns the curated array, OR SKIP (no DB) ---
http_get "${BASE_URL}/entities"
ENTITIES_STATE="$(json_extract "${HTTP_BODY_FILE}" "d.get('state') if isinstance(d, dict) else ''")"
FIRST_ID="$(json_extract "${HTTP_BODY_FILE}" "d[0].get('kcdx_id') if isinstance(d, list) and d else ''")"
ENTITY_COUNT="$(json_extract "${HTTP_BODY_FILE}" "len(d) if isinstance(d, list) else 0")"
if [ "${DB_PROVISIONED}" -ne 1 ]; then
    # No DB was mounted -> the backend returns {state:"empty"}; this is an environment gap.
    emit_skip read-returns-curated "no local data/reference.sqlite to mount (DB-not-provisioned); /entities state='${ENTITIES_STATE:-empty}'"
elif [ "${HTTP_STATUS}" = "200" ] && [ -n "${FIRST_ID}" ] && [ "${ENTITY_COUNT:-0}" -gt 0 ]; then
    emit_pass read-returns-curated
elif [ "${HTTP_STATUS}" = "200" ] && [ "${ENTITIES_STATE}" = "empty" ]; then
    # DB was mounted but the backend resolved no curated rows — a real failure (we provisioned it).
    emit_fail read-returns-curated "DB was mounted but /entities returned the empty state; the mounted reference.sqlite resolved no curated set"
else
    emit_fail read-returns-curated "expected a non-empty JSON array with kcdx_id; observed status=${HTTP_STATUS} count=${ENTITY_COUNT:-0} first_id='${FIRST_ID}' state='${ENTITIES_STATE}'"
fi

# --- save-commits: drive save->confirm (a notes re-edit) -> a NEW local git commit ---
#     A `notes` edit is the simplest hermetic mutation: it needs only the entity to exist and
#     has no pair-integrity rule (routes_save.save_edit_notes / routes_confirm.confirm_edit_notes).
#     We re-write notes to their EXISTING value's tail — a real, validating edit. Requires the DB
#     (an entity to target + a known version tag); SKIP when no DB was provisioned.
if [ "${DB_PROVISIONED}" -ne 1 ]; then
    emit_skip save-commits "no local data/reference.sqlite (DB-not-provisioned) — no entity/version to drive a save->confirm"
else
    # Read the targets from the live API: the first entity id + the newest known version tag.
    http_get "${BASE_URL}/entities"
    TARGET_ID="$(json_extract "${HTTP_BODY_FILE}" "d[0].get('kcdx_id') if isinstance(d, list) and d else ''")"
    http_get "${BASE_URL}/health"
    VERSION_TAG="$(json_extract "${HTTP_BODY_FILE}" "(d.get('known_version_tags') or [''])[0]")"

    if [ -z "${TARGET_ID}" ] || [ -z "${VERSION_TAG}" ]; then
        emit_fail save-commits "could not resolve a target entity id ('${TARGET_ID}') + version tag ('${VERSION_TAG}') from the live API"
    else
        # The entity's current notes (the names row) — append a unique marker so the edit is a
        # real, validating change that GUARANTEES an on-disk CSV delta (so a git commit is
        # produced, not a no_delta DB no-op).
        http_get "${BASE_URL}/entities/${TARGET_ID}"
        EXISTING_NOTES="$(json_extract "${HTTP_BODY_FILE}" "d.get('notes') or ''")"
        NEW_NOTES="${EXISTING_NOTES} [smoke-$$]"

        # Build the JSON body with python3 (safe escaping of arbitrary notes text).
        SAVE_JSON="$(python3 - "$TARGET_ID" "$VERSION_TAG" "$EXISTING_NOTES" "$NEW_NOTES" <<'PY'
import sys, json
kid, tag, old, new = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
print(json.dumps({
    "version_tag": tag,
    "kcdx_id": int(kid),
    "notes": new,
    "saved": {"notes": old},
    "prospective": {"notes": new},
}))
PY
)"
        # Preview (save) — must validate.
        http_post_json "${BASE_URL}/save/edit-notes" "${SAVE_JSON}"
        SAVE_VALID="$(json_extract "${HTTP_BODY_FILE}" "d.get('valid')")"
        if [ "${HTTP_STATUS}" != "200" ] || [ "${SAVE_VALID}" != "True" ]; then
            SAVE_ERR="$(json_extract "${HTTP_BODY_FILE}" "(d.get('errors') or [''])[0]")"
            emit_fail save-commits "save/edit-notes preview did not validate; status=${HTTP_STATUS} valid='${SAVE_VALID}' err='${SAVE_ERR}'"
        else
            # Confirm — the atomic transaction; commits the 3 CSVs locally (push skipped, no token).
            http_post_json "${BASE_URL}/confirm/edit-notes" "${SAVE_JSON}"
            CONFIRM_STATUS="$(json_extract "${HTTP_BODY_FILE}" "d.get('status')")"
            NO_DELTA="$(json_extract "${HTTP_BODY_FILE}" "d.get('no_delta')")"
            NEW_HEAD="$(git -C "${TMP_CHECKOUT}" rev-parse HEAD 2>/dev/null || echo '')"
            if [ "${HTTP_STATUS}" = "200" ] && [ "${CONFIRM_STATUS}" = "saved" ] && \
               [ -n "${NEW_HEAD}" ] && [ "${NEW_HEAD}" != "${FIXTURE_HEAD}" ]; then
                emit_pass save-commits
            elif [ "${CONFIRM_STATUS}" = "saved" ] && [ "${NO_DELTA}" = "True" ]; then
                # The DB committed but produced no on-disk delta -> no git commit by design.
                # Our marker should force a delta; if it didn't, surface it (not a clean PASS).
                emit_fail save-commits "confirm reported no_delta (DB no-op) so no git commit landed; expected a CSV delta from the notes edit"
            else
                CONFIRM_DETAIL="$(json_extract "${HTTP_BODY_FILE}" "d.get('detail') or ''")"
                emit_fail save-commits "confirm did not land a new local commit; status=${HTTP_STATUS} confirm='${CONFIRM_STATUS}' head_before=${FIXTURE_HEAD:0:12} head_after=${NEW_HEAD:0:12} detail='${CONFIRM_DETAIL}'"
            fi
        fi
    fi
fi

# ===========================================================================
# 4. SUITE LINE (last) + exit code
# ===========================================================================
emit_suite

# Exit non-zero if any assertion FAILED, so the user's shell signals denial too (the agent
# reads the ACCEPT-* lines regardless; this is a convenience for the user's shell).
[ "${FAIL_COUNT}" -eq 0 ]
