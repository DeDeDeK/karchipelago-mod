#ifndef TEXTBOX_H
#define TEXTBOX_H

#include "structs.h"
#include "datatypes.h"

#define TEXTBOX_QUEUE_SIZE        6
#define TEXTBOX_MESSAGE_SIZE      256
#define TEXTBOX_MAX_SEGMENTS      4
#define TEXTBOX_SEGMENT_TEXT_SIZE 80

// One coloured run of text. Multiple segments compose one TextBox; each
// segment becomes its own subtext (with its own COLOR opcode) inside the
// underlying Text GObj.
typedef struct TextSegment
{
    const char *text;
    GXColor color; // RGB; alpha is overwritten per-frame from textbox lifetime.
} TextSegment;

// Stored copy of a segment used to recreate the TextBox after a scene change.
typedef struct TextBoxSegmentEntry
{
    char text[TEXTBOX_SEGMENT_TEXT_SIZE];
    GXColor color;
} TextBoxSegmentEntry;

typedef struct TextBoxMessage
{
    TextBoxSegmentEntry segments[TEXTBOX_MAX_SEGMENTS];
    u8 segment_count;
    uint lifetime;
    Vec3 pos;
    Vec2 scale;
    Text *text;

    // Typewriter state. typewriter_active is sampled from
    // ap_menu_settings.textbox_typewriter_enabled at enqueue time so per-message
    // behavior stays stable even if the player toggles mid-reveal.
    //
    // Mechanism: text->text_end is unreliable for AddSubtext-built buffers (the
    // renderer resets it each frame and only consults it for typewriter dwell).
    // Instead we mutate the opcode stream directly — save the byte at the next
    // "after-Nth-glyph" position into typewriter_saved_byte, then write 0x00
    // (TERMINATE) there. The parser halts on TERMINATE, hiding everything past
    // it. Each tick we restore the saved byte and inject a new TERMINATE one
    // glyph further along. The lifetime/fade timer for the oldest message is
    // paused until its reveal completes.
    u8 typewriter_active;
    u16 chars_revealed;
    u16 chars_total;
    u16 typewriter_counter;
    u8 *buf_full_end;
    u8 *typewriter_term_pos; // where the injected 0x00 currently lives, NULL if none
    u8 typewriter_saved_byte; // original byte at that position
} TextBoxMessage;

// Build a Text GObj from N coloured segments. Each segment renders inline,
// positioned right after the previous one on the same line.
Text *CreateTextBoxSegmented(const TextSegment *segs, int seg_count, Vec3 pos, Vec2 scale, uint lifetime);

void CreateTextBox_OnSceneChange();
void TextBox_SetAlpha(Text *text, u8 alpha);
void TextBox_PerFrame(GOBJ *g);

// Plain message — single segment in default color.
int TextBox_Enqueue(const char *format, ...);

// N-segment message with per-segment colors.
int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count);

// Three-segment shortcut: prefix + noun + suffix, where only the noun gets
// a custom color (prefix/suffix render in TextBox_DefaultColor). Empty/NULL
// prefix or suffix is allowed.
int TextBox_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);

// Same as above, but suffix is a printf-style format string with vararg
// substitutions. Useful when the suffix carries dynamic numbers (counters,
// energy values, etc.) that the noun does not.
int TextBox_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                  const char *suffix_format, ...);

int TextBox_Dequeue(TextBoxMessage *text_out);
int TextBoxQueue_IsEmpty();
int TextBoxQueue_IsFull();
int TextBoxQueue_Count();
TextBoxMessage *TextBoxQueue_GetAt(int index);
void TextBoxQueue_RepositionAll();

#endif // TEXTBOX_H
