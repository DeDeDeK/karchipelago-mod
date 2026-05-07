#ifndef TEXTBOX_H
#define TEXTBOX_H

#include "structs.h"
#include "datatypes.h"

#include "textbox_api.h"

// Storage cap for the queue. One slot is reserved to disambiguate empty vs.
// full in the ring-buffer indexing (tail+1==head means full), so the actual
// storage capacity is TEXTBOX_QUEUE_SIZE - 1. The runtime "Max On Screen"
// setting tops out at 8, so we need 9 here.
#define TEXTBOX_QUEUE_SIZE        9
#define TEXTBOX_SEGMENT_TEXT_SIZE 80

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

    // Typewriter state. typewriter_active is sampled from the mod's settings at
    // enqueue time so per-message behavior stays stable even if the player
    // toggles mid-reveal.
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

    // Sampled from settings at enqueue time so per-message behavior is stable
    // even if the player changes settings mid-life.
    u8 typewriter_dwell;     // frames per glyph reveal
    u8 bg_alpha_target;      // background quad alpha when fully visible
} TextBoxMessage;

// Screen corner the textbox stack anchors to. Top corners stack newest at
// top with older messages flowing down; bottom corners stack newest at bottom
// with older messages flowing up. Right corners right-align each message
// against the right edge.
typedef enum TextBoxCorner
{
    TEXTBOX_CORNER_TOP_LEFT = 0,
    TEXTBOX_CORNER_TOP_RIGHT,
    TEXTBOX_CORNER_BOTTOM_LEFT,
    TEXTBOX_CORNER_BOTTOM_RIGHT,
    TEXTBOX_CORNER_NUM,
} TextBoxCorner;

// Mod-owned settings, bound to the Settings menu.
typedef struct TextBoxSettings
{
    int enabled;
    int typewriter_enabled;
    int typewriter_speed;     // 0=Slow, 1=Med, 2=Fast
    int font_size;            // 0=Small, 1=Med, 2=Large
    int colored_names;        // 0=Off, 1=On
    int message_spacing;      // 0=Tight, 1=Normal, 2=Wide
    int background_opacity;   // 0=Off, 1=Dim, 2=Solid
    int max_visible;          // 0=3, 1=4, 2=6, 3=8
    int display_time;         // 0=Short, 1=Med, 2=Long
    int corner;               // TextBoxCorner
} TextBoxSettings;

extern TextBoxSettings textbox_settings;

// Build a Text GObj from N coloured segments. Each segment renders inline,
// positioned right after the previous one on the same line. `bg_alpha` is the
// background quad alpha at full visibility (independent of the text fade).
Text *CreateTextBoxSegmented(const TextSegment *segs, int seg_count, Vec3 pos, Vec2 scale, uint lifetime, u8 bg_alpha);

void CreateTextBox_OnSceneChange();

// Sets text alpha on `text`, and clamps the background quad alpha to
// min(text_alpha, bg_target). This keeps the bg at the player-chosen opacity
// during the steady-state phase, then fades it together with the text once
// the text alpha drops below the target.
void TextBox_SetAlpha(Text *text, u8 text_alpha, u8 bg_target);
void TextBox_PerFrame(GOBJ *g);

// Concrete implementations exported through TextBoxAPI.
int TextBox_Enqueue(const char *format, ...);
int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count);
int TextBox_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);
int TextBox_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                  const char *suffix_format, ...);

int TextBox_Dequeue(TextBoxMessage *text_out);
int TextBoxQueue_IsEmpty();
int TextBoxQueue_IsFull();
int TextBoxQueue_Count();
TextBoxMessage *TextBoxQueue_GetAt(int index);
void TextBoxQueue_RepositionAll();

#endif // TEXTBOX_H
