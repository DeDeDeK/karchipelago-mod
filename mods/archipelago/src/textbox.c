#include "text.h"
#include "textbox.h"
#include "main.h"
#include "text_joint/text_joint.h"
#include <stdarg.h>

Text *textbox_text;
TextBoxPerFrameData *textbox_data;

void CreateTextBox_OnSceneChange() {
    // Create canvas, with sis_idx (font) = 1
    int canvas_idx = Text_CreateCanvas(1, 0, 0, 0, 0, 63, 0, 63);
    Text *t = Text_CreateText(1, canvas_idx);
    t->kerning = 1;
    t->use_aspect = 1;
    t->aspect = (Vec2){560, 32};
    t->trans = (Vec3){10, 10, 0};
    t->viewport_scale = (Vec2){0.4, 0.4};
    // start off transparent
    t->viewport_color = (GXColor){0, 0, 0, 0};
    // start off white, transparent
    t->color = (GXColor){255, 255, 255, 0};
    textbox_text = t;
    // Initialize first subtext
    Text_AddSubtext(t, 0, 0, "");

    // init frame counter GOBJ
    GOBJ *g = GOBJ_EZCreator(0, 0, 0, sizeof(TextBoxPerFrameData), HSD_Free, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
    TextBoxPerFrameData *gp = g->userdata;
    gp->framecounter = 0;
    textbox_data = gp;
}

// Adds a message to the textbox.
void TextBox_AddMessage(char *format, ...) {
    if (hoshi_menu_settings.textbox_enabled) {
        // Buffer to hold the formatted string
        char buffer[256];
        // Format the string with variable arguments
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // Convert ASCII to Shift-JIS for proper display
        char sanitized_buffer[512];  // Needs to be larger as Shift-JIS uses 2 bytes per character
        Text_Sanitize(buffer, sanitized_buffer, sizeof(sanitized_buffer));

        // Set alpha to be more opaque - to be subtracted from later
        TextBox_SetAlpha(200);

        // Use the first subtext to display the text data
        Text_SetText(textbox_text, 0, sanitized_buffer);

        // Get the width and height of the text
        float width = 0, height = 0;
        Text_GetWidthAndHeight(textbox_text, 0, &width, &height);
        
        // Set aspect to contain text
        textbox_text->aspect = (Vec2){width, height};

        // Reset the frame counter so the message displays for the full duration
        textbox_data->framecounter = 0;
    }
}

// Sets the alpha value for both the textbox viewport and the textbox text
void TextBox_SetAlpha(u8 alpha) {
    textbox_text->viewport_color = (GXColor){0, 0, 0, alpha};
    textbox_text->color = (GXColor){255, 255, 255, alpha};
}

void TextBox_PerFrame(GOBJ *g) {
    TextBoxPerFrameData *gp = g->userdata;
    // after 5 seconds, subtract from alpha value every frame until the textbox is transparent
    if (++gp->framecounter > 300) {
        GXColor current_color = textbox_text->viewport_color;
        u8 alpha = current_color.a;
        if (alpha > 0) {
            TextBox_SetAlpha(--alpha);
        } else {
            gp->framecounter = 0;
        }
    }
}
