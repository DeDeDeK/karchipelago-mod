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
    // Dual-purpose: seeds the peak text alpha at creation (messages top out at
    // 200/255, not fully opaque) and then counts down to 0 as the fade-out
    // timer — so peak opacity and fade length are both tied to the initial 200.
    uint lifetime;
    Vec2 scale;
    Text *text;

    // Typewriter state. The actual reveal is done by the engine's built-in
    // typewriter (we seed text->temp.char_delay). We
    // sample the setting at enqueue time so per-message behavior stays stable if
    // the player toggles mid-reveal, and keep chars_total to gate the fade-out:
    // the fade is held until the renderer's temp.reveal_count reaches it.
    //
    // chars_revealed mirrors the engine's temp.reveal_count every frame so the
    // reveal progress survives the Text being destroyed and rebuilt on a scene
    // change. Without it, CreateTextBox_OnSceneChange would restart every message's
    // typewriter from zero. Fresh messages set it to 0 at enqueue.
    u8 typewriter_active;
    u16 chars_total;
    u16 chars_revealed;
    u8 typewriter_dwell;     // frames per glyph reveal (fed to temp.char_delay)
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

void CreateTextBox_OnSceneChange();

// Re-issues the screen canvas's render pass. Called from a hook installed
// after TopRide_CustomRenderer so the textbox isn't wiped by TR's post-render
// HSD_StartRender second pass — see textbox.c for details.
void TextBox_TopRideReRender(void);

// Concrete implementations exported through TextBoxAPI.
int TextBox_Enqueue(const char *format, ...);
int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count);
int TextBox_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);
int TextBox_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                  const char *suffix_format, ...);

// Reflows the on-screen stack for the current corner/spacing settings. Called
// from the settings on_change callbacks in main.c as well as internally.
void TextBoxQueue_RepositionAll();

#endif // TEXTBOX_H
