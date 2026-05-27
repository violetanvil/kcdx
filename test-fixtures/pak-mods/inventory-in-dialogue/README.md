# Inventory In Dialogue

Open your inventory and character menu during dialog. Press your
inventory key (default `I`) while talking to anyone — swap clothes,
drink potions, check your stats without ending the conversation first.

Pure XML mod, single pak, no scripts.

## Known limitation

If you change armor during a dialog with a skill check, the displayed
stat number on the response option doesn't refresh until dialog ends.
The check itself uses your current stats — only the visible number is
stale.

## Compatibility

Conflicts with any mod that changes keybinds or input actions
(*Unlimited Saving II*, *DialogOpenInventory*, any "remap controls" or
"quicksave" mod). Mods that change items, NPCs, textures, tables, or
quests are fine.

## Credits

Based on the original `DialogOpenInventory` by ebrtsan (Nexus #771),
rebased on current vanilla.

---

## What it does (technical)

KCD2's input system normally blocks the inventory key in the `dialog`
action map. This mod adds one line —
`<include actionmap="open_apse_keyboard" />` — inside the `dialog`
actionmap of `Libs/Config/defaultProfile.xml`, so the inventory hotkey
fires during dialog the same way it does in normal play.

The stale-stat-display limitation exists because the dialog UI is
rendered by compiled C++ that doesn't expose a refresh hook to mods.

## Files in this folder

```
inventory-in-dialogue/
├── README.md
├── mod.manifest
└── Data/
    └── inventory_in_dialogue.pak
```

The pak contains exactly one file:
`Libs/Config/defaultProfile.xml` — a copy of the current vanilla file with
the one extra `<include>` line.

## Install (manual)

Copy the entire `inventory-in-dialogue` folder into your KCD2 `Mods/` folder:

```
<KCD2 install>/Mods/inventory-in-dialogue/mod.manifest
<KCD2 install>/Mods/inventory-in-dialogue/Data/inventory_in_dialogue.pak
```

## Uninstall

Delete the `Mods/inventory-in-dialogue/` folder.

## Game update breakage

This pak embeds vanilla `defaultProfile.xml` as of build
`release_1_5_1164953_841`. If Warhorse changes that file in a patch, this
mod will silently revert their changes. To rebuild: extract the new
vanilla `defaultProfile.xml`, find `<actionmap name="dialog"`, and add
`<include actionmap="open_apse_keyboard" />` right after the existing
`<include actionmap="open_pause_menu" />` line.

