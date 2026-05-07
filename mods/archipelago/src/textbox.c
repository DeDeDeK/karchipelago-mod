#include <stdarg.h>
#include <string.h>
#include "text.h"
#include "main.h"
#include "settings_menu.h"
#include "text_joint/text_joint.h"
#include "hoshi/screen_cam.h"

#include "textbox.h"
#include "textbox_colors.h"

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

// Frames between glyph reveals when typewriter is on. ~15 glyphs/sec at 60 fps.
#define TEXTBOX_TYPEWRITER_DWELL 4

// Walk an SIS opcode stream from `start` to `end` and return the byte just past
// the Nth visible glyph (char codes >= 0x20 or 0x1a SPACE). If n exceeds the
// number of glyphs in the buffer, returns `end`. Stopping after a glyph is safe:
// every prior opcode is fully parsed and the renderer's hard text_end check
// halts before reading any further header bytes. Sizes mirror text.h's
// TextCmdOpcode comments.
static u8 *Sis_AdvancePastGlyphs(u8 *start, u8 *end, int n)
{
    if (!start || !end || end <= start)
        return start;
    if (n <= 0)
        return start;

    u8 *p         = start;
    int remaining = n;
    while (p < end)
    {
        u8 op = *p;
        if (op >= 0x20)
        {
            // 2-byte glyph code
            p += 2;
            if (--remaining == 0)
                return p;
            continue;
        }
        switch (op)
        {
            case 0x05: p += 3; break;                    // DELAY
            case 0x07:                                    // POS
            case 0x08:                                    // JUMP
            case 0x09:                                    // CALL
            case 0x0a:                                    // POSPUSH
            case 0x0e:                                    // SCALE
            case 0x06: p += 5; break;                    // TIMING
            case 0x0c: p += 4; break;                    // COLOR
            case 0x1a:                                    // SPACE — counts as one glyph
                p += 1;
                if (--remaining == 0)
                    return p;
                break;
            default: p += 1; break;                       // 1-byte ops + no-ops
        }
    }
    return end;
}

// Text_AddSubtext / Text_SetText do not update text->text_end — the vanilla
// renderer stops on the inline TERMINATE (0x00) opcode at the buffer tail. So
// for AddSubtext-built buffers we have to derive the end by scanning. Returns
// a pointer to the TERMINATE byte (one past the last meaningful opcode), or
// NULL if start is NULL. A safety cap prevents runaway scans on malformed data.
static u8 *Sis_FindBufferEnd(u8 *start)
{
    if (!start)
        return NULL;
    u8 *p     = start;
    u8 *limit = start + 4096;
    while (p < limit)
    {
        u8 op = *p;
        if (op >= 0x20) { p += 2; continue; }
        switch (op)
        {
            case 0x00: return p; // TERMINATE
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0e: p += 5; break;
            case 0x05: p += 3; break;
            case 0x0c: p += 4; break;
            default:   p += 1; break;
        }
    }
    return p;
}

static int Sis_CountGlyphs(u8 *start, u8 *end)
{
    if (!start || !end || end <= start)
        return 0;
    int count = 0;
    u8 *p     = start;
    while (p < end)
    {
        u8 op = *p;
        if (op >= 0x20) { p += 2; count++; continue; }
        switch (op)
        {
            case 0x05: p += 3; break;
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0e: p += 5; break;
            case 0x0c: p += 4; break;
            case 0x1a: p += 1; count++; break;
            default:   p += 1; break;
        }
    }
    return count;
}

// Restore any pending injected 0x00 in the message's buffer. Safe to call
// whether or not a TERMINATE is currently injected.
static void TextBox_RestoreTypewriterByte(TextBoxMessage *msg)
{
    if (!msg || !msg->typewriter_term_pos)
        return;
    *msg->typewriter_term_pos = msg->typewriter_saved_byte;
    msg->typewriter_term_pos  = NULL;
}

// Inject a 0x00 (TERMINATE) at the position-after-N-glyphs into the buffer,
// hiding everything past it. Caller must restore any prior injection first.
// If chars_revealed >= chars_total the buffer is left fully revealed.
static void TextBox_InjectTypewriterTerm(TextBoxMessage *msg)
{
    if (!msg || !msg->text || !msg->buf_full_end)
        return;
    if (msg->chars_revealed >= msg->chars_total)
        return;
    u8 *stop = Sis_AdvancePastGlyphs(msg->text->text_start, msg->buf_full_end, msg->chars_revealed);
    if (!stop || stop >= msg->buf_full_end)
        return;
    msg->typewriter_saved_byte = *stop;
    msg->typewriter_term_pos   = stop;
    *stop                      = 0x00;
}

// Build a multi-segment Text GObj. Each segment becomes one subtext at the
// computed x cursor position, with its own COLOR opcode (Text_AddSubtext
// captures t->color at emit time).
Text *CreateTextBoxSegmented(const TextSegment *segs, int seg_count, Vec3 pos, Vec2 scale, uint lifetime)
{
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return NULL;

    Text *t = Hoshi_CreateScreenText();
    if (!t)
        return NULL;

    t->kerning = 1;
    t->use_aspect = 1;
    t->trans = pos;
    t->viewport_scale = scale;
    t->viewport_color = (GXColor){0, 0, 0, lifetime};

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
            msg->text = CreateTextBoxSegmented(segs, msg->segment_count, msg->pos, msg->scale, msg->lifetime);
            if (!msg->text)
            {
                OSReport("[TextBox] Failed to recreate textbox on scene change\n");
                continue;
            }

            // Re-snapshot the buffer end and glyph count for the new Text. Reveal
            // progress is preserved across scene changes — clamp to the new total
            // in case the rebuilt buffer measures slightly differently. The old
            // buffer is gone, so any prior typewriter_term_pos pointer is stale.
            msg->typewriter_term_pos   = NULL;
            msg->typewriter_saved_byte = 0;
            msg->buf_full_end = Sis_FindBufferEnd(msg->text->text_start);
            msg->chars_total  = (u16)Sis_CountGlyphs(msg->text->text_start, msg->buf_full_end);
            if (msg->chars_revealed > msg->chars_total)
                msg->chars_revealed = msg->chars_total;
            if (msg->typewriter_active && msg->chars_revealed < msg->chars_total)
                TextBox_InjectTypewriterTerm(msg);
        }
        // Reposition all recreated messages
        TextBoxQueue_RepositionAll();
    }

    // Init per-frame GOBJ for textbox operations
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
}

// The renderer uses text->color.a as a global alpha modulator — TEXTCMD_COLOR
// only updates temp.color RGB, alpha is sourced from text->color.a at init and
// applied to every glyph in every subtext (see docs/sis-text-system.md
// "Color Rendering Pipeline"). So fading the whole textbox = touching .a only.
// We must NOT overwrite RGB, or per-segment noun colors would all collapse to white.
void TextBox_SetAlpha(Text *text, u8 alpha)
{
    if (!text)
        return;
    text->color.a = alpha;
    text->viewport_color.a = alpha;
}

// Repositions all messages in the queue by moving older messages down
void TextBoxQueue_RepositionAll()
{
    int count = TextBoxQueue_Count();
    if (count == 0)
        return;

    // Start at the top position for the newest message
    float y_offset = 10.0f;

    // Iterate from newest (index count-1) to oldest (index 0).
    // Newest message stays at top, older messages stack below.
    for (int i = count - 1; i >= 0; i--)
    {
        TextBoxMessage *t = TextBoxQueue_GetAt(i);
        if (t && t->text)
        {
            t->pos.Y = y_offset;
            t->text->trans.Y = t->pos.Y;
            y_offset += t->text->aspect.Y / 2;
        }
    }
}

void TextBox_PerFrame(GOBJ *g)
{
    if (TextBoxQueue_IsEmpty())
        return;

    // Advance the typewriter on every queued message independently. Each holds
    // an injected 0x00 (TERMINATE) at the position-after-Nth-glyph; on each tick
    // we restore the saved byte, advance N, and inject again one glyph further.
    int count = TextBoxQueue_Count();
    for (int i = 0; i < count; i++)
    {
        TextBoxMessage *msg = TextBoxQueue_GetAt(i);
        if (!msg || !msg->text || !msg->typewriter_active)
            continue;
        if (msg->chars_revealed >= msg->chars_total)
            continue;
        if (++msg->typewriter_counter < TEXTBOX_TYPEWRITER_DWELL)
            continue;
        msg->typewriter_counter = 0;
        msg->chars_revealed++;
        TextBox_RestoreTypewriterByte(msg);
        if (msg->chars_revealed < msg->chars_total)
            TextBox_InjectTypewriterTerm(msg);
    }

    // Fade timer is paused while the oldest message is still revealing — we
    // don't want a slow typewriter to overlap with the fade-out countdown.
    TextBoxMessage *oldest = TextBoxQueue_GetAt(0);
    if (!oldest || !oldest->text)
        return;
    if (oldest->typewriter_active && oldest->chars_revealed < oldest->chars_total)
        return;

    // After 5 seconds since the last removed message, subtract from the alpha
    // value of the oldest message until it reaches 0. When it hits 0, dequeue it.
    if (++textbox_state.framecounter > 300)
    {
        if (oldest->lifetime > 0)
        {
            oldest->lifetime--;
            TextBox_SetAlpha(oldest->text, oldest->lifetime);
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
    if (!ap_menu_settings.textbox_enabled)
        return 0;
    if (seg_count <= 0 || seg_count > TEXTBOX_MAX_SEGMENTS)
        return 0;

    // Auto-dequeue oldest message if queue is full to make room for new message
    if (TextBoxQueue_IsFull())
    {
        TextBoxMessage removed_text;
        TextBox_Dequeue(&removed_text);
        // Reset framecounter so the next oldest message gets a fresh 5-second timer
        textbox_state.framecounter = 0;
        OSReport("[TextBox] Queue full, auto-dequeued oldest message.\n");
    }

    // Reset framecounter if adding to an empty queue
    if (TextBoxQueue_IsEmpty())
        textbox_state.framecounter = 0;

    TextBoxMessage entry;
    entry.lifetime = 200;
    entry.pos = (Vec3){10, 10, 0};
    entry.scale = (Vec2){0.4, 0.4};
    entry.segment_count = (u8)seg_count;
    for (int i = 0; i < seg_count; i++)
    {
        const char *src = segs[i].text ? segs[i].text : "";
        strncpy(entry.segments[i].text, src, TEXTBOX_SEGMENT_TEXT_SIZE - 1);
        entry.segments[i].text[TEXTBOX_SEGMENT_TEXT_SIZE - 1] = '\0';
        entry.segments[i].color = segs[i].color;
    }

    entry.text = CreateTextBoxSegmented(segs, seg_count, entry.pos, entry.scale, entry.lifetime);
    if (!entry.text)
    {
        OSReport("[TextBox] Failed to create Text object!\n");
        return 0;
    }

    // Sample the typewriter setting at enqueue time so per-message behavior
    // stays stable even if the player toggles mid-reveal.
    entry.typewriter_active    = ap_menu_settings.textbox_typewriter_enabled ? 1 : 0;
    entry.typewriter_counter   = 0;
    entry.chars_revealed       = 0;
    entry.typewriter_term_pos  = NULL;
    entry.typewriter_saved_byte = 0;
    entry.buf_full_end         = Sis_FindBufferEnd(entry.text->text_start);
    entry.chars_total          = (u16)Sis_CountGlyphs(entry.text->text_start, entry.buf_full_end);

    // queue[] stores by value, so write the entry first then operate on the
    // stored copy — the injected pointer must reference the queued entry, not
    // this stack frame.
    textbox_state.queue[textbox_state.tail] = entry;
    TextBoxMessage *queued = &textbox_state.queue[textbox_state.tail];
    if (queued->typewriter_active && queued->chars_total > 0)
        TextBox_InjectTypewriterTerm(queued);
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
    if (!ap_menu_settings.textbox_enabled)
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
int TextBox_Dequeue(TextBoxMessage *text_out)
{
    if (TextBoxQueue_IsEmpty())
        return 0;

    *text_out = textbox_state.queue[textbox_state.head];
    textbox_state.head = (textbox_state.head + 1) % TEXTBOX_QUEUE_SIZE;

    if (text_out->text)
        Text_Destroy(text_out->text);

    return 1;
}

int TextBoxQueue_IsEmpty()
{
    return textbox_state.head == textbox_state.tail;
}

int TextBoxQueue_IsFull()
{
    uint next_tail = (textbox_state.tail + 1) % TEXTBOX_QUEUE_SIZE;
    return next_tail == textbox_state.head;
}

// Derive count from head and tail indices
int TextBoxQueue_Count()
{
    return (textbox_state.tail - textbox_state.head + TEXTBOX_QUEUE_SIZE) % TEXTBOX_QUEUE_SIZE;
}

// Get a TextBoxMessage at a specific index in the queue (for iteration).
// Index 0 is the head (oldest), count-1 is the newest.
TextBoxMessage *TextBoxQueue_GetAt(int index)
{
    if (index < 0 || index >= TextBoxQueue_Count())
        return NULL;
    int actual_index = (textbox_state.head + index) % TEXTBOX_QUEUE_SIZE;
    return &textbox_state.queue[actual_index];
}
