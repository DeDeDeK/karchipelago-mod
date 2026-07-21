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

// Validate a single color index - if locked, return the first unlocked color.
// Used to intercept machine-lookup color assignments.
static int GateColors_ValidateColor(int color_idx)
{
    if (GateColors_IsColorUnlocked(color_idx))
        return color_idx;
    int fallback = first_unlocked_color();
    OSReport("[GateColors] ValidateColor: %d locked, using %d\n", color_idx, fallback);
    return fallback;
}

// Validate the 4 Air Ride color[] entries in GameData; the CSS init block sets
// color[0..3] = {0,1,2,3}, which may contain locked colors.
static void GateColors_ValidateAirRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->airride_select_ply.color);
}

// Validate the 4 Top Ride color[] entries in GameData; the TR lobby init sets
// color[0..3] = {0,1,2,3}, which may contain locked colors.
static void GateColors_ValidateTopRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->topride_select_ply.color);
}

// Pick a random unlocked Kirby color. The AP world guarantees Pink (color 0)
// is always granted, so 0 is a safe fallback if the mask is somehow empty.
int GateColors_RandomUnlockedColor(void)
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
    return unlocked[HSD_Randi(count)];
}

// Give an Air Ride CPU slot a random unlocked color instead of the shared per-slot
// default. Fires only on the CPU-slot branch, so humans keep their CSS pick.
// slot_base = airride_select_ply base + slot; color[] is at +0x51.
void GateColors_SetCpuAirRideColor(u8 *slot_base)
{
    slot_base[0x51] = (u8)GateColors_RandomUnlockedColor();
}

// Hook at 0x800236a8 in loadCPU (`stb r0, 69(r29)`, the CPU-slot ply_kind write).
// r29 = airride_select_ply base + slot; this branch is CPU-only. The clobbered store
// needs r0 = 2, which our C call wipes, so the epilogue restores it before the
// framework re-executes the store.
CODEPATCH_HOOKCREATE(0x800236a8,
    "mr 3, 29\n\t",
    GateColors_SetCpuAirRideColor,
    "li 0, 2\n\t",
    0
)

// Filter the availability result at each CSS color changer's convergence point.
// All paths (colors 0-3 hardcoded, 4-7 checklist) merge here, so we override
// entirely with the unlock mask rather than combining with the vanilla result.
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

// Hook at 0x8002978c (stb r3, 45(r28)) in zz_80028888_: validates the
// machine-to-color lookup result (r3) before it's stored as the icon color.
// The stb re-executes after, storing the validated r3.
CODEPATCH_HOOKCREATE(0x8002978c,
    "",
    GateColors_ValidateColor,
    "",
    0
)

// Hook at 0x800295e8 (li r8, 0) in zz_80028888_ (Race mode): convergence after the
// color[0..3] = {0,1,2,3} init block. Validates the entries against the mask.
// li r8, 0 re-executes after; r3/r4 are reloaded just below, so clobbers are safe.
CODEPATCH_HOOKCREATE(0x800295e8,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

// Hook at 0x8002d06c (li r3, 0) in zz_8002cfd8_ (Top Ride data reset): convergence
// after the color[0..3] = {0,1,2,3} loop. li r3, 0 re-executes after.
CODEPATCH_HOOKCREATE(0x8002d06c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x8002d704 (li r7, 0) in TopRide_RaceInit (zz_8002d0ec_, multiplayer):
// convergence after the color reset. Fires before the visual loop reads the colors,
// so corrected values are displayed.
CODEPATCH_HOOKCREATE(0x8002d704,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x8002db8c (li r28, 0) in TopRide_SoloInit (zz_8002d9e8_): covers Free Run
// and Time Attack. Right after the color assignment, before the visual loop.
CODEPATCH_HOOKCREATE(0x8002db8c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x80029e34 (li r5, 0) in zz_80029bd8_ (Air Ride Free Run / Time Attack):
// the non-Race CSS with its own color[0..3] = {0,1,2,3} init block. Validates the
// entries against the mask. li r5, 0 re-executes after; r4 is reloaded just below,
// so clobbers are safe.
CODEPATCH_HOOKCREATE(0x80029e34,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

void GateColors_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8002176c);  // CSS_airRide_colorChanger
    CODEPATCH_HOOKAPPLY(0x8002a510);  // CSS_topRide_colorChanger
    CODEPATCH_HOOKAPPLY(0x8002f350);  // CitySelect_ChangeColor
    CODEPATCH_HOOKAPPLY(0x800236a8);  // loadCPU CPU-slot color
    CODEPATCH_HOOKAPPLY(0x8002978c);  // AR machine-lookup color
    CODEPATCH_HOOKAPPLY(0x800295e8);  // AR CSS init (Race)
    CODEPATCH_HOOKAPPLY(0x80029e34);  // AR CSS init (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002d06c);  // TR data reset
    CODEPATCH_HOOKAPPLY(0x8002d704);  // TR Race init
    CODEPATCH_HOOKAPPLY(0x8002db8c);  // TR Solo init

    OSReport("[GateColors] Color gating hooks installed\n");
}

int GateColors_UnlockColor(int color_idx, int announce)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;

    ap_save->color_unlocked_mask |= (1 << color_idx);
    OSReport("[GateColors] Color %d (%s) unlocked (mask = %s)\n",
             color_idx, KirbyColor_Names[color_idx], MaskBits(ap_save->color_unlocked_mask, 8));
    if (announce)
        tb_api->EnqueueColoredNoun("Unlocked Color: ", KirbyColor_Names[color_idx],
                                   tb_api->KirbyColors[color_idx], " Kirby");
    return 1;
}

// Validate the 4 City Trial ply_color[] entries. CT has no init block to hook
// (unlike AR/TR), so persisted selections from prior sessions can reference
// colors that are now locked.
void GateColors_ValidateCityTrialColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->city_select_ply.ply_color);
}
