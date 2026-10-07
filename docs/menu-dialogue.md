# Menu Dialogue and Yes/No Windows

The game has two native confirmation windows. `MnDialogueAll.dat` is the memory card
prompt's message window, with runtime-placed premade text and two choice cursors. The
Options data delete window (`ScMenOpdelwin` in `MnAll.dat`) is a single model with its
question and Yes/No baked into textures. Neither carries its own input logic: the scene
that owns the window reads the pad and drives it. Prototypes and the `CardPromptData`,
`ScMenuCommon.dialogue` and `MainMenuData` fields are in `externals/hoshi/include/menu.h`
and `scene.h`.

## MnDialogue: the Memory Card Prompt Window

### Who uses it

Only the memory card prompt: major `MJRKIND_CARD` (18), minor `MNRKIND_CARD` (38), the
boot-time scene that checks for the save file. Its minor callbacks are
`CardPrompt_MinorLoad` (`0x80047d60`), `CardPrompt_MinorThink` (`0x80047f30`) and
`CardPrompt_MinorLeave` (`0x80047f0c`). `MnDialogue_Load` (`0x80138e68`) has no other
caller, and nothing outside the `MnDialogue_*` functions reads or writes the
`ScMenuCommon.dialogue` slots (`0x1130`-`0x1147`). The window is part of that scene, not
an overlay the menu scenes spawn.

### Build

`CardPrompt_MinorLoad` resets the prompt state, then:

1. `MnDialogue_Load` - `Gm_LoadGameFile` of `MnDialogueAll.dat` into the r13 archive slot
   `MnDialogue_GetArchiveSlot` (`0x80138e24`) returns, `ScMenDialogue_scene_data` into
   `ScMenuCommon.dialogue.sobj`, `HSD_ObjAllocInit` of the menu element pool at
   `0x80559a90` (`0x1f0`-byte userdata, 4 per block), `MnDialogue_CreateCObj` (a camera
   from the scene data, stored in `ScMenuCommon.cam_gobj`, drawing gx links 0 and 2),
   `MnDialogue_CreateLObj`, then `MnDialogue_IndexBgSymbol` and
   `MnDialogue_IndexCursorSymbol` bind the two model publics into the dialogue slots.
2. `MnDialogue_CreateWindow` (`0x80138ef4`) - `MnDialogue_CreateBg` (element kind `0xba`),
   `MnDialogue_CreateCursors` (two elements of kind `0xbb`, one per choice) and
   `MnDialogue_PlaceCursors`, which moves each cursor to its Bg anchor.
3. `MnDialogue_InitText` (`0x80138f1c`) - `SisDialogue.dat` into SIS slot 0, a new text
   canvas into `ScMenuCommon.text.canvas_idx`, and `MnDialogue_CreateTexts` (`0x8017c418`),
   which destroys whatever is in `text.description_text`, `text.x30` and `text.x34` and
   creates three Text GObjs there: the message, the left choice and the right choice.

Both element kinds are made by `MenuElement_Create` (GObj class `0x26`, gx link 2,
`MainMenu_ElementGX`). The Bg's think (`MnDialogue_BgThink`) runs its open animation every
frame; the cursors' think is empty.

The Bg model is a window frame plus ten anchor joints. The text layout reads them through
the camera with `CObj_ProjectPoint`, so the texts follow wherever the window model sits:

| Bg joint | Use | Reader |
|---|---|---|
| 1, 2 | left and right cursor position | `MnDialogue_GetCursorPos` |
| 3 | message top-left | `MnDialogue_ProjectMessagePos` |
| 4 | message right edge | `MnDialogue_ProjectMessageRight`, `MnDialogue_GetMessageWidth` |
| 5 | message bottom | `MnDialogue_ProjectMessageBottom`, `MnDialogue_GetMessageHeight` |
| 6 | left choice top-left | `MnDialogue_ProjectLeftChoicePos` |
| 7 | choice right edge | `MnDialogue_ProjectChoiceRight`, `MnDialogue_GetChoiceWidth` |
| 9 | right choice top-left | `MnDialogue_ProjectRightChoicePos` |
| 10 | choice bottom | `MnDialogue_ProjectChoiceBottom`, `MnDialogue_GetChoiceHeight` |

The two choice texts share joint 7 and 10's width and height. Joint 8 is not read.

`ScMenDialogue_scene_data`'s camera is the same as the main menu's `ScMenCommon_scene_data`
camera (perspective, eye at z 109, fov 25, 640x480), so the window models are authored in
the main menu's camera space.

### Text

All three texts are premade `SisDialogue.dat` entries, set with `MnDialogue_SetMessage`,
`MnDialogue_SetLeftChoice` and `MnDialogue_SetRightChoice` (`Text_InitPremadeText` on the
three slots):

| Id | Text |
|---|---|
| 2 | OK |
| 3 | Yes |
| 4 | No |
| 5 | Retry |
| 7 | (blank) |
| 8-32 | the memory card messages ("There is no Memory Card inserted into Slot A.", "Make a new save data file?", ...) |

### Driving it

The prompt keeps its state at the start of `GameData` while the scene runs
(`CardPromptData`, from `CardPrompt_GetData`):

| Field | Meaning |
|---|---|
| `lockout` | input is ignored until this many scene frames have passed (30) |
| `cursor` | 0 left, 1 right, 2 no choice on screen |
| `message`, `left_text`, `right_text` | SisDialogue ids |
| `state` | the prompt's state machine, 0-28 |
| `choices` | 0 message only (blank choices, cursors hidden), 1 Yes / No, 2 OK / Retry |
| `answer` | 2 nothing this frame, 0 left confirmed, 1 right confirmed |

`CardPrompt_MinorThink` runs, every frame:

1. `CardPrompt_InputThink` - left or right on the stick or D-pad (`Pad_GetRepeat`) flips
   `cursor` between 0 and 1 unless it is 2, with the cursor sound when choices are shown.
   A or Start (`Pad_GetDown`) sets `answer` to the cursor. There is no B handling. With
   `dblevel` 3 or higher, X steps `message` through every entry.
2. `CardPrompt_MenuThink` - the current state's handler reads `answer` and moves to the next
   state through `CardPrompt_MenuStateChange`, whose per-state entry sets `cursor`,
   `message` and `choices` (state 4, "no save file": cursor 0 on Yes, message 13, Yes / No).
3. The window refresh: `MnDialogue_HighlightCursor` on the selected cursor and
   `MnDialogue_UnhighlightCursor` on the other (pose frame 1 and 0 of the cursor model,
   set with `JObj_ReqAnim` + `JObj_Anim`), `MnDialogue_ShowCursor` / `HideCursor` by
   `choices`, and the three premade texts re-bound from `message`, `left_text` and
   `right_text`.

The caller learns the answer from `answer`; there is no return value or callback.

### Teardown

`CardPrompt_MinorLeave` calls `MnDialogue_Destroy` (`0x80138f44`): `Menu_DestroyCommon`
(`0x80131928`, the menu camera, light and common GObjs, and every menu archive including
`MnDialogue_FreeArchive`), `MnDialogue_DestroyBg`, `MnDialogue_DestroyCursors` and
`Menu_DestroyTexts` (every `ScMenuCommon.text` slot).

### Showing it inside the main menu

The main menu (minor 2, where hoshi's settings menu runs as `is_in_submenu` 6) can host the
window's GObjs, but not through the scene-level functions:

- `MnDialogue_Load` must not run there. It re-inits the element pool the live menu elements
  are allocated from, and replaces `cam_gobj` and the light.
- `MnDialogue_InitText` must not run there. It loads `SisDialogue.dat` over SIS slot 0, which
  the menu's own premade text uses, replaces `text.canvas_idx`, and destroys
  `text.description_text`, `x30` and `x34`.
- `MnDialogue_Destroy` must not run there. `Menu_DestroyCommon` tears the menu down.

What works in the main menu:

- Loading the archive with `Archive_LoadFile("MnDialogueAll.dat")` (16.8 KB, scene heap)
  and writing `ScMenDialogueBg_scene_models` and `ScMenDialogueCursor_scene_models` into
  `ScMenuCommon.dialogue`, or setting `*MnDialogue_GetArchiveSlot()` and calling the two
  `Index*Symbol` functions. The main menu never uses those slots, and the r13 archive slot
  is null there.
- `MnDialogue_CreateBg`, `MnDialogue_CreateCursors` and `MnDialogue_PlaceCursors` to build
  the window; `MnDialogue_HighlightCursor` / `UnhighlightCursor` each frame; and
  `MnDialogue_DestroyBg` + `MnDialogue_DestroyCursors` to close it. The element pool is
  already initialised by the main menu.
- The elements draw on gx link 2, which the main menu camera (`MainMenu_CreateCObj`,
  links 0-2) renders. hoshi's settings menu renders its own rows through a separate camera
  on `MODSETTINGS_GXLINK`, so the window draws in the main menu camera's pass, not above
  the settings rows unless its GObjs are moved onto the settings link or the rows are
  hidden while it is open. Bg and cursors are created at priority 0, below the Options data
  delete window's `0x50`; re-adding their gx link (`GObj_DestroyGXLink` +
  `GObj_AddGXLink`, keeping `gx_cb`) at `0x50` and up puts them over the Options panel.
- Text from hoshi's own canvas: `Text_CreateText` + `Text_SetText` for the prompt and the
  two answers, placed with the `MnDialogue_Project*` / `Get*` helpers (they project
  through `ScMenuCommon.cam_gobj`, the main menu camera) or at fixed coordinates. All
  English UI uses the master Latin font, so runtime "Yes" / "No" match the premade ones.
  `Text_CreateTextManual` (`0x8044f128`) is not a substitute: it sets position and box but
  leaves the text buffer (`Text.alloc`) null, so the first `Text_AddSubtext` faults; create
  with `Text_CreateText` and set `trans` / `aspect` afterwards.
- SisDialogue's premade styling, to match by hand: all three texts black. The message is
  scale 0.75, left-aligned, starting 6 px (8 text units) below the box top, at most three
  lines 32 units apart. Each answer is scale 1.0, centered in its choice box 2 px below the
  anchor, with FIT on.
- Input stays with the caller: left/right flips a cursor index, A answers, B can answer No.

A minimal wrapper is therefore: load the archive once on main menu load; on open, bind the
two symbols, create Bg + cursors, create three Text objects, set the cursor to No; per
frame, read the pad, re-highlight, and on A or B destroy the two elements and the texts and
return the answer.

## ScMenOpdelwin: the Options Data Delete Window

The Options "Data Delete" panel (`is_in_submenu` 5, `MainMenu_DataDeleteThink`
`0x80017050`) confirms with a window that is already loaded in every main menu:
`MainMenu_Init` indexes `ScMenOpdelwin_scene_models` into
`ScMenuCommon.main.ScMenOpdelwin_scene_models` through
`MainMenu_IndexDeleteConfirmSymbol`.

- `MainMenu_OpenDeleteConfirm(prompt, cursor)` (`0x80132618`) creates the window GObj
  (`MainMenu_CreateGObj`, gx link 2, priority `0x50`, element kind `0xc`) into
  `ScMenuCommon.main.ScMenOpdelwin_gobj`.
- `MainMenu_SetDeleteConfirm(prompt, cursor)` (`0x80132638`) poses it: the model's frame is
  `prompt * 2 + cursor`. Prompt 0 shows "Delete this data?", prompt 1 "Are you sure?";
  cursor 0 lights Yes, 1 lights No.
- `MainMenu_CloseDeleteConfirm` (`0x80132658`) destroys it.

The window is one joint carrying sixteen DObjs: the frame pieces, the two question images
and the No and Yes images, with material animations selecting which question shows and
which answer is lit. The text is baked; a different question means hiding the two question
DObjs and drawing a Text in their place.

`MainMenu_DataDeleteThink` drives it from `MainMenuData`: up/down (wrapping) moves `x3b`
over the seven list entries; A opens the window with `delete_confirm_cursor` on No and
`delete_confirm_stage` 1; left/right moves between Yes (0) and No (1); A on Yes deletes, or
for the last entry (all data) asks again with prompt 1 and stage 2; A on No or B closes it.
A deletion ends with `Memcard_ReqSave`.

## Other Options Panels

`menuInputGrabber` (`0x800181b0`) dispatches the main menu's think on
`MainMenuData.is_in_submenu`: 0 `MainMenu_SelectModeThink`, 1 `MainMenu_OptionsThink`,
2 `MainMenu_RumbleThink` (`0x800174fc`), 3 `MainMenu_SoundTestThink`, 4
`MainMenu_MovieSelectThink` (`0x80016da0`), 5 `MainMenu_DataDeleteThink`.

- **Rumble** (`ScMenOprum*`): one On/Off toggle per controller port, each driven by that
  port's own pad - left sets On, right sets Off, A/L/R flip it. Turning one on rumbles that
  controller. Ports with no controller (`nopad_mask`) show the no-controller plate.
- **Movies** (`ScMenOpmov*`): a wrapping list of six entries, seven once Air Ride
  checklist clear_kind `0x23` is unlocked (`movie_extra_unlocked`); A plays the movie.
- **Sound test** (`ScMenOpsnd*`): `MainMenu_SoundTestThink`.
