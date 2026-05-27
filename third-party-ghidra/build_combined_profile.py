"""Builds the combined-mod defaultProfile.xml from vanilla.

Two edits:
  1. Inside <actionmap name="dialog">, add <include actionmap="open_apse_keyboard" />
     after the existing <include actionmap="open_pause_menu" />.
  2. Inside <actionmap name="open_menu">, add a single console-action for F5
     quicksave: <action consoleCmd="1" name="lw_quicksave" onRelease="1"
     keyboard="_keybinds_ref_" />

No combat-actionmap edits. No controller (xboxpad) save action. No quickload.
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
in_open_menu = False
dialog_done = False
open_menu_done = False

for line in lines:
    result.append(line)
    text = line.decode("utf-8", errors="replace")

    if '<actionmap name="dialog"' in text:
        in_dialog = True
    elif in_dialog and "</actionmap>" in text:
        in_dialog = False
    elif in_dialog and not dialog_done and '<include actionmap="open_pause_menu" />' in text:
        indent = text[: len(text) - len(text.lstrip())]
        result.append(f'{indent}<include actionmap="open_apse_keyboard" />'.encode("utf-8"))
        dialog_done = True

    if '<actionmap name="open_menu"' in text:
        in_open_menu = True
    elif in_open_menu and "</actionmap>" in text and not open_menu_done:
        # insert the lw_quicksave action right before the closing tag
        indent = text[: len(text) - len(text.lstrip())] + "\t"
        result.insert(
            -1,
            f'{indent}<action consoleCmd="1" name="lw_quicksave" onRelease="1" keyboard="_keybinds_ref_" />'.encode("utf-8"),
        )
        open_menu_done = True
        in_open_menu = False

if not dialog_done:
    raise SystemExit("Failed to insert dialog actionmap include")
if not open_menu_done:
    raise SystemExit("Failed to insert lw_quicksave action")

out_path.write_bytes(eol.join(result))
print(f"Wrote {out_path}")
