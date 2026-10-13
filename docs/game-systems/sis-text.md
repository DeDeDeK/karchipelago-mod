# SIS Text

Almost every string the game shows - menus, HUD, dialogue, results, City Trial event banners -
is drawn by one renderer: `Text_GX` walks a byte stream of opcodes and glyph codes and emits one
textured 32x32 quad per character. Pre-composed strings ship in SIS (String Image Set) archives;
runtime strings are composed from C format strings into the same stream. This doc covers where
text comes from, the glyph banks, the stream format, colour, the typewriter reveal, canvases and
rendering, and what a mod needs to put its own text on screen.

Two other text paths exist and share nothing with this one: **DevText**, a debug-only stroke font
drawn with `GX_LINES` (`DevelopText_Create` / `DevelopText_AddString`, 0x800ab2d4 / 0x800ab78c),
and the **OS (IPL) font** (`OSText_DrawString`, 0x8038bf64), used only by the Top Ride 2D debug
menu and some select-screen overlays.

## SIS archives

`Text_LoadSisFile(slot, filename, symbol)` (0x8044f800) loads an HSD archive into one of five
slots. The archive lands in `stc_sis_archives[5]` (0x8059a848) and its relocated pointer array in
`stc_sis_data[5]` (0x8059a85c), an array of `SISEntry`. Entries 0 and 1 are the per-SIS glyph
images and kerning (usually NULL); text entries start at 2. `Text_InitPremadeText` (0x8044f8c8)
binds a Text to an entry by index. Scenes load different files into slots 0-4; City Trial keeps
`SisCitytrial.dat` in slot 0. The disc carries 21 SIS archives:

| File | Entries | Contents |
|---|---|---|
| `SisBestrap.dat` | 42 | boot and startup |
| `SisCitytrial.dat` | 42 | City Trial event and stadium prediction banners |
| `SisClrChk2D.dat` / `3D` / `CT` | 158 / 171 / 169 | checklist reward names |
| `SisDialogue.dat` | 33 | in-game dialogue |
| `SisEnding2d.dat` / `SisEnding3d.dat` | 55 / 92 | endings |
| `SisLan.dat` | 39 | LAN mode |
| `SisMenu.dat` | 123 | main menu |
| `SisProgressive.dat` | 7 | progressive scan prompt |
| `SisResultCt.dat` | 10 | City Trial results |
| `SisSelmap.dat` / `SisSelmap2d.dat` | 10 / 8 | stage select |
| `SisSelply.dat` / `SisSelplyCt.dat` / `SisSelply2d.dat` | 48 / 48 / 8 | player and machine select |
| `SisSelrule.dat` | 59 | rule settings |
| `SisSelstadium.dat` | 54 | stadium select |
| `SisSmmenu.dat` | 3 | sub-menu, carrying a kana glyph block |
| `SisStadiumTitle.dat` | 26 | stadium title cards |

Entry counts include the two data pointers. `SisSelply.dat` and `SisSelplyCt.dat` share a layout:
entries 8-27 are the machine names and 28-47 their descriptions, reached from a `CharacterKind`
through byte tables read by `AirRideSelect_SetMachineText` (0x80153d2c) and
`CitySelect_SetMachineText` (0x8015e740); -1 suppresses both texts.

## The text heap

A Text's stream (`Text.alloc`, +0x64) and its state stack (+0x68) live in a dedicated text heap of
`TextHeapCell`s, not the HSD heap. `Scene_InitHeaps` (0x8000891c) recreates it at 18432 bytes on
every scene change through `Text_CreateHeap` (0x8044f5b4), which also empties the five SIS slots,
the Text list and the canvas list. So every Text, canvas and loaded SIS archive is gone after a
scene change, and anything meant to outlive one has to be rebuilt from the mod's own record of it.
Allocation cannot fail softly: `TextHeap_Alloc` (0x8044edec) reports "Memory Empty" and panics in
`sislib.c` when the heap runs dry, and `Text_CreateText` (0x8044fa70) writes through the new Text
before returning. A mod putting much text on screen shares those 18 KB with the scene's own text.

## Glyph banks

The renderer picks a bank per character from the high bits of its 16-bit code.

- **`0x2000`-`0x3fff`: the master Latin bank** in the DOL - 256 images of 0x200 bytes (I4,
  32x32) at 0x8050a040, indexed by `(code - 0x2000) & 0xff`, and kerning pairs
  `{u8 left_pad, u8 right_edge}` at 0x80509dc0. A glyph's drawn width is
  `34 - left_pad - right_edge`, which hoshi's `Text_GetStringWidth` reproduces. Vanilla fills about
  90 slots (digits, letters, some symbols); the rest are free for a mod's own glyphs. All English
  UI shares this one font, and SIS files carry no Latin glyphs.
- **`0x4000` and up: the per-SIS bank** from entries 0 and 1 of the Text's SIS slot (`Text.sis_id`),
  same stride and kerning layout, indexed by `(code - 0x4000) & 0xff`. `SisSmmenu.dat` carries
  about 220 kana; the checklist and select files carry one icon each.

Since dispatch is per character, pointing a slot's entries 0 and 1 at custom glyphs gives one
Text its own font without affecting anything else on screen. A glyph size other than 32x32 needs
a binary patch: the `li r5,32; li r6,32` pairs feeding `GXInitTexObj` at 0x8045245c and
0x80452488, plus the `rlwinm` that multiplies the glyph index by 0x200.

## The stream

Text data is a byte stream from `text->text_start`. Bytes below 0x20 are opcodes; a byte at or
above 0x20 starts a 2-byte big-endian glyph code. hoshi's `TextCmdOpcode` enum lists every opcode
with its size. `Text_GX` (0x804516e4) draws, `Text_DetermineHeightAndWidth` (0x80451344) measures
and `Text_StorePremadeText` (0x8044f9d4) counts subtexts, all through a jump table at 0x8050983c.

| Group | Opcodes |
|---|---|
| Structure | `0x00` TERMINATE, `0x01` SUBTEXT_RESET, `0x02` SUBTEXT_BREAK, `0x07` POS (subtext header: s16 x, s16 y), `0x08` JUMP and `0x09` CALL (absolute pointers) |
| Layout | `0x03` LINEBREAK, `0x04` LINEBREAK_REFLOW, `0x1a` SPACE, `0x0a`/`0x0b` POSPUSH/POSPUSHEND |
| Style | `0x0c`/`0x0d` COLOR push/pop, `0x0e`/`0x0f` SCALE (u16 over 256), `0x10`/`0x12`/`0x14` align center/left/right with `0x11`/`0x13`/`0x15` popping, `0x16`/`0x17` kerning, `0x18`/`0x19` fit |
| Timing | `0x05` DELAY (u16 frames), `0x06` TIMING (u16 char, u16 space delays) |

DELAY carries 2 operand bytes, COLOR 3, and TIMING, POS, JUMP, CALL, POSPUSH and SCALE 4; every
other opcode is a bare byte. A walker steps by the operand count plus one, or two for a glyph;
hoshi's `Text_NextOpcode` is that step, and `Text_GetSubtext` / `Text_GetCommand` build on it. A
linear walk cannot cross JUMP or CALL, which the renderer follows by pointer. The game's own
`Text_SetScale` (0x80450774) walks with a size table that disagrees with the renderer on several
opcodes; hoshi replaces it with a walker that agrees.

Style opcodes push onto a per-Text state stack (`Text_PushState`, 0x80450828 / `Text_PopState`,
0x8045111c, buffer at `+0x68`), which is what lets them nest and lets TERMINATE double as the
return from a CALL. They change the renderer's `temp.*` mirrors, never the public `use_aspect`,
`kerning` and `align` fields. Rendering stops at TERMINATE, not at `text_end`, which is the
typewriter's frontier.

Every City Trial event banner uses one layout:

```
12              ALIGNLEFT
18              FIT_ON
16              KERNING_ON
0c BB BB BB     COLOR light grey
0e 00 B3 00 B3  SCALE ~0.70
[body]          glyph codes, 0x1a between words
03 0f 0d 17 19 13 00   LINEBREAK, SCALE_POP, COLOR_POP, KERNING_OFF, FIT_OFF, ALIGN_POP, TERMINATE
```

## Composing text

hoshi's `Text_CharToCommand` maps ASCII to glyph codes: `0x2000 + (c - '0')` for digits,
`0x200a + (c - 'A')` for capitals, `0x2024 + (c - 'a')` for lowercase, and a table for
`space ! " # $ % & ( ) * + , - . / : ; = ? @ _`; anything else, including the apostrophe, has no
code. `Text_WriteSisChar` / `Text_WriteSisString` write a string into a buffer that way (a space
as SPACE, a newline as LINEBREAK), stopping short of the end the caller passes. In pre-composed
data, gaps between words are the SPACE opcode, not the space glyph.

`Text_AddSubtext(text, x, y, fmt, ...)` (0x8044fec4) appends a subtext:

```
07 XH XL YH YL       POS
0c CR CG CB          COLOR (text->color RGB)
0e SXh SXl SYh SYl   SCALE
[converted body]
0f 0d 00             SCALE_POP, COLOR_POP, TERMINATE
```

The trailer's 0x00 is overwritten by the next `Text_AddSubtext`, so a chain of subtexts ends in
one TERMINATE and `Text_GetSubtext` can reach every index. `Text_SetText(text, idx, fmt, ...)`
(0x8045031c) replaces only subtext `idx`'s body, so its colour, position and scale survive. To
change those, `Text_SetColor`, `Text_SetSubtextScale` and `Text_SetSubtextPos` patch the header in
place.

Both run `vsnprintf` into a stack buffer and then `Text_ConvertASCIIToShiftJIS` (0x8044fb0c).
Letters become `0b` plus a glyph code, digits and `.`/`:` a tight-spacing POSPUSH prefix plus the
code, and `" ' , -` genuine Shift-JIS codes (`0x8140`, `0x8168`, `0x8143`, `0x817c`). The
converter reads at most 128 input bytes and silently drops the rest. `\n` and `\t` produce
nothing, so multi-line text is one `Text_AddSubtext` per line.

In vanilla the converter's output overlaps its input on the stack, and since it writes more than
it reads, a long string re-reads its own output and emits garbage, including stray SUBTEXT_RESET
bytes. hoshi redirects both calls (0x80450474, 0x8044ff8c) to a static buffer sized for the worst
case, so the 128-byte input cap is the only limit left; the `vsnprintf` ahead of it is unbounded
into about 132 bytes of stack, so formatted text must stay under that too.

## Colour

`Text_GX` sets one TEV stage per draw: RGB comes entirely from TEV register 1, and alpha is the
glyph's I4 value times the register's alpha, blended `SRCALPHA` / `INVSRCALPHA`. Per character it
loads `temp.color` into the register. So RGB can change per character through the COLOR stack,
but alpha is fixed for the whole draw from `text->color.a`. If `viewport_color.a` is nonzero a
flat background quad is drawn first. There is no outline, shadow or gradient stage; vanilla
"outlines" are two stacked Texts, the darker one offset by a pixel.

## Typewriter reveal

The renderer has a built-in reveal that costs nothing per frame. `temp.char_delay` (+0x90) is the
pause after each revealed glyph and `temp.space_delay` (+0x92) after a SPACE or LINEBREAK. Each
render, glyphs up to `temp.reveal_count` (+0x98) draw at once; at the frontier the renderer draws
one more, sets `wait_countdown` (+0x94) from the delay, moves `text_end` (+0x60) past it, and
pauses there for that many renders. POS and COLOR cost no step. A delay of 0 reveals everything
at once.

The only write to `temp.char_delay` inside the renderer is in the SUBTEXT_RESET / SUBTEXT_BREAK
handler, which copies `char_delay_init`. A `Text_AddSubtext` buffer contains neither opcode, so
runtime-composed text must set `temp.char_delay` and `temp.space_delay` itself; the renderer
reloads them every render and never clears them, so one write at creation holds. For the same
reason the reveal state is never reset between POS-delimited subtexts, and a multi-subtext buffer
reveals in sequence, one counter walking through all of them. Writing `text_end` from outside does
not drive a reveal.

## Canvases and rendering

Text canvases are orthographic 640x480 in pixels (`TEXT_CANVAS_W` / `TEXT_CANVAS_H`): x runs
right from 0, and the renderer negates Y, so `trans.y` is pixels below the top edge.

`Text_CreateCanvas(sis_idx, no_create_cam_gobj, entity_class, plink, priority, gxlink, gxpri,
cobj_gxpri)` (0x8044f674) chains a `TextCanvas` onto `stc_textcanvas_first` (0x805de56c) and, unless
`no_create_cam_gobj` is set, creates its camera GObj from the text camera descriptor at 0x805096a0
with `CObj_SetOrtho(0, -480, 0, 640)`. It returns the canvas's index among those bound to that SIS
slot. The canvas's GX link decides which Text GObjs draw under it.

`Text_GX(gobj, pass)` handles pass 0, the camera setup through `CObjThink_Common` (0x8042a29c),
and pass 2, the per-Text draw: return if `hidden` or `text_start` is NULL, set the Z mode, matrix,
vertex format, TEV and blend, call `render_callback`, draw the background, then interpret the
stream.

Fields worth knowing in hoshi's `Text`:

- `viewport_scale` multiplies everything: glyph size, advance, background and scissor frame.
  Around 0.4 reads as HUD-sized text.
- `aspect` is the box used by `use_aspect` auto-shrink, the background quad and the scissor frame.
- The scissor fields (gated by `is_scissor`) clip per quad, shrinking edge quads' UVs; they do not
  call `GXSetScissor`.
- `render_callback` (+0x58) is called once per render after GX setup and before drawing, with the
  Text's GObj. From it a mod can animate `color`, `trans`, `scale` or `viewport_scale`, read
  `temp.reveal_count` for per-glyph effects, install its own TEV for outlines or gradients, or bind
  another texture. Replacing the whole `gobj->gx_cb` goes further: hoshi's `TextJoint_Create`
  premultiplies the view matrix by a bone's transform to anchor text in 3D.

## Text from a mod

hoshi creates a shared overlay canvas each scene on SIS slot 1 and GX link 63
(`HOSHI_SCREENCAM_GXLINK`), with a camera priority of 63 so it draws last and no scene camera
culls it. `Hoshi_CreateScreenText()` returns a fresh `Text *` on it. The usual setup is
`kerning = 1`, a pixel-space `trans`, `viewport_scale` near 0.4, a `color`, and a
`viewport_color` whose alpha decides whether a background box shows; then
`Text_AddSubtext(t, x, y, "")` to make the slot and `Text_SetText(t, 0, string)` to fill it.
`Text_GetWidthAndHeight(t, 0, &w, &h)` measures it for `aspect`, and `Text_Destroy` frees it.

Leave `use_aspect` at 0 unless a hard width limit is wanted: it shrinks a subtext horizontally to
fit `aspect.X`, so on text whose `aspect` was just measured it can only shave it. Run user strings
through hoshi's `Text_Sanitize(in, out, size)` first; it pre-converts punctuation to the Shift-JIS
codes the converter expects (two output bytes per symbol, one per other character) and truncates
with a terminator.

| Want | Status |
|---|---|
| Per-character alpha | Not possible; the COLOR opcode is RGB only |
| `\n` in a format string | Not possible; one subtext per line |
| Gradient, outline, drop shadow | A custom TEV from `render_callback`, or stacked Texts |
| New Latin glyphs or symbols | Write a 0x200-byte I4 glyph and two kerning bytes into a free master-bank slot |
| A font for one Text | Point its SIS slot's entries 0 and 1 at custom glyphs and use codes from 0x4000 |
| Text on a 3D bone | Swap `gobj->gx_cb`, as `TextJoint_Create` does |

## City Trial event banners

During City Trial, `SisCitytrial.dat` sits in slot 0 and event banners are shown with
`Text_InitPremadeText`. The entry comes from the int table at 0x804a7b98
(`stc_event_sis_id_table`), read by `CityEvent_HudPredictionShow` (0x80127624),
`CityEvent_HudPredictionThink` (0x801276c0) and `stadiumPrediction` (0x80127864), each through a
`lis`/`addi` pair. For event kinds 0-15 the table holds `kind + 2`. The stadium prediction event
writes `stadium_kind + 16` back into the event-check kind field and looks that index up on its
next pass, so indices 16-39 are live too, one per stadium kind; the table has 40 entries and
vanilla data follows the three zero words after it. Showing custom banners therefore means
copying the table into a larger array, repointing those three loads, and extending slot 0's entry
array (hoshi's `Text_ExtendSis` copies `stc_sis_data[slot]` into a caller's array and repoints
the slot) so the new ids have text to name.
