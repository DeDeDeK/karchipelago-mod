#include "text.h"
#include "hsd.h"
#include "os.h"
#include "audio.h"
#include "game.h"

Text *textbox_text;

void CreateTextBox_OnSceneChange() {
    Text *t = Text_CreateText(1, 1);
    t->kerning = 0;
    t->use_aspect = 1;
    t->trans = (Vec3){10, 30, 0};
    t->viewport_scale = (Vec2){0.5, 0.5};
    t->viewport_color = (GXColor){0, 0, 0, 128};
    Text_AddSubtext(t, 0, 0, "");
    textbox_text = t;
    Text_SetText(textbox_text, 0, "test text!");
}
