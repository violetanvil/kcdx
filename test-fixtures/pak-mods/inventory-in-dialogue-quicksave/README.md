# Inventory In Dialogue + Quicksave

Two changes in one pak:

- Open your inventory and character menu during dialog (press `I` while
  talking).
- F5 quicksave — save without consuming a Saviour Schnapps.

Install this **instead of** *Inventory In Dialogue* — this is a superset.

## Known limitation

If you change armor during a dialog with a skill check, the displayed
stat number on the response option doesn't refresh until dialog ends.
The check itself uses your current stats — only the visible number is
stale.

## Compatibility

Conflicts with any mod that changes keybinds or input actions
(*Unlimited Saving II*, *DialogOpenInventory*, the standalone
*Inventory In Dialogue* mod, any "remap controls" or "quicksave" mod).
Mods that change items, NPCs, textures, tables, or quests are fine.

## Credits

- Dialog inventory: based on `DialogOpenInventory` by ebrtsan (Nexus #771).
- F5 quicksave: same approach as *Unlimited Saving II* by EddieShoe / Lacyway.

---

## What it does (technical)

The pak adds an `<include actionmap="open_apse_keyboard" />` line to the
`dialog` actionmap of `defaultProfile.xml`, plus an `lw_quicksave`
console action bound to F5 in `keybindSuperactions.xml`. A small Lua
script registers `lw_quicksave` and calls `Game.SaveGameViaResting()` —
the same engine call used by the "sleep in bed" save.

The stale-stat-display limitation exists because the dialog UI is
rendered by compiled C++ that doesn't expose a refresh hook to mods.

## Files in this folder

```
inventory-in-dialogue-quicksave/
├── README.md
├── mod.manifest
└── Data/
    └── inventory_in_dialogue_quicksave.pak
```

Inside the pak:
- `Libs/Config/defaultProfile.xml` — adds the dialog inventory include
  and the `lw_quicksave` console action
- `Libs/Config/keybindSuperactions.xml` — binds `lw_quicksave` to F5
- `Scripts/Startup/sl_saveload.lua` — registers the `lw_quicksave`
  console command that calls `Game.SaveGameViaResting()`

## Install (manual)

Copy the entire folder into your KCD2 `Mods/` folder:

```
<KCD2 install>/Mods/inventory-in-dialogue-quicksave/mod.manifest
<KCD2 install>/Mods/inventory-in-dialogue-quicksave/Data/inventory_in_dialogue_quicksave.pak
```

## Uninstall

Delete the `Mods/inventory-in-dialogue-quicksave/` folder. Your save
files are unaffected — F5 quicksaves are stored in the normal save slot.

## Game update breakage

This pak embeds vanilla XML from build `release_1_5_1164953_841`. If
Warhorse changes either file in a patch, the mod will silently revert
their changes. To rebuild against newer vanilla:

1. Extract `Libs/Config/defaultProfile.xml` and `keybindSuperactions.xml`
   from the current `Data/IPL_GameData.pak`
2. In `defaultProfile.xml`:
   - Inside `<actionmap name="dialog">`, add
     `<include actionmap="open_apse_keyboard" />` after the existing
     `<include actionmap="open_pause_menu" />`
   - Inside `<actionmap name="open_menu">`, add
     `<action consoleCmd="1" name="lw_quicksave" onRelease="1" keyboard="_keybinds_ref_" />`
3. In `keybindSuperactions.xml`, after the `cancel` superaction's
   `</superaction>` (just before `<!-- MOVEMENT -->`), add:
   ```xml
   <superaction name="lw_quicksave" ui_group="general" ui_name="ui_keybind_quicksave" ui_tooltip="ui_keybind_quicksave_desc" keyboard="writeable">
       <action name="lw_quicksave" map="open_menu" />
       <control input="f5" controller="keyboard" />
   </superaction>
   ```
4. Repackage as a `.pak` (zip, store-only / no compression).

The Lua file does not need to change between game versions.

