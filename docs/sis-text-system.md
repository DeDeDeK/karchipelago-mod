# SIS Text System

## Overview

SIS (String Image Set) files are HSD archives that hold pre-composed text strings used for in-game UI. The runtime renders these (and ad-hoc C-format strings) through a single GX path, `Text_GXLink`, which walks a stream of opcodes, looks up 32x32 I4 glyph bitmaps, and emits one textured quad per character.

There are **three text rendering paths** in the binary:

1. **SIS Text** — the main UI path. All in-game text (menus, HUD, dialogue, results, event text, checklists, ending) goes through `Text_GXLink` (0x804516e4). Texture-mapped 32x32 I4 glyphs.
2. **DevText / DevelopText** — debug-only stroke (vector) font drawn via `GX_LINES`. Used by `3DDebug_*` and the developer/debug menus. ASCII-only, ~10x16 px cells.
3. **OS Font (IPL)** — the GameCube SDK font path (`OSInitFont` / `OSGetFontTexture` / `OSGetFontTexel`), used by the Top Ride 2D debug menu and CSS overlays via the `OSText_DrawString` family (0x8038BF64). SJIS-aware; mojibakes on NA without an encoding-cache pin.

## Loading

```c
Text_LoadSisFile(int slot, char *filename, char *symbol)  // 0x8044F800
```

Loads an HSD archive into one of 5 slots and stores its data in:

- `stc_sis_archives[5]` at `0x8059a848` — `HSD_Archive *`
- `stc_sis_data[5]` at `0x8059a85c` — pointer arrays. For each slot, `[0]` = image data, `[1]` = kerning data, `[2+]` = text-entry data pointers.

Different scenes load different SIS files into slots 0..4; see "Scene → SIS slot map" below.

## Two Glyph Banks

The renderer dispatches per-character based on the high bits of the 16-bit text code:

| code range | bank | source |
|------------|------|--------|
| `0x2000`–`0x3FFF` | Master (Latin) | shared, baked into `main.dol` |
| `0x4000`+ | Per-SIS | from the loaded archive's `stc_sis_data[slot][0]` / `[1]` |

### Master (Latin) bank — shared, in `main.dol .data5`

| symbol | address | size | layout |
|--------|---------|------|--------|
| master image table | `0x8050a040` | 256 × `0x200` (I4 32×32) | indexed by `(code - 0x2000) & 0xFF` |
| master kerning table | `0x80509dc0` | 256 × 2 bytes | `{u8 left_pad, u8 right_edge}` |

Effective drawn glyph width: `34 - kern.x0 - kern.x1` (matches `Text_GetStringWidth` in `text.h`). Vanilla populates ~90 slots (digits, A-Z, a-z, 22 symbols at scattered addresses up to `0x21XX`); ~165 slots are empty memory and free for mods to write their own glyphs into. **All English UI in the game uses this single shared font.** SIS files don't carry Latin glyphs — they store nulls in the [0]/[1] entries and rely on the master bank.

### Per-SIS bank — codes ≥ `0x4000`

Used for Japanese hiragana/katakana and per-screen icon glyphs. Same 0x200/glyph stride, same 2-byte kerning layout, indexed by `(code - 0x4000) & 0xFF`.

| SIS file | per-SIS image block size | usage |
|----------|--------------------------|-------|
| `SisSmmenu.dat` | `~0x1B800` (~220 glyphs) | Japanese kana set for Sub-Menu rule descriptions |
| `SisClrChk2D/3D/CT.dat` | `0x200` (1 glyph) | Checkbox icon |
| `SisSelply*.dat`, `SisSelrule.dat` | `0x200` (1 glyph) | Bullet/icon |
| All others | 0 / empty | Latin-only, master bank only |

Renderer call sites: master-image `GXInitTexObj` at `0x80452478` (texel base `r28`, addr-compute `add r4,r28,r0` at `0x80452464`), per-SIS-image `GXInitTexObj` at `0x804524a4` (texel base `r26`, addr-compute `add r4,r26,r0` at `0x80452490`) — both with literal `r5=32, r6=32` (the `li r5,32; li r6,32` pairs at `0x8045245c`/`0x80452488`).

### DevText vector font

Lives in `main.dol .data5` at `0x805053f8`. Per-character variable-length stroke list, terminated by `0xFF`. Each byte encodes two 4-bit nibbles `(x_nibble << 4) | y_nibble` (16×16 logical grid); bytes are processed in pairs as `(start, end)` line-segment endpoints. Rendered via `GXBegin(GX_LINES)` in `DevelopText_DrawStrokeGlyph` (0x80438898). Used by `3DDebug_CreateText` (0x8007e964) and `DevelopText_*` (`DevelopText_Create` 0x800ab2d4, `DevelopText_AddString` 0x800ab78c). 7-bit ASCII only, 10×16 px cells. **Not** OS_FONT.

## SIS Text Command Format

Text data is a byte stream of opcodes and 2-byte character codes, parsed from `text->text_start`. The opcode interpreter halts on the `0x00` TERMINATE byte at the buffer tail — **not** on `text->text_end`. `text_end` is consulted only by the per-frame typewriter dwell logic at `0x80451c44` (when `parse_ptr == text_end` and `wait_countdown != 0`, the parser pauses); when `wait_countdown == 0` the parser falls through to opcode dispatch regardless. The renderer resets `text_end` back to `0` at `0x01` SUBTEXT_RESET (`0x80451cbc`) and otherwise drives it itself (sets it to the reveal frontier as glyphs reveal). So manually writing `text_end` from a render_callback is **not** how you drive a typewriter — seed `temp.char_delay` instead (for `Text_AddSubtext` buffers; `char_delay_init` alone is dead — see "Typewriter reveal" below).

### Renderer state-history stack

The renderer keeps a per-Text state-history buffer (`text->state_stack`, `+0x68`) and two helpers:

- `Text_PushState(text, value_ptr, kind)` (`0x80450828`) — pushes the current value of one state field
- `Text_PopState(text, kind)` (`0x8045111c`) — pops and restores

`kind` tags identify which field: `1=POS`, `2=COLOR`, `3=SCALE`, `4=ALIGN`, `5=int (call return marker)`. The high bit `0x80` means "pop without write" (used by the sizer pass). This is what makes the alignment / color / scale opcodes properly nest.

### Opcode table — complete

"Size" is total bytes including the opcode byte itself. The opcodes are interpreted by `Text_GXLink` (0x804516e4), `Text_DetermineHeightAndWidth` (0x80451344), and the parse table in `Text_StorePremadeText` (0x8044f9d4).

| Op | Size | Data | Behavior |
|----|------|------|----------|
| `0x00` TERMINATE | 1 | — | Pops a `kind=5` user int marker; if no marker, ends rendering. Doubles as subtext-end and document-end. |
| `0x01` SUBTEXT_RESET | 1 | — | Resets cursor.x, copies static fields back into temp render state, clears state-history. Falls through into `0x02`. |
| `0x02` SUBTEXT_BREAK | 1 | — | Bookmarks parse position (`text->text_data_ptr`) and resets the reveal counter (`temp.reveal_count` → 0). |
| `0x03` LINEBREAK | 1 | — | Advances `cursor.y` by `16 * scale_y * pos_scale_y`. Newline. |
| `0x04` LINEBREAK_REFLOW | 1 | — | Newline + sets `temp.x4b=1` so the next frame re-enters the renderer in reflow mode. |
| `0x05` SET_DELAY | 3 | u16 frames | Sets `temp.delay_frames = u16` — typewriter pause. |
| `0x06` SET_TIMING | 5 | u16 char, u16 space | Sets `temp.char_delay` then `temp.space_delay` (operand order @ `0x80451e20`). |
| `0x07` POS (subtext header) | 5 | s16 x, s16 y | **Subtext header.** x = pixels right of canvas-left, y = lines down (`y * 16 * scale_y`). Width is measured, alignment offset applied (`temp.align`). |
| `0x08` JUMP | 5 | s32 abs ptr | Absolute pointer jump (HSD-relocated). |
| `0x09` CALL | 5 | s32 abs ptr | Push `kind=5` return marker, jump absolute. Subroutine into a glossary entry; `0x00` at the end of the callee returns. |
| `0x0a` POS_PUSH | 5 | s16 x, s16 y | Push `kind=1`, set cursor to `(x/256, y/256)` units. Inline relative repositioning (gated by `text->x6c`). |
| `0x0b` POS_POP | 1 | — | Pop `kind=1`. |
| `0x0c` COLOR | 4 | u8 R, G, B | Push `kind=2`, write `temp.color.{r,g,b}`. **RGB only — alpha is preserved from `text->color.a`.** |
| `0x0d` COLOR_POP | 1 | — | Pop `kind=2`. |
| `0x0e` SCALE | 5 | u16 sx, u16 sy | Push `kind=3`, set `temp.scale.{x,y} = sx/256, sy/256`. |
| `0x0f` SCALE_POP | 1 | — | Pop `kind=3`. |
| `0x10` ALIGN_CENTER | 1 | — | Push `kind=4`, `temp.align = 1`. |
| `0x11` ALIGN_POP | 1 | — | Pop `kind=4`. (`0x11`/`0x13`/`0x15` are aliases — same code path.) |
| `0x12` ALIGN_LEFT | 1 | — | Push `kind=4`, `temp.align = 0`. |
| `0x13` ALIGN_POP | 1 | — | Pop `kind=4`. |
| `0x14` ALIGN_RIGHT | 1 | — | Push `kind=4`, `temp.align = 2`. |
| `0x15` ALIGN_POP | 1 | — | Pop `kind=4`. |
| `0x16` KERNING_ON | 1 | — | `temp.kerning = 1` (no push). |
| `0x17` KERNING_OFF | 1 | — | `temp.kerning = 0`. |
| `0x18` FIT_ON | 1 | — | `temp.use_aspect_fit = 1`. If line width exceeds `text->aspect.x`, the line is horizontally squeezed. |
| `0x19` FIT_OFF | 1 | — | `temp.use_aspect_fit = 0`. |
| `0x1a` SPACE | 1 | — | Advances cursor.x by `scale_x * (32 + 16) * fit_squeeze`. Word separator. |
| `0x1b`–`0x1f` | 1 | — | Truly no-ops — fall through to default branch. |
| `>=0x20` (char) | 2 | u16 code | Glyph render: dispatches to master / per-SIS bank by high nibble (see "Two Glyph Banks"). |

### Character codes (`code >= 0x20`)

ASCII → SIS code is mapped by `Text_CharToCommand` (`text.h`). Most commonly:

| Range | Characters | Encoding |
|-------|-----------|----------|
| `0x2000`–`0x2009` | `0`-`9` | `0x2000 + (c - '0')` |
| `0x200a`–`0x2023` | `A`-`Z` | `0x200a + (c - 'A')` |
| `0x2024`–`0x203d` | `a`-`z` | `0x2024 + (c - 'a')` |

Symbol codes — the 21-entry `symbol_lookup` table in `Text_CharToCommand` (`text.h`): `0x20e3` space, `0x20ec !`, `0x20f4 "`, `0x2106 #`, `0x2104 $`, `0x2105 %`, `0x2107 &`, `0x20f5 (`, `0x20f6 )`, `0x2108 *`, `0x20fd +`, `0x20e6 ,`, `0x20fe -`, `0x20e7 .`, `0x20f0 /`, `0x20e9 :`, `0x20ea ;`, `0x2100 =`, `0x20eb ?`, `0x2109 @`, `0x20ee _`. Any ASCII char not in this table (including `'`) returns -1 from `Text_CharToCommand` and is dropped by `ComposeSisText`. (The separate C-format path `Text_ConvertASCIIToShiftJIS` handles a few of these via a different mechanism — see below.)

In pre-composed SIS text, spaces between words use the `0x1a` SPACE opcode, not the `0x20e3` character code.

### Standard event text layout

All City Trial event text entries use this template:

```
12              ALIGNLEFT
18              FIT_ON
16              KERNING_ON
0c BB BB BB     COLOR(0xBB, 0xBB, 0xBB) — light gray
0e 00 B3 00 B3  SCALE(0xB3/256, 0xB3/256) ≈ 0.70x
[text body]     character codes + 0x1a spaces
03              LINEBREAK
0f              SCALE_POP
0d              COLOR_POP
17              KERNING_OFF
19              FIT_OFF
13              ALIGN_POP
00              TERMINATE
```

The `mods/custom_events/src/custom_events.c::ComposeSisText` helper emits exactly this layout for runtime-composed event text.

## Color Rendering Pipeline

In `Text_GXLink` the static TEV setup (starts `0x80451a10`):

- `GXSetNumTevStages(1)`
- `GXSetTevColorIn(0, 0xF, 0xF, 0xF, 2)` (`0x80451a1c`) → output = `Reg1 (kColor)` (RGB from register, no texture color contribution)
- `GXSetTevAlphaIn(0, 7, 4, 1, 7)` → **glyph alpha (from I4 texture) modulated by register-color alpha**
- `GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, 0)`
- `GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0)`

Per character (at `0x804524c4`):

- `GXSetTevColor(GX_TEVREG1, &temp.color)` — one color-register write per character (`temp.color` at `+0x8c`, copied to a stack temp then passed; 4 bytes RGBA).
- `GXBegin(GX_QUADS, fmt 0, 4)` at `0x804524d4`, then 4 vertices with position + s/t.

So **color is per-character RGB** (driven by the COLOR opcode stack) but **alpha is constant across all characters in a draw**, sourced from `text->color.a` at init. The `0x0c` COLOR opcode never touches alpha. Optional viewport background quad (drawn first if `text->viewport_color.a != 0`) covers the full `aspect`-rect with `viewport_color` RGBA.

**There is no gradient (no top-vs-bottom color), no outline TEV stage, and no shadow stage.** Vanilla outline effects in menus are achieved by stacking two separate Text GObjs (the darker one offset by 1 px). For a real outline/shadow pipeline, mods would need to install a custom TEV via `render_callback` (see below).

## C-Format-String Composer

`Text_AddSubtext(text, x_pixels, y_lines, fmt, ...)` (`0x8044fec4`) emits exactly this byte stream:

```
07 XH XL  YH YL                POS (s16, s16; truncates x,y to int)
0c CR CG CB                    COLOR (copies text->color RGB; alpha NOT emitted)
0e SXh SXl  SYh SYl            SCALE (`(int)scale<<8 | (int)(256*scale)&0xFF`)
[converted format-string body]
0f                             SCALE_POP
0d                             COLOR_POP
00                             TERMINATE
```

`Text_SetText(text, subtext_idx, fmt, ...)` (`0x8045031c`) walks the buffer, finds the N-th `0x07`, and replaces only the body — so per-subtext color/pos/scale persist across `SetText` calls.

Both `vsnprintf` into a stack buffer first, then run **`Text_ConvertASCIIToShiftJIS`** (`0x8044fb0c`). The name is only partly accurate: the output is a SIS opcode/character stream, but the 2-byte codes it emits for letters/digits are SIS glyph codes (`0x20xx`) while for a few punctuation marks it emits genuine full-width **Shift-JIS** codes (`0x81xx`):

| ASCII input | Emits |
|------|-------|
| `[A-Z]` | 2-byte char `0x200a + (c - 'A')` |
| `[a-z]` | 2-byte char `0x2024 + (c - 'a')` |
| `[0-9]` | `0a F4 00 00 00` (POS_PUSH, tight-spaced digit mode) + 2-byte digit |
| `' '` (space) | `0a F4 00 00 00` then kerning lookup |
| `.`, `:` | tight-mode prefix + symbol code |
| `"` | `0b` (POS_POP) + Shift-JIS `0x8140` (`0x8044fbb0`) |
| `'` | `0b` + Shift-JIS `0x8168` (`0x8044fbd4`) |
| `,` | `0b` + Shift-JIS `0x8143` (`0x8044fbfc`) |
| `-` | `0b` + Shift-JIS `0x817c` (`0x8044fc24`) |
| Other printable | `0b` + symbol code (table lookup at `0x80509b40` / `0x805098c0`) |

**`\n`, `\t`, and printf `\\n`/`\\t` are NOT mapped** — they fall into the table-lookup branch and produce no output. Multi-line text **must** use separate `Text_AddSubtext` calls (each emits its own `0x07` POS header). `%d`, `%s`, `%f` work as expected (resolved by `vsnprintf` before SIS conversion).

There is no `{...}` brace syntax for inline opcodes. To inject COLOR/SCALE/POS mid-text, write opcode bytes directly into the buffer or call `Text_SetColor` / `Text_SetScale` / `Text_SetPosition` (which patch the per-subtext header bytes via `Text_GetCommand`).

### `Text_AddSubtext` chaining and the missing TERMINATE

Each `Text_AddSubtext` call writes the trailer `0f 0d 00` (SCALE_POP, COLOR_POP, TERMINATE) at the heap cell's write pointer **without advancing the pointer past the `0x00`** (`0x804502d8`-`0x804502dc`). The next `Text_AddSubtext` call overwrites that `0x00` with its own `0x07` POS header. Net result: a chain of N subtexts ends with a single `0x00` after the *last* subtext only — there is **no** inline TERMINATE between subtexts.

This is what makes `Text_GetSubtext` (which stops walking on `0x00`) able to find subtext indices > 0.

It also means `text_end` is never set by `Text_AddSubtext` — to derive the real buffer end you must scan from `text_start` for the trailing `0x00`.

## Typewriter reveal

**The engine has a built-in, dwell-paced typewriter, and it works for `Text_AddSubtext` buffers — but for those you must seed `temp.char_delay` directly, not `char_delay_init`.** Set the per-glyph dwell (frames) to the desired speed before render; the renderer reveals one glyph every `char_delay` frames on its own — no buffer mutation, no per-frame work. `0` reveals everything instantly.

How it works (`Text_GXLink` @ `0x804516e4`):

- The live reveal counter is loaded from `temp.reveal_count` (`+0x98`) at subtext setup (`0x80451c3c`); `temp.char_delay` (`+0x90`) and `temp.space_delay` (`+0x92`) are loaded into working registers at the same point (`0x80451c34`). As the opcode loop walks glyphs it decrements the reveal counter for each already-revealed glyph (still drawing it). When the counter hits 0 it has reached the reveal frontier: it draws that glyph, increments `reveal_count`, copies `char_delay` into `wait_countdown` (`+0x94`), and sets `text_end` (`+0x60`) to just past this glyph (`0x80452618`–`0x80452640`). SPACE (`0x1a`) reveals identically using `char_delay` (`0x804521f4`); LINEBREAK/DELAY count too (using `space_delay` / their operand). `0x07` POS and `0x0c` COLOR are processed "for free" — they advance the parser without consuming a reveal step.
- The loop head pauses parsing while `parse_ptr == text_end && wait_countdown != 0`, decrementing `wait_countdown` once per render (`0x80451c44`–`0x80451c78`). After `char_delay` frames the pause releases and one more glyph reveals.

**The `char_delay_init` trap (why a fresh `Text_AddSubtext` buffer reveals everything on frame 1 if you only set `char_delay_init`):** the *only* write to `temp.char_delay` in the renderer is at `0x80451cec`, inside the `0x01` SUBTEXT_RESET / `0x02` SUBTEXT_BREAK handler — that's where `char_delay_init` (`+0x44`) is copied into `temp.char_delay`. `Text_AddSubtext` delimits its segments with `0x07` POS headers and contains **no** `0x01`/`0x02`, so the copy never fires: `temp.char_delay` stays `0`, every reveal sets `wait_countdown = 0`, and the whole buffer reveals in one frame. **Fix: write `temp.char_delay`/`temp.space_delay` directly.** The renderer reloads them into its working registers at every render top and never clears them, so a single write at enqueue persists across all frames.

**Why it reveals multi-segment buffers sequentially (not in parallel):** the reveal state (`reveal_count`, the delays, `text_end`) is reset *only* by the `0x01`/`0x02` handlers (`0x80451cb4` / `0x80451d3c`). Since `Text_AddSubtext` never emits those, the counter is loaded once and walks straight through every `0x07` segment in the buffer — the `0x07` handler (`0x80451e30`) touches none of the reveal state.

Opcode dispatch is a jump table at `0x8050983C` (entries `0x00`..`0x1a`, indexed by opcode); glyphs (`>= 0x20`) and the `0x1b`–`0x1f` no-ops branch to the glyph/no-op handler at `0x80452210`.

Reference implementation: `mods/textbox/src/textbox.c` (`TextBox_ApplyTypewriter`) seeds `temp.char_delay` / `temp.space_delay` (and, for completeness, `char_delay_init` / `space_delay_init`) from the typewriter-speed setting at Text creation, and zeroes `temp.reveal_count` / `text_end` for a clean start.

## Canvas / GObj / Render Pipeline

### Coordinate system

Text canvases are **orthographic 640×480 in raw pixels**:

- x: `[0, 640]` left → right
- y: `[0, -480]` top → bottom (the projection bottom is `-480`; vanilla code stores positive Y values and the renderer negates per-vertex)

So `text->trans.x` is pixels right of canvas-left, `text->trans.y` is pixels above canvas-bottom (i.e., set `trans.y = 480 - desired_top_offset` for top-down positioning).

### Pass model

`Text_GXLink(gobj, pass)` switches on `pass`:

| Pass | What it does |
|------|--------------|
| `0` | Camera-level setup (invoked via `CObjThink_Common` / `0x8042a29c`). Sets up `GXSetViewport(0,0,640,480)`, `GXSetScissor(0,0,640,480)`, `C_MTXOrtho(0,-480,0,640,0,2)` via `HSD_StateSetMtx`. |
| `2` | Per-Text rendering. Pulls `Text *t = gobj->userdata`, early-returns on `hidden` / null `text_start`, sets ZMode by `is_depth_compare`, loads view matrix, sets vertex format / TEV / blend mode, **invokes `render_callback`**, draws optional `viewport_color` background, then runs the opcode interpreter. |
| other | Early return. |

### Canvas creation

```c
// Map symbol: Text_CreateTextCanvas. Declared in text.h as Text_CreateCanvas
// (same address) — that's the name mod code calls (see the recipe below and
// screen_cam.c). Both names refer to 0x8044f674.
int Text_CreateTextCanvas(int sis_idx, int no_create_cam_gobj,
                          int gobj_entityclass, int gobj_plink, int gobj_ppriority,
                          int gxlink, int gxpri, int cobj_gxpri);  // 0x8044f674
```

1. Allocates a `TextCanvas` and chains it onto `stc_textcanvas_first` (`0x805de56c`).
2. Creates a GObj on `(entityclass, plink, ppriority)`.
3. `HSD_CObjLoadDesc(0x805096a0)` loads the canonical text-camera descriptor, then `HSD_CObjSetOrtho(0, -480, 0, 640)` makes it orthographic in pixel space.
4. `GObj_AddObject(g, COBJ, cobj)` and `GObj_InitCamera(g, CObjThink_Common, cobj_gxpri)` register `CObjThink_Common` (`0x8042a29c`) as the pass-0 callback that sets the viewport/scissor for child gxlinks.
5. The canvas's gxlink mask determines which Text GObjs render under it.

### Text struct fields — what each one does at render time

| Off | Field | Behavior |
|-----|-------|----------|
| `0x00` | `trans` (Vec3) | Per-vertex anchor for every quad. Pixel coords in canvas ortho. `z` is written into vertex z (depth-compares against z-buffer when `is_depth_compare=1`). |
| `0x0c` | `aspect` (Vec2) | Bounding box width/height in pixels. Used by `use_aspect` auto-shrink, `viewport_color` background size, and per-quad scissor reference. |
| `0x14`-`0x20` | `scissor_top/bot/left/right` | Per-quad clip rectangle (gated by `is_scissor`). Quads outside are dropped; quads on edge get UV+vertex shrunk (around `0x80452200`-`0x804523f0`). **Not** `GXSetScissor`. |
| `0x24` | `viewport_scale` (Vec2) | Multiplies everything: glyph quad size, X-advance, background rect, scissor reference. ~0.4 yields readable HUD-size text. Default 1.0. |
| `0x2c` | `viewport_color` (GXColor RGBA) | If `alpha != 0`, drawn first as flat-shaded background quad (size = `aspect * viewport_scale`). |
| `0x30` | `color` (GXColor RGBA) | Default per-character color. Loaded into `temp.color` at every subtext start. |
| `0x34` | `scale` (Vec2) | Initial per-char scale; opcode `0x0e` overrides per glyph. |
| `0x3c` | `x3c` (float) | Initial X cursor offset for the subtext (loaded into `temp.x78`). |
| `0x40` | `x40` (float) | Initial Y cursor offset (`temp.x7c`). |
| `0x44` | `char_delay_init` (u16) | *Designed* seed for `temp.char_delay` (per-glyph pause, frames; 0 = instant) — but copied in **only** at a `0x01`/`0x02` SUBTEXT opcode, so it's dead for `0x07`-delimited `Text_AddSubtext` buffers. Seed `temp.char_delay` directly for those. |
| `0x46` | `space_delay_init` (u16) | Seeds `temp.space_delay` — per-space/linebreak pause in frames. |
| `0x48` | `use_aspect` | Auto-shrink horizontally to fit `aspect.x`. |
| `0x49` | `kerning` | Use kerning advances vs. fixed cell width. |
| `0x4a` | `align` | 0=left, 1=center, 2=right. |
| `0x4b` | `x4b` | Internal reflow flag (set by `0x04`). Leave at 0. |
| `0x4c` | `is_depth_compare` | If 1, `GX_LEQUAL` Z mode (text z-tests). If 0, always on top. |
| `0x4d` | `hidden` | Non-zero → `Text_GXLink` early-returns. |
| `0x4e` | `is_scissor` | Enables `scissor_*` per-quad clipping. |
| `0x4f` | `sis_id` | Index into `stc_sis_data[5]` for per-SIS image/kerning bank. |
| `0x58` | `render_callback` | `void cb(GOBJ *text_gobj)`. **Invoked once per render in pass 2, after GX state setup, before any drawing.** See "Customization hooks" below. |
| `0x5c` / `0x60` | `text_start` / `text_end` | Parse start. `text_end` is **not** a hard limit (see "SIS Text Command Format" above) — it's the typewriter reveal frontier the engine sets itself, reset to `0` at `0x01` SUBTEXT_RESET. `Text_AddSubtext` / `Text_SetText` never write `text_end`; only `Text_InitPremadeText` (`0x8044f8c8`) and `Text_LoadSisFile` (`0x8044f800`) do, and both set it to `0`. |
| `0x70`–`0xa0` | `temp.*` | Per-subtext mutable state (cursor, color register, char counter, etc.). Modified by opcodes. |

### Customization hooks (`render_callback`)

Loaded from `text->render_callback` (`+0x58`) at `0x804519f4`, null-checked, and invoked (`bctrl`) at `0x80451a08`. Signature: `void cb(GOBJ *text_gobj)`. `text_gobj->userdata` is the `Text *`. Capabilities from inside the callback:

- Mutate `t->color`, `t->temp.color`, `t->trans`, `t->scale`, `t->viewport_scale` for per-frame effects (blink, fade, jitter, breathing scale, wave).
- Override TEV state (e.g. install a custom multi-stage Tev for outline / shadow / gradient).
- Read `t->temp.char_display_num` to drive per-glyph effects.
- **Typewriter reveal**: seed `temp.char_delay` (for `Text_AddSubtext` buffers — `char_delay_init` alone is dead) and let the engine reveal — see "Typewriter reveal" below. (Manually advancing `text_end` from a render_callback does **not** work; the engine manages `text_end` itself.)
- Bind a custom texture via `GXLoadTexObj` (custom font for a single Text element).

The whole `gobj->gx_cb` can also be swapped (as `text_joint.c:67` does) to wrap or replace `Text_GXLink` entirely — useful for premultiplying the view matrix to anchor text to a 3D bone, then calling the original draw.

## Recipe — Arbitrary Mod Overlays

The minimum API for screen-space text floating above any scene (matches `mods/textbox/src/textbox.c` — the textbox notification system, split out of the archipelago mod into its own `mods/textbox/` module):

1. **Once per scene** — create an ortho 640×480 canvas via hoshi's `ScreenCam_Create` (`externals/hoshi/src/screen_cam.c:19`):
   ```c
   canvas_idx = Text_CreateCanvas(/*sis_idx=*/1, /*no_cam=*/0,
                                  /*entityclass=*/0, /*plink=*/0, /*ppri=*/0,
                                  /*gxlink=*/HOSHI_SCREENCAM_GXLINK,
                                  /*gxpri=*/0, /*cobj_gxpri=*/63);
   ```
   `cobj_gxpri=63` makes the camera draw last; `HOSHI_SCREENCAM_GXLINK` is a free GX link bit hoshi reserves so the canvas isn't culled by scene cameras.

2. **Per-message** — `Text_CreateText(1, canvas_idx)` returns a fresh `Text *`. Configure: `kerning=1`, `use_aspect=1`, `trans` (pixel coords), `viewport_scale ≈ 0.4`, `color`, `viewport_color` (alpha controls background visibility).

3. **Add content** — `Text_AddSubtext(t, x, y, "")` allocates a subtext slot, then `Text_SetText(t, 0, sanitized_string)`. Call `Text_Sanitize()` (`text_joint.c:235`) first to convert ASCII punctuation.

4. **Auto-size** — `Text_GetWidthAndHeight(t, 0, &w, &h)` and set `t->aspect = (Vec2){w, h}`.

5. **Per-frame effects** — store `Text *` and tweak fields each frame, or set `render_callback`.

6. **Destroy** — `Text_Destroy(t)`.

## Capability Summary

What's possible right now without engine changes:

| Capability | Possible? | How |
|-----------|-----------|-----|
| Per-character color (RGB) | ✅ | `0x0c` COLOR opcode mid-stream |
| Per-character alpha | ❌ | Alpha is fixed across the draw (`text->color.a`); only `0x0c` opcode is RGB |
| Multi-line text | ✅ | Multiple `Text_AddSubtext` calls (each is a `0x07` subtext); `\n` does **not** work in format strings |
| Centered / right-aligned | ✅ | `0x10`/`0x14` opcodes, `text->align`, or `Text_AddSubtext` with right x |
| Variable scale | ✅ | `0x0e` SCALE opcode, `text->scale`, or `text->viewport_scale` |
| Background rectangle | ✅ | `text->viewport_color` (alpha must be non-zero) |
| Scissor / clip rect | ✅ | `text->is_scissor=1` + `scissor_*` fields |
| Typewriter reveal | ✅ | Seed `temp.char_delay` (frames/glyph) — the built-in reveal works for both pre-composed SIS text and `Text_AddSubtext` buffers (sequential across `0x07` segments). For runtime `Text_AddSubtext` buffers `char_delay_init` alone is dead (its copy fires only at `0x01`/`0x02`); write `temp.char_delay` directly. See "Typewriter reveal". |
| Fade in/out | ✅ | Mutate `text->color.a` per frame (alpha is render-state) |
| Wave / jitter / breathing | ✅ | `render_callback` mutating `trans` / `scale` per frame |
| Gradient (top-vs-bottom color) | ❌ vanilla | Single TEV stage, single color register. Possible only by replacing TEV in `render_callback`. |
| Drop shadow / outline | ❌ vanilla | Standard trick: stack two `Text` GObjs (offset, darker). True outline needs custom TEV. |
| Larger glyph cell (e.g. 64×64) | Patch | Hard-coded `r5=32, r6=32` at `GXInitTexObj` calls (`0x8045245c`/`0x80452488`); patch literals + the `rlwinm r0, r31, 9, 7, 22` (×0x200) shift to switch stride. |
| Add new Latin glyphs/symbols | ✅ | Master image table at `0x8050a040` has ~165 unused slots. Write your 0x200-byte I4 32×32 glyph and 2 kerning bytes; emit raw 2-byte code in text. |
| Different font family for one element | ✅ | Inject custom `image_data_arr` / `kerning_data_arr` into a SIS slot's `[0]`/`[1]`, compose text with codes ≥ `0x4000`. Renderer dispatches per-character, so other text on the same screen is unaffected. The `SisSmmenu.dat` Japanese-glyph mechanism already uses this. |
| Italic / decorative variant | ✅ | Same as above (per-SIS bank) — supply alternate I4 glyphs. |
| Use OS_FONT (GC IPL ROM font) | ✅ (separate path) | The IPL OS Font path **is** in the binary — `OSInitFont` (0x803d6e20), `OSGetFontTexture` (0x803d6f00), `OSGetFontTexel` (0x803d676c), `OSGetFontEncode` (0x803d6354), `OSText_InitFont` (0x80390b3c), `__OSExpandFont` (0x803d6a70) — and is driven by the `OSText_DrawString` family (0x8038bf64), not by `Text_GXLink`. It is a wholly separate rendering path (SJIS, different glyph format) used by the Top Ride 2D debug menu. (`OSLoadFont` specifically is absent — this game uses `OSInitFont`.) Not selectable from within a `Text` element. |
| Render text in 3D (bone-attached) | ✅ | Swap `gobj->gx_cb` to multiply the view matrix before calling `Text_GXLink` (`text_joint.c` does this). |

## SIS Files

21 SIS files in `iso/files/`:

| File | Entries | Description |
|------|---------|-------------|
| `SisBestrap.dat` | 42 | Boot strap / startup text |
| `SisCitytrial.dat` | 42 | City Trial event + prediction text |
| `SisClrChk2D.dat` | 158 | Top Ride checklist reward names |
| `SisClrChk3D.dat` | 171 | Air Ride checklist reward names |
| `SisClrChkCT.dat` | 169 | City Trial checklist reward names |
| `SisDialogue.dat` | 33 | In-game dialogue |
| `SisEnding2d.dat` | 55 | Top Ride ending text |
| `SisEnding3d.dat` | 92 | Air Ride ending text |
| `SisLan.dat` | 39 | LAN mode text |
| `SisMenu.dat` | 123 | Main menu text |
| `SisProgressive.dat` | 7 | Progressive scan setup |
| `SisResultCt.dat` | 10 | City Trial results screen |
| `SisSelmap.dat` | 10 | Air Ride stage select |
| `SisSelmap2d.dat` | 8 | Top Ride stage select |
| `SisSelply.dat` | 48 | Air Ride player/machine select |
| `SisSelply2d.dat` | 8 | Top Ride player select |
| `SisSelplyCt.dat` | 48 | City Trial player/machine select |
| `SisSelrule.dat` | 59 | Rule settings |
| `SisSelstadium.dat` | 54 | Stadium select |
| `SisSmmenu.dat` | 3 | Sub-menu (carries Japanese-kana glyph block) |
| `SisStadiumTitle.dat` | 26 | Stadium title cards |

Entry counts include 2 data pointer entries (image + kerning) at indices 0–1. Text entries start at index 2.

## Scene → SIS Slot Map (Loaders)

`Text_LoadSisFile` callers, by function-band:

| Address band | Scene | Files loaded |
|--------------|-------|--------------|
| `0x800ad0c8` | Pre-game / mode menu | varies |
| `0x80116xxx` | City Trial event HUD | `SisCitytrial.dat` (slot 0) |
| `0x8013Bxxx`–`0x8013Fxxx` | Main menu, mode select, stadium select, character/machine select (3D & CT) | `SisMenu.dat`, `SisSelmap.dat`, `SisSelstadium.dat`, `SisSelply.dat`, etc. |
| `0x80140xxx`–`0x80146xxx` | Rule/options/sub-menus | `SisSelrule.dat`, `SisSmmenu.dat` |
| `0x8017Cxxx`–`0x8017Dxxx` | Results / ending screens | `SisResultCt.dat`, `SisEnding2d/3d.dat` |
| `0x80182xxx` | Checklist (3 SIS-load calls in one fn) | `SisClrChk3D.dat`, `SisClrChk2D.dat`, `SisClrChkCT.dat` |
| `0x80186xxx`–`0x80187xxx` | LAN mode | `SisLan.dat` |
| `0x8027Cxxx`–`0x80283xxx` | Top Ride mode | `SisSelmap2d.dat`, `SisSelply2d.dat` |

Pre-composed (`Text_InitPremadeText`) is the dominant pattern in the `0x8017Cxxx`–`0x80182xxx` band (results, checklists). Composed (`Text_AddSubtext`/`Text_SetText`) dominates the `0x80140xxx`–`0x80146xxx` band (rule/menu screens with dynamic text).

## SisCitytrial.dat Entries

The SIS file loaded into slot 0 during City Trial gameplay. Event text uses `Text_InitPremadeText(text, sis_idx)` where `sis_idx` is looked up from the table at `0x804a7b98` as `event_kind + 2`.

| Index | Usage | Text |
|-------|-------|------|
| 0 | Image data | *(data pointer)* |
| 1 | Kerning data | *(data pointer)* |
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

### SIS ID lookup table (`stc_event_sis_id_table` @ `0x804a7b98`)

`stadiumPrediction` (`0x80127864`) reads `int table[event_kind]` from `0x804a7b98` (declared as `static int *stc_event_sis_id_table = (int *)0x804a7b98;` in `event.h`) to get the SIS entry index. For vanilla events 0-15, the value is `event_kind + 2`.

Entries at `table[16+]` contain sequential values (18, 19, 20...) and **are actively read** — the prediction event (kind 10) computes a derived index `stadium_kind + EVKIND_NUM` (= `stadium_kind + 16`; at `stadiumPrediction` `0x801279a4`/`0x801279b4`, a stadium index in `[0,STKIND_NUM)` is `+16`-offset), writes it back into the event-check struct's kind field (`+0x18`), and on the next pass reads `table[that_index]`. Indices 16-39 in the table (24 = `STKIND_NUM` entries) are used by this prediction lookup. Table entries at index 40+ (`EVKIND_NUM + STKIND_NUM`) are safe to overwrite for custom events.

### Extending for custom events

To add custom event text to the City Trial SIS system:

1. Compose SIS text entries using the layout above (see `mods/custom_events/src/custom_events.c::ComposeSisText`).
2. After `SisCitytrial.dat` loads, extend `stc_sis_data[0]` with custom entries at indices 42+.
3. Write custom SIS IDs (42, 43, ...) into the lookup table at `stc_event_sis_id_table[CUSTOM_SIS_TABLE_OFFSET + i]` where `CUSTOM_SIS_TABLE_OFFSET = EVKIND_NUM + STKIND_NUM = 40`. Indices 16-39 are already used by the prediction event for stadium-kind lookups — do NOT write to those.
4. The vanilla `stadiumPrediction` code path automatically displays the correct text.

This is implemented by `CustomEvents_InitSis` in `custom_events.c`: it copies the original 42-entry `stc_sis_data[0]` array into a static `extended_sis_ptrs[]`, appends one `ComposeSisText`-built buffer per custom event at indices `SIS_CITYTRIAL_ENTRY_COUNT` (= 42) `+ i`, repoints `stc_sis_data[0]` at the extended array, and writes `sis_id_table[CUSTOM_SIS_TABLE_OFFSET + i] = 42 + i`. The framework's `mod_desc` wires `.OnBoot` (installs the state-handler wrappers + extended roll) and `.On3DLoadEnd` (calls `CustomEvents_InitSis` when `Gm_GetCurrentGrKind() == GRKIND_CITY1`), so these custom SIS entries **are** installed each City Trial load. Note that `custom_events` is in the Makefile's default `EXCLUDE_MODS`, so a plain `make deploy` omits it; build it with `make deploy EXCLUDE_MODS=custom_weather`.

See `custom-events.md` for the full implementation (which agrees with this doc on the fixed SIS header opcode layout).

## HSD Archive Format (SIS Files)

SIS files are standard HSD archives:

```
Offset  Size    Field
0x00    4       File size
0x04    4       Data section size
0x08    4       Relocation entry count (= number of pointer entries)
0x0C    4       Root node count (always 1 for SIS)
0x10    16      Reserved (zeroes)
0x20    N*4     Pointer array (offsets relative to 0x20)
                  [0] = image data offset (often null for Latin-only files)
                  [1] = kerning data offset (often null for Latin-only files)
                  [2+] = text entry offsets
...             Text entry data (SIS command format)
...             Relocation table
...             Root node table
...             Symbol strings (e.g., "SIS_CityTrial")
```

When loaded via `Text_LoadSisFile`, the HSD loader relocates offsets to absolute memory pointers. The resulting pointer array is stored in `stc_sis_data[slot]`.

## Key Addresses

### Functions

| Address | Symbol (map) | Description |
|---------|--------------|-------------|
| `0x8044edec` | `Text_AllocFromHeap` | Internal heap alloc |
| `0x8044efa8` | `Text_FreeAlloc` | Internal heap free |
| `0x8044f128` | `Text_CreateGObj` | Create Text GObj with position params |
| `0x8044f350` | `Text_Destroy` | Destroy a Text |
| `0x8044f5b4` | `Text_CreateHeap` | Init the text heap |
| `0x8044f674` | `Text_CreateTextCanvas` (text.h: `Text_CreateCanvas`) | Create canvas + ortho camera |
| `0x8044f800` | `Text_LoadSisFile` | Load SIS file into a slot |
| `0x8044f8c8` | `Text_InitPremadeText` | Set text from SIS entry by index |
| `0x8044f9d4` | `Text_StorePremadeText` | Parse/count subtexts in SIS data |
| `0x8044fa70` | `Text_Create` | Simple text creator |
| `0x8044fb0c` | `Text_ConvertASCIIToShiftJIS` | ASCII → SIS opcode/char stream converter; emits `0x20xx` SIS glyph codes for letters/digits but real `0x81xx` Shift-JIS codes for some punctuation (`"`/`'`/`,`/`-`) |
| `0x8044fec4` | `Text_AddSubtext` | Add positioned subtext from C format string |
| `0x8045031c` | `Text_SetText` | Replace subtext body from C format string |
| `0x80450774` | `Text_SetScale` | Patch scale opcode of a subtext |
| `0x80450828` | `Text_PushState` | Push state-history frame |
| `0x80451344` | `Text_DetermineHeightAndWidth` | Bounding-box computation |
| `0x804516e4` | `Text_GXLink` (text.h: `Text_GX`) | **Renderer.** Pass 0 = camera setup, pass 2 = draw |
| `0x8045111c` | `Text_PopState` | Pop state-history frame |
| `0x800ab2d4` | `DevelopText_Create` | DevText creator |
| `0x800ab78c` | `DevelopText_AddString` | DevText print |
| `0x80438898` | `DevelopText_DrawStrokeGlyph` | DevText `GX_LINES` glyph render |

### Data

| Address | Symbol | Description |
|---------|--------|-------------|
| `0x80509b40` | (ASCII pair table) | 320×2 bytes — ASCII → SIS code lookup |
| `0x805098c0` | (SIS code table) | 320×2 bytes — SIS code values for above |
| `0x80509dc0` | master kerning table | 256 × 2 bytes |
| `0x8050a040` | master image table | 256 × `0x200` (I4 32×32) |
| `0x805053f8` | DevText stroke font | Variable-length stroke list per character |
| `0x805096a0` | text camera CObj desc | Used by `Text_CreateTextCanvas` |
| `0x8042a29c` | `CObjThink_Common` | Pass-0 camera GX callback (the canonical CObj think/GX callback hoshi registers for text cameras) |
| `0x8059a848` | `stc_sis_archives[5]` | HSD_Archive pointers per slot |
| `0x8059a85c` | `stc_sis_data[5]` | SIS data pointer arrays per slot |
| `0x804a7b98` | `stc_event_sis_id_table` (`event.h`) | `int[]` mapping `event_kind` → SIS entry index |
| `0x80112044` | `Gm_Get3dData` (map: `3D_GetData`) | Returns 3D HUD data struct |
| `0x801168e8` | `CityTrial_CreateEventTextCamera` | Creates event HUD text canvas (sis_idx=0) |
| `0x80113fb4` | `CityEvent_ShowHudText` | Mode gate → calls `stadiumPrediction` |
| `0x80127864` | `stadiumPrediction` | Event/prediction HUD text display |
| `0x801169fc` | `CityEvent_SetSisText` | Creates Text GObj + calls `Text_InitPremadeText` |
