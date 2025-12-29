extern Text *textbox_text;

typedef struct TextBoxPerFrameData {
    int framecounter;
} TextBoxPerFrameData;

void CreateTextBox_OnSceneChange();
void TextBox_PerFrame(GOBJ *g);
void TextBox_SetAlpha(u8 alpha);
void TextBox_AddMessage(char *format, ...);