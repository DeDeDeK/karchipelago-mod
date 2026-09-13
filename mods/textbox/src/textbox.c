#include <stdarg.h>
#include <string.h>
#include "text.h"
#include "text_joint/text_joint.h"
#include "obj.h"
#include "hoshi/screen_cam.h"

#include "textbox.h"
#include "textbox_colors.h"

TextBoxSettings textbox_settings = {
    .enabled            = 1,
    .typewriter         = 3,
    .font_size          = 1,
    .colored_names      = 1,
    .message_spacing    = 0,
    .background_opacity = 2,
    .max_visible        = 2,
    .display_time       = 1,
    .corner             = TEXTBOX_CORNER_TOP_LEFT,
};

#define TEXTBOX_MARGIN 10.0f

#define TEXTBOX_MAX_LINES  3
#define TEXTBOX_TRUNC_MARK ".."

// Text_ConvertASCIIToShiftJIS (0x8044fb0c) reads at most 128 input bytes, and writes its output
// into the 128 bytes below the input it is still reading while emitting up to 3 bytes per
// character. Once a run's output runs more than that ahead of its input, the converter overtakes
// its own read pointer and re-reads emitted bytes as text, so both limits bound one subtext.
#define TEXTBOX_RUN_BYTES     127
#define TEXTBOX_CONVERT_SLACK 128
#define TEXTBOX_RUN_CHARS     128

static const float font_size_scales[]    = { 0.30f, 0.40f, 0.55f };
static const u8    typewriter_dwells[]   = { 0, 8, 4, 2 };
// Extra vertical gap between stacked messages, as a fraction of the rendered text height.
static const float spacing_extras[]      = { 0.0f, 0.25f, 0.5f };
static const u8    bg_alpha_targets[]    = { 0, 100, 200 };
static const u8    max_visible_caps[]    = { 3, 4, 6, 8 };
static const u16   display_wait_frames[] = { 180, 300, 480 };

// Settings are indices into the preset tables; a corrupt value falls back to the option's default.
static int TextBox_SettingIndex(int val, int count, int fallback)
{
    return (val >= 0 && val < count) ? val : fallback;
}

#define TEXTBOX_PRESET(table, setting, fallback) \
    ((table)[TextBox_SettingIndex((setting), GetElementsIn(table), (fallback))])

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
static void            TextBox_Dequeue(void);
static int             TextBoxQueue_IsEmpty(void);
static int             TextBoxQueue_Count(void);
static TextBoxMessage *TextBoxQueue_GetAt(int index);

// Byte width of the opcode at a stream position; codes >= TEXTCMD_NUM are 2-byte glyphs.
static int Sis_OpWidth(u8 op)
{
    if (op >= TEXTCMD_NUM)
        return 2;
    switch (op)
    {
        case TEXTCMD_TIMING:
        case TEXTCMD_POS:
        case TEXTCMD_JUMP:
        case TEXTCMD_CALL:
        case TEXTCMD_POSPUSH:
        case TEXTCMD_SCALE:    return 5;
        case TEXTCMD_COLOR:    return 4;
        case TEXTCMD_DELAY:    return 3;
        default:               return 1;
    }
}

// Text_AddSubtext / Text_SetText never write text->text_end, so the glyph count is walked out of
// the stream. The limit guards against a runaway scan on malformed data.
static int Sis_CountGlyphs(u8 *start)
{
    if (!start)
        return 0;
    int count = 0;
    u8 *p     = start;
    u8 *limit = start + 4096;
    while (p < limit && *p != TEXTCMD_TERMINATE)
    {
        if (*p >= TEXTCMD_NUM) // the only thing the typewriter counts
            count++;
        p += Sis_OpWidth(*p);
    }
    return count;
}

// Output bytes Text_ConvertASCIIToShiftJIS emits for an already-sanitized run. A letter costs a
// TEXTCMD_POSPUSHEND plus its 2-byte code; a digit entering tight-spacing mode pays a 5-byte
// TEXTCMD_POSPUSH first, and stays at 2 bytes while it holds.
static int TextBox_ConvertCost(const char *s)
{
    const u8 *p = (const u8 *)s;
    int cost  = 0;
    int tight = 0;

    while (*p != '\0')
    {
        if (*p >= 0x80 && p[1] != '\0') // a 2-byte code Text_Sanitize already emitted
        {
            cost += 3;
            tight = 0;
            p += 2;
        }
        else if ((*p >= '0' && *p <= '9') || *p == '.')
        {
            cost += tight ? 2 : 7;
            tight = 1;
            p++;
        }
        else
        {
            cost += 3;
            tight = 0;
            p++;
        }
    }
    return cost;
}

// Arms the engine's built-in typewriter; a dwell of 0 reveals instantly.
static void TextBox_ApplyTypewriter(TextBoxMessage *msg)
{
    if (!msg || !msg->text)
        return;

    // Text_GX only copies char_delay_init across at a TEXTCMD_SUBTEXT_RESET/BREAK (0x80451cec),
    // which these TEXTCMD_POS-delimited buffers never contain, so the live temp fields are seeded
    // directly. The renderer reloads temp each render and never clears it, so one write persists.
    msg->text->temp.char_delay  = msg->typewriter_dwell;
    msg->text->temp.space_delay = msg->typewriter_dwell;

    u16 revealed = msg->chars_revealed;
    if (revealed > msg->chars_total)
        revealed = msg->chars_total;
    // text_end stays NULL so the engine re-derives the reveal frontier from reveal_count.
    msg->text->temp.reveal_count = revealed;
    msg->text->text_end          = NULL;
}

int TextBox_IsReady(void)
{
    return textbox_settings.enabled && *stc_textcanvas_first != NULL;
}

// Sets subtext `sub` to as much of the first `len` characters of `s` (plus `tail`, if given) as one
// subtext holds, and measures what actually landed. Returns the characters of `s` placed, short of
// `len` only when the run hits an engine limit. Sanitized text is not in the code space
// Text_GetStringWidth assumes, so widths have to come from the engine.
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

        // A failed sanitize overflowed, so charge the whole buffer: too long to keep, and a ratio
        // that shrinks the next attempt hard.
        int ok    = Text_Sanitize(raw, buf, sizeof(buf));
        int bytes = ok ? (int)strlen(buf) : (int)sizeof(buf);
        int slack = ok ? TextBox_ConvertCost(buf) - bytes : (int)sizeof(buf);

        if ((bytes <= TEXTBOX_RUN_BYTES && slack <= TEXTBOX_CONVERT_SLACK) || len == 0)
            break;

        // Both costs are near enough to linear in character count that scaling by the overshoot
        // lands within a character or two; the -1 floor keeps the search strictly decreasing.
        int next = len;
        if (bytes > TEXTBOX_RUN_BYTES)
        {
            int by_bytes = len * TEXTBOX_RUN_BYTES / bytes;
            if (by_bytes < next)
                next = by_bytes;
        }
        if (slack > TEXTBOX_CONVERT_SLACK)
        {
            int by_slack = len * TEXTBOX_CONVERT_SLACK / slack;
            if (by_slack < next)
                next = by_slack;
        }
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

static int TextBox_PrevSpace(const char *s, int from)
{
    for (int i = from; i > 0; i--)
        if (s[i] == ' ')
            return i;
    return -1;
}

static int TextBox_NextSpace(const char *s, int from)
{
    for (int i = from + 1; s[i] != '\0'; i++)
        if (s[i] == ' ')
            return i;
    return -1;
}

// Longest prefix of `s` that fits `avail`, broken at a space where there is one. Returns 0 when
// nothing fits and the caller must open a new line; at a line start it always takes at least one
// character, so the walk cannot stall.
static int TextBox_FitRun(Text *t, int sub, const char *s, int len, float avail,
                          int at_line_start, float *out_w)
{
    float w = 0.0f;
    int placed = TextBox_SetRun(t, sub, s, len, NULL, &w, NULL);
    int capped = (placed < len);
    len = placed;

    if (w <= avail)
    {
        if (capped)
        {
            int brk = TextBox_PrevSpace(s, len - 1);
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

    int brk = TextBox_PrevSpace(s, est);
    while (brk > 0)
    {
        TextBox_SetRun(t, sub, s, brk, NULL, &w, NULL);
        if (w <= avail)
            break;
        brk = TextBox_PrevSpace(s, brk - 1);
    }

    if (brk > 0)
    {
        for (;;)
        {
            int nxt = TextBox_NextSpace(s, brk);
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

// One Text GObj: a subtext per run of a segment that shares a line, wrapping onto at most
// TEXTBOX_MAX_LINES. Nothing is ever scaled down to fit.
static Text *TextBox_CreateSegmented(const TextSegment *segs, int seg_count, Vec2 scale, uint lifetime, u8 bg_alpha)
{
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return NULL;

    Text *t = Hoshi_CreateScreenText();
    if (!t)
        return NULL;

    t->kerning = 1;
    // Placeholder; TextBoxQueue_RepositionAll owns on-screen position.
    t->trans = (Vec3){0, 0, 0};
    t->viewport_scale = scale;
    // Background alpha is clamped against text alpha so the panel can't outlast the glyphs.
    t->viewport_color = (GXColor){0, 0, 0, (bg_alpha < lifetime) ? bg_alpha : (u8)lifetime};

    // Measured widths are pre-viewport-scale units, so the pixel budget is divided through.
    float budget = (scale.X > 0.0f) ? (TEXT_CANVAS_W - 2.0f * TEXTBOX_MARGIN) / scale.X : 0.0f;

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
            if (x <= 0.0f)
            {
                while (*p == ' ')
                    p++;
                if (*p == '\0')
                {
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
                // Text_AddSubtext captures t->color into the subtext's COLOR opcode.
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

            // On the last line the tail gives way to the marker, fitted with room reserved for it.
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

    // aspect sizes the viewport_color background rect, so it must enclose every line.
    t->aspect = (Vec2){widest, line_h * (float)(last_line + 1)};

    return t;
}

// Points `segs` at the stored blob's NUL-terminated runs.
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

void TextBox_OnSceneChange()
{
    int count = TextBoxQueue_Count();

    // The scene reset freed every Text with the old SIS heap. Clearing first means an enqueue that
    // interleaves with the rebuild sees NULL rather than a pointer into the new heap's memory.
    for (int i = 0; i < count; i++)
        TextBoxQueue_GetAt(i)->text = NULL;

    int failed = 0;
    for (int i = 0; i < count; i++)
    {
        TextBoxMessage *msg = TextBoxQueue_GetAt(i);

        TextSegment segs[TEXTBOX_MAX_SEGMENTS];
        int n = TextBox_MessageSegments(msg, segs);
        msg->text = TextBox_CreateSegmented(segs, n, msg->scale, msg->lifetime, msg->bg_alpha_target);
        if (!msg->text)
        {
            failed++;
            continue;
        }

        msg->chars_total = (u16)Sis_CountGlyphs(msg->text->text_start);
        TextBox_ApplyTypewriter(msg);
    }

    if (failed != 0)
    {
        static u8 rebuild_warned;
        if (!rebuild_warned)
        {
            rebuild_warned = 1;
            OSReport("[TextBox] Failed to rebuild %d of %d messages on scene change\n", failed, count);
        }
    }

    TextBoxQueue_RepositionAll();

    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
}

// text->color.a is a global alpha modulator and COLOR opcodes carry no alpha, so a fade touches
// .a alone - overwriting RGB would collapse the per-segment noun colors to white.
static void TextBox_SetAlpha(Text *text, u8 text_alpha, u8 bg_target)
{
    if (!text)
        return;
    text->color.a          = text_alpha;
    text->viewport_color.a = (text_alpha < bg_target) ? text_alpha : bg_target;
}

// Newest sits at the anchor corner and older flows away from it; right corners right-align each
// message individually, since messages differ in width.
void TextBoxQueue_RepositionAll(void)
{
    int count = TextBoxQueue_Count();
    if (count == 0)
        return;

    int corner    = TextBox_SettingIndex(textbox_settings.corner, TEXTBOX_CORNER_NUM,
                                         TEXTBOX_CORNER_TOP_LEFT);
    int is_right  = (corner == TEXTBOX_CORNER_TOP_RIGHT  || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);
    int is_bottom = (corner == TEXTBOX_CORNER_BOTTOM_LEFT || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);

    float spacing_extra = TEXTBOX_PRESET(spacing_extras, textbox_settings.message_spacing, 0);

    // Canvas y of the next anchor edge.
    float edge_y = is_bottom ? (TEXT_CANVAS_H - TEXTBOX_MARGIN) : TEXTBOX_MARGIN;

    for (int i = count - 1; i >= 0; i--)
    {
        TextBoxMessage *t = TextBoxQueue_GetAt(i);
        if (!t->text)
            continue;

        float w_px   = t->text->aspect.X * t->text->viewport_scale.X;
        float text_h = t->text->aspect.Y * t->text->viewport_scale.Y;
        float line_h = text_h * (1.0f + spacing_extra);

        // trans is the message's top-left.
        float trans_x = is_right  ? (TEXT_CANVAS_W - TEXTBOX_MARGIN - w_px) : TEXTBOX_MARGIN;
        float trans_y = is_bottom ? (edge_y - line_h) : edge_y;

        t->text->trans.X = trans_x;
        t->text->trans.Y = trans_y;

        if (is_bottom)
            edge_y -= line_h;
        else
            edge_y += line_h;
    }
}

void TextBoxQueue_TrimToCap(void)
{
    u8 max_visible = TEXTBOX_PRESET(max_visible_caps, textbox_settings.max_visible, 2);
    while (TextBoxQueue_Count() > max_visible)
    {
        TextBox_Dequeue();
        textbox_state.framecounter = 0;
    }
    TextBoxQueue_RepositionAll();
}

void TextBoxQueue_Flush(void)
{
    while (!TextBoxQueue_IsEmpty())
        TextBox_Dequeue();
    textbox_state.framecounter = 0;
}

static void TextBox_PerFrame(GOBJ *g)
{
    if (TextBoxQueue_IsEmpty())
        return;

    // The engine paces each Text independently, so the whole queue is snapshotted, not just the
    // oldest, and the mirror is what lets a scene-change rebuild resume instead of re-typing.
    int count = TextBoxQueue_Count();
    for (int i = 0; i < count; i++)
    {
        TextBoxMessage *m = TextBoxQueue_GetAt(i);
        if (m->text)
            m->chars_revealed = (u16)m->text->temp.reveal_count;
    }

    TextBoxMessage *oldest = TextBoxQueue_GetAt(0);

    // A failed rebuild has nothing to reveal or fade, and would hold the whole queue behind it.
    if (!oldest->text)
    {
        TextBox_Dequeue();
        textbox_state.framecounter = 0;
        return;
    }
    if (oldest->typewriter_dwell != 0 && oldest->text->temp.reveal_count < oldest->chars_total)
        return;

    if (++textbox_state.framecounter > TEXTBOX_PRESET(display_wait_frames, textbox_settings.display_time, 1))
    {
        if (oldest->lifetime > 0)
        {
            oldest->lifetime--;
            TextBox_SetAlpha(oldest->text, oldest->lifetime, oldest->bg_alpha_target);
        }
        else
        {
            TextBox_Dequeue();
            textbox_state.framecounter = 0;
        }
    }
}

int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count)
{
    if (!textbox_settings.enabled)
        return 0;
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return 0;
    // Text_CreateTextManual (0x8044f198) reads the canvas list head with no NULL check, so an enqueue
    // before hoshi creates the canvas on the first scene change faults.
    if (!*stc_textcanvas_first)
    {
        static u8 no_canvas_warned;
        if (!no_canvas_warned)
        {
            no_canvas_warned = 1;
            OSReport("[TextBox] Dropping enqueue - no canvas yet (pre-first-scene)\n");
        }
        return 0;
    }

    u8 max_visible = TEXTBOX_PRESET(max_visible_caps, textbox_settings.max_visible, 2);
    while (TextBoxQueue_Count() >= max_visible)
    {
        TextBox_Dequeue();
        textbox_state.framecounter = 0;
    }

    if (TextBoxQueue_IsEmpty())
        textbox_state.framecounter = 0;

    TextBoxMessage entry;
    entry.lifetime = 200;
    float font_scale = TEXTBOX_PRESET(font_size_scales, textbox_settings.font_size, 1);
    entry.scale = (Vec2){font_scale, font_scale};
    entry.bg_alpha_target = TEXTBOX_PRESET(bg_alpha_targets, textbox_settings.background_opacity, 2);

    // Copied in first: the caller's strings need not outlive the call, and applying the Colored
    // Names setting here keeps it out of the caller's buffer. A segment that overruns the blob is
    // truncated and the segments after it are dropped.
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
    entry.text = TextBox_CreateSegmented(stored_segs, stored_count, entry.scale, entry.lifetime,
                                         entry.bg_alpha_target);
    if (!entry.text)
    {
        static u8 create_warned;
        if (!create_warned)
        {
            create_warned = 1;
            OSReport("[TextBox] Failed to create Text object\n");
        }
        return 0;
    }

    // Sampled at enqueue so per-message behavior stays stable if the player retunes mid-reveal.
    entry.typewriter_dwell = TEXTBOX_PRESET(typewriter_dwells, textbox_settings.typewriter, 3);
    entry.chars_total      = (u16)Sis_CountGlyphs(entry.text->text_start);
    entry.chars_revealed   = 0;

    TextBox_ApplyTypewriter(&entry);

    textbox_state.queue[textbox_state.tail] = entry;
    textbox_state.tail = (textbox_state.tail + 1) % TEXTBOX_QUEUE_SIZE;

    TextBoxQueue_RepositionAll();
    return 1;
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

    return TextBox_EnqueueSegments(segs, n);
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
    char buffer[TEXTBOX_MESSAGE_TEXT_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    TextSegment seg = {.text = buffer, .color = TextBox_DefaultColor};
    return TextBox_EnqueueSegments(&seg, 1);
}

static void TextBox_Dequeue(void)
{
    if (TextBoxQueue_IsEmpty())
        return;

    TextBoxMessage *msg = &textbox_state.queue[textbox_state.head];
    textbox_state.head = (textbox_state.head + 1) % TEXTBOX_QUEUE_SIZE;

    if (msg->text)
    {
        Text_Destroy(msg->text);
        msg->text = NULL;
    }
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

// TopRide_CustomRenderer's second HSD_StartRender pass overdraws the EFB and wipes the screen
// canvas, so each canvas cam is re-issued on top of it.
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
