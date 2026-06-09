# Textbox System

On-screen notification system (`mods/textbox/`). Queued, color-segmented messages with an optional typewriter reveal, rendered on a hoshi screen-space canvas. Used by the archipelago mod for AP grants/losses, deathlink/traplink notifications, etc. Exposes a `TextBoxAPI` (`textbox_api.h`) imported via `Hoshi_ImportMod` — `Enqueue` / `EnqueueSegments` / `EnqueueColoredNoun*` plus a palette of `*Color` helpers.

The canvas is the hoshi ortho screen camera (640×480; see `hoshi/src/screen_cam.c`). `TEXTBOX_MARGIN` (10px) keeps the stack off the edges. Player-facing wording/coloring conventions (`Unlocked <Category>: X` vs `Received: X`, the `ModeColors`/`FillerColor` palette, the 5-segment cap) are a separate convention, not covered here.

## Multi-segment colored-noun rendering

A single message is one `Text` GObj laid out as **N horizontal subtexts on one line**, each with its own color — this is how `EnqueueColoredNoun` paints a noun in a category color inside an otherwise-default sentence. `CreateTextBoxSegmented` builds it:

- For each segment, set `t->color` to that segment's RGB (+ the message's current alpha) **before** calling `Text_AddSubtext` — `Text_AddSubtext` captures `t->color` into the subtext's `COLOR` opcode at emit time. Setting it after would not take effect.
- Each subtext is added at an `x_cursor` that advances by the previous subtext's measured width (`Text_GetWidthAndHeight`, pre-viewport-scale), so segments sit flush on the same line.
- `t->aspect` is set to the whole line's bounding box (`total_width`, `max line_height`) so the `viewport_color` background rect (and any future scissor) encloses all segments.
- `t->trans` is left at the origin as a placeholder — `TextBoxQueue_RepositionAll` runs before the next render and is the single source of truth for on-screen position.

## Alpha / fade model

The renderer treats `text->color.a` as a **global alpha modulator**: the `COLOR` opcode only updates `temp.color` RGB, while alpha is sourced from `text->color.a` at init and applied to every glyph in every subtext. So fading the whole textbox means touching `.a` **only** — overwriting RGB would collapse all per-segment noun colors to white. `TextBox_SetAlpha` writes `color.a` and nothing else.

The background quad alpha (`viewport_color.a`) is independent: it sits at the configured `bg_target` until the text fade brings text alpha below the target, then fades together with the text so the panel can't outlast the glyphs.

## Typewriter seeding

`TextBox_ApplyTypewriter` arms the engine's built-in per-glyph reveal (the renderer reveals one glyph every `char_delay` frames on its own — no per-frame work mod-side). The non-obvious part: `char_delay_init`/`space_delay_init` (the *designed* seeds) are only copied into the live `temp.char_delay`/`temp.space_delay` on a `0x01`/`0x02` SUBTEXT opcode (the sole write is in `Text_GXLink` at `0x80451cec`). `Text_AddSubtext` buffers are delimited by `0x07 POS` and contain **no** `0x01`/`0x02`, so that copy never fires — leaving every glyph revealed on frame one. The fix is to seed the live `temp.char_delay`/`temp.space_delay` fields directly; the renderer reloads them into working registers at the top of each render (`0x80451c34`) and never clears them, so a single write at enqueue persists.

Reveal resumes from `chars_revealed` (mirrored from the engine's `temp.reveal_count` every frame in `TextBox_PerFrame`), with `text_end` left `NULL` so the engine re-derives the reveal frontier from `reveal_count`. This is what lets a message survive the scene-change rebuild below without re-typing.

## Scene-change rebuild and persistence

`Text` pointers are invalidated when the scene changes, but messages should persist visually across the transition. The queue (`TextBoxQueue`, a ring buffer of `TEXTBOX_QUEUE_SIZE` = 9 with one reserved slot) stores **segment copies + `chars_revealed`**, not just the live `Text*`. `CreateTextBox_OnSceneChange` (the mod's `OnSceneChange` hook) walks the queue, rebuilds each message's `Text` via `CreateTextBoxSegmented`, re-snapshots `chars_total` (`Sis_CountGlyphs`), re-arms the typewriter (resuming from `chars_revealed`), and repositions — so a finished message stays fully shown and a mid-reveal one picks up where it was. It then creates the per-frame `TextBox_PerFrame` GObj.

### Pre-first-scene canvas-NULL guard

Hoshi creates the screen canvas in `Hook_SceneChange`. A caller that enqueues **before the first scene change** (e.g. a `ChecklistRewards` regrant from `OnSaveLoaded` at boot) would walk an empty canvas list inside `Text_CreateText` and dereference `NULL+0xA`. `TextBox_EnqueueInternal` guards on `*stc_textcanvas_first` and drops the message if no canvas exists yet. Any mod enqueuing text from boot/`OnSaveLoaded` needs the same guard.

## Corner-anchored stacking layout

`TextBoxQueue_RepositionAll` reflows the whole stack against the chosen corner (read live, so a settings change reflows what's already on screen):

- **Top corners** stack newest at the top, older flowing down; **bottom corners** stack newest at the bottom, older flowing up.
- **Right corners** right-align each message individually against the right edge (per-message width, since each message can differ).
- Line advance is the rendered text height (`aspect.Y * viewport_scale.Y`) plus an optional fractional gap from the Spacing setting — so Tight always lays messages flush regardless of font size, and Med/Wide scale their gap with the font.

`trans` is the top-left of each message's bounding box; bottom corners shift up by `line_h` so the message's bottom edge sits at the anchor edge.

## Top Ride re-render

Top Ride's post-render runs a second `HSD_StartRender` pass that overwrites the EFB and wipes screen-canvas overlays every frame. `TextBox_TopRideReRender` (hooked at `0x80009084`, just after `bl TopRide_CustomRenderer`) re-issues `CObjThink_Common` on each canvas's `cam_gobj` to redraw on top. See `topride-system.md` § "Post-render second pass wipes screen overlays" — any TR HUD/overlay mod needs this.
