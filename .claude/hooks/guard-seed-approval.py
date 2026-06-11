#!/usr/bin/env python3
# PreToolUse(Write|Edit) — Address Library seed-addition approval reminder.
# WARN-ONLY: never blocks. Flags a Write/Edit that ADDS a row to either curated
# seed CSV (address_names_seed.csv / address_versions_seed.csv). A new seed row
# is a new Address Library DB entity/version, which requires EXPLICIT user
# approval before it lands (data/maintainer-tool/policy.md). The warn is a standing
# reminder; the agent confirms approval in-conversation and the review gates
# carry the hard check.
import sys, os, re, json


def count_data_rows(text):
    # A data row is a non-empty line that is not a comment (leading '#') and not
    # the header (the importer reads by DictReader; the header line starts with
    # 'id,' or 'kcdx_id,'). Count data rows; warn only on a net gain.
    if text is None:
        return 0
    n = 0
    for line in text.split("\n"):
        t = line.strip()
        if t == '':
            continue
        if t.startswith('#'):
            continue
        if re.match(r'^(id|kcdx_id),', t):
            continue
        n += 1
    return n


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    path = ti.get("file_path")
    if not path:
        sys.exit(0)

    # Only the two curated seed CSVs are gated. module_seed.csv (module registry)
    # is not an address entity — excluded. Match the leaf regardless of path shape
    # (the curated CSVs are the maintainer-tool's git-tracked export at
    # data/db-export/ since D38 retired data/seeds/; matching by leaf keeps the
    # guard firing wherever the export lands).
    # PowerShell -notmatch is case-insensitive -> re.I.
    norm = path.replace("\\", "/")
    if not re.search(r'(^|/)(address_names_seed|address_versions_seed)\.csv$', norm, re.I):
        sys.exit(0)
    leaf = os.path.basename(path)

    # Reconstruct the post-operation content. Write supplies it whole; Edit
    # applies the replacement against the current file. We then count non-comment,
    # non-header data rows added — a pure edit to an existing row (no net new row)
    # does not warn; only a row ADDITION does.
    old_text = None
    new_text = None
    if ti.get("content") is not None:
        # Write: whole-file replace. Old = current on disk (empty if new file).
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as fh:
                old_text = fh.read()
        else:
            old_text = ''
        new_text = ti.get("content")
    elif ti.get("old_string") is not None:
        if not os.path.isfile(path):
            sys.exit(0)
        with open(path, "r", encoding="utf-8") as fh:
            old_text = fh.read()
        old = ti.get("old_string")
        new = ti.get("new_string") or ""
        if ti.get("replace_all"):
            new_text = old_text.replace(old, new)
        else:
            idx = old_text.find(old)
            if idx < 0:
                sys.exit(0)
            new_text = old_text[:idx] + new + old_text[idx + len(old):]
    else:
        sys.exit(0)

    before = count_data_rows(old_text)
    after = count_data_rows(new_text)
    added = after - before
    if added <= 0:
        sys.exit(0)   # no net new row — a pure in-place edit.

    msg = ("Address Library seed WARN: this edit adds {0} row(s) to {1}. ".format(added, leaf) +
           "A new seed row is a new Address Library DB entity/version and requires EXPLICIT user approval before it lands (data/maintainer-tool/policy.md - 'DB additions require explicit user approval'). " +
           "Confirm the user explicitly approved adding this entity to the DB. An un-approved seed addition is a review-gate finding (AP18).")
    sys.stderr.write(msg)
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
