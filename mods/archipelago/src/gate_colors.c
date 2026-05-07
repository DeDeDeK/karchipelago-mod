#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_colors.h"
#include "inline.h"
#include "textbox_api.h"

// Check if a color is unlocked via the AP bitmask.
static int GateColors_IsColorUnlocked(int color_idx)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;
    return (ap_save->color_unlocked_mask & (1 << color_idx)) != 0;
}

// Find the first unlocked color. The AP world guarantees Pink (color 0)
// is always granted, so 0 is a safe fallback if the mask is somehow empty.
static int first_unlocked_color()
{
    for (int i = 0; i < KIRBYCOLOR_NUM; i++)
    {
        if (ap_save->color_unlocked_mask & (1 << i))
            return i;
    }
    return 0;
}

// Replace any locked entries in a 4-element color array with the first
// unlocked color. Shared by the AR / TR HOOKCREATE post-init validators.
static void validate_color_array(u8 *colors)
{
    int fallback = first_unlocked_color();
    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(colors[i]))
            colors[i] = fallback;
    }
}

// Validate a single color index — if locked, return the first unlocked color.
// Used to intercept machine-lookup color assignments.
static int GateColors_ValidateColor(int color_idx)
{
    if (GateColors_IsColorUnlocked(color_idx))
        return color_idx;
    int fallback = first_unlocked_color();
    OSReport("[GateColors] ValidateColor: %d locked, using %d\n", color_idx, fallback);
    return fallback;
}

// Validate the 4 Air Ride color[] entries in GameData.
// The CSS init block in zz_80028888_ sets color[0..3] = {0,1,2,3}
// which may contain locked colors. Called inline via HOOKCREATE.
static void GateColors_ValidateAirRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->airride_select_ply.color);
}

// Validate the 4 Top Ride color[] entries in GameData.
// The TR lobby init functions (zz_8002d0ec_ / zz_8002d9e8_) set
// color[0..3] = {0,1,2,3} which may contain locked colors.
static void GateColors_ValidateTopRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->topride_select_ply.color);
}

// Pick a random unlocked color. Replaces HSD_Randi call in loadCPU
// for Air Ride CPU color assignment.
static int GateColors_GetRandomUnlockedColor(int unused)
{
    int unlocked[KIRBYCOLOR_NUM];
    int count = 0;

    for (int i = 0; i < KIRBYCOLOR_NUM; i++)
    {
        if (ap_save->color_unlocked_mask & (1 << i))
            unlocked[count++] = i;
    }

    if (count == 0)
        return 0;

    int result = unlocked[HSD_Randi(count)];
    OSReport("[GateColors] GetRandomUnlockedColor: mask=%s count=%d result=%d\n",
             MaskBits(ap_save->color_unlocked_mask, 8), count, result);
    return result;
}

// Filter the availability result at the convergence point of each CSS
// color changer. All code paths (colors 0-3 hardcoded, colors 4-7
// checklist) merge here, so we override entirely with the unlock mask
// instead of combining with the vanilla result.
static int GateColors_FilterResult(int color_idx)
{
    return GateColors_IsColorUnlocked(color_idx) ? 1 : 0;
}

// Hook for CSS_airRide_colorChanger convergence (0x8002176c).
// Clobbered: extsb. r0, r3. r23 = candidate color.
CODEPATCH_HOOKCREATE(0x8002176c,
    "extsb 3, 23\n\t",
    GateColors_FilterResult,
    "",
    0
)

// Hook for CSS_topRide_colorChanger convergence (0x8002a510).
// Clobbered: extsb. r0, r0. r23 = candidate color, result returned in r0.
CODEPATCH_HOOKCREATE(0x8002a510,
    "extsb 3, 23\n\t",
    GateColors_FilterResult,
    "mr 0, 3\n\t",
    0
)

// Hook for CitySelect_ChangeColor convergence (0x8002f350).
// Clobbered: extsb. r0, r3. r30 = candidate color.
CODEPATCH_HOOKCREATE(0x8002f350,
    "extsb 3, 30\n\t",
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

// Hook for Top Ride Race / Start Game init (TopRide_RaceInit, zz_8002d0ec_).
// Dispatched from TopRide_LobbyInit when TopRide_GetMode() == 0 (multiplayer).
// At 0x8002d704 (li r7, 0): convergence after the conditional color reset
// block that sets color[0..3] = {0,1,2,3}. Fires BEFORE the visual loop
// reads the colors, so the corrected values are used for display.
CODEPATCH_HOOKCREATE(0x8002d704,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook for Top Ride Solo init (TopRide_SoloInit, zz_8002d9e8_).
// Dispatched from TopRide_LobbyInit when TopRide_GetMode() != 0 — covers
// both Free Run (mode 1) and Time Attack (mode 2).
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
    CODEPATCH_HOOKAPPLY(0x8002d06c);  // TopRide_InitSelectData (zz_8002cfd8_)
    CODEPATCH_HOOKAPPLY(0x8002d704);  // TopRide_RaceInit — Race / Start Game (zz_8002d0ec_)
    CODEPATCH_HOOKAPPLY(0x8002db8c);  // TopRide_SoloInit — Free Run + Time Attack (zz_8002d9e8_)

    OSReport("[GateColors] Color gating hooks installed\n");
}

int GateColors_UnlockColor(int color_idx)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;

    ap_save->color_unlocked_mask |= (1 << color_idx);
    OSReport("[GateColors] Color %d (%s) unlocked (mask = %s)\n",
             color_idx, KirbyColor_Names[color_idx], MaskBits(ap_save->color_unlocked_mask, 8));
    tb_api->EnqueueColoredNoun(NULL, KirbyColor_Names[color_idx],
                               tb_api->KirbyColors[color_idx], " Kirby");
    return 1;
}

// Validate the 4 City Trial ply_color[] entries. CT has no init block to
// hook (unlike AR/TR), so persisted selections from prior sessions can
// reference colors that are now locked. Called from OnPlayerSelectLoad on
// CT CSS entry.
void GateColors_ValidateCityTrialColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->city_select_ply.ply_color);
}
