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
int GateColors_IsColorUnlocked(int color_idx)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;
    return (save_data->color_unlocked_mask & (1 << color_idx)) != 0;
}

// Find the first unlocked color, or 0 as absolute fallback.
static int first_unlocked_color()
{
    for (int i = 0; i < KIRBYCOLOR_NUM; i++)
    {
        if (save_data->color_unlocked_mask & (1 << i))
            return i;
    }
    return 0;
}

// Validate a color index — if locked, return the first unlocked color.
// Used to intercept machine-lookup color assignments.
int GateColors_ValidateColor(int color_idx)
{
    if (GateColors_IsColorUnlocked(color_idx))
        return color_idx;
    int fallback = first_unlocked_color();
    OSReport("ValidateColor: %d locked, using %d\n", color_idx, fallback);
    return fallback;
}

// Validate the 4 Air Ride color[] entries in GameData.
// The CSS init block in zz_80028888_ sets color[0..3] = {0,1,2,3}
// which may contain locked colors. This replaces any locked entries
// with the first unlocked color. Called inline via HOOKCREATE.
void GateColors_ValidateAirRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    int fallback = first_unlocked_color();
    u8 *colors = gd->airride_select_ply.color;

    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(colors[i]))
            colors[i] = fallback;
    }
}

// Validate the 4 Top Ride color[] entries in GameData.
// The TR lobby init functions (zz_8002d0ec_ / zz_8002d9e8_) set
// color[0..3] = {0,1,2,3} which may contain locked colors.
void GateColors_ValidateTopRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    int fallback = first_unlocked_color();
    u8 *colors = gd->topride_select_ply.color;

    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(colors[i]))
            colors[i] = fallback;
    }
}

// Pick a random unlocked color. Replaces HSD_Randi call in loadCPU
// for Air Ride CPU color assignment.
int GateColors_GetRandomUnlockedColor(int unused)
{
    int unlocked[KIRBYCOLOR_NUM];
    int count = 0;

    for (int i = 0; i < KIRBYCOLOR_NUM; i++)
    {
        if (save_data->color_unlocked_mask & (1 << i))
            unlocked[count++] = i;
    }

    if (count == 0)
        return 0;

    int result = unlocked[HSD_Randi(count)];
    OSReport("GetRandomUnlockedColor: mask=0x%04x count=%d result=%d\n",
             save_data->color_unlocked_mask, count, result);
    return result;
}

// Filter the availability result at the convergence point of each CSS
// color changer. All code paths (colors 0-3 hardcoded, colors 4-7
// checklist) merge here. We AND the vanilla result with our mask.
static int GateColors_FilterResult(int vanilla_result, int color_idx)
{
    (void)vanilla_result;
    return GateColors_IsColorUnlocked(color_idx) ? 1 : 0;
}

// Hook for CSS_airRide_colorChanger convergence (0x8002176c).
// Clobbered: extsb. r0, r3. r23 = candidate color, r3 = vanilla result.
// All color paths (0-3 hardcoded available, 4-7 checklist) land here.
CODEPATCH_HOOKCREATE(0x8002176c,
    "extsb 4, 23\n\t",
    GateColors_FilterResult,
    "",
    0
)

// Hook for CSS_topRide_colorChanger convergence (0x8002a510).
// Clobbered: extsb. r0, r0. r23 = candidate color, r0 = vanilla result.
// Need to pass r0 as first arg and get result back in r0.
CODEPATCH_HOOKCREATE(0x8002a510,
    "mr 3, 0\n\t"
    "extsb 4, 23\n\t",
    GateColors_FilterResult,
    "mr 0, 3\n\t",
    0
)

// Hook for CitySelect_ChangeColor convergence (0x8002f350).
// Clobbered: extsb. r0, r3. r30 = candidate color, r3 = vanilla result.
CODEPATCH_HOOKCREATE(0x8002f350,
    "extsb 4, 30\n\t",
    GateColors_FilterResult,
    "",
    0
)

// Hook for machine-lookup color assignment in zz_80028888_ (Air Ride CSS).
// At 0x8002978c (stb r3, 45(r28)): the machine-to-color lookup result is
// about to be stored as the icon color. r3 = color from lookup.
// Clobbered instruction is the stb itself — it re-executes after our
// function, storing the validated r3. r28 is callee-saved so it survives.
CODEPATCH_HOOKCREATE(0x8002978c,
    "",
    GateColors_ValidateColor,
    "",
    0
)

// Hook for Air Ride CSS color[] init in zz_80028888_ (Race mode).
// At 0x800295e8 (li r8, 0): convergence point after the init block that
// sets color[0..3] = {0,1,2,3}. Both paths (init executed or skipped)
// reach this point. Validates all color entries against the unlock mask.
// Clobbered instruction is li r8, 0 — re-executed after our function.
// r3/r4 are set at 0x800295ec/0x800295f0 immediately after, so clobbering
// volatile registers is safe.
CODEPATCH_HOOKCREATE(0x800295e8,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

// Hook for Top Ride general data reset (zz_8002cfd8_).
// At 0x8002d06c (li r3, 0): convergence after the loop that sets
// color[0..3] = {0,1,2,3}. Called from MainMenu_InitAllVariables,
// Gm_ResetAllData, and scene transitions.
// Clobbered instruction is li r3, 0 — re-executed after.
CODEPATCH_HOOKCREATE(0x8002d06c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook for Top Ride Free Run init (zz_8002d0ec_).
// At 0x8002d704 (li r7, 0): convergence after the conditional color reset
// block that sets color[0..3] = {0,1,2,3}. Fires BEFORE the visual loop
// reads the colors, so the corrected values are used for display.
CODEPATCH_HOOKCREATE(0x8002d704,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook for Top Ride Time Attack init (zz_8002d9e8_).
// At 0x8002db8c (li r28, 0): right after unconditional color assignment,
// before the visual loop. Same purpose as above.
CODEPATCH_HOOKCREATE(0x8002db8c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook for Air Ride CSS color[] init in zz_80029bd8_ (Free Run / Time Attack).
// The CSS dispatcher (zz_8002a1b0_) calls zz_80029bd8_ instead of zz_80028888_
// when airride_mode != RACE. This alternate CSS has its own color init block at
// 0x80029dd0 that sets color[0..3] = {0,1,2,3}. Without this hook, locked
// colors appear on first entry.
// At 0x80029e34 (li r5, 0): convergence point — both paths reach here.
// Clobbered instruction is li r5, 0 — re-executed after our function.
// r4 is set at 0x80029e38 (extsb r4, r28) immediately after, so clobbering
// volatile registers is safe.
CODEPATCH_HOOKCREATE(0x80029e34,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

void GateColors_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8002176c);
    CODEPATCH_HOOKAPPLY(0x8002a510);
    CODEPATCH_HOOKAPPLY(0x8002f350);

    // Air Ride CPU color: replace all HSD_Randi calls that assign random
    // colors to CPU slots. Three call sites across different CSS functions.
    CODEPATCH_REPLACECALL(0x800236b4, GateColors_GetRandomUnlockedColor); // loadCPU
    CODEPATCH_REPLACECALL(0x80026534, GateColors_GetRandomUnlockedColor); // CSS_airRide_inputGrabberReadyScreen
    CODEPATCH_REPLACECALL(0x8002988c, GateColors_GetRandomUnlockedColor); // CSS_airRide unnamed

    // Air Ride machine-lookup color: validate the color assigned from the
    // machine-to-color table before it's stored as icon color.
    CODEPATCH_HOOKAPPLY(0x8002978c);

    // Air Ride CSS color[] init: validate color entries after the init
    // block sets them to default values (which may be locked).
    // Race mode uses zz_80028888_ (hook at 0x800295e8).
    // Free Run / Time Attack use zz_80029bd8_ (hook at 0x80029e34).
    CODEPATCH_HOOKAPPLY(0x800295e8);
    CODEPATCH_HOOKAPPLY(0x80029e34);

    // Top Ride: validate colors after all init paths that reset to {0,1,2,3}.
    // Hooks fire INSIDE the init functions, before the visual loop reads colors.
    CODEPATCH_HOOKAPPLY(0x8002d06c);  // general data reset (zz_8002cfd8_)
    CODEPATCH_HOOKAPPLY(0x8002d704);  // Free Run init (zz_8002d0ec_)
    CODEPATCH_HOOKAPPLY(0x8002db8c);  // Time Attack init (zz_8002d9e8_)

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

    int fallback = first_unlocked_color();

    OSReport("ForceDefaultColors: mask=0x%04x fallback=%d\n",
             save_data->color_unlocked_mask, fallback);

    u8 *ar_colors = gd->airride_select_ply.color;
    u8 *ar_icons = gd->airride_select_ply.icon;
    u8 *ct_colors = gd->city_select_ply.ply_color;
    u8 *tr_colors = gd->topride_select_ply.color;

    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(ar_colors[i]))
        {
            OSReport("  AR ply %d: color %d locked, forcing to %d\n",
                     i, ar_colors[i], fallback);
            ar_colors[i] = fallback;
        }
        if (!GateColors_IsColorUnlocked(ar_icons[i]))
        {
            OSReport("  AR ply %d: icon %d locked, forcing to %d\n",
                     i, ar_icons[i], fallback);
            ar_icons[i] = fallback;
        }
        if (!GateColors_IsColorUnlocked(ct_colors[i]))
            ct_colors[i] = fallback;
        if (!GateColors_IsColorUnlocked(tr_colors[i]))
            tr_colors[i] = fallback;
    }
}
