# SIS Text System

SIS (String Image Set) files are HSD archives holding pre-composed text strings for in-game UI. The runtime renders those, and ad-hoc C-format strings, through a single GX path that walks a stream of opcodes, looks up 32x32 I4 glyph bitmaps, and emits one textured quad per character.

A few entry points carry one name in the symbol map and another in `link.ld` / `externals/hoshi/include/text.h`; mod code links against the `link.ld` names. `Text_CreateTextCanvas` is exported as `Text_CreateCanvas` (0x8044f674), `Text_GXLink` as `Text_GX` (0x804516e4), and `Text_Create` as `Text_CreateText` (0x8044fa70).

## Rendering paths

Three unrelated text paths exist in the binary:

1. **SIS text** - the main UI path. Every menu, HUD, dialogue, results, event and checklist string goes through `Text_GXLink` (0x804516e4) as texture-mapped 32x32 I4 glyphs.
2. **DevText** - a debug-only stroke (vector) font drawn with `GX_LINES`, used by `3DDebug_*` and the developer menus. 7-bit ASCII, 10x16 px cells.
3. **OS font (IPL)** - the GameCube SDK font (`OSInitFont` 0x803d6e20, `OSGetFontTexture` 0x803d6f00, `OSGetFontTexel` 0x803d676c), driven by the `OSText_DrawString` family (0x8038bf64). Used only by the Top Ride 2D debug menu and CSS overlays. Shift-JIS aware; mojibakes on NA without an encoding-cache pin. `OSLoadFont` is absent from this build. Not reachable from a `Text` element.

## Loading

`Text_LoadSisFile(slot, filename, symbol)` (0x8044f800) loads an HSD archive into one of five slots. The archive pointer lands in `stc_sis_archives[5]` (0x8059a848) and the relocated pointer array in `stc_sis_data[5]` (0x8059a85c); both are declared in `text.h`. Within a slot's array, `[0]` is the image data pointer, `[1]` the kerning data pointer, and `[2]` onward the text entries. Different scenes load different files into slots 0-4.

## Glyph banks

The renderer dispatches per character on the high bits of the 16-bit code.

Codes `0x2000`-`0x3FFF` use the **master Latin bank** baked into `main.dol .data5`: images at `0x8050a040` (256 slots of `0x200` bytes each, I4 32x32, indexed by `(code - 0x2000) & 0xFF`) and kerning at `0x80509dc0` (320 x 2 bytes, `{u8 left_pad, u8 right_edge}`, running up to the image bank). Effective drawn width is `34 - left_pad - right_edge`, which is what `Text_GetStringWidth` in `text.h` reproduces. Vanilla populates roughly 90 slots (digits, A-Z, a-z, 22 scattered symbols up to `0x21xx`); the other ~165 are empty memory that a mod can write its own glyphs into. **All English UI in the game shares this one font.** SIS files carry no Latin glyphs at all.

Codes `0x4000` and up use the **per-SIS bank** taken from the loading slot's `SISData` (`image_data_arr` / `kerning_data_arr`), same `0x200` stride and same 2-byte kerning layout, indexed by `(code - 0x4000) & 0xFF`. Only a few files supply one:

| SIS file | per-SIS image block | contents |
|----------|---------------------|----------|
| `SisSmmenu.dat` | ~`0x1B800` (~220 glyphs) | Japanese kana for sub-menu rule descriptions |
| `SisClrChk2D/3D/CT.dat` | `0x200` | checkbox icon |
| `SisSelply*.dat`, `SisSelrule.dat` | `0x200` | bullet icon |
| all others | none | Latin-only, master bank |

Both banks reach GX through `GXInitTexObj` calls with literal `r5=32, r6=32`: master image at 0x80452478 (texel base `r28`, address computed at 0x80452464), per-SIS image at 0x804524a4 (texel base `r26`, computed at 0x80452490).

The DevText stroke font is a separate blob at `0x805053f8`: a variable-length per-character stroke list terminated by `0xFF`, each byte packing two 4-bit nibbles as `(x << 4) | y` on a 16x16 logical grid, consumed in pairs as line-segment endpoints. `DevelopText_DrawStrokeGlyph` (0x80438898) emits them under `GXBegin(GX_LINES)` for the 3D debug text helper at 0x8007e964 and for `DevelopText_Create` / `DevelopText_AddString` (0x800ab2d4 / 0x800ab78c). It is not OS_FONT.

## The opcode stream

Text data is a byte stream parsed from `text->text_start`. Bytes below `0x20` are opcodes; bytes `0x20` and up begin a 2-byte big-endian glyph code. The authoritative list is the `TextCmdOpcode` enum in `externals/hoshi/include/text.h`, which carries each opcode's byte size and operands. Three consumers interpret it: `Text_GXLink` (0x804516e4) draws, `Text_DetermineHeightAndWidth` (0x80451344) measures, and `Text_StorePremadeText` (0x8044f9d4) counts subtexts. Dispatch is a jump table at `0x8050983c` covering `0x00`-`0x1a`; glyphs and the `0x1b`-`0x1f` no-ops fall to the handler at 0x80452210.

Functionally the opcodes group as:

- **Structure**: `0x00` TERMINATE, `0x01` SUBTEXT_RESET, `0x02` SUBTEXT_BREAK, `0x07` POS (the subtext header: s16 x in pixels right of canvas-left, s16 y in lines down), `0x08` JUMP and `0x09` CALL (both take an HSD-relocated absolute pointer).
- **Layout**: `0x03` LINEBREAK (advances `cursor.y` by `16 * scale_y * viewport_scale.y`), `0x04` LINEBREAK_REFLOW, `0x1a` SPACE (advances `cursor.x` by `scale_x * (32 + 16) * fit_squeeze`), `0x0a`/`0x0b` POSPUSH/POSPUSHEND for inline relative repositioning in 1/256 units, gated by `text->pospush_flags`.
- **Style**: `0x0c`/`0x0d` COLOR, `0x0e`/`0x0f` SCALE (operands are u16 fixed-point over 256), `0x10`/`0x12`/`0x14` align center/left/right with `0x11`, `0x13` and `0x15` all aliasing the same pop, `0x16`/`0x17` kerning on/off, `0x18`/`0x19` aspect-fit on/off.
- **Timing**: `0x05` DELAY (u16 frames into `temp.wait_countdown`), `0x06` TIMING (u16 char then u16 space, operand order at 0x80451e20; it updates the renderer's working registers and `temp.space_delay`, not `temp.char_delay`).

The style opcodes mutate the `temp.*` mirrors, never the public `use_aspect` / `kerning` / `align` fields at `+0x48`-`+0x4a`.

### State-history stack

`Text_PushState(text, value_ptr, kind)` (0x80450828) and `Text_PopState(text, kind)` (0x8045111c) maintain a lazily allocated per-Text buffer at `text->state_stack` (`+0x68`). `kind` is `1=POS`, `2=COLOR`, `3=SCALE`, `4=ALIGN`, `5=int` (the CALL return marker); bit `0x80` means pop without writing back, which the measuring pass uses. This is what makes the style opcodes nest properly, and what lets `0x00` TERMINATE double as "return from CALL": it pops a `kind=5` marker if one is on the stack and only ends rendering when there is none.

### Where parsing stops

The interpreter halts on the `0x00` TERMINATE byte, **not** on `text->text_end`. `text_end` is the typewriter reveal frontier that the renderer sets itself; the loop head at 0x80451c44 consults it only to decide whether to pause. Writing `text_end` from outside is therefore not a way to drive a reveal.

### Standard event text layout

Every City Trial event string uses one template, which `ComposeSisText` in `mods/custom_events/src/custom_events.c` reproduces byte for byte for runtime-composed text:

```
12              ALIGNLEFT
18              FIT_ON
16              KERNING_ON
0c BB BB BB     COLOR light gray
0e 00 B3 00 B3  SCALE ~0.70x
[body]          2-byte glyph codes, 0x1a between words
03 0f 0d 17 19 13 00   LINEBREAK, SCALE_POP, COLOR_POP, KERNING_OFF, FIT_OFF, ALIGN_POP, TERMINATE
```

## Character codes

`Text_CharToCommand` in `text.h` maps ASCII to SIS codes: `0x2000 + (c - '0')` for digits, `0x200a + (c - 'A')` for capitals, `0x2024 + (c - 'a')` for lowercase, and a 21-entry symbol table for `space ! " # $ % & ( ) * + , - . / : ; = ? @ _`. Anything else, including the apostrophe, returns -1 and gets dropped by callers such as `ComposeSisText`.

In pre-composed SIS data, gaps between words are the `0x1a` SPACE opcode, not the `0x20e3` space glyph.

## Color pipeline

`Text_GXLink` sets one TEV stage once per draw (from 0x80451a10):

- `GXSetTevColorIn(0, 0xF, 0xF, 0xF, 2)` at 0x80451a1c, so RGB comes entirely from TEV register 1 with no texture color contribution.
- `GXSetTevAlphaIn(0, 7, 4, 1, 7)`, so glyph alpha from the I4 texture is modulated by the register's alpha.
- `GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, 0)` and `GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0)`.

Per character (0x804524c4) it writes `GXSetTevColor(GX_TEVREG1, &temp.color)` and then `GXBegin(GX_QUADS, fmt 0, 4)` with four position + s/t vertices.

The consequence: **RGB is per character**, driven by the COLOR opcode stack, but **alpha is constant for the whole draw**, sourced from `text->color.a` at subtext init. The `0x0c` opcode never touches alpha. If `text->viewport_color.a` is non-zero, a flat background quad covering `aspect * viewport_scale` is drawn first.

There is no gradient, outline or shadow stage. Vanilla menu "outlines" are two stacked Text GObjs with the darker one offset by a pixel. A real outline or gradient requires installing a custom TEV from `render_callback`.

## Composing from C format strings

`Text_AddSubtext(text, x_pixels, y_lines, fmt, ...)` (0x8044fec4) appends a subtext and emits exactly:

```
07 XH XL YH YL       POS (x, y truncated to int)
0c CR CG CB          COLOR (text->color RGB; alpha not emitted)
0e SXh SXl SYh SYl   SCALE
[converted body]
0f 0d 00             SCALE_POP, COLOR_POP, TERMINATE
```

`Text_SetText(text, subtext_idx, fmt, ...)` (0x8045031c) walks the buffer to the N-th `0x07` and replaces only the body, so per-subtext color, position and scale survive repeated `SetText` calls.

Both first `vsnprintf` into a stack buffer, then run `Text_ConvertASCIIToShiftJIS` (0x8044fb0c). The name is only half right: letters and digits become SIS glyph codes (`0x20xx`), but a handful of punctuation marks become genuine full-width Shift-JIS codes (`0x81xx`).

| ASCII input | Emitted |
|-------------|---------|
| `A-Z` | 2-byte code `0x200a + (c - 'A')` |
| `a-z` | 2-byte code `0x2024 + (c - 'a')` |
| `0-9` | `0a F4 00 00 00` (POSPUSH, tight-spacing mode) then the digit code |
| space | `0a F4 00 00 00` then a kerning lookup |
| `.` `:` | tight-mode prefix then the symbol code |
| `"` | `0b` then Shift-JIS `0x8140` (0x8044fbb0) |
| `'` | `0b` then Shift-JIS `0x8168` (0x8044fbd4) |
| `,` | `0b` then Shift-JIS `0x8143` (0x8044fbfc) |
| `-` | `0b` then Shift-JIS `0x817c` (0x8044fc24) |
| other printable | `0b` then a symbol code from the tables at `0x80509b40` / `0x805098c0` |

**The converter reads at most 128 input bytes.** Its loop bails once the input index passes `0x7f`, so anything beyond that is dropped without a return code saying so - and since both `Text_SetText` and `Text_AddSubtext` route through it, that is the hard ceiling on one subtext's text. It is a byte limit, not a character one: pre-sanitized punctuation already costs 2 bytes apiece, so a symbol-heavy string hits it well before 128 characters. Measuring with `Text_GetWidthAndHeight` afterwards measures only what survived, so a caller that hands over more than fits gets a width for text that never rendered.

**`\n` and `\t` are not mapped.** They fall into the table-lookup branch and produce nothing, so multi-line text must be built from separate `Text_AddSubtext` calls, one `0x07` header each. `%d`, `%s` and `%f` work normally because `vsnprintf` resolves them first.

There is no brace syntax for inline opcodes. To change color or scale mid-buffer, write opcode bytes directly or call `Text_SetColor` / `Text_SetScale`, which patch the per-subtext header bytes located by `Text_GetCommand` (`text.h`). Position is the `0x07` header itself rather than an inline opcode, so `Text_SetSubtextPos` rewrites its two `s16` fields in place.

### Chained subtexts share one TERMINATE

Each `Text_AddSubtext` writes its `0f 0d 00` trailer at the heap cell's write pointer **without advancing past the `0x00`** (0x804502d8-0x804502dc). The next call overwrites that `0x00` with its own `0x07` header. So a chain of N subtexts contains exactly one `0x00`, after the last one. That is what lets `Text_GetSubtext` (which stops walking at `0x00`) reach indices above 0, and it means the real buffer end can only be found by scanning from `text_start` for the trailing `0x00` - `Text_AddSubtext` never writes `text_end`.

## Typewriter reveal

The engine has a built-in dwell-paced reveal that costs nothing per frame: set the per-glyph dwell in frames and the renderer uncovers one glyph at a time on its own, with no buffer mutation. `0` reveals everything instantly.

Inside `Text_GXLink`, subtext setup loads `temp.reveal_count` (`+0x98`) at 0x80451c3c and `temp.char_delay` / `temp.space_delay` (`+0x90` / `+0x92`) into working registers at 0x80451c34. The opcode loop decrements the reveal counter for each already-revealed glyph while still drawing it; when the counter hits zero it has reached the frontier, so it draws that glyph, increments `reveal_count`, copies `char_delay` into `wait_countdown` (`+0x94`), and points `text_end` (`+0x60`) just past the glyph (0x80452618-0x80452640). SPACE reveals identically using `char_delay` (0x804521f4); LINEBREAK and DELAY consume steps too, using `space_delay` or their own operand. POS and COLOR are processed for free without consuming a step. On the next render the loop head pauses while `parse_ptr == text_end && wait_countdown != 0`, decrementing once per render (0x80451c44-0x80451c78), and releases after `char_delay` frames.

**The `char_delay_init` trap.** The only write to `temp.char_delay` in the renderer is at 0x80451cec, inside the `0x01`/`0x02` SUBTEXT handler. `Text_AddSubtext` buffers are delimited by `0x07` POS headers and contain no `0x01` or `0x02`, so that copy never fires: `temp.char_delay` stays 0, every reveal sets `wait_countdown = 0`, and the whole buffer appears on frame one. Runtime-composed text must seed `temp.char_delay` (and `temp.space_delay`) directly. The renderer reloads them at every render top and never clears them, so a single write at creation persists.

The same asymmetry explains why a multi-segment buffer reveals **sequentially rather than in parallel**: the reveal state is reset only by the `0x01`/`0x02` handlers (0x80451cb4 / 0x80451d3c), and the `0x07` handler (0x80451e30) touches none of it, so one counter walks straight through every segment.

`TextBox_ApplyTypewriter` in `mods/textbox/src/textbox.c` is the reference user: it seeds both the `_init` fields and the live `temp` fields from the typewriter-speed setting, and zeroes `temp.reveal_count` and `text_end` for a clean start.

## Canvas, GObj and render pipeline

### Coordinate system

Text canvases are orthographic 640x480 in raw pixels: x runs `[0, 640]` left to right, y runs `[0, -480]` top to bottom because the projection bottom is `-480` and the renderer negates per vertex. Code stores positive Y, so `trans.y` is pixels **above canvas bottom** - set `trans.y = 480 - desired_top_offset` to position from the top.

### Pass model

`Text_GXLink(gobj, pass)` handles two passes and early-returns on anything else.

Pass 0 is camera-level setup, reached through `CObjThink_Common` (0x8042a29c): `GXSetViewport(0,0,640,480)`, `GXSetScissor(0,0,640,480)`, and `C_MTXOrtho(0,-480,0,640,0,2)`.

Pass 2 is the per-Text draw. It pulls `Text *t` from `gobj->userdata`, early-returns if `hidden` or `text_start` is null, picks the Z mode from `is_depth_compare`, loads the view matrix, sets vertex format, TEV and blend state, **invokes `render_callback`**, draws the optional `viewport_color` background, and then runs the opcode interpreter.

### Canvas creation

`Text_CreateCanvas(sis_idx, no_create_cam_gobj, gobj_entityclass, gobj_plink, gobj_ppriority, gxlink, gxpri, cobj_gxpri)` (0x8044f674):

1. Allocates a `TextCanvas` and chains it onto `stc_textcanvas_first` (0x805de56c).
2. Creates a GObj on the given entity class / plink / priority.
3. `HSD_CObjLoadDesc` on the canonical text-camera descriptor at `0x805096a0`, then `HSD_CObjSetOrtho(0, -480, 0, 640)`.
4. `GObj_AddObject(g, COBJ, cobj)` and `GObj_InitCamera(g, CObjThink_Common, cobj_gxpri)`, which registers the pass-0 viewport/scissor callback for every gxlink beneath it.

The canvas's gxlink mask decides which Text GObjs render under it.

### Text fields worth knowing

The `Text` struct is fully described in `externals/hoshi/include/text.h`. The parts that surprise people:

- `scissor_top/bot/left/right` gated by `is_scissor` are a **per-quad** clip, not `GXSetScissor`. Quads fully outside are dropped and edge quads have both UVs and vertices shrunk (0x80452200-0x804523f0).
- `viewport_scale` multiplies everything at once: glyph quad size, X advance, background rect and the scissor reference frame. Around 0.4 gives readable HUD-size text; the default is 1.0.
- `aspect` is the bounding box used by `use_aspect` auto-shrink, the background quad size and the scissor reference.
- `sis_id` selects which `stc_sis_data` slot supplies the per-SIS glyph bank for codes at or above `0x4000`.
- `text_end` is engine-owned (see above). `Text_InitPremadeText` (0x8044f8c8) and `Text_LoadSisFile` are the only functions that write it, and both write 0.
- `reflow_flag` (`+0x4b`) is internal to `0x04` LINEBREAK_REFLOW; leave it at 0 from mod code.

### render_callback

`text->render_callback` (`+0x58`) is loaded at 0x804519f4, null-checked, and called at 0x80451a08 with the Text's GObj, once per render, after GX state setup and before any drawing. From inside it you can:

- Mutate `t->color`, `t->temp.color`, `t->trans`, `t->scale` or `t->viewport_scale` for blink, fade, jitter, breathing or wave effects.
- Override the TEV state to build outline, shadow or gradient pipelines the vanilla single stage cannot express.
- Read `t->temp.reveal_count` to drive per-glyph effects.
- Bind a different texture with `GXLoadTexObj` to give one element its own font.

For deeper changes the whole `gobj->gx_cb` can be replaced: `TextJoint_Create` in `externals/hoshi/Lib/text_joint/text_joint.c` swaps it so the view matrix is premultiplied by a bone transform before delegating to the normal draw, which is how text gets anchored in 3D.

## Screen-space overlays from a mod

hoshi reserves GX link bit 63 (`HOSHI_SCREENCAM_GXLINK` in `externals/hoshi/include/hoshi/screen_cam.h`) for a shared overlay canvas so it is never culled by scene cameras. `ScreenCam_Create` in `externals/hoshi/src/screen_cam.c` creates it once per scene with `Text_CreateCanvas(1, 0, 0, 0, 0, HOSHI_SCREENCAM_GXLINK, 0, 63)`; `cobj_gxpri = 63` makes that camera draw last.

Per message: `Text_CreateText(sis_idx, canvas_idx)` returns a fresh `Text *`, which typically wants `kerning = 1`, a pixel-space `trans`, `viewport_scale` around 0.4, a `color`, and a `viewport_color` whose alpha decides whether a background box appears. Content goes in with `Text_AddSubtext(t, x, y, "")` to allocate the slot followed by `Text_SetText(t, 0, string)`. `Text_GetWidthAndHeight(t, 0, &w, &h)` then gives the values to store into `t->aspect`. `Text_Destroy(t)` tears it down.

`use_aspect` is a separate decision and defaults to 0: it makes the renderer shrink a subtext horizontally to fit `aspect.X`, so set it only when a hard width limit is wanted. Code that measures its own layout and then stores the result into `aspect` should leave it clear - the flag can only shave the text it just fitted. `aspect` still drives the `viewport_color` background rect and the scissor reference frame either way.

Run user strings through `Text_Sanitize(in, out, size)` (`externals/hoshi/Lib/text_joint/text_joint.c`) first: it pre-converts ASCII punctuation to the Shift-JIS codes the game's converter expects, spending 2 output bytes per converted symbol and 1 per alphanumeric or unlisted character. It reserves one byte for the terminator, so a `char[128]` holds at most 126 bytes of output; past that it truncates, terminates and returns 0.

`mods/textbox/src/textbox.c` is the working example of all of this.

## What the path can and cannot do

Per-character RGB, multi-line layout (as separate subtexts), alignment, variable scale, background rectangles, scissor clipping, typewriter reveal, whole-element fades and per-frame `render_callback` motion all work as described above. The constraints worth knowing before designing around them:

| Want | Status |
|------|--------|
| Per-character alpha | Not possible. Alpha is fixed for the whole draw from `text->color.a`; the COLOR opcode is RGB only. |
| `\n` in a format string | Not possible. Use one `Text_AddSubtext` per line. |
| Gradient (top-vs-bottom color) | Needs a replacement TEV installed from `render_callback`; vanilla has one stage and one color register. |
| Drop shadow / outline | Same. The vanilla trick is stacking two offset Text GObjs. |
| Glyph cell other than 32x32 | Needs a binary patch: the `li r5,32; li r6,32` pairs at 0x8045245c / 0x80452488 feeding `GXInitTexObj`, plus the `rlwinm r0, r31, 9, 7, 22` that multiplies the glyph index by `0x200`. |
| New Latin glyphs or symbols | Supported. ~165 unused slots in the master image table at `0x8050a040`; write a `0x200`-byte I4 32x32 glyph plus 2 kerning bytes and emit the raw code. |
| A different font for one element | Supported. Point a SIS slot's `image_data_arr` / `kerning_data_arr` at custom glyphs and compose with codes at or above `0x4000`. Dispatch is per character, so other text on screen is unaffected. This is the same mechanism `SisSmmenu.dat` uses for kana. |
| Text anchored to a 3D bone | Supported by swapping `gobj->gx_cb`, as `text_joint.c` does. |

## SIS files

21 SIS archives ship in `iso/files/`. Entry counts include the two data pointers at indices 0-1; text entries start at index 2.

| File | Entries | Contents |
|------|---------|----------|
| `SisBestrap.dat` | 42 | boot strap / startup |
| `SisCitytrial.dat` | 42 | City Trial event + prediction text |
| `SisClrChk2D.dat` | 158 | Top Ride checklist reward names |
| `SisClrChk3D.dat` | 171 | Air Ride checklist reward names |
| `SisClrChkCT.dat` | 169 | City Trial checklist reward names |
| `SisDialogue.dat` | 33 | in-game dialogue |
| `SisEnding2d.dat` | 55 | Top Ride ending |
| `SisEnding3d.dat` | 92 | Air Ride ending |
| `SisLan.dat` | 39 | LAN mode |
| `SisMenu.dat` | 123 | main menu |
| `SisProgressive.dat` | 7 | progressive scan setup |
| `SisResultCt.dat` | 10 | City Trial results |
| `SisSelmap.dat` | 10 | Air Ride stage select |
| `SisSelmap2d.dat` | 8 | Top Ride stage select |
| `SisSelply.dat` | 48 | Air Ride player/machine select |
| `SisSelply2d.dat` | 8 | Top Ride player select |
| `SisSelplyCt.dat` | 48 | City Trial player/machine select |
| `SisSelrule.dat` | 59 | rule settings |
| `SisSelstadium.dat` | 54 | stadium select |
| `SisSmmenu.dat` | 3 | sub-menu (carries the kana glyph block) |
| `SisStadiumTitle.dat` | 26 | stadium title cards |

`SisSelply.dat` and `SisSelplyCt.dat` share a layout: entries 2-7 are screen furniture, 8-27 the 20 machine names, 28-47 their descriptions. Both load into slot 0, and both screens turn a `CharacterKind` into that index pair through tables of words read as signed bytes - `0x804aa3d8` / `0x804aa428` for `AirRideSelect_SetMachineText` (0x80153d2c), `0x804aa598` / `0x804aa5e8` for `CitySelect_SetMachineText` (0x8015e740). An index of -1 in either table suppresses both texts.

### Scene to slot map

`Text_LoadSisFile` call sites, by address band:

| Band | Scene | Files |
|------|-------|-------|
| `0x800ad0c8` | pre-game / mode menu | varies |
| `0x80116xxx` | City Trial event HUD | `SisCitytrial.dat` (slot 0) |
| `0x8013bxxx`-`0x8013fxxx` | main menu, mode select, stadium select, character/machine select | `SisMenu.dat`, `SisSelmap.dat`, `SisSelstadium.dat`, `SisSelply.dat` |
| `0x80140xxx`-`0x80146xxx` | rule / options / sub-menus | `SisSelrule.dat`, `SisSmmenu.dat` |
| `0x8017cxxx`-`0x8017dxxx` | results / ending | `SisResultCt.dat`, `SisEnding2d/3d.dat` |
| `0x80182xxx` | checklist (three loads in one function) | `SisClrChk3D/2D/CT.dat` |
| `0x80186xxx`-`0x80187xxx` | LAN mode | `SisLan.dat` |
| `0x8027cxxx`-`0x80283xxx` | Top Ride | `SisSelmap2d.dat`, `SisSelply2d.dat` |

Pre-composed text via `Text_InitPremadeText` dominates the results and checklist band; runtime composition via `Text_AddSubtext` / `Text_SetText` dominates the rule and menu band, where text is dynamic.

## City Trial event text

`SisCitytrial.dat` occupies slot 0 during City Trial. Event text is shown with `Text_InitPremadeText(text, sis_idx)`, where `sis_idx` comes from the table at `0x804a7b98`.

| Index | Usage | Text |
|-------|-------|------|
| 0 | image data | *(data pointer)* |
| 1 | kerning data | *(data pointer)* |
| 2 | EVKIND_DYNABLADE | The mystery bird Dyna Blade appeared! Aim for his head! |
| 3 | EVKIND_TAC | Tac stole items and fled the scene! He's hiding somewhere! |
| 4 | EVKIND_METEOR | DANGER! DANGER! Huge meteors are incoming! |
| 5 | EVKIND_PILLAR | A huge, unidentified pillar appeared! Bust it! |
| 6 | EVKIND_RUNAMOK | All Air Ride machine energy tanks have run amok! |
| 7 | EVKIND_RESTORATIONAREA | A restoration area has appeared somewhere in the city! |
| 8 | EVKIND_RAILFIRE | The rail stations are all burning out of control! |
| 9 | EVKIND_SAMEITEM | No fair! The boxes all contain the same items! |
| 10 | EVKIND_LIGHTHOUSE | The city lighthouse has turned on! |
| 11 | EVKIND_SECRETCHAMBER | The secret chamber in Castle Hall is open! Get some items! |
| 12 | EVKIND_PREDICTION | (not used) |
| 13 | EVKIND_MACHINEFORMATION | Air Ride machine formation approaching! |
| 14 | EVKIND_UFO | A mysterious flying machine is approaching! |
| 15 | EVKIND_BOUNCE | The items are getting rubbery! They're bouncing! |
| 16 | EVKIND_FOG | A dense fog has covered the city! |
| 17 | EVKIND_FAKEPOWERUPS | Some power-up items are fakes! Be careful! |
| 18 | Prediction (STGROUP_DRAGRACE, sub 0) | Stadium Prediction: I see a simple, straight course... |
| 19 | Prediction (STGROUP_DRAGRACE, sub 1) | Stadium Prediction: I see a test of speed on a straight course... |
| 20 | Prediction (STGROUP_AIRGLIDER, sub 0) | Stadium Prediction: A faster machine will have an advantage, I feel... |
| 21 | Prediction (STGROUP_AIRGLIDER, sub 1) | Stadium Prediction: If you go for speed, you won't be let down... |
| 22 | Prediction (STGROUP_TARGETFLIGHT, sub 0) | Stadium Prediction: I sense you'll fly far, farther, faaarther... |
| 23 | Prediction (STGROUP_TARGETFLIGHT, sub 1) | Stadium Prediction: I see numbers on the side of a machine... |
| 24 | Prediction (STGROUP_HIGHJUMP, sub 0) | Stadium Prediction: The farther you can fly, the better you'll do... |
| 25 | Prediction (STGROUP_HIGHJUMP, sub 1) | Stadium Prediction: I feel that you'll be fighting numerous enemies... |
| 26 | Prediction (STGROUP_MELEE, sub 0) | Stadium Prediction: I sense countless enemies awaiting you at the castle... |
| 27 | Prediction (STGROUP_MELEE, sub 1) | Stadium Prediction: A machine with offensive power might be the ticket... |
| 28 | Prediction (STGROUP_DESTRUCTION, sub 0) | Stadium Prediction: It's time to think about strength rather than speed... |
| 29 | Prediction (STGROUP_DESTRUCTION, sub 1) | Stadium Prediction: I think you should spend time preparing for battle... |
| 30 | Prediction (STGROUP_SINGLERACE, sub 0) | Stadium Prediction: The occupants of the city may prove a hindrance... |
| 31 | Prediction (STGROUP_SINGLERACE, sub 1) | Stadium Prediction: The difficult terrain may change for the better... |
| 32 | Prediction (course hint 0) | Stadium Prediction: It will be nice to race in the air of the plains... |
| 33 | Prediction (course hint 1) | Stadium Prediction: I see you flying into a crater and it looks HOT... |
| 34 | Prediction (course hint 2) | Stadium Prediction: I see a machine that stirs up a wake of sand... |
| 35 | Prediction (course hint 3) | Stadium Prediction: You're headed for a place shrouded in cold and ice... |
| 36 | Prediction (course hint 4) | Stadium Prediction: I see a course with a huge Ferris wheel... |
| 37 | Prediction (course hint 5) | Stadium Prediction: I see a dark valley with raging rapids... |
| 38 | Prediction (course hint 6) | Stadium Prediction: I see a steel course. You're ready to go but can't... |
| 39 | Prediction (course hint 7) | Stadium Prediction: I can see a long, checkered course... |
| 40 | Prediction (course hint 8) | Stadium Prediction: I see a vast universe that erases all your cares... |
| 41 | Prediction (STGROUP_VSKINGDEDEDE) | Stadium Prediction: I see you meeting up with King Dedede... |

### The SIS id lookup table

`stadiumPrediction` (0x80127864) indexes `stc_event_sis_id_table` (`int *` at `0x804a7b98`, declared in `externals/hoshi/include/event.h`) by event kind to get the SIS entry index. For vanilla kinds 0-15 the value is simply `kind + 2`.

Entries from index 16 up are live, not padding. The prediction event (kind 10) computes `stadium_kind + EVKIND_NUM` (that is, `+16`; at 0x801279a4 / 0x801279b4), writes it back into the event-check struct's kind field at `+0x18`, and reads `table[that_index]` on the next pass. That claims indices 16 through 39, one per `STKIND_NUM`. **Index 40 (`EVKIND_NUM + STKIND_NUM`) is the first slot a mod may take.**

`CustomEvents_InitSis` in `mods/custom_events/src/custom_events.c` uses that: it copies the original 42-entry `stc_sis_data[0]` array into a static extended array, appends one `ComposeSisText`-built buffer per custom event at index 42 and up, repoints `stc_sis_data[0]` at the extended array, and writes `sis_id_table[40 + i] = 42 + i`. The vanilla `stadiumPrediction` path then displays custom text with no further hooking. The mod's `mod_desc` runs it from `.On3DLoadEnd` whenever the loaded stage is `STAGEKIND_CITY1`, so the entries are reinstalled on every City Trial load.

## HSD archive layout

SIS files are ordinary HSD archives:

```
0x00  4      file size
0x04  4      data section size
0x08  4      relocation entry count
0x0C  4      root node count (always 1 for SIS)
0x10  16     reserved
0x20  N*4    pointer array, offsets relative to 0x20
               [0] image data (usually null for Latin-only files)
               [1] kerning data (usually null)
               [2+] text entries
...          text entry data (opcode streams)
...          relocation table
...          root node table
...          symbol strings, e.g. "SIS_CityTrial"
```

`Text_LoadSisFile` relocates the offsets to absolute pointers and stores the resulting array in `stc_sis_data[slot]`.

## Address reference

### Functions

| Address | Symbol | Role |
|---------|--------|------|
| `0x8044edec` | `Text_AllocFromHeap` | text heap alloc |
| `0x8044efa8` | `Text_FreeAlloc` | text heap free |
| `0x8044f128` | `Text_CreateGObj` | create Text GObj with position params |
| `0x8044f350` | `Text_Destroy` | destroy a Text |
| `0x8044f5b4` | `Text_CreateHeap` | init the text heap |
| `0x8044f674` | `Text_CreateTextCanvas` (`Text_CreateCanvas`) | canvas + ortho camera |
| `0x8044f800` | `Text_LoadSisFile` | load a SIS archive into a slot |
| `0x8044f8c8` | `Text_InitPremadeText` | bind a SIS entry by index |
| `0x8044f9d4` | `Text_StorePremadeText` | parse and count subtexts |
| `0x8044fa70` | `Text_Create` (`Text_CreateText`) | create a Text under a canvas |
| `0x8044fb0c` | `Text_ConvertASCIIToShiftJIS` | ASCII to opcode/glyph stream |
| `0x8044fec4` | `Text_AddSubtext` | append a positioned subtext |
| `0x8045031c` | `Text_SetText` | replace a subtext body |
| `0x80450774` | `Text_SetScale` | patch a subtext's SCALE opcode |
| `0x80450828` | `Text_PushState` | push a state-history frame |
| `0x8045111c` | `Text_PopState` | pop a state-history frame |
| `0x80451344` | `Text_DetermineHeightAndWidth` | bounding-box measure pass |
| `0x804516e4` | `Text_GXLink` (`Text_GX`) | renderer; pass 0 camera, pass 2 draw |
| `0x800ab2d4` | `DevelopText_Create` | DevText creator |
| `0x800ab78c` | `DevelopText_AddString` | DevText print |
| `0x80438898` | `DevelopText_DrawStrokeGlyph` | DevText `GX_LINES` glyph |
| `0x8042a29c` | `CObjThink_Common` | pass-0 camera GX callback |
| `0x80112044` | `Gm_Get3dData` | 3D HUD data struct (unnamed in the map) |
| `0x801168e8` | `CityTrial_CreateEventTextCamera` | event HUD canvas, slot 0 |
| `0x80113fb4` | `CityEvent_ShowHudText` | mode gate into `stadiumPrediction` |
| `0x80127864` | `stadiumPrediction` | event / prediction HUD text |
| `0x801169fc` | `CityEvent_SetSisText` | creates the Text GObj and binds the entry |
| `0x80153d2c` | `AirRideSelect_SetMachineText` | machine name/description lookup |
| `0x8015e740` | `CitySelect_SetMachineText` | machine name/description lookup |
| `0x8038bf64` | `OSText_DrawString` | IPL font path (separate from SIS) |

### Data

These are unnamed in the symbol map; hoshi headers pin them as literal-address pointers.

| Address | Meaning |
|---------|---------|
| `0x8050a040` | master image table, 256 x `0x200` I4 32x32 |
| `0x80509dc0` | master kerning table, 320 x 2 bytes |
| `0x80509b40` | ASCII pair table, 320 x 2 bytes |
| `0x805098c0` | SIS code table paired with the above |
| `0x8050983c` | opcode dispatch jump table, entries `0x00`-`0x1a` |
| `0x805053f8` | DevText stroke font |
| `0x805096a0` | text camera CObj descriptor |
| `0x8059a848` | `stc_sis_archives[5]` (`text.h`) |
| `0x8059a85c` | `stc_sis_data[5]` (`text.h`) |
| `0x805de56c` | `stc_textcanvas_first` (`text.h`) |
| `0x804a7b98` | `stc_event_sis_id_table` (`event.h`) |
| `0x804aa3d8` / `0x804aa428` | Air Ride select machine name / description indices |
| `0x804aa598` / `0x804aa5e8` | City Trial select machine name / description indices |
