#include "textbox_colors.h"

const GXColor TextBox_DefaultColor = {255, 255, 255, 255};

// Per-ability colors keyed by element/visual identity.
const GXColor TextBox_AbilityColors[COPYKIND_NUM] = {
    [COPYKIND_FIRE]    = {255,  90,  40, 255}, // red-orange
    [COPYKIND_WHEEL]   = {220,  60,  80, 255}, // crimson
    [COPYKIND_SLEEP]   = {255, 180, 220, 255}, // soft pink
    [COPYKIND_SWORD]   = {200, 220, 255, 255}, // pale blue steel
    [COPYKIND_BOMB]    = {120, 120, 130, 255}, // gunmetal
    [COPYKIND_PLASMA]  = {200, 120, 255, 255}, // violet
    [COPYKIND_NEEDLE]  = {180, 220, 255, 255}, // light blue
    [COPYKIND_MIC]     = {255, 200,  90, 255}, // gold
    [COPYKIND_ICE]     = {130, 230, 255, 255}, // cyan
    [COPYKIND_TORNADO] = { 90, 220, 200, 255}, // teal
    [COPYKIND_BIRD]    = {240, 240, 250, 255}, // off-white (Cupid)
};

// Per-Kirby-color colors mirror the actual Kirby palette.
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

const GXColor TextBox_PatchColor       = {255, 200,  60, 255};
const GXColor TextBox_MachineColor     = {130, 180, 255, 255};
const GXColor TextBox_BoxColor         = {220, 170, 100, 255};
const GXColor TextBox_EventColor       = {200, 130, 255, 255};
const GXColor TextBox_StadiumColor     = {255, 160,  60, 255};
const GXColor TextBox_StageColor       = {140, 220, 240, 255};
const GXColor TextBox_TopRideItemColor = {255, 220, 100, 255};
const GXColor TextBox_ItemColor        = {180, 240, 160, 255};
const GXColor TextBox_TrapColor        = {200,  80,  80, 255};
const GXColor TextBox_DeathColor       = {255,  60,  60, 255};
const GXColor TextBox_EnergyColor      = {100, 200, 255, 255};
const GXColor TextBox_CheckColor       = {120, 230, 120, 255};
const GXColor TextBox_GoalColor        = {255, 215,   0, 255};
const GXColor TextBox_RewardColor      = {255, 240, 160, 255};
const GXColor TextBox_ShopColor        = { 90, 220, 200, 255};
