# Textbox System

On-screen notification system (`mods/textbox/`). Queued, color-segmented messages with an optional typewriter reveal, rendered on a hoshi screen-space canvas. Used by the archipelago mod for AP grants/losses, deathlink/traplink notifications, EnergyLink spends, and so on. The mod owns no game hooks beyond one Top Ride re-render patch - everything else is driven by the exported API and hoshi's `OnSceneChange` callback.

## Entry Points

| Hook | Function | Role |
|------|----------|------|
| `mod_desc.OnBoot` | `OnBoot` (`main.c`) | Fills the API palette fields, `Hoshi_ExportMod`s the struct, applies the TR post-render hook |
| `mod_desc.OnSceneChange` | `CreateTextBox_OnSceneChange` | Rebuilds every queued message's `Text` and creates the per-frame GObj |
| `mod_desc.option_desc` | `ModSettings` | "Text Box" settings menu |

## Public API

`TextBoxAPI` (`mods/textbox/include/textbox_api.h`) is exported via `Hoshi_ExportMod` and imported by other mods with `Hoshi_ImportMod(TEXTBOX_MOD_NAME)`. Every `Enqueue*` returns 1 on success and 0 if the message was dropped (textbox disabled, bad segment count, or no screen canvas yet).

| Member | Purpose |
|--------|---------|
| `Enqueue(fmt, ...)` | printf-style single segment in `DefaultColor` |
| `EnqueueSegments(segs, n)` | 1..`TEXTBOX_MAX_SEGMENTS` (8) segments with per-segment colors |
| `EnqueueColoredNoun(prefix, noun, color, suffix)` | Only the noun colored; NULL/empty prefix or suffix allowed |
| `EnqueueColoredNounFmt(prefix, noun, color, suffix_fmt, ...)` | As above with a printf-style suffix |
| `IsReady()` | 1 when an `Enqueue*` would be accepted - the textbox is on and a screen canvas exists |
| `DefaultColor`, `MachineColor`, `EventColor`, `StadiumColor`, `StageColor`, `TopRideItemColor`, `ItemColor`, `TrapColor`, `DeathColor`, `EnergyColor`, `CheckColor`, `GoalColor`, `RewardColor`, `ShopColor`, `FillerColor` | Named category colors |
| `AbilityColors[COPYKIND_NUM]`, `KirbyColors[KIRBYCOLOR_NUM]`, `ModeColors[GMMODE_NUM]`, `PatchColors[PATCHKIND_NUM]`, `BoxColors[BOXKIND_NUM]` | Indexed palettes |

Palette RGB values live in `textbox_colors.c`; the alpha byte is ignored, since alpha is owned by the fade machinery. The palette fields are populated at `OnBoot` rather than in the static initializer because `extern const GXColor`s are not constant expressions in C.

Segment text is **copied** at enqueue, so callers may pass stack buffers. The copy is one `TEXTBOX_MESSAGE_TEXT_SIZE` (248 byte) blob per queued message holding the segments' NUL-terminated strings back to back - a per-message budget rather than a per-segment one, since that is what a producer actually spends. A message that overruns the blob loses its trailing segments rather than any one segment losing its tail.

`IsReady` exists for producers with their own queue: the archipelago mod renders client-authored messages out of a shared-memory mailbox, and polling `IsReady` lets it hold one during a scene transition instead of enqueuing into a missing canvas and losing the message.

## Canvas and Layout

The canvas is the hoshi ortho screen camera (640x480 raw pixels, created by `ScreenCam_Create` inside hoshi's `Hook_SceneChange`). Messages are allocated with `Hoshi_CreateScreenText`. `TEXTBOX_MARGIN` (10px) keeps the stack off the edges.

`TextBoxQueue_RepositionAll` reflows the whole stack against the chosen corner. It reads the settings live, so a Position/Spacing change reflows what is already on screen instead of waiting for the next message:

- **Top corners** stack newest at the top, older flowing down; **bottom corners** stack newest at the bottom, older flowing up.
- **Right corners** right-align each message individually against the right edge (per-message width, since each message can differ).
- Line advance is the rendered text height (`aspect.Y * viewport_scale.Y`) plus an optional fractional gap from the Spacing setting - so Tight always lays messages flush regardless of font size, and Normal/Wide scale their gap with the font.

`trans` is the top-left of each message's bounding box; bottom corners shift up by `line_h` so the message's bottom edge sits at the anchor edge.

## Multi-Segment Colored-Noun Rendering

A single message is one `Text` GObj laid out as **subtexts flowing left to right and wrapping onto up to `TEXTBOX_MAX_LINES` (3) lines**, each with its own color - this is how `EnqueueColoredNoun` paints a noun in a category color inside an otherwise-default sentence. A segment that spans a line break becomes more than one subtext, so the subtext count is not the segment count. `CreateTextBoxSegmented` builds it:

- For each segment, set `t->color` to that segment's RGB (+ the message's current alpha) **before** calling `Text_AddSubtext` - `Text_AddSubtext` captures `t->color` into the subtext's `COLOR` opcode at emit time. Setting it after would not take effect.
- Each subtext is added at the origin, measured, then moved to its final spot with `Text_SetSubtextPos`, which rewrites the subtext's `0x07` POS header in place. Adding-then-moving is what lets a run's width be known before its position is chosen. Coordinates are pre-viewport-scale units with y increasing downward; a line spans 32 units at a character scale of 1, which is also what `Text_GetWidthAndHeight` reports as height.
- Break points are chosen by measuring through the engine, not by summing glyph advances. `Text_Sanitize` re-encodes ASCII into a different code space than `Text_GetStringWidth` assumes, so that helper reads the wrong kerning bank for sanitized text; `TextBox_SetRun` sets the subtext and calls `Text_GetWidthAndHeight` instead. `TextBox_FitRun` measures the whole run first, so a segment that already fits costs exactly one measurement, and only an overflowing one pays for the search.
- One subtext holds at most `TEXTBOX_RUN_BYTES` (127) bytes of sanitized text, because `Text_ConvertASCIIToShiftJIS` (0x8044fb0c) stops reading its input after 128 bytes and both `Text_SetText` and `Text_AddSubtext` go through it. A character costs 1 byte if alphanumeric and 2 otherwise, so `TextBox_SetRun` scales its request by the overshoot until the sanitized form fits, and returns how many characters actually landed. Every caller works from that return value - measuring text the engine silently dropped would report a fit for a run that never rendered.
- Wrapping prefers the last space that fits, including when the break came from the byte limit above rather than the line width. A single word wider than a whole line splits mid-word, which is also what guarantees the walk always advances. A wrapped line never starts with a space.
- A subtext carries its segment's color in an opcode emitted at `Text_AddSubtext` time, so one opened for a segment is never filled by the next one - a segment that strips to nothing closes it out instead of handing it over.
- Text past the last line is replaced by `TEXTBOX_TRUNC_MARK` (`..`), fitted with room reserved for the marker itself.
- **Nothing is ever scaled down to fit.** `viewport_scale` is exactly the chosen font size, so the Font Size setting means readability and nothing else.
- `t->aspect` is set to the whole block's bounding box (widest line, `line_height * line_count`) so the `viewport_color` background rect (and any future scissor) encloses every line.
- `t->trans` is left at the origin as a placeholder - `TextBoxQueue_RepositionAll` runs before the next render and is the single source of truth for on-screen position.

With Colored Names off, `TextBox_EnqueueInternal` rewrites every segment's color to `TextBox_DefaultColor` in a local copy, leaving the caller's array untouched.

## Alpha / Fade Model

The renderer treats `text->color.a` as a **global alpha modulator**: the `COLOR` opcode only updates `temp.color` RGB, while alpha is sourced from `text->color.a` at init and applied to every glyph in every subtext. So fading the whole textbox means touching `.a` **only** - overwriting RGB would collapse all per-segment noun colors to white. `TextBox_SetAlpha` writes `color.a` and nothing else.

The background quad alpha (`viewport_color.a`) is independent: it sits at the configured `bg_target` until the text fade brings text alpha below the target, then fades together with the text so the panel can't outlast the glyphs.

## Queue and Lifetime

`TextBoxQueue` is a ring buffer of `TEXTBOX_QUEUE_SIZE` = 9 with one slot reserved to distinguish empty from full - capacity 8, matching the highest "Max On Screen" setting. A message's `lifetime` field is seeded to 200, doubling as its peak text alpha and its fade countdown.

`TextBox_PerFrame` (a GObj created each scene change) runs the whole lifecycle:

1. Mirror every queued message's engine-side `temp.reveal_count` into `chars_revealed` (each `Text` is paced independently, so the whole queue is snapshotted, not just the oldest).
2. Drop the oldest message outright if it has no `Text` - a scene-change rebuild that failed has nothing to reveal or fade, and leaving it at the head would hold every message behind it forever.
3. Hold everything while the oldest message is still typing (`reveal_count < chars_total`).
4. Otherwise advance a shared frame counter. Past the Display Time threshold, decrement the oldest message's `lifetime` and push it into alpha each frame; at zero, dequeue (which `Text_Destroy`s it) and reset the counter.

Enqueuing when the queue is already at the "Max On Screen" cap drops oldest messages until the new one fits.

`Sis_CountGlyphs` derives `chars_total` by walking the SIS opcode stream from `text->text_start` to its inline `0x00` TERMINATE, counting the 2-byte character codes (`>= 0x20`), which is everything the engine's reveal counter advances on. Nothing here emits the 1-byte `0x1a` SPACE opcode: `Text_Sanitize` turns a space into code `0x8140` and the game's converter maps it to a glyph like any other. This is necessary because `Text_AddSubtext` / `Text_SetText` never write `text->text_end`. The walk is capped at 4096 bytes as a runaway guard.

## Typewriter Seeding

`TextBox_ApplyTypewriter` arms the engine's built-in per-glyph reveal (the renderer reveals one glyph every `temp.char_delay` frames on its own - no per-frame work mod-side). It writes `temp.char_delay`/`temp.space_delay` **directly** rather than relying on `char_delay_init`/`space_delay_init`: the engine only copies the `*_init` seeds across on a `0x01`/`0x02` SUBTEXT opcode (the sole write is in `Text_GXLink` at `0x80451cec`), and `Text_AddSubtext` buffers are delimited by `0x07` POS headers with no `0x01`/`0x02` in them, so the copy never fires and every glyph would reveal on frame one. The renderer reloads the live `temp` fields into working registers at the top of each render (`0x80451c34`) and never clears them, so one write at enqueue persists.

Reveal resumes from `chars_revealed` (mirrored from the engine's `temp.reveal_count` every frame), with `text_end` left `NULL` so the engine re-derives the reveal frontier from `reveal_count`. This is what lets a message survive the scene-change rebuild below without re-typing.

Whether the typewriter is active and how fast it runs are sampled **at enqueue**, so toggling the setting mid-reveal cannot change a message already on screen.

## Scene-Change Rebuild and Persistence

`Text` pointers are invalidated when the scene changes, but messages should persist visually across the transition. The queue stores **the message's text blob + `chars_revealed`**, not just the live `Text*`. Both the first render and the rebuild go through `TextBox_MessageSegments`, which points a `TextSegment` array at that stored blob, so a message can never draw differently the second time. `CreateTextBox_OnSceneChange` walks the queue, rebuilds each message's `Text` via `CreateTextBoxSegmented`, re-snapshots `chars_total` (`Sis_CountGlyphs`), re-arms the typewriter (resuming from `chars_revealed`), and repositions - so a finished message stays fully shown and a mid-reveal one picks up where it was. It then creates the per-frame `TextBox_PerFrame` GObj.

### Pre-first-scene canvas-NULL guard

Hoshi creates the screen canvas in `Hook_SceneChange`. A caller that enqueues **before the first scene change** (e.g. a `ChecklistRewards` regrant from `OnSaveLoaded` at boot) would walk an empty canvas list inside `Text_CreateText` and dereference `NULL+0xA`. `TextBox_EnqueueInternal` guards on `*stc_textcanvas_first` and drops the message if no canvas exists yet. Any mod enqueuing text from boot/`OnSaveLoaded` needs the same guard.

## Settings

Bound to `textbox_settings` (mod-owned storage, not `APSave`). Each option's stored value is an index into a preset table in `textbox.c`; out-of-range indices fall back to the Med/Normal preset.

| Option | Values | Default | Effect |
|--------|--------|---------|--------|
| Enabled | Off / On | On | `Enqueue*` returns 0 immediately when off |
| Position | Top-Left / Top-Right / Bottom-Left / Bottom-Right | Top-Left | Anchor corner; reflows live |
| Font Size | Small / Med / Large | Med | `viewport_scale` 0.30 / 0.40 / 0.55 |
| Colored Names | Off / On | On | Off forces every segment to `DefaultColor` |
| Background | Off / Dim / Solid | Solid | `viewport_color.a` target 0 / 100 / 200 |
| Spacing | Tight / Normal / Wide | Tight | Extra gap 0 / 0.25 / 0.5 x rendered text height; reflows live |
| Max On Screen | 3 / 4 / 6 / 8 | 6 | Queue cap; enqueuing over it drops oldest |
| Display Time | Short / Med / Long | Med | 180 / 300 / 480 frames at full opacity before the fade starts |
| Typewriter -> Enabled | Off / On | On | Sampled per message at enqueue |
| Typewriter -> Speed | Slow / Med / Fast | Fast | 8 / 4 / 2 frames per glyph (`temp.char_delay`) |

## Top Ride Re-Render

Top Ride's post-render callback runs a second `HSD_StartRender` pass (`TopRide_CustomRenderer`, `0x80286d7c`) that overwrites the EFB and wipes screen-canvas overlays every frame. `TextBox_TopRideReRender`, hooked at `0x80009084` (the instruction right after the `bl TopRide_CustomRenderer` inside `TopRide_PostRenderCallback`), walks the `stc_textcanvas_first` list and re-issues `CObjThink_Common` on each canvas's `cam_gobj` to redraw on top. Any mod with a Top Ride HUD or text overlay needs the same treatment.
