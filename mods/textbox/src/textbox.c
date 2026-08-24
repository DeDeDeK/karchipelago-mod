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

// The hoshi screen canvas is ortho 640x480; MARGIN keeps the stack off the screen edges.
#define TEXTBOX_CANVAS_W 640.0f
#define TEXTBOX_CANVAS_H 480.0f
#define TEXTBOX_MARGIN   10.0f

// Lines a single message may wrap onto before its tail is replaced by TEXTBOX_TRUNC_MARK.
#define TEXTBOX_MAX_LINES  3
#define TEXTBOX_TRUNC_MARK ".."

// Text_ConvertASCIIToShiftJIS stops after 128 input bytes, so one subtext holds at most this
// much sanitized text. A character costs 1 byte if alphanumeric and 2 otherwise, so no run of
// TEXTBOX_RUN_CHARS or more can fit and there is never a reason to sanitize past it.
#define TEXTBOX_RUN_BYTES 127
#define TEXTBOX_RUN_CHARS 128

// Preset tables, indexed by the matching settings field.
static const float font_size_scales[] = { 0.30f, 0.40f, 0.55f };
static const u8    typewriter_dwells[] = { 8, 4, 2 };
// Extra vertical gap between stacked messages, as a fraction of the rendered text height.
static const float spacing_extras[] = { 0.0f, 0.25f, 0.5f };
static const u8    bg_alpha_targets[] = { 0, 100, 200 };
static const u8    max_visible_caps[] = { 3, 4, 6, 8 };
static const u16   display_wait_frames[] = { 180, 300, 480 };

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// Bounds-checked accessors, falling back to the Med/Normal preset.
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

// Text* pointers inside each entry are invalidated on scene change and recreated afterwards.
typedef struct
{
    TextBoxMessage queue[TEXTBOX_QUEUE_SIZE];
    uint head;
    uint tail;
    uint framecounter;
} TextBoxState;

static TextBoxState textbox_state;

static void            TextBox_PerFrame(GOBJ *g);
static int             TextBox_Dequeue(TextBoxMessage *text_out);
static int             TextBoxQueue_IsEmpty(void);
static int             TextBoxQueue_Count(void);
static TextBoxMessage *TextBoxQueue_GetAt(int index);

// Byte width of the opcode at a stream position: char codes (>= 0x20) are 2-byte glyphs, the
// rest are SIS control opcodes.
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

// Text_AddSubtext / Text_SetText don't update text->text_end, so the glyph count is derived by
// walking the stream to its inline 0x00 TERMINATE. The result matches the engine's final
// temp.reveal_count. The limit guards against a runaway scan on malformed data.
static int Sis_CountGlyphs(u8 *start)
{
    if (!start)
        return 0;
    int count = 0;
    u8 *p     = start;
    u8 *limit = start + 4096;
    while (p < limit && *p != 0x00) // 0x00 = TERMINATE
    {
        if (*p >= 0x20) // a 2-byte glyph code, and the only thing the typewriter counts
            count++;
        p += Sis_OpWidth(*p);
    }
    return count;
}

// Arms the engine's built-in typewriter, which reveals one glyph every char_delay frames on its
// own. A delay of 0 reveals instantly.
static void TextBox_ApplyTypewriter(TextBoxMessage *msg)
{
    if (!msg || !msg->text)
        return;
    u16 delay = msg->typewriter_active ? msg->typewriter_dwell : 0;

    // The live temp fields must be seeded directly: char_delay_init is only copied in on a
    // 0x01/0x02 SUBTEXT opcode, which these 0x07-POS-delimited buffers never contain. The
    // renderer reloads temp each render and never clears it, so one write at enqueue persists.
    msg->text->char_delay_init  = delay;
    msg->text->space_delay_init = delay;
    msg->text->temp.char_delay  = delay;
    msg->text->temp.space_delay = delay;

    // Resume from chars_revealed so a scene-change rebuild doesn't re-type finished messages.
    // text_end stays NULL so the engine re-derives the reveal frontier from reveal_count.
    u16 revealed = msg->chars_revealed;
    if (revealed > msg->chars_total)
        revealed = msg->chars_total;
    msg->text->temp.reveal_count = revealed;
    msg->text->text_end          = NULL;
}

int TextBox_IsReady(void)
{
    return textbox_settings.enabled && *stc_textcanvas_first != NULL;
}

// Sets subtext `sub` to as much of the first `len` characters of `s` (plus `tail`, if given) as
// one subtext holds, and measures what actually landed. Returns the number of characters of `s`
// placed, short of `len` only when the run hits TEXTBOX_RUN_BYTES. Sanitized text is not in the
// code space Text_GetStringWidth assumes, so widths have to come from the engine.
static int TextBox_SetRun(Text *t, int sub, const char *s, int len, const char *tail,
                          float *out_w, float *out_h)
{
    char raw[TEXTBOX_RUN_CHARS + 8];
    char buf[TEXTBOX_RUN_CHARS * 2 + 16];

    if (len < 0)
        len = 0;
    if (len > TEXTBOX_RUN_CHARS)
        len = TEXTBOX_RUN_CHARS;

    for (;;)
    {
        int n = len;
        memcpy(raw, s, n);
        if (tail)
        {
            for (int i = 0; tail[i] != '\0' && n < (int)sizeof(raw) - 1; i++)
                raw[n++] = tail[i];
        }
        raw[n] = '\0';

        // A sanitize that overflows leaves buf empty, so it reports the whole buffer as its
        // cost: too long to keep, and a ratio that halves the next attempt.
        int bytes = Text_Sanitize(raw, buf, sizeof(buf)) ? (int)strlen(buf) : (int)sizeof(buf);
        if (bytes <= TEXTBOX_RUN_BYTES || len == 0)
            break;

        // Cost is between 1 and 2 bytes per character, so scaling by the overshoot lands within
        // a character or two; the -1 floor keeps it strictly decreasing.
        int next = len * TEXTBOX_RUN_BYTES / bytes;
        len = (next < len) ? next : len - 1;
    }

    Text_SetText(t, sub, buf);

    float w = 0.0f, h = 0.0f;
    Text_GetWidthAndHeight(t, sub, &w, &h);
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;

    return len;
}

static int PrevSpace(const char *s, int from)
{
    for (int i = from; i > 0; i--)
        if (s[i] == ' ')
            return i;
    return -1;
}

static int NextSpace(const char *s, int from)
{
    for (int i = from + 1; s[i] != '\0'; i++)
        if (s[i] == ' ')
            return i;
    return -1;
}

// Longest prefix of `s` that renders within `avail`, broken at a space where one is available.
// Leaves subtext `sub` holding that prefix and its width in *out_w. Returns 0 when nothing fits
// and the caller should start a new line first; at a line start it always takes at least one
// character, so the walk cannot stall. The first measurement covers the whole run, so a segment
// that already fits costs exactly one measure.
static int TextBox_FitRun(Text *t, int sub, const char *s, int len, float avail,
                          int at_line_start, float *out_w)
{
    float w = 0.0f;
    // Everything below searches within what one subtext can hold, so a run the engine had to
    // trim wraps at the trim rather than reporting a fit for text it never measured.
    int placed = TextBox_SetRun(t, sub, s, len, NULL, &w, NULL);
    int capped = (placed < len);
    len = placed;

    if (w <= avail)
    {
        // A run the engine trimmed still breaks like any other: at a space where there is one.
        if (capped)
        {
            int brk = PrevSpace(s, len - 1);
            if (brk > 0)
                len = TextBox_SetRun(t, sub, s, brk, NULL, &w, NULL);
        }
        *out_w = w;
        return len;
    }

    // Width is near enough to linear in character count to seed the search within a word or two.
    int est = (w > 0.0f) ? (int)((float)len * (avail / w)) : 0;
    if (est >= len)
        est = len - 1;
    if (est < 0)
        est = 0;

    int brk = PrevSpace(s, est);
    while (brk > 0)
    {
        TextBox_SetRun(t, sub, s, brk, NULL, &w, NULL);
        if (w <= avail)
            break;
        brk = PrevSpace(s, brk - 1);
    }

    if (brk > 0)
    {
        for (;;)
        {
            int nxt = NextSpace(s, brk);
            if (nxt < 0 || nxt > len)
                nxt = len;

            float w2 = 0.0f;
            TextBox_SetRun(t, sub, s, nxt, NULL, &w2, NULL);
            if (w2 > avail)
            {
                TextBox_SetRun(t, sub, s, brk, NULL, &w, NULL);
                break;
            }
            brk = nxt;
            w = w2;
            if (nxt == len)
                break;
        }
        *out_w = w;
        return brk;
    }

    if (!at_line_start)
        return 0;

    // A single word wider than a whole line, so it splits mid-word.
    int n = (est > 0) ? est : 1;
    for (;;)
    {
        TextBox_SetRun(t, sub, s, n, NULL, &w, NULL);
        if (w <= avail || n <= 1)
            break;
        n--;
    }
    *out_w = w;
    return n;
}

// Build a multi-segment Text GObj: one subtext per run of a segment that shares a line. Segments
// flow left to right and wrap onto up to TEXTBOX_MAX_LINES lines; text past the last line is
// replaced by TEXTBOX_TRUNC_MARK. Nothing is ever scaled down to fit - the chosen font size is
// what renders.
static Text *CreateTextBoxSegmented(const TextSegment *segs, int seg_count, Vec2 scale, uint lifetime, u8 bg_alpha)
{
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return NULL;

    Text *t = Hoshi_CreateScreenText();
    if (!t)
        return NULL;

    t->kerning = 1;
    // A placeholder - TextBoxQueue_RepositionAll runs before the next render and is the single
    // source of truth for on-screen position.
    t->trans = (Vec3){0, 0, 0};
    t->viewport_scale = scale;
    // The background quad's alpha is independent of the text alpha, but clamped against it so
    // the panel can't outlast the glyphs.
    t->viewport_color = (GXColor){0, 0, 0, (bg_alpha < lifetime) ? bg_alpha : (u8)lifetime};

    // Subtext positions and measured widths are in pre-viewport-scale units, so the pixel budget
    // is divided through rather than the widths multiplied up.
    float budget = (scale.X > 0.0f) ? (TEXTBOX_CANVAS_W - 2.0f * TEXTBOX_MARGIN) / scale.X : 0.0f;

    float x       = 0.0f;
    float widest  = 0.0f;
    float line_h  = 0.0f;
    float trunc_w = -1.0f;
    int line      = 0;
    int last_line = 0;
    int sub       = 0;
    int sub_open  = 0;
    int done      = 0;

    for (int i = 0; i < seg_count && !done; i++)
    {
        const char *p = segs[i].text;
        if (!p)
            continue;

        while (*p != '\0' && !done)
        {
            // A line never opens with a space, including when a fresh segment lands on one.
            if (x <= 0.0f)
            {
                while (*p == ' ')
                    p++;
                if (*p == '\0')
                {
                    // Text_AddSubtext baked this segment's color into any subtext opened for it,
                    // so the next segment must not inherit it.
                    if (sub_open)
                    {
                        sub++;
                        sub_open = 0;
                    }
                    break;
                }
            }

            if (!sub_open)
            {
                // Text_AddSubtext captures t->color into the subtext's COLOR opcode, so it must
                // be set before the subtext is added.
                t->color = (GXColor){segs[i].color.r, segs[i].color.g, segs[i].color.b, lifetime};
                Text_AddSubtext(t, 0, 0, "");
                sub_open = 1;
            }

            int   len   = (int)strlen(p);
            int   final = (line >= TEXTBOX_MAX_LINES - 1);
            float w     = 0.0f;
            int   take  = TextBox_FitRun(t, sub, p, len, budget - x, x <= 0.0f, &w);

            if (take == 0 && !final)
            {
                x = 0.0f;
                line++;
                continue;
            }

            // Anything still unplaced once the last line is reached gives way to the marker,
            // which is fitted with room reserved for itself.
            int truncated = 0;
            if (final && (take < len || i + 1 < seg_count))
            {
                if (trunc_w < 0.0f)
                    TextBox_SetRun(t, sub, TEXTBOX_TRUNC_MARK, sizeof(TEXTBOX_TRUNC_MARK) - 1,
                                   NULL, &trunc_w, NULL);

                float avail = budget - x - trunc_w;
                take = (avail > 0.0f) ? TextBox_FitRun(t, sub, p, len, avail, 1, &w) : 0;
                TextBox_SetRun(t, sub, p, take, TEXTBOX_TRUNC_MARK, &w, NULL);
                truncated = 1;
            }

            if (line_h <= 0.0f)
            {
                float dw = 0.0f;
                Text_GetWidthAndHeight(t, sub, &dw, &line_h);
            }

            Text_SetSubtextPos(t, sub, (int)(x + 0.5f), (int)((float)line * line_h + 0.5f));
            x += w;
            if (x > widest)
                widest = x;
            last_line = line;
            sub++;
            sub_open = 0;

            if (truncated)
            {
                done = 1;
                break;
            }

            p += take;
            while (*p == ' ')
                p++;
            if (*p != '\0')
            {
                x = 0.0f;
                line++;
            }
        }
    }

    // Aspect must enclose every line, or the viewport_color background rect won't cover the
    // whole message.
    t->aspect = (Vec2){widest, line_h * (float)(last_line + 1)};

    return t;
}

// Points `segs` at the stored blob's NUL-terminated runs. Both the first render and the
// scene-change rebuild go through here, so a message can never draw differently the second time.
static int TextBox_MessageSegments(const TextBoxMessage *msg, TextSegment *segs)
{
    int pos = 0;
    for (int i = 0; i < msg->segment_count; i++)
    {
        segs[i].text  = &msg->segment_text[pos];
        segs[i].color = msg->colors[i];
        pos += (int)strlen(&msg->segment_text[pos]) + 1;
    }
    return msg->segment_count;
}

// Rebuilds the queued Text objects invalidated by the scene change, then installs the per-frame
// GObj.
void CreateTextBox_OnSceneChange()
{
    if (!TextBoxQueue_IsEmpty())
    {
        int count = TextBoxQueue_Count();
        for (int i = 0; i < count; i++)
        {
            TextBoxMessage *msg = TextBoxQueue_GetAt(i);
            if (!msg)
                continue;

            TextSegment segs[TEXTBOX_MAX_SEGMENTS];
            int n = TextBox_MessageSegments(msg, segs);
            msg->text = CreateTextBoxSegmented(segs, n, msg->scale, msg->lifetime, msg->bg_alpha_target);
            if (!msg->text)
            {
                OSReport("[TextBox] Failed to recreate textbox on scene change\n");
                continue;
            }

            // The engine's reveal_count died with the old Text, so the reveal resumes from the
            // mirrored chars_revealed instead of re-typing from scratch.
            msg->chars_total = (u16)Sis_CountGlyphs(msg->text->text_start);
            TextBox_ApplyTypewriter(msg);
        }
        TextBoxQueue_RepositionAll();
    }

    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
}

// text->color.a is a global alpha modulator: TEXTCMD_COLOR only updates temp.color RGB, while
// alpha comes from text->color.a and applies to every glyph in every subtext. So a fade touches
// .a only - overwriting RGB would collapse the per-segment noun colors to white.
static void TextBox_SetAlpha(Text *text, u8 text_alpha, u8 bg_target)
{
    if (!text)
        return;
    text->color.a          = text_alpha;
    text->viewport_color.a = (text_alpha < bg_target) ? text_alpha : bg_target;
}

// Top corners stack newest at top with older flowing down, bottom corners the reverse; right
// corners right-align each message individually, since messages differ in width.
void TextBoxQueue_RepositionAll()
{
    int count = TextBoxQueue_Count();
    if (count == 0)
        return;

    int corner    = Settings_Corner();
    int is_right  = (corner == TEXTBOX_CORNER_TOP_RIGHT  || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);
    int is_bottom = (corner == TEXTBOX_CORNER_BOTTOM_LEFT || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);

    float spacing_extra = Settings_SpacingExtra();

    // Canvas y of the next anchor edge.
    float edge_y = is_bottom ? (TEXTBOX_CANVAS_H - TEXTBOX_MARGIN) : TEXTBOX_MARGIN;

    for (int i = count - 1; i >= 0; i--)
    {
        TextBoxMessage *t = TextBoxQueue_GetAt(i);
        if (!t || !t->text)
            continue;

        float w_px   = t->text->aspect.X * t->text->viewport_scale.X;
        float text_h = t->text->aspect.Y * t->text->viewport_scale.Y;
        float line_h = text_h * (1.0f + spacing_extra);

        // trans is the top-left of the message bounding box, so bottom corners shift up by
        // line_h to put the message's bottom edge at edge_y.
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

    // Mirror the engine-side reveal progress so it survives a scene change. The engine paces
    // each Text independently, so the whole queue is snapshotted, not just the oldest.
    int count = TextBoxQueue_Count();
    for (int i = 0; i < count; i++)
    {
        TextBoxMessage *m = TextBoxQueue_GetAt(i);
        if (m && m->text)
            m->chars_revealed = (u16)m->text->temp.reveal_count;
    }

    TextBoxMessage *oldest = TextBoxQueue_GetAt(0);
    if (!oldest)
        return;

    // Nothing to reveal or fade if the scene-change rebuild failed, so drop it now instead of
    // holding the whole queue behind a message that will never age out.
    if (!oldest->text)
    {
        TextBoxMessage dead;
        TextBox_Dequeue(&dead);
        textbox_state.framecounter = 0;
        return;
    }
    if (oldest->typewriter_active && oldest->text->temp.reveal_count < oldest->chars_total)
        return;

    // Once the display window since the last removal elapses, fade the oldest message out and
    // dequeue it at zero alpha.
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

// Shared by every enqueue entry point.
static int TextBox_EnqueueInternal(const TextSegment *segs, int seg_count)
{
    if (!textbox_settings.enabled)
        return 0;
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return 0;
    // Hoshi creates the screen canvas on scene change, so a caller that fires before the first
    // one would walk an empty canvas list inside Text_CreateText and dereference NULL+0xA.
    if (!*stc_textcanvas_first)
    {
        OSReport("[TextBox] Dropping enqueue - no canvas yet (pre-first-scene)\n");
        return 0;
    }

    // Drop oldest until the new message fits under the player's "Max On Screen" cap.
    u8 max_visible = Settings_MaxVisible();
    while (TextBoxQueue_Count() >= max_visible)
    {
        TextBoxMessage removed_text;
        TextBox_Dequeue(&removed_text);
        textbox_state.framecounter = 0;
    }

    if (TextBoxQueue_IsEmpty())
        textbox_state.framecounter = 0;

    TextBoxMessage entry;
    entry.lifetime = 200;
    float font_scale = Settings_FontScale();
    entry.scale = (Vec2){font_scale, font_scale};
    entry.bg_alpha_target = Settings_BgAlphaTarget();

    // Copied in first: the caller's strings need not outlive the call, and applying the Colored
    // Names setting here keeps it out of the caller's buffer. A message longer than the blob
    // loses its tail segments rather than any segment losing its tail.
    int colored = textbox_settings.colored_names ? 1 : 0;
    int pos = 0;
    entry.segment_count = 0;
    for (int i = 0; i < seg_count; i++)
    {
        const char *src = segs[i].text ? segs[i].text : "";
        int room = TEXTBOX_MESSAGE_TEXT_SIZE - 1 - pos;
        int n    = (int)strlen(src);
        if (room <= 0)
            break;
        if (n > room)
            n = room;
        memcpy(&entry.segment_text[pos], src, n);
        entry.segment_text[pos + n] = '\0';
        entry.colors[i] = colored ? segs[i].color : TextBox_DefaultColor;
        pos += n + 1;
        entry.segment_count++;
    }

    TextSegment stored_segs[TEXTBOX_MAX_SEGMENTS];
    int stored_count = TextBox_MessageSegments(&entry, stored_segs);
    entry.text = CreateTextBoxSegmented(stored_segs, stored_count, entry.scale, entry.lifetime, entry.bg_alpha_target);
    if (!entry.text)
    {
        OSReport("[TextBox] Failed to create the Text object\n");
        return 0;
    }

    // Sampled at enqueue so per-message behavior stays stable if the player toggles mid-reveal.
    entry.typewriter_active = textbox_settings.typewriter_enabled ? 1 : 0;
    entry.typewriter_dwell  = Settings_TypewriterDwell();
    entry.chars_total       = (u16)Sis_CountGlyphs(entry.text->text_start);
    entry.chars_revealed    = 0;

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
    char suffix_buf[TEXTBOX_MESSAGE_TEXT_SIZE];
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

    char buffer[TEXTBOX_MESSAGE_TEXT_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    TextSegment seg = {.text = buffer, .color = TextBox_DefaultColor};
    return TextBox_EnqueueInternal(&seg, 1);
}

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

static int TextBoxQueue_Count(void)
{
    return (textbox_state.tail - textbox_state.head + TEXTBOX_QUEUE_SIZE) % TEXTBOX_QUEUE_SIZE;
}

// Index 0 is the head (oldest), count-1 the newest.
static TextBoxMessage *TextBoxQueue_GetAt(int index)
{
    if (index < 0 || index >= TextBoxQueue_Count())
        return NULL;
    int actual_index = (textbox_state.head + index) % TEXTBOX_QUEUE_SIZE;
    return &textbox_state.queue[actual_index];
}

// TopRide_PostRenderCallback (0x80009074) runs TopRide_CustomRenderer, whose second
// HSD_StartRender pass overwrites the EFB and wipes the screen canvas. Re-issuing
// CObjThink_Common on each canvas cam redraws the text on top of that pass.
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
