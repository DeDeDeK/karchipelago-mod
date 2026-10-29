# Settings Menu

hoshi's settings menu is the tree of options every mod exposes through `ModDesc.option_desc`, drawn
inside the main menu's Options panel and saved in each mod's block of the hoshi save file. This
covers where the menu runs, the option kinds, when a mod hears about a change, and how values
persist - the rules that decide what a mod can rely on when it reads an option. Each mod's own
options are documented with that mod.

## Where It Lives

hoshi adds a fifth entry to the main menu's Options panel. Pressing A on it (`OptionsMenu_OnPressA`,
hook at 0x80016818) sets the main menu's `is_in_submenu` to 6 and builds the menu with
`Settings_Create`; while it is open, `OptionsMenu_PerFrame` (hook at 0x800182c0) runs
`Settings_Think` in place of the panel's own think. The models come from `MnSettings.dat`, loaded
on every main menu load, and the rows render through hoshi's own camera on GX link 4.

There is no other entry point: no pause-menu page and no hotkey. A value changes through the menu
only while the player is in the main menu, so every option holds one value from a round's load to
its end unless a mod writes the variable itself.

At boot `Settings_Init` builds the root from every installed mod whose `option_desc` is set and
sorts it by `pri` (`MenuPriority`) ascending, then `OptionKind` ascending, then name. The root
therefore changes shape with the set of installed mods.

## Option Kinds

An `OptionDesc` carries a name, a description shown at the bottom of the screen, a kind, a
`pri`, a `no_save` flag, and a kind-specific payload:

| Kind | Payload | Behavior |
|---|---|---|
| `OPTKIND_VALUE` | `val`, `value_num`, `value_names`, `on_change` | Left/right steps `*val` through `0..value_num-1`, wrapping, and calls `on_change(*val)` after each step |
| `OPTKIND_NUM` | `val`, `min`, `max`, `on_change` | Left/right steps `*val` through `min..max` inclusive, wrapping, shows it as a decimal number, and calls `on_change(*val)` after each step |
| `OPTKIND_MENU` | `menu_ptr` | A or Start opens a nested `MenuDesc` |
| `OPTKIND_SCENE` | `major_idx` | A or Start writes the save and leaves for that major scene |
| `OPTKIND_ACTION` | `on_action`, `user_data` | A or Start calls `on_action(self)`; a nonzero return plays the confirm sound and redraws the menu, zero plays the cancel sound |

`VALUE` and `NUM` are the value kinds: they draw on the value frame, a name plus a value box the
cursor sits on, and they are the only kinds that persist. The others draw on the button frame.

A menu shows five rows and scrolls; up and down wrap from the last row to the first. B returns to
the parent menu, and on the root closes the menu and writes the save.

## When `on_change` Runs

- Each time the player steps the value in the menu.
- Once at boot for every value option of every mod: after every mod's save block is in place and
  every `OnSaveLoaded` has run, `Menu_ExecAllOptionChange` walks the whole tree. A callback
  therefore sees the restored value at boot without the mod replaying it.

Nothing else calls it. A mod that writes an option variable itself - seeding a value from somewhere
else, for instance - does not trigger the callback and has to apply the change directly.

## Persistence

Each saved value is a `MenuSave` row in the mod's block of the save file: a 16-bit hash and a `u8`
index - the value itself for `VALUE`, its offset from `min` for `NUM`, so a saved `NUM` spans at
most 256 values. `Option_Hash` hashes the name of the option's immediate parent menu concatenated
with the option's own name, so options are matched by name, not position:

- **Names are the key.** Reordering a menu is free. Renaming an option, or moving it under a
  differently named parent, orphans its saved value and the option falls back to its initializer.
  Two options whose parent-plus-name strings collide share one row.
- **Changing the saved count resets the block.** When the number of saved options changes, the
  mod's block changes size, and hoshi answers a resized block by writing the options' current
  values - their initializers, at that point of boot - over every row. Adding or removing one
  option therefore resets every other option of the same mod.
- **`no_save`** keeps a value option out of the block entirely: it costs no row and is skipped in
  both copy directions. It suits options whose value is re-derived at load from another source.

Values move in two directions:

- **Load.** `Option_CopyFromSave` writes each matching row over the option's default. A saved
  index at or past the option's value count (`value_num`, or `max - min + 1`) keeps the default,
  so a mod never sees an out-of-range value from the card.
- **Store.** Every save write first copies each option's live value into its row
  (`Mod_CopyAllToSave`), so a value a mod changes in code persists with the next write, not only
  values changed through the menu. `Option_CopyToSave` skips a `NUM` whose offset does not fit a
  `u8`, leaving its row as it was.

Because the only writers of an option index are the menu, which wraps within range, and the load,
which rejects anything out of range, code reading an option the mod does not write itself never
needs to clamp it.
