"""Builds the combined-mod keybindSuperactions.xml from vanilla.

Single edit: insert one new <superaction> for F5 quicksave, right after the
existing 'cancel' superaction's closing </superaction>. No quickload entry.
"""
import sys
from pathlib import Path

vanilla = Path("vanilla/IPL_extracted/Libs/Config/keybindSuperactions.xml")
out_path = Path(sys.argv[1])

raw = vanilla.read_bytes()
eol = b"\r\n" if b"\r\n" in raw else b"\n"
lines = raw.split(eol)

# Find the 'cancel' superaction's </superaction> close: it is the
# </superaction> line that immediately precedes the '<!-- MOVEMENT -->' comment.
insert_after = None
for i, line in enumerate(lines):
    if b"<!-- MOVEMENT -->" in line:
        # walk back to find the </superaction> just above
        j = i - 1
        while j >= 0 and lines[j].strip() == b"":
            j -= 1
        if b"</superaction>" not in lines[j]:
            raise SystemExit(f"Expected </superaction> above MOVEMENT comment, found: {lines[j]!r}")
        insert_after = j
        break

if insert_after is None:
    raise SystemExit("Failed to find insertion anchor")

addition = [
    b"",
    b'    <superaction name="lw_quicksave" ui_group="general" ui_name="ui_keybind_quicksave" ui_tooltip="ui_keybind_quicksave_desc" keyboard="writeable">',
    b'\t\t<action name="lw_quicksave" map="open_menu" />',
    b'\t\t<control input="f5" controller="keyboard" />',
    b"\t</superaction>",
]

result = lines[: insert_after + 1] + addition + lines[insert_after + 1 :]
out_path.write_bytes(eol.join(result))
print(f"Wrote {out_path}")
