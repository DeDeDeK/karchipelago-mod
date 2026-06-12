#include <stdarg.h>
#include <string.h>
#include "text.h"
#include "text_joint/text_joint.h"
#include "obj.h"
#include "hoshi/screen_cam.h"

#include "textbox.h"
#include "textbox_colors.h"

// Mod-owned settings backing storage.
TextBoxSettings textbox_settings = {
    .enabled            = 1,
    .typewriter_enabled = 1,
    .typewriter_speed   = 1, // Med (4 frames/glyph)
    .font_size          = 1, // Med (0.4)
    .colored_names      = 1, // On
    .message_spacing    = 0, // Tight (touching)
    .background_opacity = 2, // Solid
    .max_visible        = 2, // 6
    .display_time       = 1, // Med (300 frames)
    .corner             = TEXTBOX_CORNER_TOP_LEFT,
};

// Canvas extents and edge padding for corner anchoring. The screen canvas is
// ortho 640x480 (see hoshi/src/screen_cam.c). MARGIN keeps the textbox stack
// off the screen edges.
#define TEXTBOX_CANVAS_W 640.0f
#define TEXTBOX_CANVAS_H 480.0f
#define TEXTBOX_MARGIN   10.0f

// Preset tables. Indexed by the matching settings field.
static const float font_size_scales[] = { 0.30f, 0.40f, 0.55f };
static const u8    typewriter_dwells[] = { 8, 4, 2 };
// Extra vertical gap between stacked messages, expressed as a fraction of the
// rendered text height. Tight=touching, Med/Wide add progressively more space.
static const float spacing_extras[] = { 0.0f, 0.25f, 0.5f };
static const u8    bg_alpha_targets[] = { 0, 100, 200 };
static const u8    max_visible_caps[] = { 3, 4, 6, 8 };
static const u16   display_wait_frames[] = { 180, 300, 480 };

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// Bounds-checked accessors so an out-of-range setting can't index past the
// preset array. Defaults match the Med/Normal preset for each.
static float Settings_FontScale(void)
{
    int i = textbox_settings.font_size;
    if (i < 0 || i >= (int)ARRAY_COUNT(font_size_scales)) i = 1;
    return font_size_scales[i];
}

static u8 Settings_TypewriterDwell(void)
{
    int i = textbox_settings.typewriter_speed;
    if (i < 0 || i >= (int)ARRAY_COUNT(typewriter_dwells)) i = 1;
    return typewriter_dwells[i];
}

static float Settings_SpacingExtra(void)
{
    int i = textbox_settings.message_spacing;
    if (i < 0 || i >= (int)ARRAY_COUNT(spacing_extras)) i = 0;
    return spacing_extras[i];
}

static u8 Settings_BgAlphaTarget(void)
{
    int i = textbox_settings.background_opacity;
    if (i < 0 || i >= (int)ARRAY_COUNT(bg_alpha_targets)) i = 2;
    return bg_alpha_targets[i];
}

static u8 Settings_MaxVisible(void)
{
    int i = textbox_settings.max_visible;
    if (i < 0 || i >= (int)ARRAY_COUNT(max_visible_caps)) i = 2;
    return max_visible_caps[i];
}

static u16 Settings_DisplayWait(void)
{
    int i = textbox_settings.display_time;
    if (i < 0 || i >= (int)ARRAY_COUNT(display_wait_frames)) i = 1;
    return display_wait_frames[i];
}

static int Settings_Corner(void)
{
    int i = textbox_settings.corner;
    if (i < 0 || i >= TEXTBOX_CORNER_NUM) i = TEXTBOX_CORNER_TOP_LEFT;
    return i;
}

// Text* pointers inside each entry are invalidated on scene change and recreated
// by CreateTextBox_OnSceneChange.
typedef struct
{
    TextBoxMessage queue[TEXTBOX_QUEUE_SIZE];
    uint head;
    uint tail;
    uint framecounter;
} TextBoxState;

static TextBoxState textbox_state;

// Internal helpers defined lower in the file but referenced before their
// definition (the per-frame callback and the queue accessors). Everything else
// is either declared in textbox.h (public) or defined before first use.
static void            TextBox_PerFrame(GOBJ *g);
static int             TextBox_Dequeue(TextBoxMessage *text_out);
static int             TextBoxQueue_IsEmpty(void);
static int             TextBoxQueue_Count(void);
static TextBoxMessage *TextBoxQueue_GetAt(int index);

// Byte width of the opcode at a stream position. Char codes (>= 0x20) are
// 2-byte glyphs; the rest are SIS control opcodes whose sizes mirror text.h's
// TextCmdOpcode comments. This is the single source of truth shared by the
// three walkers below, so a newly-learned opcode width only changes one place.
static int Sis_OpWidth(u8 op)
{
    if (op >= 0x20)
        return 2;
    switch (op)
    {
        case 0x06: // TIMING
        case 0x07: // POS
        case 0x08: // JUMP
        case 0x09: // CALL
        case 0x0a: // POSPUSH
        case 0x0e: return 5; // SCALE
        case 0x0c: return 4; // COLOR
        case 0x05: return 3; // DELAY
        default:   return 1; // 0x00 TERMINATE, 0x1a SPACE, 1-byte ops + no-ops
    }
}

// A visible glyph is either a 2-byte char code (>= 0x20) or the 1-byte SPACE
// opcode (0x1a); both advance the typewriter by one.
static int Sis_IsGlyph(u8 op)
{
    return op >= 0x20 || op == 0x1a;
}

// Count visible glyphs (2-byte char codes >= 0x20 and the 1-byte SPACE) in a SIS
// opcode stream, walking from `start` to the inline 0x00 TERMINATE at the buffer
// tail. Text_AddSubtext / Text_SetText don't update text->text_end, so we derive
// the count by scanning the stream ourselves. This matches the engine's final
// temp.reveal_count (glyphs and SPACE each advance the reveal frontier) and so
// bounds the typewriter fade-pause. A safety cap guards against runaway scans on
// malformed data.
static int Sis_CountGlyphs(u8 *start)
{
    if (!start)
        return 0;
    int count = 0;
    u8 *p     = start;
    u8 *limit = start + 4096;
    while (p < limit && *p != 0x00) // 0x00 = TERMINATE
    {
        if (Sis_IsGlyph(*p))
            count++;
        p += Sis_OpWidth(*p);
    }
    return count;
}

// Enable (or clear) the engine's built-in typewriter on a message's Text. The
// renderer reveals one glyph every char_delay frames on its own (paced via
// temp.wait_countdown / text_end), so there's no per-frame work on our side.
// 0 = reveal instantly.
static void TextBox_ApplyTypewriter(TextBoxMessage *msg)
{
    if (!msg || !msg->text)
        return;
    u16 delay = msg->typewriter_active ? msg->typewriter_dwell : 0;

    // Seed the live temp.char_delay/space_delay directly: char_delay_init is dead
    // for Text_AddSubtext buffers (the renderer only copies it on a 0x01/0x02 SUBTEXT
    // opcode, which these 0x07-POS-delimited buffers never contain). The renderer
    // reloads temp each render and never clears it, so one write at enqueue persists.
    msg->text->char_delay_init  = delay;
    msg->text->space_delay_init = delay;
    msg->text->temp.char_delay  = delay;
    msg->text->temp.space_delay = delay;

    // Resume from chars_revealed (mirrored from temp.reveal_count each frame), so a
    // scene-change rebuild doesn't re-type finished messages. text_end stays NULL so
    // the engine re-derives the reveal frontier from reveal_count.
    u16 revealed = msg->chars_revealed;
    if (revealed > msg->chars_total)
        revealed = msg->chars_total;
    msg->text->temp.reveal_count = revealed;
    msg->text->text_end          = NULL;
}

// Build a multi-segment Text GObj. Each segment becomes one subtext at the
// computed x cursor position, with its own COLOR opcode (Text_AddSubtext
// captures t->color at emit time).
static Text *CreateTextBoxSegmented(const TextSegment *segs, int seg_count, Vec2 scale, uint lifetime, u8 bg_alpha)
{
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return NULL;

    Text *t = Hoshi_CreateScreenText();
    if (!t)
        return NULL;

    t->kerning = 1;
    t->use_aspect = 1;
    // trans is a placeholder - TextBoxQueue_RepositionAll runs before the next
    // render and is the single source of truth for on-screen position.
    t->trans = (Vec3){0, 0, 0};
    t->viewport_scale = scale;
    // Background quad alpha is independent of text alpha - clamped against the
    // text fade in TextBox_SetAlpha so the bg can't outlast the text.
    t->viewport_color = (GXColor){0, 0, 0, (bg_alpha < lifetime) ? bg_alpha : (u8)lifetime};

    char sanitize_buf[TEXTBOX_SEGMENT_TEXT_SIZE * 2];
    float x_cursor = 0.0f;
    float total_width = 0.0f;
    float line_height = 0.0f;

    for (int i = 0; i < seg_count; i++)
    {
        // t->color is captured by Text_AddSubtext into the subtext's COLOR
        // opcode. Set it BEFORE adding the subtext.
        t->color = (GXColor){segs[i].color.r, segs[i].color.g, segs[i].color.b, lifetime};

        Text_AddSubtext(t, x_cursor, 0, "");

        Text_Sanitize((char *)segs[i].text, sanitize_buf, sizeof(sanitize_buf));
        Text_SetText(t, i, sanitize_buf);

        // Measure this subtext's rendered width (pre-viewport-scale, post-text-scale)
        // so the next segment starts immediately after it on the same line.
        float seg_w = 0, seg_h = 0;
        Text_GetWidthAndHeight(t, i, &seg_w, &seg_h);
        x_cursor += seg_w;
        total_width += seg_w;
        if (seg_h > line_height)
            line_height = seg_h;
    }

    // Aspect must enclose the whole line so the viewport_color background
    // rect (and any future scissor) covers all segments.
    t->aspect = (Vec2){total_width, line_height};

    return t;
}

// Creates the GOBJ for per-frame textbox operations
void CreateTextBox_OnSceneChange()
{
    // Re-create any Text objects that are still in the queue, for persistent
    // message display across scenes
    if (!TextBoxQueue_IsEmpty())
    {
        int count = TextBoxQueue_Count();
        for (int i = 0; i < count; i++)
        {
            TextBoxMessage *msg = TextBoxQueue_GetAt(i);
            if (!msg)
                continue;

            TextSegment segs[TEXTBOX_MAX_SEGMENTS];
            for (int s = 0; s < msg->segment_count; s++)
            {
                segs[s].text = msg->segments[s].text;
                segs[s].color = msg->segments[s].color;
            }
            msg->text = CreateTextBoxSegmented(segs, msg->segment_count, msg->scale, msg->lifetime, msg->bg_alpha_target);
            if (!msg->text)
            {
                OSReport("[TextBox] Failed to recreate textbox on scene change\n");
                continue;
            }

            // Re-snapshot the glyph count and re-arm the built-in typewriter on the
            // rebuilt Text. The engine's reveal_count lived in the old (destroyed)
            // Text, but chars_revealed mirrored it every frame (TextBox_PerFrame), so
            // TextBox_ApplyTypewriter resumes the reveal from there - a finished
            // message stays fully shown instead of re-typing from scratch.
            msg->chars_total = (u16)Sis_CountGlyphs(msg->text->text_start);
            TextBox_ApplyTypewriter(msg);
        }
        // Reposition all recreated messages
        TextBoxQueue_RepositionAll();
    }

    // Init per-frame GOBJ for textbox operations
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
}

// The renderer uses text->color.a as a global alpha modulator - TEXTCMD_COLOR
// only updates temp.color RGB, alpha is sourced from text->color.a at init and
// applied to every glyph in every subtext. So fading the whole textbox =
// touching .a only.
// We must NOT overwrite RGB, or per-segment noun colors would all collapse to white.
//
// Background opacity is set independently from text alpha: bg.a sits at
// `bg_target` until the text fade brings text_alpha below the target, at which
// point bg fades together with the text so it can't outlast the glyphs.
static void TextBox_SetAlpha(Text *text, u8 text_alpha, u8 bg_target)
{
    if (!text)
        return;
    text->color.a          = text_alpha;
    text->viewport_color.a = (text_alpha < bg_target) ? text_alpha : bg_target;
}

// Repositions all messages in the queue based on the selected corner. Top
// corners stack newest at top, older flowing down; bottom corners stack
// newest at bottom, older flowing up. Right corners right-align each
// message individually against the right edge (per-message width since
// each TextBox can have a different width).
//
// Line advance is the rendered text height (aspect.Y * viewport_scale.Y)
// plus an optional fractional gap from the Spacing setting, so Tight always
// lays messages flush regardless of font size and Med/Wide scale their gap
// with the font.
void TextBoxQueue_RepositionAll()
{
    int count = TextBoxQueue_Count();
    if (count == 0)
        return;

    int corner    = Settings_Corner();
    int is_right  = (corner == TEXTBOX_CORNER_TOP_RIGHT  || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);
    int is_bottom = (corner == TEXTBOX_CORNER_BOTTOM_LEFT || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);

    float spacing_extra = Settings_SpacingExtra();

    // edge_y is the canvas y of the next anchor edge - top edge for top
    // corners, bottom edge for bottom corners.
    float edge_y = is_bottom ? (TEXTBOX_CANVAS_H - TEXTBOX_MARGIN) : TEXTBOX_MARGIN;

    for (int i = count - 1; i >= 0; i--)
    {
        TextBoxMessage *t = TextBoxQueue_GetAt(i);
        if (!t || !t->text)
            continue;

        float w_px   = t->text->aspect.X * t->text->viewport_scale.X;
        float text_h = t->text->aspect.Y * t->text->viewport_scale.Y;
        float line_h = text_h * (1.0f + spacing_extra);

        // trans is the top-left of the message bounding box. For bottom
        // corners we shift up by line_h so the message's bottom edge sits
        // at edge_y.
        float trans_x = is_right  ? (TEXTBOX_CANVAS_W - TEXTBOX_MARGIN - w_px) : TEXTBOX_MARGIN;
        float trans_y = is_bottom ? (edge_y - line_h) : edge_y;

        t->text->trans.X = trans_x;
        t->text->trans.Y = trans_y;

        if (is_bottom)
            edge_y -= line_h;
        else
            edge_y += line_h;
    }
}

static void TextBox_PerFrame(GOBJ *g)
{
    if (TextBoxQueue_IsEmpty())
        return;

    // Mirror each visible message's engine-side reveal progress into the message,
    // so it survives the Text being destroyed and recreated on a scene change.
    // Every message reveals independently (the engine paces each Text on its own),
    // so we snapshot the whole queue, not just the oldest.
    int count = TextBoxQueue_Count();
    for (int i = 0; i < count; i++)
    {
        TextBoxMessage *m = TextBoxQueue_GetAt(i);
        if (m && m->text)
            m->chars_revealed = (u16)m->text->temp.reveal_count;
    }

    TextBoxMessage *oldest = TextBoxQueue_GetAt(0);
    if (!oldest || !oldest->text)
        return;
    if (oldest->typewriter_active && oldest->text->temp.reveal_count < oldest->chars_total)
        return;

    // After the player-configured display window since the last removed message,
    // subtract from the alpha value of the oldest message until it reaches 0.
    // When it hits 0, dequeue it.
    if (++textbox_state.framecounter > Settings_DisplayWait())
    {
        if (oldest->lifetime > 0)
        {
            oldest->lifetime--;
            TextBox_SetAlpha(oldest->text, oldest->lifetime, oldest->bg_alpha_target);
        }
        else
        {
            TextBoxMessage text_out;
            TextBox_Dequeue(&text_out);
            textbox_state.framecounter = 0;
        }
    }
}

// Common queue/dequeue/creation logic shared by all enqueue entry points.
static int TextBox_EnqueueInternal(const TextSegment *segs, int seg_count)
{
    if (!textbox_settings.enabled)
        return 0;
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return 0;
    // Hoshi creates the screen canvas in Hook_SceneChange, so callers that
    // fire pre-first-scene (e.g. ChecklistRewards regrant from OnSaveLoaded)
    // would walk an empty canvas list inside Text_CreateText and dereference
    // NULL+0xA. Drop the message instead.
    if (!*stc_textcanvas_first)
    {
        OSReport("[TextBox] Dropping enqueue - no canvas yet (pre-first-scene)\n");
        return 0;
    }

    // Honor the runtime "Max On Screen" cap. The static array can hold up to
    // TEXTBOX_QUEUE_SIZE; the cap is whatever the player picked. Drop oldest
    // until count < cap so the new message fits.
    u8 max_visible = Settings_MaxVisible();
    while (TextBoxQueue_Count() >= max_visible)
    {
        TextBoxMessage removed_text;
        TextBox_Dequeue(&removed_text);
        textbox_state.framecounter = 0;
        OSReport("[TextBox] Visible cap reached, auto-dequeued oldest message.\n");
    }

    // Reset framecounter if adding to an empty queue
    if (TextBoxQueue_IsEmpty())
        textbox_state.framecounter = 0;

    // Build a local segment array so we can apply the Colored Names policy
    // without mutating the caller's buffer. When colors are off, every
    // segment renders in DefaultColor.
    int colored = textbox_settings.colored_names ? 1 : 0;
    TextSegment local_segs[TEXTBOX_MAX_SEGMENTS];
    for (int i = 0; i < seg_count; i++)
    {
        local_segs[i].text  = segs[i].text;
        local_segs[i].color = colored ? segs[i].color : TextBox_DefaultColor;
    }

    TextBoxMessage entry;
    entry.lifetime = 200;
    float font_scale = Settings_FontScale();
    entry.scale = (Vec2){font_scale, font_scale};
    entry.bg_alpha_target = Settings_BgAlphaTarget();
    entry.segment_count = (u8)seg_count;
    for (int i = 0; i < seg_count; i++)
    {
        const char *src = local_segs[i].text ? local_segs[i].text : "";
        strncpy(entry.segments[i].text, src, TEXTBOX_SEGMENT_TEXT_SIZE - 1);
        entry.segments[i].text[TEXTBOX_SEGMENT_TEXT_SIZE - 1] = '\0';
        entry.segments[i].color = local_segs[i].color;
    }

    entry.text = CreateTextBoxSegmented(local_segs, seg_count, entry.scale, entry.lifetime, entry.bg_alpha_target);
    if (!entry.text)
    {
        OSReport("[TextBox] Failed to create Text object!\n");
        return 0;
    }

    // Sample the typewriter setting at enqueue time so per-message behavior
    // stays stable even if the player toggles mid-reveal. chars_total bounds the
    // fade-pause (we hold the fade until temp.reveal_count reaches it).
    entry.typewriter_active = textbox_settings.typewriter_enabled ? 1 : 0;
    entry.typewriter_dwell  = Settings_TypewriterDwell();
    entry.chars_total       = (u16)Sis_CountGlyphs(entry.text->text_start);
    entry.chars_revealed    = 0; // fresh message - reveal starts from the first glyph

    // Hand the message's reveal to the engine's built-in typewriter.
    TextBox_ApplyTypewriter(&entry);

    textbox_state.queue[textbox_state.tail] = entry;
    textbox_state.tail = (textbox_state.tail + 1) % TEXTBOX_QUEUE_SIZE;

    TextBoxQueue_RepositionAll();
    return 1;
}

int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count)
{
    return TextBox_EnqueueInternal(segs, seg_count);
}

int TextBox_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color, const char *suffix)
{
    TextSegment segs[3];
    int n = 0;

    if (prefix && *prefix)
    {
        segs[n].text = prefix;
        segs[n].color = TextBox_DefaultColor;
        n++;
    }
    if (noun && *noun)
    {
        segs[n].text = noun;
        segs[n].color = noun_color;
        n++;
    }
    if (suffix && *suffix)
    {
        segs[n].text = suffix;
        segs[n].color = TextBox_DefaultColor;
        n++;
    }
    if (n == 0)
        return 0;

    return TextBox_EnqueueInternal(segs, n);
}

int TextBox_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                  const char *suffix_format, ...)
{
    char suffix_buf[TEXTBOX_SEGMENT_TEXT_SIZE];
    if (suffix_format)
    {
        va_list args;
        va_start(args, suffix_format);
        vsnprintf(suffix_buf, sizeof(suffix_buf), suffix_format, args);
        va_end(args);
    }
    else
    {
        suffix_buf[0] = '\0';
    }
    return TextBox_EnqueueColoredNoun(prefix, noun, noun_color, suffix_buf);
}

int TextBox_Enqueue(const char *format, ...)
{
    if (!textbox_settings.enabled)
        return 0;

    char buffer[TEXTBOX_SEGMENT_TEXT_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    TextSegment seg = {.text = buffer, .color = TextBox_DefaultColor};
    return TextBox_EnqueueInternal(&seg, 1);
}

// Dequeue a TextBoxMessage object from the textbox queue
static int TextBox_Dequeue(TextBoxMessage *text_out)
{
    if (TextBoxQueue_IsEmpty())
        return 0;

    *text_out = textbox_state.queue[textbox_state.head];
    textbox_state.head = (textbox_state.head + 1) % TEXTBOX_QUEUE_SIZE;

    if (text_out->text)
        Text_Destroy(text_out->text);

    return 1;
}

static int TextBoxQueue_IsEmpty(void)
{
    return textbox_state.head == textbox_state.tail;
}

// Derive count from head and tail indices
static int TextBoxQueue_Count(void)
{
    return (textbox_state.tail - textbox_state.head + TEXTBOX_QUEUE_SIZE) % TEXTBOX_QUEUE_SIZE;
}

// Get a TextBoxMessage at a specific index in the queue (for iteration).
// Index 0 is the head (oldest), count-1 is the newest.
static TextBoxMessage *TextBoxQueue_GetAt(int index)
{
    if (index < 0 || index >= TextBoxQueue_Count())
        return NULL;
    int actual_index = (textbox_state.head + index) % TEXTBOX_QUEUE_SIZE;
    return &textbox_state.queue[actual_index];
}

// Top Ride's `cb_ThinkPostRender` (TopRide_PostRenderCallback @ 0x80009074)
// runs TopRide_CustomRenderer, which kicks off an entirely new HSD_StartRender
// pass for the 2D engine. That pass overwrites the EFB after the standard
// frame render - wiping the textbox drawn by the hoshi screen canvas's
// camera. Re-issuing CObjThink_Common on each canvas's cam_gobj after TR's
// post-render returns redraws the text on top of the second pass.
void TextBox_TopRideReRender(void)
{
    TextCanvas *canvas = *stc_textcanvas_first;
    while (canvas != NULL)
    {
        if (canvas->cam_gobj != NULL)
            CObjThink_Common(canvas->cam_gobj);
        canvas = canvas->next;
    }
}
