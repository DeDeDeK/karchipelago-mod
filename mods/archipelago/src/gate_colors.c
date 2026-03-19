#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_colors.h"
#include "textbox.h"

static const char *color_names[KIRBYCOLOR_NUM] = {
    [KIRBYCOLOR_PINK]   = "Pink Kirby",
    [KIRBYCOLOR_YELLOW] = "Yellow Kirby",
    [KIRBYCOLOR_BLUE]   = "Blue Kirby",
    [KIRBYCOLOR_RED]    = "Red Kirby",
    [KIRBYCOLOR_GREEN]  = "Green Kirby",
    [KIRBYCOLOR_PURPLE] = "Purple Kirby",
    [KIRBYCOLOR_BROWN]  = "Brown Kirby",
    [KIRBYCOLOR_WHITE]  = "White Kirby",
};

// Check if a color is unlocked via the AP bitmask.
// Pink (0) is always available as the default color.
int GateColors_IsColorUnlocked(int color_idx)
{
    if (color_idx == KIRBYCOLOR_PINK)
        return 1;
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;
    return (save_data->color_unlocked_mask & (1 << color_idx)) != 0;
}

// Hook for CSS_airRide_colorChanger (0x80021654).
// At 0x80021704 (cmpwi r0, 0): reached for colors 0-3 only.
// Clobbered instruction is the cmpwi (only sets CR, doesn't touch r3).
// r23 = current color index being tested.
// Result expected in r3; convergence at 0x8002176c checks r3.
CODEPATCH_HOOKCREATE(0x80021704,
    "extsb 3, 23\n\t",
    GateColors_IsColorUnlocked,
    "",
    0x8002176c
)

// Hook for CSS_topRide_colorChanger (0x8002a400).
// At 0x8002a4b0 (cmpwi r0, 0): reached for colors 0-3 only.
// r23 = current color index.
// Result expected in r0; convergence at 0x8002a510 checks r0.
CODEPATCH_HOOKCREATE(0x8002a4b0,
    "extsb 3, 23\n\t",
    GateColors_IsColorUnlocked,
    "mr 0, 3\n\t",
    0x8002a510
)

// Hook for CitySelect_ChangeColor (0x8002f238).
// At 0x8002f2e8 (cmpwi r0, 0): reached for colors 0-3 only.
// r30 = current color index.
// Result expected in r3; convergence at 0x8002f350 checks r3.
CODEPATCH_HOOKCREATE(0x8002f2e8,
    "extsb 3, 30\n\t",
    GateColors_IsColorUnlocked,
    "",
    0x8002f350
)

void GateColors_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x80021704);
    CODEPATCH_HOOKAPPLY(0x8002a4b0);
    CODEPATCH_HOOKAPPLY(0x8002f2e8);
    OSReport("Color gating hooks installed\n");
}

int GateColors_UnlockColor(int color_idx)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;

    save_data->color_unlocked_mask |= (1 << color_idx);
    OSReport("Color %d (%s) unlocked (mask = 0x%02x)\n",
             color_idx, color_names[color_idx], save_data->color_unlocked_mask);
    TextBox_Enqueue(color_names[color_idx]);
    return 1;
}

// Force all players' selected colors to their first unlocked color
// if their current selection is locked. Called on select screen load.
void GateColors_ForceDefaultColors()
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    u8 *ar_colors = gd->airride_select_ply.color;
    u8 *ct_colors = gd->city_select_ply.ply_color;
    u8 *tr_colors = gd->topride_select_ply.color;

    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(ar_colors[i]))
            ar_colors[i] = KIRBYCOLOR_PINK;
        if (!GateColors_IsColorUnlocked(ct_colors[i]))
            ct_colors[i] = KIRBYCOLOR_PINK;
        if (!GateColors_IsColorUnlocked(tr_colors[i]))
            tr_colors[i] = KIRBYCOLOR_PINK;
    }
}
