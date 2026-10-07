# Rules Screens (MnSelrule)

Each mode has a rules screen, built from `MnSelruleAll.dat`: Air Ride (minor 3,
`MNRKIND_AIRRIDESETINGS`), Top Ride (minor 4, `MNRKIND_4`) and City Trial (minor 5,
`MNRKIND_CITYSETTINGS`). They share one element layer (`RuleMenu_*` calls over
`MnSelrule_*` implementations) and differ in their row tables, their think and where they
store values. Prototypes, the row/label enums and the tables are in
`externals/hoshi/include/menu.h`; the working state structs are in `game.h`
(`GameData.airride_rules` / `topride_rules` / `city_rules`); the element slots are
`ScMenuCommon.rules` in `scene.h`.

Each mode's major lists its rules minor as its initial minor. In Air Ride,
`MinorExit_AirRideMachineSelect` sends a select screen that exited with direction bit
`0x10` (Z) to minor 3.

## Screen Layout

A screen has up to two pages of up to five rows. Every element is a menu element GObj
(`MenuElement_Create`, gx link 2) with a `MenuElementData` userdata kind `0xa2`-`0xab`:

| Public | Kind | Slot | What it is |
|---|---|---|---|
| `ScMenSelruleBg` / `Bgm2d` / `BgCt` | `0xa2` | `bg_gobj` | backdrop per mode; intro anim, then a loop |
| `ScMenSelrulePanel` | `0xa3` | `panel_gobj` | the board; anim 1/2 on page turns, reveals the description 5 frames after load |
| `ScMenSelruleFpos` | `0xa4` | `fpos_gobj[page]` | five row anchors, frame = row count; slides +/-60 in 12-unit steps on page turns |
| `ScMenSelruleFrame` | `0xa5` | `frame_gobj[page][row]` | row name plate; label TexAnim frame = `RuleRowKind`, selected anim |
| `ScMenSelruleFrame2` | `0xa6` | `frame2_gobj[page]` | the "Additional Rules" page button |
| `ScMenSelruleCpos` | `0xa7` | `cpos_gobj[page][row]` | five value slot anchors per row, frame = value count (1-4) |
| `ScMenSelruleContents` | `0xa8` | `contents_gobj[page][row][slot]` | value label, TexAnim frame = `RuleValueLabel`, highlight anim |
| `ScMenSelruleNum` | `0xa9` | `num_gobj` | two-digit counter with a "min" unit, or "Recommended" below 1 |
| `ScMenSelruleStadium` | `0xaa` | `stadium_gobj` | one stadium name label (Shuffle, then the eight stadium groups) |
| `ScMenSelruleCursor` | `0xab` | `cursor_gobj` | selection box with left and right arrow joints |

Frames, Frame2, Cpos and Contents re-read their anchor every frame, so a page slide carries
the whole page. Slot 0 of a row is its collapsed value; slots 1-4 are the options laid out
while the row has the cursor. Number and stadium widgets are single per screen and move
between slot 0 and slot 1 (`Expand` / `Collapse`).

The description line under the board is a premade `SisSelrule.dat` text
(`RuleMenu_ShowDescriptionText`), picked per value from `stc_airride_rule_desc`,
`stc_topride_rule_desc` or `stc_city_rule_desc`.

## Row Kinds

A row's id (`RuleRowKind`) is its name label and decides its widget and storage. The
widgets are:

| Widget | Rows | Look | Left / right | A |
|---|---|---|---|---|
| Value list | Rules, Damage, Speed Help, Enemies, Game Tempo, Course Selection, Items, Camera, Camera Angle, Features, Events | collapsed: one label; focused: all 2-4 labels in a row, cursor box on the current one | moves the value, no wrap; the arrow on an end is hidden | leaves the screen |
| Number | Laps, Time, Air Ride's Laps-or-Time | two digits, "min" for Time, "Recommended" for 0 laps | steps by 1 with fast auto-repeat (`Pad_GetRapidHeld`), wraps; both arrows shown | leaves the screen |
| Stadium picker | Stadium (City Trial) | one stadium name, wide cursor (`RuleMenu_SetCursorAnim(1)`) | steps through Shuffle and the unlocked stadium groups, wraps; both arrows shown | leaves the screen |
| Page button | Additional Rules | Frame2 plate, no value | right turns to page 2 | turns to page 2 |

Behaviour shared by every row:

- Up/down (`Pad_GetRepeat`) moves the row cursor, no wrap. Leaving a row collapses it
  (`*Rules_DeselectRow`); entering one expands it (`*Rules_SelectRow`), moves the cursor to
  the value, shows its description and sets the arrows (`*Rules_UpdateArrows`).
- A value list reads `Pad_GetRepeat`; a number or stadium row reads `Pad_GetRapidHeld`.
- A value change takes effect in the working state at once, with the cursor sound, the
  highlight moving between slots and the description updated.
- A wrap (99 to 0, 7 to 3, last stadium to Shuffle, and back) does not land at once: the
  new value waits in `wrap_value` and lands after 8 frames (`wrap_timer`), during which the
  row ignores input.
- The Rules row is a value list whose change also relabels the next row (`RuleMenu_SetRowLabel`
  to Laps or Time) and swaps its number between the lap count and the minutes.
- A, Start, L or R on a non-button row leaves the screen. B on page 2 turns back to page 1,
  on page 1 leaves. Z on page 1 turns to page 2, on page 2 leaves. City Trial has one page,
  so A, B and Z all leave.
- Every exit sets direction `0x100`, and the minor's exit callback saves the working state
  into the settings, so B keeps the changes too.
- After a page turn input waits 5 frames (`input_delay`). Input is ignored for the first 5
  frames of the scene, 30 when `long_lockout` is set; the `*Rules_ClearLongLockout`
  functions clear it on the way in.

## Air Ride (minor 3)

`AirRideRules_MinorLoad` (`0x8001aa94`) resets the cursor, records `Net_IsSessionActive`
in `is_lan`, loads the settings (`AirRideRules_LoadSettings`) and builds both pages from
the fixed tables. `AirRideRules_Think` (`0x800196e4`) runs the input;
`AirRideRules_MinorExit` saves with `AirRideRules_SaveSettings`.

| Page | Row | Values | Stored in |
|---|---|---|---|
| 1 | Rules | Laps / Time | `airride_rule` |
| 1 | Laps or Time | laps 0-99 (0 = Recommended) or 1-99 minutes | `airride_lap_total`, `airride_time_seconds` (minutes * 60) |
| 1 | Damage | None / On | `airride_damage` |
| 1 | Speed Help | None / Weak / Strong | `airride_speed_help` |
| 1 | Additional Rules | - | - |
| 2 | Enemies | On / Off | `airride_enemies` (set = On) |
| 2 | Game Tempo | Normal / Slow | `airride_tempo` (1 / 2) |
| 2 | Course Selection | On / Off / Loser | `airride_course_selection` via `AirRide_SetCourseSelect`; Loser is dropped in a LAN session |

Course Selection Off makes `MinorExit_AirRideMachineSelect` skip the course select.

## Top Ride (minor 4)

`TopRideRules_MinorLoad` (`0x8001eaac`) builds its rows with `TopRideRules_BuildRows`
(`0x8001da14`) before creating anything: each row of `stc_topride_rule_row_kind` is copied
into `topride_rules`, and for the unlock-gated rows (Camera Angle, Features, Item kind)
each option whose `stc_topride_rule_unlock` entry is a clear_kind is kept only if
`ClearChecker_CheckUnlocked(GMMODE_TOPRIDE, kind)` passes. A gated row with no unlocked
gated option is dropped and the rows below move up. With `dblevel` 3 or higher and R +
D-pad Up held on load, every row and option is kept.

Value rows name options through `stc_topride_rule_option` (`{label, desc, value}`), so
the stored value need not equal the list position. `TopRideRules_Think` (`0x8001c608`)
keeps the focused row's list position in `option` and writes the option's `value` into the
row's bitfield on every change.

| Page | Row | Options (gating clear_kind) | Stored in |
|---|---|---|---|
| 1 | Laps | 0-99, 0 = Recommended | `topride_laps` |
| 1 | Items | Off / Few / Normal / Many | `topride_item_amount` (3 / 2 / 0 / 1) |
| 1 | Speed Help | None / Weak / Strong | `topride_speed_help` |
| 1 | Camera | Normal / Fixed | `topride_camera` |
| 1 | Additional Rules | - | - |
| 2 | Game Tempo | Normal / Slow | `topride_tempo` (1 / 2) |
| 2 | Course Selection | On / Off / Loser | `topride_course_selection` |
| 2 | Camera Angle | Normal, Diagonal (5), Side (6) | `topride_camera_angle` |
| 2 | Features | Off (7), Normal, Many (7) | `topride_features` |
| 2 | Items (kind) | Normal, Attack (12), Mystery (11) | `topride_item_kind` (0 / 2 / 1) |

Each setting has a `TopRide_Get*` / `TopRide_Set*` accessor (`game.h`) the load and save use.

## City Trial (minor 5)

`CityRules_MinorLoad` (`0x8001fbd8`) builds one page from `stc_city_rule_row_kind`.
`CityRules_LoadSettings` (`0x8001f950`) also builds the stadium list: Shuffle first, then
every stadium group with at least one unlocked stadium (`Gm_StadiumCheckUnlocked`), in
group order. `CityRules_Think` (`0x8001ee60`) runs the input; `CityRules_MinorExit` saves.

| Row | Values | Stored in |
|---|---|---|
| Time | 3-7 minutes | `city.time_seconds` (minutes * 60) |
| Game Tempo | Normal / Slow | `city.game_tempo` (1 / 2) |
| Stadium | Shuffle, then the unlocked groups | `city.menu_stadium_selection` (0 = Shuffle, else StadiumGroup + 1) |
| Events | On / Off | `city.events_enable` |

## Working State and Initialisation

`Gm_InitData` calls `AirRideRules_InitData`, `TopRideRules_InitData` and
`CityRules_InitData`, which clear the working state and load it from the settings. The
working state then persists between visits; each load re-reads the settings and resets the
cursor.

## Element Calls

The `RuleMenu_*` wrappers (`0x80132cdc`-`0x80133520`) are the API each mode's code calls.
`RuleMenu_Load*` loads the archive (`Gm_LoadGameFile`, `ScMenSelrule_scene_data` into
`rules.sobj`), inits the element pool, creates the camera (`RuleMenu_CreateCObj`) and light,
and binds every public, picking the Bg variant by mode. A screen is then built with
`RuleMenu_CreateBackground`, `RuleMenu_CreateDescription`, `RuleMenu_CreatePage` per page,
`RuleMenu_CreateRow` (frame + value anchors) or `RuleMenu_CreatePageButton` per row,
`RuleMenu_CreateValue` per value slot (or `RuleMenu_CreateNumber` / `RuleMenu_CreateStadium`),
and `RuleMenu_CreateCursor`. `RuleMenu_TurnPageNext` / `Prev` play the panel and page
slides; `RuleMenu_Destroy` runs `Menu_DestroyCommon` and destroys every element.

## Comparison With hoshi's Settings Menu

| Vanilla widget | Closest hoshi kind |
|---|---|
| Value list | `OPTKIND_VALUE` |
| Number | `OPTKIND_NUM` |
| Stadium picker | `OPTKIND_VALUE` with a long, wrapping list |
| Page button | `OPTKIND_MENU` |
| Rules row relabelling its neighbour | none |
| Unlock-gated options and rows | none |

Vanilla details a port would have to reproduce: a value list lays out every option at once
with the cursor on the current one and hides the arrow at either end; a number shows a word
("Recommended") for its zero value and spins with the fast repeat; a wrap pauses 8 frames at
the boundary; a row's change can relabel another row; and leaving the screen by any button
keeps the changes.
