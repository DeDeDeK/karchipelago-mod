# Menu Dialogue and Yes/No Windows

The game has two native confirmation windows. `MnDialogueAll.dat` is the memory card prompt's
message window, with premade text placed at runtime and two choice cursors. The Options data delete
window (`ScMenOpdelwin` in `MnAll.dat`) is a single model with its question and Yes/No baked into
textures. Neither carries input logic: the scene that owns the window reads the pad and drives it.
A mod that wants a native-looking confirmation - in the main menu, say - reuses one of them.

## MnDialogue: the Memory Card Prompt Window

Only the memory card prompt uses it: `MJRKIND_CARD` (18), minor 38, the boot scene that checks for
the save file, with `CardPrompt_MinorLoad` (0x80047d60), `CardPrompt_MinorThink` (0x80047f30) and
`CardPrompt_MinorLeave` (0x80047f0c). `MnDialogue_Load` has no other caller, and nothing outside
the MnDialogue functions touches the `ScMenuCommon.dialogue` slots. The window is part of that
scene, not an overlay other scenes spawn.

### Build

`CardPrompt_MinorLoad` resets the prompt state, then:

1. `MnDialogue_Load` (0x80138e68) loads `MnDialogueAll.dat` with `Gm_LoadGameFile` into the archive
   slot `MnDialogue_GetArchiveSlot` returns, takes `ScMenDialogue_scene_data` into
   `ScMenuCommon.dialogue.sobj`, initializes the menu element pool, creates the camera (into
   `ScMenuCommon.cam_gobj`, drawing gx links 0 and 2) and light, and binds the two model publics
   with `MnDialogue_IndexBgSymbol` and `MnDialogue_IndexCursorSymbol`.
2. `MnDialogue_CreateWindow` (0x80138ef4) creates the Bg (element kind 0xba) and two cursors (kind
   0xbb, one per choice) and moves each cursor to its anchor.
3. `MnDialogue_InitText` (0x80138f1c) loads `SisDialogue.dat` into SIS slot 0, creates a text
   canvas into `ScMenuCommon.text.canvas_idx`, and `MnDialogue_CreateTexts` replaces the Text GObjs
   in `text.description_text`, `text.x30` and `text.x34` with three new ones: the message, the left
   choice and the right choice.

Both element kinds are made by `MenuElement_Create` (gx link 2, drawn by `MainMenu_ElementGX`). The
Bg's think plays its open animation; the cursors' think is empty.

The Bg model is a window frame plus ten anchor joints. The text layout projects them through the
camera, so the texts follow wherever the window sits:

| Bg joint | Use | Reader |
|---|---|---|
| 1, 2 | Left and right cursor position | `MnDialogue_GetCursorPos` |
| 3 | Message top-left | `MnDialogue_ProjectMessagePos` |
| 4 | Message right edge | `MnDialogue_ProjectMessageRight`, `MnDialogue_GetMessageWidth` |
| 5 | Message bottom | `MnDialogue_ProjectMessageBottom`, `MnDialogue_GetMessageHeight` |
| 6 | Left choice top-left | `MnDialogue_ProjectLeftChoicePos` |
| 7 | Choice right edge | `MnDialogue_ProjectChoiceRight`, `MnDialogue_GetChoiceWidth` |
| 9 | Right choice top-left | `MnDialogue_ProjectRightChoicePos` |
| 10 | Choice bottom | `MnDialogue_ProjectChoiceBottom`, `MnDialogue_GetChoiceHeight` |

Both choices share joint 7 and 10's width and height; joint 8 is not read. The scene's camera is
the main menu's (perspective, eye at z 109, fov 25, 640x480), so the window is authored in the
main menu's camera space.

### Text and Input

All three texts are premade `SisDialogue.dat` entries, set with `MnDialogue_SetMessage`,
`MnDialogue_SetLeftChoice` and `MnDialogue_SetRightChoice`: 2 OK, 3 Yes, 4 No, 5 Retry, 7 blank,
and 8-32 the memory card messages.

The prompt keeps its state in `CardPromptData` at the start of `GameData` while it runs:

| Field | Meaning |
|---|---|
| `lockout` | Input is ignored for this many scene frames (30) |
| `cursor` | 0 left, 1 right, 2 no choice on screen |
| `message`, `left_text`, `right_text` | SisDialogue ids |
| `state` | The prompt's state machine, 0-28 |
| `choices` | 0 message only, 1 Yes / No, 2 OK / Retry |
| `answer` | 2 nothing this frame, 0 left confirmed, 1 right confirmed |

Each frame `CardPrompt_MinorThink` runs `CardPrompt_InputThink` (left/right flips `cursor` unless it
is 2; A or Start sets `answer`; there is no B), then `CardPrompt_MenuThink` (the current state reads
`answer` and moves on through `CardPrompt_MenuStateChange`, whose per-state entry sets `cursor`,
`message` and `choices`), then refreshes the window: highlight on the selected cursor (pose frame 1)
and not the other (frame 0), cursors shown or hidden by `choices`, and the three texts re-bound.
The caller learns the answer only from `answer`.

`CardPrompt_MinorLeave` calls `MnDialogue_Destroy` (0x80138f44): `Menu_DestroyCommon` (the menu
camera, light, common GObjs and every menu archive), the Bg and cursors, and every
`ScMenuCommon.text` slot.

### Showing It Inside the Main Menu

The main menu (minor 2, where hoshi's settings menu runs) can host the window's GObjs, but not
through the scene-level functions:

- `MnDialogue_Load` re-initializes the element pool the live menu elements come from and replaces
  `cam_gobj` and the light.
- `MnDialogue_InitText` loads `SisDialogue.dat` over SIS slot 0, which the menu's own text uses,
  replaces the canvas and destroys three of the menu's texts.
- `MnDialogue_Destroy` tears the whole menu down through `Menu_DestroyCommon`.

What works:

- Load the archive with `Archive_LoadFile("MnDialogueAll.dat")` (16.8 KB, scene heap) and either
  write `ScMenDialogueBg_scene_models` and `ScMenDialogueCursor_scene_models` into
  `ScMenuCommon.dialogue` or set `*MnDialogue_GetArchiveSlot()` and call the two `Index*Symbol`
  functions. The main menu never uses those slots.
- Build with `MnDialogue_CreateBg`, `MnDialogue_CreateCursors` and `MnDialogue_PlaceCursors`;
  highlight with `MnDialogue_HighlightCursor` / `MnDialogue_UnhighlightCursor`; close with
  `MnDialogue_DestroyBg` and `MnDialogue_DestroyCursors`.
- The elements draw on gx link 2, which the main menu camera renders. hoshi's settings rows render
  through a separate camera on another link, so the window draws under them unless its GObjs move
  to that link or the rows are hidden. The elements are created at gx priority 0, below the Options
  delete window's 0x50; re-adding their gx link at 0x50 or above (`GObj_DestroyGXLink` then
  `GObj_AddGXLink`, keeping `gx_cb`) puts them over the Options panel.
- Text from one's own canvas with `Text_CreateText` and `Text_SetText`, placed with the
  projection and size helpers in the joint table above, which project through the main menu camera. English UI
  uses the master Latin font, so runtime "Yes" / "No" match the premade ones. `Text_CreateTextManual`
  (0x8044f128) is not a substitute: it leaves the text buffer null, so the first `Text_AddSubtext`
  faults.
- To match SisDialogue's styling: all three texts black; the message at scale 0.75, left-aligned,
  6 px below the box top, at most three lines 32 units apart; each answer at scale 1.0, centered in
  its choice box 2 px below the anchor, with fit on.
- Input stays with the caller: left/right flips the cursor, A answers, B can answer No.

## ScMenOpdelwin: the Options Data Delete Window

The Options "Data Delete" panel (`is_in_submenu` 5, `MainMenu_DataDeleteThink` 0x80017050)
confirms with a window every main menu already loads: `MainMenu_Init` indexes
`ScMenOpdelwin_scene_models` through `MainMenu_IndexDeleteConfirmSymbol`.

| Function | Address | Does |
|---|---|---|
| `MainMenu_OpenDeleteConfirm(prompt, cursor)` | 0x80132618 | Creates the window GObj (gx link 2, priority 0x50) |
| `MainMenu_SetDeleteConfirm(prompt, cursor)` | 0x80132638 | Poses it: frame `prompt * 2 + cursor` |
| `MainMenu_CloseDeleteConfirm` | 0x80132658 | Destroys it |

Prompt 0 shows "Delete this data?", prompt 1 "Are you sure?"; cursor 0 lights Yes, 1 lights No.
The window is one joint carrying sixteen DObjs - frame pieces, the two question images, and the No
and Yes images - with material animations selecting which question shows and which answer is lit.
The text is baked: a different question means hiding the two question DObjs and drawing a Text in
their place.

The panel opens the window on No, asks again with prompt 1 before deleting all data, closes on No
or B, and ends a deletion with `Memcard_ReqSave`.

## Other Options Panels

`menuInputGrabber` (0x800181b0) dispatches the main menu's think on `MainMenuData.is_in_submenu`:
0 `MainMenu_SelectModeThink`, 1 `MainMenu_OptionsThink`, 2 `MainMenu_RumbleThink` (0x800174fc),
3 `MainMenu_SoundTestThink`, 4 `MainMenu_MovieSelectThink` (0x80016da0), 5
`MainMenu_DataDeleteThink`. hoshi's settings menu takes value 6.

- **Rumble**: one On/Off toggle per port, each driven by that port's own pad; turning one on
  rumbles that controller.
- **Movies**: a wrapping list of six entries, seven once Air Ride checklist reward 0x23 is unlocked.
- **Sound test**: `MainMenu_SoundTestThink`.
