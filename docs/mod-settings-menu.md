# Mod Settings Menu

hoshi's settings menu: the tree of options every mod exposes through `ModDesc.option_desc`, how
the values persist on the memory card, and when they can change. Implementation:
`externals/hoshi/src/settings.c`, types in `externals/hoshi/include/hoshi/settings.h`. Each
mod's own tree is documented with that mod.

## Where it lives

The menu is a submenu of the main menu's Options panel. `OptionsMenu_OnPressA` (hook at
0x80016818) opens it with `Settings_Create`, and `OptionsMenu_PerFrame` (hook at 0x800182c0)
drives it through `Settings_Think` while it is open. There is no in-round entry point: no
pause-menu page, no hotkey. A value therefore changes only while the player is in the main menu,
and every option holds one value from a round's load to its end.

`Settings_Init` builds the root at boot from every mod whose `option_desc` is set and sorts the
entries by `pri` (`MenuPriority`) ascending, then `OptionKind` descending, then name.

## Option kinds

`OptionDesc` (`settings.h`) carries a name, a description shown at the bottom of the screen, a
kind, and a kind-specific payload:

| Kind | Payload | Behavior |
|---|---|---|
| `OPTKIND_VALUE` | `val`, `value_num`, `value_names`, `on_change` | Left/right steps `*val` through `0..value_num-1`, wrapping, and calls `on_change(*val)` after each step |
| `OPTKIND_MENU` | `menu_ptr` | Opens a nested `MenuDesc` |
| `OPTKIND_SCENE` | `major_idx` | Writes the save and leaves for that major scene |
| `OPTKIND_ACTION` | `on_action`, `user_data` | Calls `on_action(self)`; a nonzero return plays the confirm sound and redraws the menu |

`no_save` keeps a value option out of the memory card.

## When `on_change` runs

- Each time the player steps the value in the menu.
- Once at boot for every value option in every mod: `Mods_OnLoadSaveData` calls
  `Menu_ExecAllOptionChange` after every mod's save data, and so every mod's `OnSaveLoaded`, is
  in place. A callback therefore sees the restored value at boot without the mod replaying it.

Nothing else calls it. A mod that writes an option variable itself - seeding a value from the
AP slot options, for instance - does not trigger the callback and has to apply the change
directly.

## Persistence

Each saved value is a `MenuSave` row in the mod's memory-card block: a 16-bit hash and a `u8`
value. `Option_Hash` hashes the name of the option's immediate parent menu concatenated with the
option's own name (`hashstr_16`), so two options with the same name under different submenus
save separately.

- **Load.** `Mod_CopyFromSave` walks the tree when the card's block is read and
  `Option_CopyFromSave` writes each matching row over the default. A saved value at or past the
  option's `value_num` keeps the default, so a mod never sees an out-of-range index from the
  card. An option whose hash has no row keeps its default too - which is what renaming the
  option, or its parent menu, does to its saved value.
- **Store.** `KARPlusSave_Write` calls `Mod_CopyAllToSave` before every card write, so current
  values go out with any save write, not only the menu's own write on exit.

Because the only writers of an option index are the menu, which wraps within range, and the
load, which rejects anything out of range, code reading an option never needs to clamp it.
