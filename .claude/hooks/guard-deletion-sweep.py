#!/usr/bin/env python3
# PreToolUse(Write|Edit) — deletion-hygiene sweep guard (.claude/rules/deletion-hygiene.md).
# WARN-ONLY: never blocks. A per-write hook cannot grep the doc tree; it does what
# it can — fire a sweep reminder when an edit REMOVES a high-signal public-surface
# shape (count drops new-vs-old), so the survivor grep happens before the deletion
# lands. The review skills (step-review/code-review §2) carry the actual check.
# A line carrying `// approved: <reason>` (after explicit user sign-off) is exempt.
import sys, os, re, json


def main():
    data = json.load(sys.stdin)
    ti = data.get("tool_input") or {}
    path = ti.get("file_path")
    if not path:
        sys.exit(0)
    # Runs on .cpp/.h/.toml/.md — where public-surface / TOML-table deletions land.
    if not re.search(r'\.(cpp|h|hpp|cc|cxx|inl|toml|md)$', path):
        sys.exit(0)

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

    # Drop `// approved: <reason>` lines from both sides before counting.
    approved_rx = re.compile(r'//\s*approved:')

    def filter_approved(text):
        return "\n".join(ln for ln in text.split("\n") if not approved_rx.search(ln))

    new_filtered = filter_approved(new_content)
    old_filtered = filter_approved(old_content)

    # ── Deletion hygiene — public surface removed, sweep for stale docs ────────
    # High-signal surface shapes: a TOML table, an exported entry point, a parser,
    # a kcdx.* registration. A drop in count means a surface was REMOVED.
    surface_re = re.compile(r'(?m)^\s*(\[\[[a-z_]+\]\]|extern\s+"C"|.*\bkcdxPlugin_Load\b|.*\bParseOne[A-Z]\w*|.*\bkcdx\.[a-z]\w*\s*=)')
    new_surfaces = len(surface_re.findall(new_filtered))
    old_surfaces = len(surface_re.findall(old_filtered))
    if new_surfaces < old_surfaces:
        sys.stderr.write("Deletion-hygiene WARN: {0} removes a public surface (TOML table / exported entry point / parser / kcdx.* registration). Before this lands, grep docs/ + .claude/rules/ + CLAUDE.md for surviving PRESCRIPTIVE references to the removed token and fix them in the SAME commit (historical/comparative/superseded mentions are exempt). Per .claude/rules/deletion-hygiene.md.".format(path))

    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
