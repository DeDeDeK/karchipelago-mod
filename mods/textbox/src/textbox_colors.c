#include "textbox_colors.h"

const GXColor TextBox_DefaultColor = {255, 255, 255, 255}; // white

const GXColor TextBox_AbilityColors[COPYKIND_NUM] = {
    [COPYKIND_FIRE]    = {255, 60,  10,  255}, // red-orange
    [COPYKIND_WHEEL]   = {145, 145, 145, 255}, // dark grey
    [COPYKIND_SLEEP]   = {255, 180, 220, 255}, // soft pink
    [COPYKIND_SWORD]   = {115, 185, 40,  255}, // dark green
    [COPYKIND_BOMB]    = {120, 120, 130, 255}, // gunmetal
    [COPYKIND_PLASMA]  = {65,  215, 165, 255}, // cyan
    [COPYKIND_NEEDLE]  = {215, 205, 25,  255}, // muted yellow
    [COPYKIND_MIC]     = {175, 50,  0,   255}, // dark red
    [COPYKIND_FREEZE]  = {0,   100, 255, 255}, // blue
    [COPYKIND_TORNADO] = {130, 180, 255, 255}, // light blue
    [COPYKIND_BIRD]    = {220, 0,   220, 255}, // light magenta
};

const GXColor TextBox_ModeColors[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = {235,  70,  60, 255}, // red
    [GMMODE_TOPRIDE]   = {255, 220,  60, 255}, // yellow
    [GMMODE_CITYTRIAL] = { 90, 215, 110, 255}, // green
};

const GXColor TextBox_KirbyColors[KIRBYCOLOR_NUM] = {
    [KIRBYCOLOR_PINK]   = {255, 130, 200, 255},
    [KIRBYCOLOR_YELLOW] = {255, 220,  80, 255},
    [KIRBYCOLOR_BLUE]   = { 90, 140, 255, 255},
    [KIRBYCOLOR_RED]    = {240,  70,  60, 255},
    [KIRBYCOLOR_GREEN]  = {110, 220, 100, 255},
    [KIRBYCOLOR_PURPLE] = {200, 100, 220, 255},
    [KIRBYCOLOR_BROWN]  = {180, 130,  80, 255},
    [KIRBYCOLOR_WHITE]  = {240, 240, 240, 255},
};

const GXColor TextBox_PatchColors[PATCHKIND_NUM] = {
    [PATCHKIND_WEIGHT]   = {160, 100,   0, 255}, // brown
    [PATCHKIND_ACCEL]    = {225,   0, 220, 255}, // purple
    [PATCHKIND_TOPSPEED] = { 80, 200, 255, 255}, // cyan
    [PATCHKIND_TURN]     = {  0, 215,  50, 255}, // green
    [PATCHKIND_CHARGE]   = {235, 255,   0, 255}, // yellow
    [PATCHKIND_GLIDE]    = {255, 255, 235, 255}, // off-white
    [PATCHKIND_OFFENSE]  = {250, 175,   0, 255}, // orange
    [PATCHKIND_DEFENSE]  = {  0,  90, 210, 255}, // blue
    [PATCHKIND_HP]       = {255,  75,  75, 255}, // red
};

const GXColor TextBox_BoxColors[BOXKIND_NUM] = {
    [BOXKIND_BLUE]  = { 55, 115, 250, 255}, // blue
    [BOXKIND_GREEN] = {  0, 195,   0, 255}, // green
    [BOXKIND_RED]   = {255,  78,   0, 255}, // red
};

const GXColor TextBox_MachineColor     = {130, 180, 255, 255}; // sky blue
const GXColor TextBox_EventColor       = {200, 130, 255, 255}; // violet
const GXColor TextBox_StadiumColor     = {255, 160,  60, 255}; // orange
const GXColor TextBox_StageColor       = {140, 220, 240, 255}; // cyan
const GXColor TextBox_TopRideItemColor = {255, 220, 100, 255}; // mustard
const GXColor TextBox_ItemColor        = {180, 240, 160, 255}; // light green
const GXColor TextBox_TrapColor        = {210,   0,   0, 255}; // dark red
const GXColor TextBox_DeathColor       = {255,  60,  60, 255}; // red
const GXColor TextBox_EnergyColor      = {100, 200, 255, 255}; // cyan
const GXColor TextBox_CheckColor       = {120, 230, 120, 255}; // green
const GXColor TextBox_GoalColor        = {255, 215,   0, 255}; // gold
const GXColor TextBox_RewardColor      = {255, 20,   20, 255}; // red
const GXColor TextBox_ShopColor        = { 90, 220, 200, 255}; // teal
const GXColor TextBox_FillerColor      = {165, 110, 210, 255}; // purple
