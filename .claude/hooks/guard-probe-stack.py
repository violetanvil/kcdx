#!/usr/bin/env python3
# PreToolUse(Write|Edit) — stacked-LIVE-diagnostic-probe guard
# (.claude/rules/results-driven.md §"Probe-archive hygiene").
# WARN-ONLY: never blocks. Fires when an edit ADDS a `// === DIAGNOSTIC (PROBE ...)`
# LIVE marker while another un-archived LIVE probe marker already exists
# uncommitted in the working tree (git diff HEAD adds). That is the "stack
# PROBE B.2 on un-archived PROBE B" shape /debug §2f forbids. Disable + archive
# the prior probe (#if 0 with the four-line archive header per debug/SKILL.md
# §3d) before adding the next. The DIAGNOSTIC regex naturally excludes
# `// === ARCHIVED PROBE` headers, so archived sites do not trip this guard
# even when many sit on disk simultaneously.
import sys, os, re, json, subprocess


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    path = ti.get("file_path")
    if not path:
        sys.exit(0)
    if not re.search(r'\.(cpp|h|hpp|cc|cxx|inl)$', path):
        sys.exit(0)

    marker = re.compile(r'//\s*===\s*DIAGNOSTIC\s*\(PROBE')

    # Post-operation content (Write supplies it; Edit applies the replacement).
    new_content = None
    old_content = ''
    if ti.get("content"):
        new_content = ti.get("content")
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as fh:
                old_content = fh.read()
    elif ti.get("old_string"):
        if not os.path.isfile(path):
            sys.exit(0)
        with open(path, "r", encoding="utf-8") as fh:
            old_content = fh.read()
        old = ti.get("old_string")
        new = ti.get("new_string") or ""
        if ti.get("replace_all"):
            new_content = old_content.replace(old, new)
        else:
            idx = old_content.find(old)
            if idx < 0:
                sys.exit(0)
            new_content = old_content[:idx] + new + old_content[idx + len(old):]
    if not new_content:
        sys.exit(0)

    # Does THIS edit add a probe marker? (more markers after than before)
    new_n = len(marker.findall(new_content))
    old_n = len(marker.findall(old_content))
    if new_n <= old_n:
        sys.exit(0)

    # Is there an un-archived LIVE probe marker ALREADY in the working tree, in some
    # OTHER file? git diff HEAD added-lines carrying the marker, excluding $path.
    # No git / not a repo -> stay silent (warn-only, never error). Resolve the
    # repo root first so the `src` pathspec is anchored correctly.
    file_dir = os.path.dirname(path) or "."
    try:
        r = subprocess.run(["git", "-C", file_dir, "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(0)
        root = r.stdout.strip()
    except Exception:
        sys.exit(0)
    if not root:
        sys.exit(0)
    try:
        r = subprocess.run(["git", "-C", root, "diff", "HEAD", "--", "src"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(0)
        diff = r.stdout
    except Exception:
        sys.exit(0)
    if not diff:
        sys.exit(0)

    edited_leaf = os.path.basename(path)
    cur_file = None
    has_other_probe = False
    for line in diff.split("\n"):
        m = re.match(r'^\+\+\+ b/(.+)$', line)
        if m:
            cur_file = m.group(1)
            continue
        if line.startswith('+') and not line.startswith('+++') and marker.search(line):
            if cur_file and os.path.basename(cur_file) != edited_leaf:
                has_other_probe = True
                break

    if has_other_probe:
        sys.stderr.write("Probe-stack WARN: " + edited_leaf + " adds a // === DIAGNOSTIC (PROBE ...) LIVE marker while another un-archived LIVE probe site already exists uncommitted in src/. Stacking two live probes (e.g. PROBE B.2 on un-archived PROBE B) confounds the next launch and violates one-variable-per-probe. Disable + archive the prior probe (#if 0 with the four-line archive header per .claude/skills/debug/SKILL.md §3d) before adding this one, unless you are explicitly building on it (.claude/rules/results-driven.md §Probe-archive-hygiene; debug/SKILL.md §2f). NEVER revert / delete a probe (CLAUDE.md hard rule). >2 LIVE probes or a hard bug -> switch to /debug for active-instrumentation tracking.")
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
