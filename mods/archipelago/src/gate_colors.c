#include "game.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_colors.h"
#include "inline.h"
#include "textbox_api.h"
#include "ap_announce.h"

static int GateColors_IsColorUnlocked(int color_idx)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;
    return (ap_save->color_unlocked_mask & (1 << color_idx)) != 0;
}

// The AP world always grants Pink (color 0), so 0 is a safe fallback on an empty mask.
static int first_unlocked_color()
{
    for (int i = 0; i < KIRBYCOLOR_NUM; i++)
    {
        if (ap_save->color_unlocked_mask & (1 << i))
            return i;
    }
    return 0;
}

static void validate_color_array(u8 *colors)
{
    int fallback = first_unlocked_color();
    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(colors[i]))
            colors[i] = fallback;
    }
}

// The AR CSS init block sets color[0..3] = {0,1,2,3}, which may contain locked colors.
static void GateColors_ValidateAirRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->airride_select_ply.color);
}

// The TR lobby init sets color[0..3] = {0,1,2,3}, which may contain locked colors.
static void GateColors_ValidateTopRideColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->topride_select_ply.color);
}

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

// Replaces the shared per-slot default on the CPU-slot branch only, so humans keep
// their CSS pick. slot_base = airride_select_ply base + slot; color[] is at +0x51.
void GateColors_SetCpuAirRideColor(u8 *slot_base)
{
    slot_base[0x51] = (u8)GateColors_RandomUnlockedColor();
}

// Hook at 0x800236a8 in loadCPU (`stb r0, 69(r29)`, the CPU-slot ply_kind write).
// r29 = airride_select_ply base + slot. The clobbered store needs r0 = 2, which the
// C call wipes.
CODEPATCH_HOOKCREATE(0x800236a8,
    "mr 3, 29\n\t",
    GateColors_SetCpuAirRideColor,
    "li 0, 2\n\t",
    0
)

// Availability filter at each CSS color changer's convergence point. All vanilla paths
// (colors 0-3 hardcoded, 4-7 checklist) merge here, so the mask overrides outright.
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

// Hook at 0x800295e8 (li r8, 0) in zz_80028888_ (Race mode): convergence after the
// color[0..3] init block. r3/r4 are reloaded just below, so clobbers are safe.
CODEPATCH_HOOKCREATE(0x800295e8,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

// Hook at 0x8002d06c (li r3, 0) in zz_8002cfd8_ (Top Ride data reset): convergence
// after the color[0..3] loop.
CODEPATCH_HOOKCREATE(0x8002d06c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x8002d704 (li r7, 0) in TopRide_RaceInit (zz_8002d0ec_, multiplayer):
// convergence after the color reset, before the visual loop reads the colors.
CODEPATCH_HOOKCREATE(0x8002d704,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x8002db8c (li r28, 0) in TopRide_SoloInit (zz_8002d9e8_), covering Free Run
// and Time Attack: after the color assignment, before the visual loop.
CODEPATCH_HOOKCREATE(0x8002db8c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x80029e34 (li r5, 0) in zz_80029bd8_ (Air Ride Free Run / Time Attack), the
// non-Race CSS with its own color[0..3] init block. r4 is reloaded just below, so
// clobbers are safe.
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
    if (!ap_regrant_quiet)
        OSReport("[GateColors] Color %d (%s) unlocked (mask = %s)\n",
                 color_idx, KirbyColor_Names[color_idx], MaskBits(ap_save->color_unlocked_mask, 8));
    if (announce)
        APAnnounce_Grant("Unlocked Color: ", KirbyColor_Names[color_idx],
                         tb_api->KirbyColors[color_idx], " Kirby");
    return 1;
}

// CT has no init block to hook (unlike AR/TR), so persisted selections from prior
// sessions can reference colors that are now locked.
void GateColors_ValidateCityTrialColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;
    validate_color_array(gd->city_select_ply.ply_color);
}
