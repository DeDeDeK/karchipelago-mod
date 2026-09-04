#ifndef TEXTBOX_H
#define TEXTBOX_H

#include "structs.h"
#include "datatypes.h"

#include "textbox_api.h"

// Ring buffer with one slot reserved to tell empty from full, so capacity is one less than the
// size; "Max On Screen" tops out at 8.
#define TEXTBOX_QUEUE_SIZE 9

// A whole message's text, however it is split into segments. Both the first render and the
// rebuild after a scene change read this, so what is stored is what is drawn.
#define TEXTBOX_MESSAGE_TEXT_SIZE 248

typedef struct TextBoxMessage
{
    char segment_text[TEXTBOX_MESSAGE_TEXT_SIZE]; // segment_count NUL-terminated strings back to back
    GXColor colors[TEXTBOX_MAX_SEGMENTS];
    u8 segment_count;
    uint lifetime;           // seeds peak text alpha (200), then counts down as the fade timer
    Vec2 scale;
    Text *text;

    u8 typewriter_active;    // sampled at enqueue so a mid-reveal toggle can't change it
    u16 chars_total;         // fade is held until temp.reveal_count reaches this
    u16 chars_revealed;      // mirrors temp.reveal_count so a scene-change rebuild can resume
    u8 typewriter_dwell;     // frames per glyph reveal (fed to temp.char_delay)
    u8 bg_alpha_target;      // background quad alpha when fully visible
} TextBoxMessage;

// Screen corner the textbox stack anchors to.
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

// Re-issues the screen canvas's render pass, so the textbox survives Top Ride's post-render
// second HSD_StartRender pass. Call after TopRide_CustomRenderer runs.
void TextBox_TopRideReRender(void);

// Concrete implementations exported through TextBoxAPI.
int TextBox_Enqueue(const char *format, ...);
int TextBox_IsReady(void);
int TextBox_EnqueueSegments(const TextSegment *segs, int seg_count);
int TextBox_EnqueueColoredNoun(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);
int TextBox_EnqueueColoredNounFmt(const char *prefix, const char *noun, GXColor noun_color,
                                  const char *suffix_format, ...);

// Reflows the on-screen stack for the current corner/spacing settings.
void TextBoxQueue_RepositionAll();

#endif // TEXTBOX_H
