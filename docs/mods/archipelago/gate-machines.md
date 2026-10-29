# Machine Gating

Every machine is locked behind an unlock item. One mask, `APSave.machine_unlocked_mask`, covers
all three modes - City Trial's field spawns and select grids, the Air Ride character select, and
the Top Ride lobby's Free Star / Steer Star control types - so the AP pool holds one item per
machine, and unlocking it once makes it available everywhere. The characters are machines here
too: King Dedede and Meta Knight unlock with their machines.

## The mask

Bits 0-25 are the vanilla `MachineKind`s (`VCKIND_*`) and bit 26 (`AP_MACHINE_BIT_AP_STAR`) is
the Archipelago Star; `AP_MACHINE_BIT_NUM` is 27. Unlock ID = 830 + bit.
`GateMachines_UnlockMachine` sets the bit and announces "Unlocked Machine: <name>", or "Unlocked
Character: King Dedede / Meta Knight" for `VCKIND_WHEELDEDEDE` (854) and `VCKIND_WINGMETAKNIGHT`
(848). Checklist rewards that grant a machine or a character call it silently.

Machines that never belong to a player have no item: `VCKIND_WHEELVSDEDEDE` (855, King Dedede's
stadium machine) is rejected, and `WINGKIRBY`, `WHEELNORMAL` and `WHEELKIRBY` (847, 849, 850,
transformation forms) are accepted but never shipped.

**Machines from custom_machines.** That mod appends machines past `VCKIND_NUM` in disc order, so
a machine's `MachineKind` depends on the build. The Archipelago Star is bound by name instead:
the ap_star mod reports the kind it registered as, and that kind maps to bit 26. Any other
registered machine has no bit, no item, and is always available. A character's machine is
resolved through `CustomMachines_ResolveKind`, because `CharacterDesc.machine_kind` is relative
to its class (bikes store slot 0-6 against `VCKIND`s 19-25, a registered machine an appended
slot).

**custom_machines is required.** It owns every engine seam the widened kind space breaks - both
select screens' icon packing, the City Trial spawn roll, the legendary assembly cutscene - and
calls back into this mod through filters registered at `OnSaveLoaded` - the first point past
every mod's `OnBoot`, so the first point an import can be trusted. A build without it boots with
machine gating off: the screens offer the engine's roster and the field spawns the vanilla
table.

## City Trial field spawns

custom_machines replaces the spawn roll and asks `GateMachines_SpawnWeight(kind, default)` for
each kind's weight in the progress window being rolled (`default` is the vanilla table's chance,
or a registered machine's descriptor weight):

- **0** for a locked kind, and for every kind that must never appear on the field
  (`CT_SPAWN_EXCLUDED_MASK`): Free and Steer (Top Ride's), the three transformation forms, and
  the King Dedede and Meta Knight machines - the last two would otherwise slip in through the
  fallback below once their characters unlock.
- **The default** for a registered machine (which brings its own weight; 0 keeps it off the
  field) and for any vanilla kind the table already weighs.
- **A fallback** for an unlocked vanilla kind the table gives 0: Compact 5, Flight 2, Hydra and
  Dragoon 1. These are deliberately token weights, well under the table's own, so an owned
  machine can appear without crowding the field; for the legendaries a whole-machine spawn is a
  rare bonus beside the piece assembly.

custom_machines shrinks the spawn history to keep at least one candidate and falls back to the
first permitted kind when the filter zeroes everything.

**Free Run** places one of each machine instead, through `CityMachineSpawn_PickFreeRunKind`
(0x801de41c), which checks only Hydra and Dragoon - against the checklist
(`CityTrial_CheckLegendaryMachineUnlocked`, 0x8000c508). The mod widens the `beq` guarding that
query (0x801de518) so every kind reaches it, and replaces the call (0x801de528) with a mask test.
The city then holds one machine per unlocked kind; with none unlocked the picker returns -1 and
places nothing. Its loop stops at `VCKIND_NUM`, so a registered machine never appears in Free
Run.

## Select screens

Both character select screens are packed by custom_machines, which asks
`GateMachines_FilterSelectCharacter(ckind, default)` per character. The mod ignores the engine's
answer and returns whether the character's machine is unlocked. That is what makes an owned
machine selectable in every mode that offers it - including the Dragoon, Hydra, Flight and
Compact that Air Ride's `AirRide_CheckCharacterAvailable` (0x8002090c) always refuses, and the
specials City Trial's Stadium grid hardcodes out. Air Ride CPUs pick from the same packed list,
so they always ride an unlocked machine.

The title screen's attract demo picks its machines through `TitleScreen_CheckMachineUnlocked`
(0x8000c364), which the mod replaces with the same mask test.

## City Trial start and respawn

`CitySelect_InitPlayerMachines` (0x8002ddd8) commits every slot's starting machine. A hook at
0x8002dea0, where its branches merge, re-picks the machine for each human and CPU slot of a
**Trial** run, which has no machine grid: with **Random Start Machine** on (the default) a random
unlocked machine, otherwise the Compact Star, or a random one while Compact is locked. The pick
excludes King Dedede and Meta Knight - Trial's HUD loader never loads their riders' assets, and
`3DHud_CreateSpeedometerInner` would crash. Stadium and Free Run take the machine from their
grid, which only shows unlocked machines, and are left alone.

On respawn, `Rider_ResetStartingMachine` (0x80195288) hardcodes the Compact Star. A hook at
0x801952c8 replaces its two setters with, in order: the machine custom_machines last mounted the
player on this scene; the machine the player spawned on (recorded at `On3DLoadEnd`); and, if
that is locked, the lowest unlocked kind outside the exclusion mask. The kind is converted back
to its class slot with `CustomMachines_ClassIndexOf`.

## Top Ride lobby

The lobby's middle row picks the control type, Free Star or Steer Star
(`topride_select_ply.panel_machine`). The race lobby and the solo lobby (Free Run, Time Attack)
are separate code with separate copies of everything, so each hook comes in pairs:

| Sites | In | Does |
|---|---|---|
| 0x8002d070, 0x8002d748, 0x8002dc48 | `TopRide_InitSelectData` (0x8002cfd8), `TopRide_RaceInit` (0x8002d0ec), `TopRide_SoloInit` (0x8002d9e8) | After each init's per-slot loop resets `panel_machine` to Free: humans get the first unlocked type, CPUs a random unlocked type and a random unlocked color |
| 0x8002be44, 0x8002cb98 | `TopRide_CSS_PanelThink` (0x8002b8a8), `TopRide_SoloPanelThink` (0x8002ca80) | The L/R cycler, refusing to step onto a locked type |
| 0x8002c52c, 0x8002cc80 | `TopRide_PreGameThink` (0x8002c06c), `TopRide_OnCourseSelect` (0x8002cc30) | Start refused, with the error sound and "Unlock a Top Ride machine to start!", while both types are locked |

Each hook restores the registers its exit path reads: `r3 = 0` for the lobby-flag stores after
`InitSelectData`, `r0 = 0` after `SoloInit`, the slot-scan registers `r4` / `r5` in the race
start gate, and the held-button word in `r3` in the solo one.

The apworld ships the Free and Steer unlocks only with Top Ride in the seed, tagged to Top Ride
locations, and precollects one of them as a starter; a seed without Top Ride gets both bits from
the ungated pre-fill.

## Legendary gives

Give Dragoon and Give Hydra (10, 11) are not gates: they hand a player the assembled machine
through the assembly cutscene, which custom_machines owns (`StartAssembly`). The mod tries each
human in turn and stops at the first one the cutscene accepts; the cutscene refuses outside the
open City Trial map, for a non-Kirby rider, while another run is playing, and a second time in
one scene, so the item retries until a round can take it. Without custom_machines the give is
dropped. It ignores the machine mask.

## Limitations

- Nothing gates the Air Ride start: with no Air Ride machine unlocked the select screen is empty,
  but a race can still start on whatever the empty icon index resolves to.
- A legendary give lands on one human, not every one, because the engine plays one cutscene at a
  time.
