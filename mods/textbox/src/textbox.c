#include <stdarg.h>
#include <string.h>
#include "text.h"
#include "text_joint/text_joint.h"
#include "obj.h"
#include "hoshi/screen_cam.h"

#include "textbox.h"
#include "textbox_colors.h"

// One slot reserved to tell empty from full; capacity 8 matches the highest "Max On Screen".
#define TEXTBOX_QUEUE_SIZE 9

#define TEXTBOX_MESSAGE_TEXT_SIZE 248

#define TEXTBOX_MARGIN 10.0f

#define TEXTBOX_MAX_LINES  3
#define TEXTBOX_TRUNC_MARK ".."

// Text_ConvertASCIIToShiftJIS (0x8044fb0c) reads at most 128 input bytes, which bounds one subtext.
#define TEXTBOX_RUN_BYTES 127
#define TEXTBOX_RUN_CHARS 128

typedef struct TextBoxMessage
{
    char segment_text[TEXTBOX_MESSAGE_TEXT_SIZE]; // segment_count NUL-terminated strings back to back
    GXColor colors[TEXTBOX_MAX_SEGMENTS];
    u8 segment_count;
    u8 lifetime;             // seeds peak text alpha (200), then counts down as the fade timer
    float scale;             // viewport_scale on both axes
    Text *text;              // rebuilt on every scene change

    u16 chars_total;         // fade is held until temp.reveal_count reaches this
    u16 chars_revealed;      // mirrors temp.reveal_count so a scene-change rebuild can resume
    u8 typewriter_dwell;     // frames per glyph reveal, 0 revealing instantly
    u8 bg_alpha_target;      // background quad alpha when fully visible
} TextBoxMessage;

typedef enum TextBoxCorner
{
    TEXTBOX_CORNER_TOP_LEFT,
    TEXTBOX_CORNER_TOP_RIGHT,
    TEXTBOX_CORNER_BOTTOM_LEFT,
    TEXTBOX_CORNER_BOTTOM_RIGHT,
    TEXTBOX_CORNER_NUM,
} TextBoxCorner;

// Each is an index into its option's preset table below, except max_visible, a count.
static struct
{
    int enabled;
    int corner;
    int font_size;
    int colored_names;
    int background;
    int spacing;
    int max_visible;
    int display_time;
    int typewriter;
} textbox_settings = {
    .enabled       = 1,
    .corner        = TEXTBOX_CORNER_TOP_LEFT,
    .font_size     = 1,
    .colored_names = 1,
    .background    = 2,
    .spacing       = 0,
    .max_visible   = 6,
    .display_time  = 1,
    .typewriter    = 3,
};

static char *off_on_names[] = {"Off", "On"};

static char *corner_names[] = {"Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right"};
_Static_assert(GetElementsIn(corner_names) == TEXTBOX_CORNER_NUM, "one name per TextBoxCorner");

static const float font_size_scales[] = {0.30f, 0.40f, 0.55f};
static char *font_size_names[]        = {"Small", "Med", "Large"};

static const u8 bg_alpha_targets[] = {0, 100, 200};
static char *background_names[]    = {"Off", "Dim", "Solid"};

// Extra vertical gap between stacked messages, as a fraction of the rendered text height.
static const float spacing_extras[] = {0.0f, 0.25f, 0.5f};
static char *spacing_names[]        = {"Tight", "Normal", "Wide"};


static const u16 display_wait_frames[] = {180, 300, 480};
static char *display_time_names[]      = {"Short", "Med", "Long"};

static const u8 typewriter_dwells[] = {0, 8, 4, 2};
static char *typewriter_names[]     = {"Off", "Slow", "Med", "Fast"};

static struct
{
    TextBoxMessage queue[TEXTBOX_QUEUE_SIZE];
    uint head;
    uint tail;
    uint framecounter;
} textbox_state;

// This scene's GObj running TextBox_PerFrame, alive only while a message is queued.
static GOBJ *textbox_gobj;

static int TextBoxQueue_IsEmpty(void)
{
    return textbox_state.head == textbox_state.tail;
}

static int TextBoxQueue_Count(void)
{
    return (textbox_state.tail - textbox_state.head + TEXTBOX_QUEUE_SIZE) % TEXTBOX_QUEUE_SIZE;
}

// Index 0 is the oldest message, count-1 the newest.
static TextBoxMessage *TextBoxQueue_GetAt(int index)
{
    return &textbox_state.queue[(textbox_state.head + index) % TEXTBOX_QUEUE_SIZE];
}

// The frame counter is shared by the queue, so every removal restarts it for the next message.
static void TextBox_Dequeue(void)
{
    Text_Destroy(textbox_state.queue[textbox_state.head].text);
    textbox_state.head = (textbox_state.head + 1) % TEXTBOX_QUEUE_SIZE;
    textbox_state.framecounter = 0;
}

// Text_AddSubtext / Text_SetText never write text->text_end, so the glyph count is walked out of
// the stream. The limit guards against a runaway scan on malformed data.
static int Sis_CountGlyphs(u8 *start)
{
    int count = 0;
    u8 *p     = start;
    u8 *limit = start + 4096;
    while (p < limit && *p != TEXTCMD_TERMINATE)
    {
        if (*p >= TEXTCMD_NUM) // the only thing the typewriter counts
            count++;
        p = Text_NextOpcode(p);
    }
    return count;
}

// text->color.a is a global alpha modulator and COLOR opcodes carry no alpha, so a fade touches
// .a alone - overwriting RGB would collapse the per-segment noun colors to white. The background
// is clamped to the text alpha so the panel can't outlast the glyphs.
static void TextBox_SetAlpha(Text *text, u8 text_alpha, u8 bg_target)
{
    text->color.a          = text_alpha;
    text->viewport_color.a = (text_alpha < bg_target) ? text_alpha : bg_target;
}

// Sets subtext `sub` to as much of the first `len` characters of `s` (plus `tail`, if given) as one
// subtext holds, and measures what actually landed. Returns the characters of `s` placed, short of
// `len` only when the run hits an engine limit. Sanitized text is not in the code space
// Text_GetStringWidth assumes, so widths have to come from the engine.
static int TextBox_SetRun(Text *t, int sub, const char *s, int len, const char *tail, float *out_w)
{
    char raw[TEXTBOX_RUN_CHARS + 8];
    char buf[TEXTBOX_RUN_CHARS * 2 + 16];

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

        if (bytes <= TEXTBOX_RUN_BYTES || len == 0)
            break;

        // Bytes are near enough to linear in character count that scaling by the overshoot lands
        // within a character or two; the -1 floor keeps the search strictly decreasing.
        int next = len * TEXTBOX_RUN_BYTES / bytes;
        len = (next < len) ? next : len - 1;
    }

    Text_SetText(t, sub, buf);

    float h = 0.0f;
    *out_w  = 0.0f;
    Text_GetWidthAndHeight(t, sub, out_w, &h);

    return len;
}

static int TextBox_PrevSpace(const char *s, int from)
{
    for (int i = from; i > 0; i--)
        if (s[i] == ' ')
            return i;
    return -1;
}

static int TextBox_HasText(const char *s)
{
    while (*s == ' ')
        s++;
    return *s != '\0';
}

// Longest prefix of `s` that fits `avail`, broken at a space where there is one. Returns 0 when
// nothing fits and the caller must open a new line; at a line start it always takes at least one
// character, so the walk cannot stall.
static int TextBox_FitRun(Text *t, int sub, const char *s, int len, float avail,
                          int at_line_start, float *out_w)
{
    float w = 0.0f;
    int placed = TextBox_SetRun(t, sub, s, len, NULL, &w);
    int capped = (placed < len);
    len = placed;

    if (w <= avail)
    {
        if (capped)
        {
            int brk = TextBox_PrevSpace(s, len - 1);
            if (brk > 0)
                len = TextBox_SetRun(t, sub, s, brk, NULL, &w);
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
        TextBox_SetRun(t, sub, s, brk, NULL, &w);
        if (w <= avail)
            break;
        brk = TextBox_PrevSpace(s, brk - 1);
    }

    if (brk > 0)
    {
        for (;;)
        {
            const char *sp = strchr(s + brk + 1, ' ');
            int nxt = (sp && sp - s <= len) ? (int)(sp - s) : len;

            float w2 = 0.0f;
            TextBox_SetRun(t, sub, s, nxt, NULL, &w2);
            if (w2 > avail)
            {
                TextBox_SetRun(t, sub, s, brk, NULL, &w);
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
        TextBox_SetRun(t, sub, s, n, NULL, &w);
        if (w <= avail || n <= 1)
            break;
        n--;
    }
    *out_w = w;
    return n;
}

// One Text GObj: a subtext per run of a segment that shares a line, wrapping onto at most
// TEXTBOX_MAX_LINES. Nothing is ever scaled down to fit.
static Text *TextBox_CreateSegmented(const TextSegment *segs, int seg_count, float scale, u8 alpha, u8 bg_alpha)
{
    Text *t = Hoshi_CreateScreenText();

    t->kerning = 1;
    t->viewport_scale = (Vec2){scale, scale};
    TextBox_SetAlpha(t, alpha, bg_alpha);

    // Measured widths are pre-viewport-scale units, so the pixel budget is divided through.
    float budget = (TEXT_CANVAS_W - 2.0f * TEXTBOX_MARGIN) / scale;

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
                // Text_AddSubtext captures t->color's RGB into the subtext's COLOR opcode.
                t->color = (GXColor){segs[i].color.r, segs[i].color.g, segs[i].color.b, t->color.a};
                Text_AddSubtext(t, 0, 0, "");
                sub_open = 1;
            }

            int final = (line >= TEXTBOX_MAX_LINES - 1);
            if (final && trunc_w < 0.0f)
                TextBox_SetRun(t, sub, TEXTBOX_TRUNC_MARK, sizeof(TEXTBOX_TRUNC_MARK) - 1, NULL, &trunc_w);

            int   len  = (int)strlen(p);
            float w    = 0.0f;
            int   take = TextBox_FitRun(t, sub, p, len, budget - x, x <= 0.0f, &w);

            if (take == 0 && !final)
            {
                x = 0.0f;
                line++;
                continue;
            }

            int truncated = 0;
            if (final)
            {
                int more = TextBox_HasText(p + take);
                for (int j = i + 1; j < seg_count && !more; j++)
                    more = TextBox_HasText(segs[j].text);

                // Text still to come needs room left on the last line for the marker.
                if (more && (take < len || x + w + trunc_w > budget))
                {
                    float avail = budget - x - trunc_w;
                    take = (avail > 0.0f) ? TextBox_FitRun(t, sub, p, len, avail, x <= 0.0f, &w) : 0;
                    TextBox_SetRun(t, sub, p, take, TEXTBOX_TRUNC_MARK, &w);
                    truncated = 1;
                }
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

// Builds msg's Text from its stored blob, so a rebuild draws exactly what the first build did, and
// arms the engine's built-in typewriter to resume from chars_revealed.
static void TextBox_BuildText(TextBoxMessage *msg)
{
    TextSegment segs[TEXTBOX_MAX_SEGMENTS];
    int pos = 0;
    for (int i = 0; i < msg->segment_count; i++)
    {
        segs[i].text  = &msg->segment_text[pos];
        segs[i].color = msg->colors[i];
        pos += (int)strlen(&msg->segment_text[pos]) + 1;
    }

    Text *t = TextBox_CreateSegmented(segs, msg->segment_count, msg->scale, msg->lifetime,
                                      msg->bg_alpha_target);
    msg->text        = t;
    msg->chars_total = (u16)Sis_CountGlyphs(t->text_start);

    // Text_GX only copies char_delay_init across at a TEXTCMD_SUBTEXT_RESET/BREAK (0x80451cec),
    // which these TEXTCMD_POS-delimited buffers never contain, so the live temp fields are seeded
    // directly. The renderer reloads temp each render and never clears it, so one write persists.
    t->temp.char_delay  = msg->typewriter_dwell;
    t->temp.space_delay = msg->typewriter_dwell;

    u16 revealed = msg->chars_revealed;
    if (revealed > msg->chars_total)
        revealed = msg->chars_total;
    // text_end stays NULL so the engine re-derives the reveal frontier from reveal_count.
    t->temp.reveal_count = revealed;
    t->text_end          = NULL;
}

// Newest sits at the anchor corner and older flows away from it; right corners right-align each
// message individually, since messages differ in width.
static void TextBoxQueue_RepositionAll(void)
{
    int count = TextBoxQueue_Count();

    int corner    = textbox_settings.corner;
    int is_right  = (corner == TEXTBOX_CORNER_TOP_RIGHT  || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);
    int is_bottom = (corner == TEXTBOX_CORNER_BOTTOM_LEFT || corner == TEXTBOX_CORNER_BOTTOM_RIGHT);

    float spacing_extra = spacing_extras[textbox_settings.spacing];

    // Canvas y of the next anchor edge.
    float edge_y = is_bottom ? (TEXT_CANVAS_H - TEXTBOX_MARGIN) : TEXTBOX_MARGIN;

    for (int i = count - 1; i >= 0; i--)
    {
        Text *t = TextBoxQueue_GetAt(i)->text;

        float w_px   = t->aspect.X * t->viewport_scale.X;
        float text_h = t->aspect.Y * t->viewport_scale.Y;

        // trans is the message's top-left.
        t->trans.X = is_right  ? (TEXT_CANVAS_W - TEXTBOX_MARGIN - w_px) : TEXTBOX_MARGIN;
        t->trans.Y = is_bottom ? (edge_y - text_h) : edge_y;

        float advance = text_h * (1.0f + spacing_extra);
        edge_y += is_bottom ? -advance : advance;
    }
}

static void TextBox_PerFrame(GOBJ *g)
{
    if (TextBoxQueue_IsEmpty())
    {
        GObj_Destroy(g);
        textbox_gobj = NULL;
        return;
    }

    // The engine paces each Text independently, so the whole queue is snapshotted, not just the
    // oldest, and the mirror is what lets a scene-change rebuild resume instead of re-typing.
    int count = TextBoxQueue_Count();
    for (int i = 0; i < count; i++)
    {
        TextBoxMessage *m = TextBoxQueue_GetAt(i);
        m->chars_revealed = (u16)m->text->temp.reveal_count;
    }

    TextBoxMessage *oldest = TextBoxQueue_GetAt(0);
    if (oldest->typewriter_dwell != 0 && oldest->text->temp.reveal_count < oldest->chars_total)
        return;

    if (++textbox_state.framecounter > display_wait_frames[textbox_settings.display_time])
    {
        if (oldest->lifetime > 0)
        {
            oldest->lifetime--;
            TextBox_SetAlpha(oldest->text, oldest->lifetime, oldest->bg_alpha_target);
        }
        else
        {
            TextBox_Dequeue();
        }
    }
}

static void TextBox_StartPerFrame(void)
{
    if (!textbox_gobj)
        textbox_gobj = GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
}

void TextBox_OnSceneChange(void)
{
    // The scene reset freed the GObj and every Text along with the old heaps.
    textbox_gobj = NULL;

    int count = TextBoxQueue_Count();
    for (int i = 0; i < count; i++)
        TextBox_BuildText(TextBoxQueue_GetAt(i));

    if (count > 0)
    {
        TextBoxQueue_RepositionAll();
        TextBox_StartPerFrame();
    }
}

int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count)
{
    if (!textbox_settings.enabled)
        return 0;
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return 0;
    // Text_CreateTextManual (0x8044f128) reads the canvas list head with no NULL check, so an
    // enqueue before hoshi creates the canvas on the first scene change faults.
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

    while (TextBoxQueue_Count() >= textbox_settings.max_visible)
        TextBox_Dequeue();

    // Built in the free tail slot. The text is copied so callers may pass stack buffers; a segment
    // that overruns the blob is truncated and the segments after it are dropped.
    TextBoxMessage *msg = &textbox_state.queue[textbox_state.tail];
    int pos = 0;
    msg->segment_count = 0;
    for (int i = 0; i < seg_count; i++)
    {
        int room = TEXTBOX_MESSAGE_TEXT_SIZE - 1 - pos;
        if (room <= 0)
            break;
        const char *src = segs[i].text ? segs[i].text : "";
        int n = (int)strlen(src);
        if (n > room)
            n = room;
        memcpy(&msg->segment_text[pos], src, n);
        msg->segment_text[pos + n] = '\0';
        msg->colors[i] = textbox_settings.colored_names ? segs[i].color : TextBox_DefaultColor;
        pos += n + 1;
        msg->segment_count++;
    }

    // Sampled per message so a scene-change rebuild draws it the way it first appeared.
    msg->lifetime         = 200;
    msg->scale            = font_size_scales[textbox_settings.font_size];
    msg->bg_alpha_target  = bg_alpha_targets[textbox_settings.background];
    msg->typewriter_dwell = typewriter_dwells[textbox_settings.typewriter];
    msg->chars_revealed   = 0;
    TextBox_BuildText(msg);

    textbox_state.tail = (textbox_state.tail + 1) % TEXTBOX_QUEUE_SIZE;

    TextBoxQueue_RepositionAll();
    TextBox_StartPerFrame();
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

static void TextBox_OnChangeEnabled(int val)
{
    if (!val)
    {
        while (!TextBoxQueue_IsEmpty())
            TextBox_Dequeue();
    }
    OSReport("[TextBox] Text box %s\n", val ? "enabled" : "disabled");
}

static void TextBox_OnChangeCorner(int val)
{
    TextBoxQueue_RepositionAll();
    OSReport("[TextBox] Position %s\n", corner_names[val]);
}

static void TextBox_OnChangeSpacing(int val)
{
    TextBoxQueue_RepositionAll();
    OSReport("[TextBox] Spacing %s\n", spacing_names[val]);
}

static void TextBox_OnChangeMaxVisible(int val)
{
    while (TextBoxQueue_Count() > val)
        TextBox_Dequeue();
    TextBoxQueue_RepositionAll();
    OSReport("[TextBox] Max on screen %d\n", val);
}

static void TextBox_OnChangeTypewriter(int val)
{
    OSReport("[TextBox] Typewriter %s\n", typewriter_names[val]);
}

static MenuDesc textbox_menu = {
    .option_num = 9,
    .options = {
        &(OptionDesc){
            .name = "Enabled",
            .description = "Enable or disable the in-game textbox",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.enabled,
            .value_num = GetElementsIn(off_on_names),
            .value_names = off_on_names,
            .on_change = TextBox_OnChangeEnabled,
        },
        &(OptionDesc){
            .name = "Position",
            .description = "Which corner of the screen the textbox stack anchors to",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.corner,
            .value_num = TEXTBOX_CORNER_NUM,
            .value_names = corner_names,
            .on_change = TextBox_OnChangeCorner,
        },
        &(OptionDesc){
            .name = "Font Size",
            .description = "Size of the message text",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.font_size,
            .value_num = GetElementsIn(font_size_scales),
            .value_names = font_size_names,
        },
        &(OptionDesc){
            .name = "Colored Names",
            .description = "Color item, machine, event names, etc. by category",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.colored_names,
            .value_num = GetElementsIn(off_on_names),
            .value_names = off_on_names,
        },
        &(OptionDesc){
            .name = "Background",
            .description = "Background panel opacity behind the text",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.background,
            .value_num = GetElementsIn(bg_alpha_targets),
            .value_names = background_names,
        },
        &(OptionDesc){
            .name = "Spacing",
            .description = "Vertical gap between stacked messages",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.spacing,
            .value_num = GetElementsIn(spacing_extras),
            .value_names = spacing_names,
            .on_change = TextBox_OnChangeSpacing,
        },
        &(OptionDesc){
            .name = "Max On Screen",
            .description = "Maximum number of messages visible at once",
            .kind = OPTKIND_NUM,
            .val = &textbox_settings.max_visible,
            .min = 1,
            .max = TEXTBOX_QUEUE_SIZE - 1,
            .on_change = TextBox_OnChangeMaxVisible,
        },
        &(OptionDesc){
            .name = "Display Time",
            .description = "How long the oldest message holds before it fades and the stack advances",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.display_time,
            .value_num = GetElementsIn(display_wait_frames),
            .value_names = display_time_names,
        },
        &(OptionDesc){
            .name = "Typewriter",
            .description = "Speed of the per-glyph reveal, or Off to show a message at once",
            .kind = OPTKIND_VALUE,
            .val = &textbox_settings.typewriter,
            .value_num = GetElementsIn(typewriter_dwells),
            .value_names = typewriter_names,
            .on_change = TextBox_OnChangeTypewriter,
        },
    },
};

OptionDesc TextBox_ModSettings = {
    .name = "Text Box",
    .description = "Configure the in-game textbox",
    .kind = OPTKIND_MENU,
    .menu_ptr = &textbox_menu,
};
