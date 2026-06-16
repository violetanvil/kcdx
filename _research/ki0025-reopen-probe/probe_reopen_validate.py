"""PROBE H (KI-0025): does the kcdx_id=12 interval-reopen edit pass the data-core's
pre-write whole-state validation?

Isolates ONE variable: the validate_only path of update_version_row for the reopen
edit (clear valid_through_version on kcdx_id=12's baseline-tag row). validate_only=True
opens NO DB (apply_direct_edit spine step 1 validates the prospective seed and STOPS
before any DB open), so this is read-only against the live curated DB -- zero write risk.

Outcome -> meaning map (pre-committed, theory-independent):
  - returns {"tag","ordinal"}  -> the reopen edit VALIDATES clean. The recorded PROBE G
        ("baseline-tag reopen produces no action / is dropped") is then the wrong
        diagnosis; the reopen path is present and the earlier /confirm failure was a
        DIFFERENT cause (a confound -- e.g. the uncommitted validator firing on the
        DEV state, or the stale-backend confound PROBE F already half-saw).
  - raises RuntimeError(validator ...) -> the validation genuinely rejects the reopen.
        The printed error names WHICH check fails -> that is the real gap to fix.
  - raises DbEditError(... no row ...) -> the identity key (kcdx_id=12, valid_from tag)
        does not match -> the edit is keyed wrong (read the seed's valid_from cell).

Run headless: python _research/ki0025-reopen-probe/probe_reopen_validate.py
"""
import os
import sys
import traceback

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PYDIR = os.path.join(REPO, "data", "refdata-extractor", "python")
sys.path.insert(0, PYDIR)

import seeds_shared as ss  # noqa: E402
from seeds_shared import db_editor  # noqa: E402

OUT_DIR = os.path.join(REPO, "data")            # holds reference.sqlite + reference-dev.sqlite
GAME_TAG = "1.5.1164953"                          # the baseline tag == kcdx_id=12's valid_from


def _log(msg):
    print(f"  [probe] {msg}")


def main():
    print("PROBE H: validate the kcdx_id=12 interval-reopen edit (validate_only -- no DB write)\n")
    # The version param the backend pre-resolves client-side (D15): the baseline (tag, ordinal).
    # ordinal is the game_versions.id for the baseline tag; the validate path needs only the tag
    # for the prospective-seed export, but update_version_row requires a resolved version. The
    # baseline ordinal is GAME_VERSION_ID = 1 (import_to_sqlite L137).
    version = (GAME_TAG, 1)
    try:
        result = db_editor.update_version_row(
            OUT_DIR,
            dll_path=None,
            kcdx_id=12,
            valid_from_version=GAME_TAG,
            edits={"valid_through_version": ""},   # the REOPEN: clear the close
            version=version,
            log=_log,
            validate_only=True,
        )
        print(f"\nRESULT: VALIDATES CLEAN -> {result!r}")
        print("=> the reopen edit passes pre-write validation; PROBE G's 'dropped' "
              "diagnosis is refuted. The /confirm failure was a different cause.")
    except Exception as e:
        print(f"\nRESULT: REJECTED -> {type(e).__name__}: {e}")
        print("---full traceback---")
        traceback.print_exc()


if __name__ == "__main__":
    main()
