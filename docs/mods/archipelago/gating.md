# Gating

Gating is how the archipelago mod turns game content into multiworld items. Each category of
content - machines, copy abilities, City Trial events, patch types, City Trial items, box
colors, courses, Top Ride items, Kirby colors, stadiums, Kirby's base moves, the Archipelago
Star's spheres - has an unlock mask in the save, and a piece of content is unavailable until its
bit is set by an unlock item. This doc covers the model every category shares, and the
categories small enough to describe in a section each.

## The model

Every category is one `APUnlockCategory`, one mask in `APSave`, one range of unlock item IDs and
one gating slot option:

| Category | Mask (`APSave`) | Width | Bit | Unlock IDs | Gating option |
|---|---|---|---|---|---|
| `AP_UNLOCK_MACHINE` | `machine_unlocked_mask` | u32 | `MachineKind`, then the Archipelago Star | 830-856 | `machine_gating_enabled` |
| `AP_UNLOCK_ABILITY` | `ability_unlocked_mask` | u16 | `CopyKind` | 760-770 | `ability_gating_enabled` |
| `AP_UNLOCK_EVENT` | `event_unlocked_mask` | u32 | `EventKind` | 700-715 | `event_gating_enabled` |
| `AP_UNLOCK_PATCH` | `patch_unlocked_mask` | u16 | `PatchKind` | 780-788 | `patch_gating_enabled` |
| `AP_UNLOCK_ITEM` | `item_unlocked_mask` | u32 | `ItemUnlockKind` | 790-819 | `item_gating_enabled` |
| `AP_UNLOCK_BOX` | `box_unlocked_mask` | u8 | `BoxKind` | 860-862 | `box_gating_enabled` |
| `AP_UNLOCK_AIRRIDE_STAGE` | `airride_stage_unlocked_mask` | u16 | `AirRideCourse` | 870-878 | `airride_stage_gating_enabled` |
| `AP_UNLOCK_TOPRIDE_STAGE` | `topride_stage_unlocked_mask` | u16 | `TopRideCourse` | 890-896 | `topride_stage_gating_enabled` |
| `AP_UNLOCK_TOPRIDE_ITEM` | `topride_item_unlocked_mask` | u32 | `TopRideItemKind` | 900-921 | `topride_item_gating_enabled` |
| `AP_UNLOCK_COLOR` | `color_unlocked_mask` | u8 | `KirbyColor` | 880-887 | `color_gating_enabled` |
| `AP_UNLOCK_STADIUM` | `stadium_unlocked_mask` | u32 | `StadiumKind` | 400-423 | `stadium_gating_enabled` |
| `AP_UNLOCK_BASE_ABILITY` | `base_ability_unlocked_mask` | u8 | `BaseAbilityKind` | 771-773 | `base_ability_gating_enabled` |
| `AP_UNLOCK_AP_STAR_PIECE` | `ap_star_piece_unlocked_mask` | u8 | `APStarPiece` | 820-825 | follows `item_gating_enabled` |

`ItemUnlockKind`, `BaseAbilityKind` and `APStarPiece` are the mod's own enums, in its public
header; the rest are the game's.

**Unlocking.** An unlock item sets its bit through the category's `*_Unlock*` function, which
logs and announces "Unlocked <Category>: <name>" (shown only with Settings -> Messages -> Local
-> Items on). Checklist rewards that grant the same content - a machine, a color, a stadium -
call the same functions silently, so a reward and an unlock item share one mask. Bits are never
cleared except by the debug mod.

**Reading.** Every gate reads its mask live, at the moment it decides, so an unlock that arrives
mid-session takes effect at the next decision with no sync step. Gates that filter a table
loaded with the stage take effect at the next load of that table.

**Other mods** reach the masks through `ArchipelagoAPI.GetUnlockMask` / `SetUnlockMask`.
`SetUnlockMask` truncates to the mask's width, and a write to the box or sphere mask also pushes
the sphere gate into the ap_star mod.

## Ungated categories

A gating option of 0 means the seed holds no unlock items for that category - the apworld sends
1 only for a category that really holds keys this seed. On the save's first options write the
mod therefore fills each such mask with every bit, less the bits `goal_forced_gates` keeps back
(the legendary pieces, the Vs. King Dedede stadium or the spheres, when the goal is that feat).
The fill bypasses the announce path, so connecting does not flood the text box. Three
adjustments ride along:

- **Top Ride machines.** One machine mask covers City Trial, Air Ride and the Top Ride lobby,
  so it stays gated whenever City Trial or Air Ride is in the seed - but the apworld ships the
  Free Star and Steer Star unlocks only when Top Ride is. With machine gating on and the Top
  Ride row's goal `GOAL_NONE`, the mod sets those two bits itself.
- **Top Ride "New Item" rewards.** With Top Ride items ungated, the three checklist rewards that
  unlock Chickie, Who? Paint and Lantern in vanilla are marked received, so the engine keeps
  those items enabled.
- **Unplaced checklist rewards** are marked received per `checklist_reward_placed_types`.

The fill runs once per save. The gating flags themselves are read almost nowhere else afterwards;
the one live reader is the Top Ride item gate below.

## City Trial spawn tables

Patch types, copy abilities and City Trial items are all gated by removing locked kinds from the
tables City Trial draws items from. There are two families:

- **Box pools**, in `grBoxGeneObj` (`*stc_grBoxGeneObj`): one pool per box color
  (`item_group_spawn[BOXKIND_NUM]`, also used for sky and ground drops), the Same Item event's
  pool and the follow-up pool a multi-item blue box draws from. Each is parallel `it_kind[]` /
  `chance[]` arrays with a count, sampled by random index.
- **The event drop table**, `grBoxGeneInfo->item_desc->event_source_drop[]`: one row per kind
  with a weight column per source - Dyna Blade, Tac, the meteor, destructible objects, the Secret
  Chamber, the UFO. Callers index it directly.

`CityItemSpawn_InitItemFallChances` (0x800eb374) fills them at round start and
`CityEvent_ModifyItemFallDesc` (0x800ed784) refills them when an event changes the drop mix, so
one filter pass is hooked at the end of each (0x800eb558, and 0x800ed7f4 with the caller's LR
saved around the call). Stadiums and Air Ride never run that init, so the same pass runs at
`On3DLoadEnd` when the box pools exist off the city map. The pass:

1. injects All Up when the City Trial goal is Max Stats in One Run;
2. compacts every box pool, dropping each kind any gate reports locked - a stable two-pointer
   copy that shrinks the count, since a hole would still be rolled;
3. zeroes all six weight columns of every locked row of the event drop table, which cannot move.

Each gate contributes only a predicate - `GateAbilities_IsItemLocked`, `GatePatches_IsItemLocked`,
`GateItems_IsItemLocked` - that answers for its own kinds and 0 for every other, so they combine
with an OR. One hook owns each site because two `CODEPATCH_HOOKCREATE`s at one address in one
mod would both define the same hook symbol.

### Patch types

Nine bits, one per `PatchKind`. A bit covers all three kinds of that stat - the +1 patch, the
Down and the Fake - through `Item_KindToPatchKind`; HP has only the +1. All Up and the four Max
items are not stat patches by that mapping and are gated as City Trial items.

### City Trial items

Thirty bits for everything that is neither a patch nor a copy panel: All Up, Speed Max / Min,
Offense Max, Defense Max, Charge Max / None, Candy, the twelve foods, Fireworks, Panic Spin, Sensor
Bomb, Gordo, and the six legendary pieces. The bit index is `ItemUnlockKind`, not the
`ItemKind`.

Two event sources throw an All Up without rolling any table, so the filter cannot reach them and
each is gated at its call site. A locked All Up gives way to one roll of that source's own
event-drop column, which the filter has already cleaned; a roll of -1 throws nothing.

- **The UFO.** Slot 0 of every ring its five stops drop is a hardcoded `li r29, ITKIND_ALLUP`
  (from `CityUFO_State0Think`, 0x8010b024, onward). Each is replaced with a call, and the branch
  after it shortened to land on the move the other slots' roll returns through.
- **Dyna Blade.** `DynaBlade_ThrowItems` (0x8021db44) throws one All Up once enough damage
  lands; the `bl CityItem_Throw` at 0x8021ddf4 is replaced.

**Legendary pieces** never enter a pool: `LegendaryPieces_Init` (0x800ecfac) arms each machine's
three piece kinds, and `CityItemSpawn_SpawnLegendaryPiece` (0x800ed384) writes the next one
straight into a carrier box's `forced_item` through `LegendaryPiece_MarkAsSpawned`
(0x80252f10). So the pieces are gated there instead:

- A hook right after `LegendaryPieces_Init` in `CityItemSpawn_Init` (0x800ec284) clears a
  machine's `is_enabled` when none of its three pieces is unlocked, and both machines' when the
  Red Box is locked. A disabled machine never becomes pending, and its pieces never spawn.
- Both `bl LegendaryPiece_MarkAsSpawned`, at 0x800ed41c (Dragoon) and 0x800ed49c (Hydra), go
  through a wrapper that skips a locked piece: the carrier keeps its default `forced_item` of -1
  and falls as an ordinary red box, and the slot is consumed for the round.

Gating at arming time rather than at the spawn matters: while a piece is pending,
`CityItemSpawn_UpdateAndCheckToSpawn` returns the carrier category every tick, so suppressing
the spawn would stall the whole item spawner for the round.

### The Archipelago Star

The Archipelago Star is the ap_star mod's legendary machine, assembled from six spheres the way
Dragoon and Hydra are from three pieces. ap_star owns the spheres, their spawning and the
assembly; this mod owns everything Archipelago about them, through the `ApStarAPI` it imports at
`OnSaveLoaded` (after ap_star has bound its machine at its own):

| Item | IDs | Effect |
|---|---|---|
| Sphere unlock | 820-825 (`APStarPiece` order: Rose, Green, Violet, Tan, Blue, Yellow) | Sets the sphere's bit in `ap_star_piece_unlocked_mask` |
| Sphere give | 980-985 | Collects the sphere straight into every human's set; the sixth assembles the star |
| Give Archipelago Star | 14 | Runs the star's assembly for the first human ap_star accepts |
| Machine unlock | 856, mask bit 26 | Gates the star as a machine: spawning loose on the City Trial field, and selection wherever the build offers it |

The spheres follow `item_gating_enabled` and `GOALGATE_AP_STAR_PIECES` the way the legendary
pieces do, but are not `ItemKind`s, so they have their own u8 mask. The gate is ap_star's: the
mod pushes a mask into it with `SetPieceMask`, and ap_star keeps a locked sphere out of the item
registry at the next round load, so it never spawns. The spheres ride the red carrier box, so the
pushed mask is 0 while Red is locked. It is pushed at save load, when a sphere unlocks, when Red
unlocks, and on any `SetUnlockMask` of the box or sphere mask. Without ap_star in the build the
unlock bits are still saved, and the gives are dropped.

The gives do not need the sphere's unlock - like a Hydra or Dragoon part give, they collect
directly - and wait for a City Trial round. ap_star reports every assembly back through
`AddAssembleHandler`; a human's completes the Archipelago tab's "assemble the Archipelago Star"
objective, whichever way the star was built.

## Box colors

Three bits, Blue / Green / Red. City Trial picks a box's color and size in
`GrBoxGeneratorDetermine` (0x800ebc04), a weighted roll over a nine-entry table
`box_spawn_chances[BoxKind][size]` from the stage; the city's is `[20, 15, 10, 5, 4, 3, 7, 7,
0]`. The mod replaces it with the same roll over a stack copy (the table is read-only stage
data) where a color is zeroed when its bit is clear or when the filters have left its pool with
no positive weight - so opening a box always awards something. With nothing eligible it returns
-1, which `PowerUp_SpawnFromSky` (0x800ecdf4) takes as "place no box"; vanilla, given an
all-zero table, would fall off the end and assert.

The legendary-piece carrier never reaches the picker: `CityItemSpawn_Think` (0x800eb108)
hardcodes it red and large. Red's large weight is 0 in the city's table, so every large red box
on the field is a carrier. Locking Red is applied to carriers at arming time instead, above.

## City Trial events

Sixteen bits, one per `EventKind`. `CityEvent_Decide` (0x800edcf8) builds a chance array from
the stadium group's weights, boosts events sharing the last event's category, zeroes every
event in its recent history (`EventCheckData.prev_kind`, up to `prev_kind_num` entries), then
rolls. A hook at 0x800ede24 runs between the fill and the history passes, with the stack array
and the `EventCheckData`:

- it zeroes the chance of every locked event;
- it counts the events left with a positive weight and caps `prev_kind_num` at five eighths of
  that count. With few events unlocked, the history would otherwise exclude every candidate and
  no event could ever fire again; the cap always leaves at least one eligible.

The clobbered instruction reloads `prev_kind_num`, so the history passes see the capped value.
The event gives (200-215) start an event directly and never consult this mask; the EnergyLink
shop does, and refuses to sell a locked event.

## Top Ride items

Twenty-two bits, one per `TopRideItemKind`. The Top Ride item manager keeps its own
`enabled_mask`, built by `TopRideItem_MgrInit` (0x8034b5f4). The mod gates three ways:

- a hook right after that init (0x802db05c in `TopRide_KirbyMgrInit`) ANDs `enabled_mask` with
  the unlocked set;
- a conditional hook at the entry of `TopRideItem_SpawnAtPosition` (0x8034bf50) refuses any
  kind not in `enabled_mask`, and any out-of-range kind - with every item locked, the Party
  Ball's pick falls through to `TRITEM_NUM`, which would read past the descriptor table;
- the Party Ball burst in `TopRideItem_PartyBallUpdate` (0x80356dac) weighs all 22 items with no
  mask check, so its two `TopRideItem_GetDataByIndex` calls return a zero-weight stub for a
  locked kind.

Two quirks of the item set:

- **The ability items have a second key.** Freeze Fan, Fire, Bomb and Walky also unlock with
  their copy ability (`COPYKIND_ICE`, `_FIRE`, `_BOMB`, `_MIKE`), but only while
  `ability_gating_enabled` is 1 - an ungated ability mask is all ones and would free all four.
- **There are two Party Balls.** `TRITEM_PARTY_BALL_ALT` (12) is an engine duplicate; only
  `TRITEM_PARTY_BALL` (21) ships an unlock, and its state is mirrored onto 12.

`TopRideItem_MgrInit` enables Chickie, Who? Paint and Lantern only once their Top Ride checklist
rewards are received, so unlocking one of them also marks its reward received, and the engine's
own bit survives the init.
