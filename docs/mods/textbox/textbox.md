# Textbox

An on-screen notification stack for other mods: queued messages made of colored segments, laid
out in a screen corner, revealed by the engine's own typewriter and faded out one at a time. A
consumer hands it text through `TextBoxAPI` and never touches a `Text` object; the stack survives
scene changes and keeps fading through the match pause. Apart from the API, the mod's only game
hook is one Top Ride render patch.

## The API

`TextBoxAPI` is exported at `OnBoot` and imported with
`Hoshi_ImportMod(TEXTBOX_MOD_NAME, TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR)`.

| Member | Purpose |
|---|---|
| `Enqueue(fmt, ...)` | One printf-style segment in `DefaultColor` |
| `EnqueueSegments(segs, n)` | 1 to `TEXTBOX_MAX_SEGMENTS` `TextSegment`s, each `{text, color}` |
| `EnqueueColoredNoun(prefix, noun, color, suffix)` | Only the noun colored; prefix and suffix may be NULL or empty |
| `EnqueueColoredNounFmt(prefix, noun, color, suffix_format, ...)` | The same with a printf-style suffix |
| `DefaultColor`, `MachineColor`, `EventColor`, `StadiumColor`, `StageColor`, `TopRideItemColor`, `ItemColor` | Category colors |
| `AbilityColors`, `KirbyColors`, `ModeColors`, `PatchColors`, `BoxColors` | Palettes indexed by `CopyKind`, Kirby color, game mode, `PatchKind` and box kind |

Every `Enqueue*` answers 1, or 0 when the message was dropped: the textbox is off, the segment
count is out of range, or there is no screen canvas yet. The palette names only things the game
itself has; a consumer with its own vocabulary (traps, checks, rewards) keeps those colors itself
and passes them as segment colors. Only a color's RGB is used - alpha belongs to the fade.

Segment text is copied at enqueue, so a caller may pass stack buffers. Each message holds one
248-byte blob of its segments' strings back to back - a per-message budget rather than a
per-segment one. A segment that overruns it is cut at the byte that does not fit, and the segments
after it are dropped.

Two moments are off limits to a producer. Before the first scene change hoshi has created no
screen canvas, and `Text_CreateTextManual` (0x8044f128) reads the canvas list head without a NULL
check, so an enqueue then is dropped (the textbox checks `*stc_textcanvas_first`); a mod that wants
to announce something at `OnSaveLoaded` has to wait for a scene. And a producer must not enqueue
from its own `OnSceneChange`: hoshi runs the mods' scene-change callbacks after creating the new
canvas, so the canvas check passes while every queued message still points into the freed heap, and
the eviction an enqueue may trigger would write through those pointers.

## Layout

Messages are built with `Hoshi_CreateScreenText`, on hoshi's screen-space canvas. `Text_GX`
(0x804516e4) projects a canvas as x from 0 to 640 rightward and y from 0 to -480, negating each
vertex's y, so `Text.trans` is in pixels right of the left edge and down from the top
(`TEXT_CANVAS_W` and `TEXT_CANVAS_H`). The stack keeps a 10-pixel margin from the edges.

The whole stack is reflowed against the chosen corner whenever it changes, and from the Position
and Spacing settings' callbacks, since the stack stays on screen in the main menu where those
change. The newest message sits at the anchor corner and older ones flow away from it - down from
a top corner, up from a bottom one. On a right corner each message is right-aligned on its own
width. A message advances the next by its rendered height (`aspect.Y * viewport_scale.Y`) plus the
Spacing gap, a fraction of that height, so Tight lays messages flush at any font size.

## One message

A message is one `Text` holding a subtext per colored run, flowing left to right and wrapping onto
at most three lines. A segment that crosses a line break becomes more than one subtext. Each run
is built the same way:

- `Text.color`'s RGB is set to the segment's color, with the message's current alpha, before
  `Text_AddSubtext` (0x8044fec4), which captures it into the subtext's `COLOR` opcode as it emits
  it. Setting it afterwards does nothing, so a subtext opened for one segment is never filled with
  the next segment's text.
- The run is set and then measured through the engine. Hoshi's `Text_Sanitize` re-encodes ASCII
  into a different code space than `Text_GetStringWidth` assumes, so widths come from
  `Text_GetWidthAndHeight` on the subtext itself. A run that already fits costs one measurement;
  an overflowing one is cut back to the last space that fits. A word wider than a whole line splits
  mid-word, and a wrapped line never starts with a space.
- One subtext holds at most 127 sanitized bytes: `Text_ConvertASCIIToShiftJIS` (0x8044fb0c), which
  `Text_SetText` and `Text_AddSubtext` both go through, stops reading after 128 input bytes. Its
  output is no limit, since hoshi redirects it to a buffer sized for the worst case. A run over the
  limit is shortened in proportion to its overshoot until it fits, and every caller works from the
  count that actually landed, so nothing measures text the engine dropped.
- The run is added at the origin and then moved with hoshi's `Text_SetSubtextPos`, which rewrites
  the subtext's `TEXTCMD_POS` header in place - adding first is what lets its width be known before
  its position is chosen. Positions are in pre-scale units with y down, a line spanning 32 units.

Text past the last line becomes `..`. On the last line, a run with more text after it keeps room
for the marker: if it does not fit with that room it is refit and the marker appended. The run
that ends the message may use the full width. Nothing is ever scaled down to fit -
`viewport_scale` is exactly the Font Size setting. `Text.aspect` is set to the whole block's
bounding box so the background rect (`viewport_color`) covers every line.

## Fading and the queue

The renderer treats `Text.color.a` as a global alpha for every glyph of every subtext, while each
`COLOR` opcode sets only RGB. Fading a message therefore writes `color.a` alone; writing RGB too
would turn every colored noun white. The background alpha holds at its target until the text alpha
drops below it, then follows the text down, so the panel cannot outlast the glyphs.

The queue is a ring of nine slots, one kept free to tell empty from full, so it holds the eight
the Max On Screen setting can ask for. A message's `lifetime` starts at 200 and is both its peak
text alpha and its fade countdown. Enqueuing at the cap evicts the oldest messages first; lowering
the cap trims the stack and turning the textbox off clears it, from those options' callbacks.

The per-frame proc runs on a p_link 0 GObj, which no match pause freezes, and exists only while a
message is queued: an enqueue creates it, a scene change recreates it if messages carried over,
and it destroys itself on the first frame it finds the queue empty. Each frame it copies every
message's `temp.reveal_count` aside, holds everything while the oldest message is still typing,
and otherwise advances a frame counter shared by the queue. Past the Display Time threshold it
fades the oldest message one alpha step a frame and removes it at zero. Every removal restarts the
counter, so Display Time paces removals rather than bounding any one message's time on screen.

## Typewriter

The reveal is the engine's own: `Text_GX` reveals one glyph every `temp.char_delay` frames with no
work from the mod. The textbox writes `temp.char_delay` and `temp.space_delay` directly. The
`char_delay_init` seeds would do nothing, because `Text_GX` copies them across only at a
`TEXTCMD_SUBTEXT_RESET` or `TEXTCMD_SUBTEXT_BREAK` opcode (the store at 0x80451cec), and buffers
built by `Text_AddSubtext` hold only `TEXTCMD_POS` headers. The renderer reloads the live fields at
the top of each render and never clears them, so one write lasts. The delay is sampled per message
at enqueue, along with the font size and background target, and a delay of 0 reveals the whole
message at once.

Holding the fade needs the message's glyph count, which the engine never records -
`Text_AddSubtext` and `Text_SetText` leave `Text.text_end` unset. The textbox walks the opcode
stream from `text_start` to its terminator counting glyph codes (every byte at or above
`TEXTCMD_NUM` starts a two-byte code), which is exactly what the reveal counter advances on.
Spaces are glyphs too: `Text_Sanitize` turns a space into code `0x8140`, never the `TEXTCMD_SPACE`
opcode.

## Across a scene change

A scene change frees every `Text` with the heap, but the stack should carry over. So a message
keeps its text blob, colors, settings and revealed count, and its `Text` is always built from that
record - the first time and after every scene change alike, so it cannot draw differently the
second time. At `OnSceneChange` each queued message is rebuilt, its glyph count retaken, and its
typewriter resumed from the saved count with `text_end` left NULL, so the engine re-derives the
reveal frontier from `reveal_count`: a finished message stays whole and a half-typed one picks up
where it was.

The rebuild runs against the text heap `Scene_InitHeaps` (0x8000891c) has just recreated at 18432
bytes through `Text_CreateHeap` (0x8044f5b4), before the new scene allocates any text. A full stack
costs a few kilobytes, no more than it did in the scene being left. Allocation cannot fail softly:
`TextHeap_Alloc` (0x8044edec) panics ("Memory Empty", `sislib.c`) when the heap runs dry, and
`Text_CreateText` (0x8044fa70) writes through the new `Text` before returning, so a queued message
always has a `Text` and nothing checks for one.

## Top Ride

Top Ride's post-render callback, `TopRide_PostRenderCallback` (0x80009074), calls
`TopRide_CustomRenderer` (0x80286d7c), which runs a second `HSD_StartRender` pass that overwrites
the frame and wipes every screen-canvas overlay. A hook at 0x80009084, the instruction after that
call, walks the `stc_textcanvas_first` list and reissues `CObjThink_Common` (0x8042a29c) on each
canvas's camera GObj so the text draws on top. Any mod with its own screen overlay in Top Ride
needs the same.

## Settings

The **Text Box** page. Each option's value indexes a preset table.

| Option | Values | Default | Effect |
|---|---|---|---|
| Enabled | Off / On | On | Off drops every enqueue and clears the stack |
| Position | Top-Left / Top-Right / Bottom-Left / Bottom-Right | Top-Left | Anchor corner; reflows at once |
| Font Size | Small / Med / Large | Med | `viewport_scale` 0.30 / 0.40 / 0.55 |
| Colored Names | Off / On | On | Off stores every segment in `DefaultColor` |
| Background | Off / Dim / Solid | Solid | Panel alpha target 0 / 100 / 200 |
| Spacing | Tight / Normal / Wide | Tight | Gap of 0 / 0.25 / 0.5 times the text height; reflows at once |
| Max On Screen | 1 - 8 | 6 | Queue cap; the oldest messages are evicted to fit |
| Display Time | Short / Med / Long | Med | 180 / 300 / 480 frames before the oldest message fades |
| Typewriter | Off / Slow / Med / Fast | Fast | 0 / 8 / 4 / 2 frames per glyph |
