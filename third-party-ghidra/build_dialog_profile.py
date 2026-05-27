"""Builds a dialog-only defaultProfile.xml from vanilla.

Inserts <include actionmap="open_apse_keyboard" /> inside the <actionmap name="dialog">
block (right after its <include actionmap="open_pause_menu" /> line).
Does NOT modify the combat actionmap (combat already allows inventory in vanilla
via the player actionmap include chain).
"""
import sys
from pathlib import Path

vanilla = Path("vanilla/IPL_extracted/Libs/Config/defaultProfile.xml")
out_path = Path(sys.argv[1])

raw = vanilla.read_bytes()
eol = b"\r\n" if b"\r\n" in raw else b"\n"
lines = raw.split(eol)

result = []
in_dialog = False
inserted = False
for line in lines:
    result.append(line)
    text = line.decode("utf-8", errors="replace")
    if '<actionmap name="dialog"' in text:
        in_dialog = True
    elif in_dialog and "</actionmap>" in text:
        in_dialog = False
    elif in_dialog and '<include actionmap="open_pause_menu" />' in text and not inserted:
        indent = text[: len(text) - len(text.lstrip())]
        result.append(f'{indent}<include actionmap="open_apse_keyboard" />'.encode("utf-8"))
        inserted = True

if not inserted:
    raise SystemExit("Failed to locate dialog actionmap insertion point")

out_path.write_bytes(eol.join(result))
print(f"Wrote {out_path}")
