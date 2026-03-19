#ifndef TEXTBOX_H
#define TEXTBOX_H

#include "structs.h"
#include "datatypes.h"

#define TEXTBOX_QUEUE_SIZE   6
#define TEXTBOX_MESSAGE_SIZE 256

typedef struct TextBoxMessage
{
    char message[TEXTBOX_MESSAGE_SIZE];
    uint lifetime;
    Vec3 pos;
    Vec2 scale;
    Text *text;
} TextBoxMessage;

Text *CreateTextBox(char *message, Vec3 pos, Vec2 scale, uint lifetime);
void CreateTextBox_OnSceneChange();
void TextBox_SetAlpha(Text *text, u8 alpha);
void TextBox_PerFrame(GOBJ *g);

int TextBox_Enqueue(char *format, ...);
int TextBox_Dequeue(TextBoxMessage *text_out);
int TextBoxQueue_IsEmpty();
int TextBoxQueue_IsFull();
int TextBoxQueue_Count();
TextBoxMessage *TextBoxQueue_GetAt(int index);
void TextBoxQueue_RepositionAll();

#endif // TEXTBOX_H
