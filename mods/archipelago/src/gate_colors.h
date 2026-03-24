#ifndef GATE_COLORS_H
#define GATE_COLORS_H

// Kirby color indices (matching the game's internal color_idx values)
typedef enum KirbyColor
{
    KIRBYCOLOR_PINK   = 0,
    KIRBYCOLOR_YELLOW = 1,
    KIRBYCOLOR_BLUE   = 2,
    KIRBYCOLOR_RED    = 3,
    KIRBYCOLOR_GREEN  = 4,
    KIRBYCOLOR_PURPLE = 5,
    KIRBYCOLOR_BROWN  = 6,
    KIRBYCOLOR_WHITE  = 7,
    KIRBYCOLOR_NUM    = 8,
} KirbyColor;

void GateColors_OnBoot();
int GateColors_IsColorUnlocked(int color_idx);
int GateColors_UnlockColor(int color_idx);
int GateColors_GetRandomUnlockedColor(int unused);
void GateColors_ForceDefaultColors();

#endif
