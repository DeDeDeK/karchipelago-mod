#ifndef TEXTBOX_COLORS_H
#define TEXTBOX_COLORS_H

#include "datatypes.h"
#include "structs.h"
#include "rider.h"
#include "item.h"
#include "game.h"

// Per-noun text colors. RGB only; alpha is owned by the fade machinery.

extern const GXColor TextBox_DefaultColor;
extern const GXColor TextBox_AbilityColors[COPYKIND_NUM];
extern const GXColor TextBox_KirbyColors[KIRBYCOLOR_NUM];
extern const GXColor TextBox_ModeColors[GMMODE_NUM];
extern const GXColor TextBox_PatchColors[PATCHKIND_NUM];
extern const GXColor TextBox_BoxColors[BOXKIND_NUM];

extern const GXColor TextBox_MachineColor;
extern const GXColor TextBox_EventColor;
extern const GXColor TextBox_StadiumColor;
extern const GXColor TextBox_StageColor;
extern const GXColor TextBox_TopRideItemColor;
extern const GXColor TextBox_ItemColor;

#endif // TEXTBOX_COLORS_H
