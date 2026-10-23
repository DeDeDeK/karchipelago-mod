# Custom Machines: Characters and Select Screens

How the `custom_machines` mod gives a registered machine a cell on the Air Ride and City Trial
select screens. The screens do not list machines; they list `CharacterKind`s, 20 of them, each a
rider on a machine. A machine whose descriptor sets `wants_character` gets an appended
`CharacterKind` (20 and up, in the order of the machines that ask), and the mod widens the roster
tables, the select grid, the icon lists and the name text so that kind can be shown, picked and
described. A consumer gating the roster does it through one filter rather than any of this.

## The roster tables

Four DOL tables describe the roster and sit back to back with no slack:

| Table | Address | Shape | Reader |
|---|---|---|---|
| `stc_selicon_ckind_linear` | 0x804957ec | 20 bytes, the one-row strip | `SelIcon_GetCKindLinear` (0x8000b9a8) |
| `stc_selicon_ckind_grid` | 0x80495800 | 2 x 10 bytes | `SelIcon_GetCKind` (0x8000b9bc) |
| `stc_character_desc` | 0x80495814 | 20 `CharacterDesc` rows | `Character_GetDesc` (0x8000b9dc) |
| `stc_machine_ckind_star` | 0x80495850 | 19 bytes, star slot to `CharacterKind` | `Machine_GetCKind` (0x8000b9f4) |

The first three are each read by one accessor that does nothing but form an address, so they are
relocated by rewriting that accessor's `lis`/`addi` pair - 0x8000b9a8/0x8000b9b0,
0x8000b9c0/0x8000b9cc and 0x8000b9e0/0x8000b9e8 - with no hook. An appended `CharacterDesc` row
is `{rider_kind, is_bike, class slot}`, class-relative like every vanilla row.

`Machine_GetCKind` is the reverse lookup the results screens and the time-attack board use to
reach a machine's art, over the star table above and the 7-byte `stc_machine_ckind_bike`
(`r13 - 0x7fbc`), which no `lis`/`addi` pair forms; it is replaced outright. Vanilla parks its own
art-less machines on a neighbour's art - the Free, Steer and both wing stars on the Warp Star, the
plain Wheel, Wheel Kirby and both Dedede bikes on the Wheelie Scooter - and a custom machine with
no `CharacterKind` is parked the same way. Left to the engine, an appended star slot would read
past the star table into zeros and draw the Compact Star on every results screen.

## The grid

The grid gains a column per two appended characters, and its runtime row stride is written into
`SelIcon_GetCKind`'s `mulli r5, r0, cols` at 0x8000b9c4. Appended characters fill the new columns
top row first, so one addition makes an 11/10 grid rather than 10/11. At most one cell is left
over; it holds a sentinel `CharacterKind` past every kind the registry can hand out, which reads a
zeroed `CharacterDesc` row and which every availability check rejects, so it is never packed into
a list.

## Packing the icon lists

Both screens build their icon list by walking the grid, and the vanilla packing loops are a hard
10 columns into two 10-byte stack rows, so an 11th column has nowhere to land. Four things cap a
screen at 20 icons, and all four are moved to `SELECT_ICON_MAX` - the 20 vanilla characters plus
one per registry slot. These patches are installed whether or not a machine is registered, because
the availability filter works through them.

**The list in `GameData`.** Each screen's select block (`GameData+0x10a` on Air Ride, `+0x1d0` on
City Trial) keeps a count at `+0x65` and one `CharacterKind` per icon from `+0x66`, followed
immediately by live flags: Air Ride's row-split flag at `+0x7a` and debug-grid flag at `+0x7b`,
City Trial's debug-grid flag at `+0x7a`. Bytes `+0x11` to `+0x20` of each block are read by no
engine code, so the flags move there by rewriting the displacement of every instruction that
reaches them - five base-relative sites for Air Ride's row-split, five for its debug grid plus
`lbz r0, 0x185(r3)` at 0x800216d8 in `CSS_airRide_colorChanger`, and six for City Trial's debug
grid plus two relative to `GameData` (0x8002f2bc in `CitySelect_ChangeColor`, the color changer's
debug unlock bypass, and the clear at 0x80038d98 in `CitySelect_LoadCityTrial`). A rebuild clears
the vanilla list and the vacated flag bytes, 22 bytes on Air Ride and 21 on City Trial.

The list can still take every icon, and entries past that span land on the per-slot
controller-claim arrays after it - state for multi-console (LAN) play. `CSS_airRide_ModeDispatch`
and `CitySelect_MinorLoad` initialize those arrays before the list is built, every other reader sits
behind the session check `Net_IsSessionActive` (0x8007b650), and nothing reads a list entry past
the count, so an overflowing list is harmless offline and breaks controller claiming only in LAN
play.

**The layout.** Icon positions come from a strip of 20 anchor joints posed by an animation whose
frame is the icon count, with no key past 20. `AirRideSelect_LayoutIcons` and
`CitySelect_LayoutMachineIcons` are replaced: they run the engine's pass, which is exact up to 20,
and past that redo the grid arithmetically - two interleaved rows, the bottom one shifted half a
column, the spacing tightened to keep the block's width and the shared icon scale shrunk by the
same ratio so the tiles still fit. The block's width, center and row heights are measured off the
strip the engine just posed rather than named as constants: Air Ride hangs its anchors under a
joint the layout animation scales, and past 20 icons the animation holds its 20-icon pose, so the
strip always arrives in that layout. The rows stay at two - the engine's cursor navigation is
built for two. The ipos userdata (`AirRideSelectIposData`, `CitySelectIposData`) ends its 20
positions flush against the shared scale, so positions past 20 are held in the mod and
`AirRideSelect_GetIconPos` / `CitySelect_GetIconPos` are replaced to hand them out.

**The icon GObj arrays.** `ScMenuCommon` keeps 20 icon GObj pointers per screen, and their writers
(`_AirRideSelect_CreateSIcon`, `_CitySelect_CreateScMenSelplySiconCt`) index them unguarded, so an
icon past the strip would overwrite the scene-model pointers that follow - by the seventh, the
ipos GObj the layout reads. Both writers end in the same `extsb`/`slwi`/`add`/`stw`, so hooks at
0x8015181c and 0x8015c2dc take over the store, keep indices under 20 and drop the rest, exiting
past it to 0x8015182c / 0x8015c2ec. Nothing reads either array.

**The packing.** `AirRide_PopulateSelectIcons` (0x80020a08) is replaced outright. City Trial's
packing lives inside `CitySelect_CreateMachineIcons` (0x8002e3c4), whose Stadium and Free Run
passes pack the vanilla grid and then reorder it assuming the special characters sit at fixed
columns, which duplicates icons on a filtered list. The instructions at 0x8002e4d0 and 0x8002e5c0
each become a branch to the function's tail at 0x8002f0b8, where a hook packs the list, lays it
out, creates the icons, returns the count in `r27` for the store it displaced, and exits to
0x8002f220.

Replacing Air Ride's packer also removes two vanilla hazards that only a filtered roster reaches:

- **The rebalance hang.** At 10 or more icons vanilla packs the grid's two rows and then rebalances
  them. When the top row is behind, each pass moves two entries across. When it is ahead, a pass
  moves one entry, then restores the saved row counts, so the loop only ends if one move already
  balances the rows. A top row four or more ahead of the bottom spins forever and hangs the console
  inside `AirRide_PopulateSelectIcons`. Vanilla never gets there, because
  `AirRide_CheckCharacterAvailable` (0x8002090c) always refuses four top-row characters (Compact
  Star, Dragoon, Hydra, Flight Warp Star). A filter that hands those back can produce a 7/3 split.
  The replacement packs in grid order and never rebalances; the rebalance only reordered icons for
  looks.
- **Stale entries.** `CSS_airRide_RaceUpdate` and `CSS_airRide_FreeTimeUpdate` repack on every
  update, and vanilla rewrites only the first `count` entries. A list that shrinks between visits
  keeps the earlier fill's entries past the new count, and the screen shows and commits a slot's
  machine as `list[icon]`. The replacement clears the whole list span before every rebuild, so
  nothing past the count survives.

Air Ride packs from the one-row strip when fewer than 10 characters are available and from the
grid otherwise; its debug grid offers every character, gated or not, but never the sentinel. City
Trial always packs from the grid. Appended characters are always available - a drop-in machine
has no checklist reward behind it - and the `CustomMachineAvailabilityFilter` set through
`SetAvailabilityFilter` gets the last word on every candidate, receiving the screen's own answer
(its unlock, or the mode's roster). An appended character never reaches
`AirRide_CheckCharacterAvailable`, whose 20-entry jump table has no arm for one.

**The cursor.** `CitySelect_Cursor1InputThink` (0x800312fc) splits the cursor into two rows at 10
icons (`cmpwi r3, 9; ble` at 0x80031350), while the renderer keeps up to 10 icons on one drawn row,
so at exactly 10 the cursor split 5+5 across a single row. Vanilla City Trial only produces 15-20
icons; a filtered roster can land on 10, so the compare becomes `cmpwi r3, 10`.

## Name and description

Moving the cursor onto an icon draws the machine's name and a description under it. Both screens
load their own SIS file into SIS slot 0 - `SisSelply.dat` on Air Ride, `SisSelplyCt.dat` on City
Trial - each holding exactly 48 entries: the image and kerning banks, six pieces of screen
furniture, the 20 names from entry 8 and the 20 descriptions from entry 28. A `CharacterKind`
becomes a pair of entry indices through two 20-entry tables of words read as signed bytes, -1 in
either suppressing both texts:

| Screen | Reader | Name table | Description table |
|---|---|---|---|
| Air Ride | `AirRideSelect_SetMachineText` (0x80153d2c) | 0x804aa3d8 | 0x804aa428 |
| City Trial | `CitySelect_SetMachineText` (0x8015e740) | 0x804aa598 | 0x804aa5e8 |

Neither the tables nor the SIS files have a spare entry, so an appended `CharacterKind` would read
an index out of whatever follows the table and crash in `Text_StorePremadeText` the moment the
cursor lands on it. All four tables are relocated, widened, by the same `lis`/`addi` rewrite the
roster tables use. The appended entries are added to the SIS array itself: hooks on the epilogues
of `AirRideSelect_LoadSisFile` (0x8013bacc) and `CitySelect_LoadSisFile` (0x8013c4a8) copy the
loaded 48 pointers into a mod-owned array with the extra entries after them and repoint slot 0.
That runs on every load, because the array it copies lives in the scene heap.

Entries `48 + 2n` and `48 + 2n + 1` are the n-th appended character's name and description,
composed from its descriptor in the same SIS opcodes the vanilla entries use. The name is
centered, kerned, black at scale 0.5 and upper-cased, as every vanilla name is. The description is
left-aligned and gray at scale 0.55 in a box 20 units down, with `\n` becoming a line break;
vanilla blurbs are two lines of about 24 characters and anything wider is squeezed to fit. A
machine with no description still gets an empty entry, since either index at -1 hides both texts.
