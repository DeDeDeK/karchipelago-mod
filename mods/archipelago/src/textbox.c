#include <stdarg.h>
#include <string.h>
#include "text.h"
#include "main.h"
#include "settings_menu.h"
#include "text_joint/text_joint.h"
#include "hoshi/screen_cam.h"

#include "textbox.h"

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

// Create a textbox with the given Text parameters
Text *CreateTextBox(char *message, Vec3 pos, Vec2 scale, uint lifetime)
{
    Text *t = Hoshi_CreateScreenText();

    // set Text properties
    t->kerning = 1;
    t->use_aspect = 1;
    t->trans = pos;
    t->viewport_scale = scale;
    // start off black, set alpha value to lifetime
    t->viewport_color = (GXColor){0, 0, 0, lifetime};
    // start off white, set alpha value to lifetime
    t->color = (GXColor){255, 255, 255, lifetime};

    // Initialize the first subtext
    Text_AddSubtext(t, 0, 0, "");

    // convert ascii special characters to SHIFT-JIS encoding
    // needs to be twice the size as SHIFT-JIS takes 2 bytes instead of 1
    char sanitize_buffer[TEXTBOX_MESSAGE_SIZE * 2];
    Text_Sanitize(message, sanitize_buffer, sizeof(sanitize_buffer));

    Text_SetText(t, 0, sanitize_buffer);

    // Get the width and height of the text
    float width = 0, height = 0;
    Text_GetWidthAndHeight(t, 0, &width, &height);

    // Set aspect to contain text
    t->aspect = (Vec2){width, height};

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
            TextBoxMessage *text_box_message = TextBoxQueue_GetAt(i);
            if (text_box_message)
            {
                text_box_message->text = CreateTextBox(
                    text_box_message->message,
                    text_box_message->pos,
                    text_box_message->scale,
                    text_box_message->lifetime);
                if (!text_box_message->text)
                {
                    OSReport("[TextBox] Failed to recreate textbox on scene change for message: %s\n",
                             text_box_message->message);
                }
            }
        }
        // Reposition all recreated messages
        TextBoxQueue_RepositionAll();
    }

    // Init per-frame GOBJ for textbox operations
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TextBox_PerFrame, 0, 0, 0, 0);
}

// Sets the alpha value for both the textbox viewport and the textbox text
void TextBox_SetAlpha(Text *text, u8 alpha)
{
    text->viewport_color = (GXColor){0, 0, 0, alpha};
    text->color = (GXColor){255, 255, 255, alpha};
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

    // After 5 seconds since the last removed message, subtract from the alpha
    // value of the oldest message until it reaches 0. When it hits 0, dequeue it.
    if (++textbox_state.framecounter > 300)
    {
        TextBoxMessage *msg = TextBoxQueue_GetAt(0);
        if (msg && msg->text)
        {
            if (msg->lifetime > 0)
            {
                msg->lifetime--;
                TextBox_SetAlpha(msg->text, msg->lifetime);
            }
            else
            {
                TextBoxMessage text_out;
                TextBox_Dequeue(&text_out);
                textbox_state.framecounter = 0;
            }
        }
    }
}

// Enqueue a message to the textbox queue by creating a Text object
int TextBox_Enqueue(const char *format, ...)
{
    if (!ap_menu_settings.textbox_enabled)
        return 0;

    // Auto-dequeue oldest message if queue is full to make room for new message
    if (TextBoxQueue_IsFull())
    {
        TextBoxMessage removed_text;
        TextBox_Dequeue(&removed_text);
        // Reset framecounter so the next oldest message gets a fresh 5-second timer
        textbox_state.framecounter = 0;
        OSReport("[TextBox] TextBox_Enqueue: Queue full, auto-dequeued oldest message.\n");
    }

    // Reset framecounter if adding to an empty queue
    if (TextBoxQueue_IsEmpty())
        textbox_state.framecounter = 0;

    // Format the string with variable arguments
    char buffer[TEXTBOX_MESSAGE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Create the TextBoxMessage
    TextBoxMessage text_box_message;
    text_box_message.lifetime = 200;
    text_box_message.pos = (Vec3){10, 10, 0};
    text_box_message.scale = (Vec2){0.4, 0.4};
    text_box_message.text = CreateTextBox(
        buffer, text_box_message.pos, text_box_message.scale, text_box_message.lifetime);
    if (!text_box_message.text)
    {
        OSReport("[TextBox] TextBox_Enqueue: Failed to create Text object!\n");
        return 0;
    }

    strncpy(text_box_message.message, buffer, TEXTBOX_MESSAGE_SIZE - 1);
    text_box_message.message[TEXTBOX_MESSAGE_SIZE - 1] = '\0';

    // Add the TextBoxMessage to the queue
    textbox_state.queue[textbox_state.tail] = text_box_message;
    textbox_state.tail = (textbox_state.tail + 1) % TEXTBOX_QUEUE_SIZE;

    // Reposition all messages so they stack properly with newest at top
    TextBoxQueue_RepositionAll();

    return 1;
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
