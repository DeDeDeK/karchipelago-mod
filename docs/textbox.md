# Textbox System

On-screen notification system (`mods/textbox/`). Queued, color-segmented messages with an optional typewriter reveal, rendered on a hoshi screen-space canvas. Used by the archipelago mod for AP grants/losses, deathlink/traplink notifications, EnergyLink spends, and so on. The mod owns no game hooks beyond one Top Ride re-render patch - everything else is driven by the exported API and hoshi's `OnSceneChange` callback.

## Entry Points

| Hook | Function | Role |
|------|----------|------|
| `mod_desc.OnBoot` | `OnBoot` (`main.c`) | Fills the API palette fields, `Hoshi_ExportMod`s the struct, applies the TR post-render hook |
| `mod_desc.OnSceneChange` | `TextBox_OnSceneChange` | Rebuilds every queued message's `Text` and, if any are queued, creates the per-frame GObj |
| `mod_desc.option_desc` | `TextBox_ModSettings` | "Text Box" settings menu |

## Public API

`TextBoxAPI` (`mods/textbox/include/textbox_api.h`) is exported via `Hoshi_ExportMod` and imported by other mods with `Hoshi_ImportMod(TEXTBOX_MOD_NAME)`. Every `Enqueue*` returns 1 on success and 0 if the message was dropped (textbox disabled, bad or empty segment count, or no screen canvas yet).

| Member | Purpose |
|--------|---------|
| `Enqueue(fmt, ...)` | printf-style single segment in `DefaultColor` |
| `EnqueueSegments(segs, n)` | 1..`TEXTBOX_MAX_SEGMENTS` (8) segments with per-segment colors |
| `EnqueueColoredNoun(prefix, noun, color, suffix)` | Only the noun colored; NULL/empty prefix or suffix allowed |
| `EnqueueColoredNounFmt(prefix, noun, color, suffix_fmt, ...)` | As above with a printf-style suffix |
| `DefaultColor`, `MachineColor`, `EventColor`, `StadiumColor`, `StageColor`, `TopRideItemColor`, `ItemColor` | Named category colors |
| `AbilityColors[COPYKIND_NUM]`, `KirbyColors[KIRBYCOLOR_NUM]`, `ModeColors[GMMODE_NUM]`, `PatchColors[PATCHKIND_NUM]`, `BoxColors[BOXKIND_NUM]` | Indexed palettes |

Palette RGB values live in `textbox_colors.c`; the alpha byte is ignored, since alpha is owned by the fade machinery. The palette fields are populated at `OnBoot` rather than in the static initializer because `extern const GXColor`s are not constant expressions in C.

The palette names only things the game itself has - machines, events, stadiums, stages, items. Colors for a consumer's own vocabulary belong to that consumer: the archipelago mod keeps its trap/death/energy/check/goal/reward/shop/filler colors in its own `ap_colors.c` and passes them in as segment colors.

Segment text is **copied** at enqueue, so callers may pass stack buffers. The copy is one `TEXTBOX_MESSAGE_TEXT_SIZE` (248 byte) blob per queued message holding the segments' NUL-terminated strings back to back - a per-message budget rather than a per-segment one, since that is what a producer actually spends. A segment that overruns the blob is truncated at the byte that does not fit, and the segments after it are dropped.

## Canvas and Layout

The canvas is the hoshi ortho screen camera (created by `ScreenCam_Create` inside hoshi's `Hook_SceneChange`). `Text_GX` (`0x804516e4`) projects every text canvas with x spanning 0..640 rightward and y spanning 0..-480, negating each vertex's y, so `Text.trans` is measured in raw pixels right of the canvas left edge and down from its top - `TEXT_CANVAS_W` / `TEXT_CANVAS_H` in hoshi's `text.h`. Messages are allocated with `Hoshi_CreateScreenText`. `TEXTBOX_MARGIN` (10px) keeps the stack off the edges.

`TextBoxQueue_RepositionAll` reflows the whole stack against the chosen corner. The stack stays on screen in the main menu, where the options change, so the Position and Spacing callbacks reflow it:

- **Top corners** stack newest at the top, older flowing down; **bottom corners** stack newest at the bottom, older flowing up.
- **Right corners** right-align each message individually against the right edge (per-message width, since each message can differ).
- Line advance is the rendered text height (`aspect.Y * viewport_scale.Y`) plus an optional fractional gap from the Spacing setting - so Tight always lays messages flush regardless of font size, and Normal/Wide scale their gap with the font.

`trans` is the top-left of each message's bounding box; bottom corners shift up by the rendered text height so the message's bottom edge sits at the anchor edge, and the spacing gap only ever falls between messages.

## Multi-Segment Colored-Noun Rendering

A single message is one `Text` GObj laid out as **subtexts flowing left to right and wrapping onto up to `TEXTBOX_MAX_LINES` (3) lines**, each with its own color - this is how `EnqueueColoredNoun` paints a noun in a category color inside an otherwise-default sentence. A segment that spans a line break becomes more than one subtext, so the subtext count is not the segment count. `TextBox_CreateSegmented` builds it:

- For each segment, set `t->color` to that segment's RGB (+ the message's current alpha) **before** calling `Text_AddSubtext` - `Text_AddSubtext` captures `t->color` into the subtext's `COLOR` opcode at emit time. Setting it after would not take effect.
- Each subtext is added at the origin, measured, then moved to its final spot with `Text_SetSubtextPos`, which rewrites the subtext's `0x07` POS header in place. Adding-then-moving is what lets a run's width be known before its position is chosen. Coordinates are pre-viewport-scale units with y increasing downward; a line spans 32 units at a character scale of 1, which is also what `Text_GetWidthAndHeight` reports as height.
- Break points are chosen by measuring through the engine, not by summing glyph advances. `Text_Sanitize` re-encodes ASCII into a different code space than `Text_GetStringWidth` assumes, so that helper reads the wrong kerning bank for sanitized text; `TextBox_SetRun` sets the subtext and calls `Text_GetWidthAndHeight` instead. `TextBox_FitRun` measures the whole run first, so a segment that already fits costs exactly one measurement, and only an overflowing one pays for the search.
- One subtext is bounded by the input limit of `Text_ConvertASCIIToShiftJIS` (0x8044fb0c), which both `Text_SetText` and `Text_AddSubtext` route through: it stops reading after 128 bytes, so sanitized text is capped at `TEXTBOX_RUN_BYTES` (127). Its output is no limit: hoshi redirects it to a static buffer sized for the worst case, so a letter's 3 output bytes (a `TEXTCMD_POSPUSHEND` plus its 2-byte code) never overrun anything. `TextBox_SetRun` scales its request by the overshoot until the run fits, returning how many characters actually landed. Every caller works from that return value - measuring text the engine silently dropped would report a fit for a run that never rendered.
- Wrapping prefers the last space that fits, including when the break came from one of the converter limits above rather than the line width. A single word wider than a whole line splits mid-word, which is also what guarantees the walk always advances. A wrapped line never starts with a space.
- A subtext carries its segment's color in an opcode emitted at `Text_AddSubtext` time, so one opened for a segment is never filled by the next one - a segment that strips to nothing closes it out instead of handing it over.
- Text past the last line is replaced by `TEXTBOX_TRUNC_MARK` (`..`). On the last line, a run with more text after it - later in its segment or in a later segment - must leave room for the marker: a run that fits whole and still leaves that room is placed normally and the next segment continues on the line, otherwise the run is refitted with the marker's width reserved and gets the marker appended. The run that ends the message can use the full width.
- **Nothing is ever scaled down to fit.** `viewport_scale` is exactly the chosen font size, so the Font Size setting means readability and nothing else.
- `t->aspect` is set to the whole block's bounding box (widest line, `line_height * line_count`) so the `viewport_color` background rect (and any future scissor) encloses every line.
- `t->trans` is left at the origin `Text_CreateText` gives it - `TextBoxQueue_RepositionAll` runs before the next render and is the single source of truth for on-screen position.

With Colored Names off, `TextBox_EnqueueSegments` stores every segment's color as `TextBox_DefaultColor`, leaving the caller's array untouched.

## Alpha / Fade Model

The renderer treats `text->color.a` as a **global alpha modulator**: the `COLOR` opcode only updates `temp.color` RGB, while alpha is sourced from `text->color.a` at init and applied to every glyph in every subtext. So fading the whole textbox means touching `.a` **only** - overwriting RGB would collapse all per-segment noun colors to white. `TextBox_SetAlpha` writes `color.a` and nothing else.

The background quad alpha (`viewport_color.a`) is independent: it sits at the configured `bg_target` until the text fade brings text alpha below the target, then fades together with the text so the panel can't outlast the glyphs.

## Queue and Lifetime

`TextBoxQueue` is a ring buffer of `TEXTBOX_QUEUE_SIZE` = 9 with one slot reserved to distinguish empty from full - capacity 8, matching the highest "Max On Screen" setting. A message's `lifetime` field is seeded to 200, doubling as its peak text alpha and its fade countdown.

`TextBox_PerFrame` runs the whole lifecycle on a p_link 0 GObj, so the stack keeps fading through the match pause. The GObj exists only while a message is queued: an enqueue creates it when the scene has none, `TextBox_OnSceneChange` recreates it when messages carried over, and it destroys itself on the first frame it finds the queue empty (`GObj_Destroy` on the GObj `GObj_UpdateAll` is running only flags it, and the free happens after the proc returns). Each frame it:

1. Mirrors every queued message's engine-side `temp.reveal_count` into `chars_revealed` (each `Text` is paced independently, so the whole queue is snapshotted, not just the oldest).
2. Holds everything while the oldest message is still typing (`typewriter_dwell != 0` and `reveal_count < chars_total`).
3. Otherwise advances a shared frame counter. Past the Display Time threshold, it decrements the oldest message's `lifetime` and pushes it into alpha each frame; at zero, it dequeues (which `Text_Destroy`s it).

Enqueuing when the queue is already at the "Max On Screen" cap drops oldest messages until the new one fits. The stack persists into the main menu, where the options change, so lowering the cap trims it and turning the textbox off retires all of it from their `on_change` callbacks. The frame counter is shared by the queue and `TextBox_Dequeue` resets it on every removal, so the Display Time setting paces removals rather than bounding any one message's time on screen.

`Sis_CountGlyphs` derives `chars_total` by walking the SIS opcode stream from `text->text_start` to its inline `0x00` TERMINATE, counting the 2-byte character codes (`>= 0x20`), which is everything the engine's reveal counter advances on. Nothing here emits the 1-byte `0x1a` SPACE opcode: `Text_Sanitize` turns a space into code `0x8140` and the game's converter maps it to a glyph like any other. This is necessary because `Text_AddSubtext` / `Text_SetText` never write `text->text_end`. The walk is capped at 4096 bytes as a runaway guard.

## Typewriter Seeding

`TextBox_BuildText` arms the engine's built-in per-glyph reveal (the renderer reveals one glyph every `temp.char_delay` frames on its own - no per-frame work mod-side). It writes `temp.char_delay`/`temp.space_delay` **directly** and leaves `char_delay_init`/`space_delay_init` alone: the engine only copies the `*_init` seeds across on a `0x01`/`0x02` SUBTEXT opcode (the sole write is in `Text_GX` at `0x80451cec`), and `Text_AddSubtext` buffers are delimited by `0x07` POS headers with no `0x01`/`0x02` in them, so the copy never fires and writing the `*_init` fields would do nothing at all. The renderer reloads the live `temp` fields into working registers at the top of each render (`0x80451c34`) and never clears them, so one write at enqueue persists.

Reveal resumes from `chars_revealed` (mirrored from the engine's `temp.reveal_count` every frame), with `text_end` left `NULL` so the engine re-derives the reveal frontier from `reveal_count`. This is what lets a message survive the scene-change rebuild below without re-typing.

The dwell is sampled **at enqueue** into `typewriter_dwell`, along with the font scale and background target, so a rebuild draws the message the way it first appeared. A dwell of 0 is the Off setting and reveals the whole message at once, so the setting needs no separate enable flag.

## Scene-Change Rebuild and Persistence

`Text` pointers are invalidated when the scene changes, but messages should persist visually across the transition. The queue stores **the message's text blob + `chars_revealed`**, not just the live `Text*`. Both the first build and the rebuild go through `TextBox_BuildText`, which points a `TextSegment` array at that stored blob, so a message can never draw differently the second time. `TextBox_OnSceneChange` walks the queue, rebuilds each message's `Text` via `TextBox_CreateSegmented`, re-snapshots `chars_total` (`Sis_CountGlyphs`), re-arms the typewriter (resuming from `chars_revealed`), and repositions - so a finished message stays fully shown and a mid-reveal one picks up where it was. It then creates the per-frame `TextBox_PerFrame` GObj if anything is queued.

Hoshi runs mods' `OnSceneChange` in mod order after creating the new screen canvas, so between the heap reset and `TextBox_OnSceneChange` the canvas check passes while every queued `Text*` still points into the freed heap. An enqueue from another mod's `OnSceneChange` would evict through those pointers and corrupt the new heap, so producers must not enqueue from there.

The rebuild runs against a heap that `Scene_InitHeaps` has just re-created at a fixed 18432 bytes (`Text_CreateHeap`, `0x8044f5b4`), before the incoming scene allocates any text of its own. Each message costs 160 bytes for the `Text` plus 16 for its cell plus an opcode buffer that `Text_AddSubtext` grows in 128-byte steps, so a full stack of 8 runs a few kilobytes - well inside the heap, and no more than the same 8 messages held in the scene being left. Building a `Text` cannot fail softly: `TextHeap_Alloc` (`0x8044edec`) `OSPanic`s with `sislib.c` "Memory Empty" when the heap runs dry, and `Text_CreateText` (`0x8044fa70`) writes through its new `Text` before returning, so it never returns NULL. A queued message therefore always has a `Text`, and nothing checks for one.

### Pre-first-scene canvas-NULL guard

Hoshi creates the screen canvas in `Hook_SceneChange`. A caller that enqueues **before the first scene change** (e.g. a `ChecklistRewards` regrant from `OnSaveLoaded` at boot) would walk an empty canvas list inside `Text_CreateText` and dereference `NULL+0xA`. `TextBox_EnqueueSegments` guards on `*stc_textcanvas_first` and drops the message if no canvas exists yet. Any mod enqueuing text from boot/`OnSaveLoaded` needs the same guard.

## Settings

Bound to `textbox_settings` in `textbox.c`, beside the menu. Each option's stored value is an index into a preset table, and each option's `value_num` is that table's length.

| Option | Values | Default | Effect |
|--------|--------|---------|--------|
| Enabled | Off / On | On | `Enqueue*` returns 0 immediately when off |
| Position | Top-Left / Top-Right / Bottom-Left / Bottom-Right | Top-Left | Anchor corner; reflows live |
| Font Size | Small / Med / Large | Med | `viewport_scale` 0.30 / 0.40 / 0.55 |
| Colored Names | Off / On | On | Off forces every segment to `DefaultColor` |
| Background | Off / Dim / Solid | Solid | `viewport_color.a` target 0 / 100 / 200 |
| Spacing | Tight / Normal / Wide | Tight | Extra gap 0 / 0.25 / 0.5 x rendered text height; reflows live |
| Max On Screen | 3 / 4 / 6 / 8 | 6 | Queue cap; enqueuing over it drops oldest |
| Display Time | Short / Med / Long | Med | 180 / 300 / 480 frames on the shared counter before the oldest message fades |
| Typewriter | Off / Slow / Med / Fast | Fast | 0 / 8 / 4 / 2 frames per glyph (`temp.char_delay`); 0 reveals at once. Sampled per message at enqueue |

## Top Ride Re-Render

Top Ride's post-render callback calls `TopRide_CustomRenderer` (`0x80286d7c`), which reaches a second `HSD_StartRender` pass through `TopRide_RenderScene` (`0x802c5520`) that overwrites the EFB and wipes screen-canvas overlays every frame. `TextBox_TopRideReRender` (`main.c`), hooked at `0x80009084` (the instruction right after the `bl TopRide_CustomRenderer` inside `TopRide_PostRenderCallback`, `0x80009074`), walks the `stc_textcanvas_first` list and re-issues `CObjThink_Common` on each canvas's `cam_gobj` to redraw on top. Any mod with a Top Ride HUD or text overlay needs the same treatment.
