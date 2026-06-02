#!/usr/bin/env python3
# PreToolUse(Write|Edit) — C++ comment-density guard.
# WARN-ONLY: never blocks. Flags files that are mostly comments (history,
# restated purpose, quoted MSDN/wiki paragraphs) over actual code.
import sys, os, re, json


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    path = ti.get("file_path")
    if not path:
        sys.exit(0)
    if not re.search(r'\.(cpp|h|hpp|cc|cxx|inl)$', path):
        sys.exit(0)

    # Post-operation content (Write supplies it; Edit applies the replacement).
    content = None
    if ti.get("content"):
        content = ti.get("content")
    elif ti.get("old_string"):
        if not os.path.isfile(path):
            sys.exit(0)
        with open(path, "r", encoding="utf-8") as fh:
            current = fh.read()
        old = ti.get("old_string")
        new = ti.get("new_string") or ""
        if ti.get("replace_all"):
            content = current.replace(old, new)
        else:
            idx = current.find(old)
            if idx < 0:
                sys.exit(0)
            content = current[:idx] + new + current[idx + len(old):]
    if not content:
        sys.exit(0)

    lines = content.split("\n")
    total_nonblank = 0
    comment_lines = 0
    in_block = False

    for line in lines:
        trimmed = line.strip()
        if trimmed == "":
            continue
        total_nonblank += 1

        if in_block:
            comment_lines += 1
            if re.search(r'\*/', trimmed):
                in_block = False
            continue
        if re.match(r'^/\*', trimmed):
            comment_lines += 1
            if not re.search(r'\*/', trimmed):
                in_block = True
        elif re.match(r'^//', trimmed):
            comment_lines += 1

    # Ratio only meaningful on files with enough lines — small stubs run high.
    if total_nonblank >= 30:
        # PowerShell [math]::Round uses banker's rounding; Python round() matches.
        ratio = int(round((comment_lines / total_nonblank) * 100))
        if ratio > 60:
            sys.stderr.write("Comment-density WARN: {0} is {1}% comments by line ({2}/{3}). Target <60%. Drop history, restated purpose, and quoted MSDN/wiki paragraphs — keep intent + WHY + // SOURCE: only.".format(path, ratio, comment_lines, total_nonblank))
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
