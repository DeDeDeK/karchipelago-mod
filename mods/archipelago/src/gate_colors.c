#include "game.h"
#include "os.h"
#include "scene.h"
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

// Pink when nothing is unlocked: some color has to show.
static int FirstUnlockedColor(void)
{
    u8 m = ap_save->color_unlocked_mask;
    return m ? __builtin_ctz(m) : 0;
}

static void ValidateColorArray(u8 *colors)
{
    int fallback = FirstUnlockedColor();
    for (int i = 0; i < 4; i++)
    {
        if (!GateColors_IsColorUnlocked(colors[i]))
            colors[i] = fallback;
    }
}

// The AR and TR init blocks seed color[0..3] = {0,1,2,3}, which may hold locked colors.
static void GateColors_ValidateAirRideColors(void)
{
    ValidateColorArray(Gm_GetGameData()->airride_select_ply.color);
}

static void GateColors_ValidateTopRideColors(void)
{
    ValidateColorArray(Gm_GetGameData()->topride_select_ply.color);
}

// A random unlocked color none of `taken` shows; repeats only when nothing else is left.
static int GateColors_RandomUnlockedColorExcept(const u8 *taken, int num_taken)
{
    u32 taken_mask = 0;
    for (int j = 0; j < num_taken; j++)
    {
        if (taken[j] < KIRBYCOLOR_NUM)
            taken_mask |= 1u << taken[j];
    }

    int pick = RandomBitInField(ap_save->color_unlocked_mask & ~taken_mask);
    if (pick < 0)
        pick = RandomBitInField(ap_save->color_unlocked_mask);
    return pick < 0 ? 0 : pick;
}

_Static_assert((int)TR_PANEL_CPU == (int)SELECTSLOT_CPU, "every select screen encodes a CPU alike");

// The human kind differs between the select screens, so callers pass it.
int GateColors_RandomForPanel(const u8 *kinds, const u8 *colors, int slot, u8 human_kind)
{
    u8 taken[4];
    int num_taken = 0;
    for (int i = 0; i < 4; i++)
    {
        if (i != slot && (kinds[i] == human_kind || kinds[i] == SELECTSLOT_CPU))
            taken[num_taken++] = colors[i];
    }
    return GateColors_RandomUnlockedColorExcept(taken, num_taken);
}

// CPU slots only, so humans keep their CSS pick. loadCPU walks the slots in order, so the
// ones already picked show here.
static void GateColors_SetCpuAirRideColor(int slot)
{
    GameData *gd = Gm_GetGameData();
    if ((u32)slot >= 4)
        return;
    gd->airride_select_ply.color[slot] = (u8)GateColors_RandomForPanel(
        gd->airride_select_ply.slot_kind, gd->airride_select_ply.color, slot, SELECTSLOT_HMN);
}

// Hook at 0x800236a8 (stb r0, 69(r29), the CPU-slot ply_kind write) in loadCPU
// (0x80023600). r28 = the loop's slot; the clobbered store needs r0 = 2.
CODEPATCH_HOOKCREATE(0x800236a8,
    "clrlwi 3, 28, 24\n\t",
    GateColors_SetCpuAirRideColor,
    "li 0, 2\n\t",
    0
)

// Each color changer's availability answer, where its vanilla paths (colors 0-3
// hardcoded, 4-7 by checklist) merge, so the mask overrides them outright.

// In CSS_airRide_colorChanger (0x80021654). Clobbered: extsb. r0, r3. r23 = candidate.
CODEPATCH_HOOKCREATE(0x8002176c,
    "extsb 3, 23\n\t",
    GateColors_IsColorUnlocked,
    "",
    0
)

// In CSS_topRide_colorChanger (0x8002a400). Clobbered: extsb. r0, r0; the answer goes in
// r0. r23 = candidate.
CODEPATCH_HOOKCREATE(0x8002a510,
    "extsb 3, 23\n\t",
    GateColors_IsColorUnlocked,
    "mr 0, 3\n\t",
    0
)

// In CitySelect_ChangeColor (0x8002f238). Clobbered: extsb. r0, r3. r30 = candidate.
CODEPATCH_HOOKCREATE(0x8002f350,
    "extsb 3, 30\n\t",
    GateColors_IsColorUnlocked,
    "",
    0
)

// Hook at 0x800295e8 (li r8, 0) in CSS_airRide_RaceUpdate (0x80028888), after the
// color[0..3] init block. r3 / r4 are reloaded just below.
CODEPATCH_HOOKCREATE(0x800295e8,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

// Hook at 0x8002d06c (li r3, 0) in TopRide_InitSelectData (0x8002cfd8), after the
// color[0..3] loop.
CODEPATCH_HOOKCREATE(0x8002d06c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x8002d704 (li r7, 0) in TopRide_RaceInit (0x8002d0ec), after the color reset
// and before the visual loop reads it.
CODEPATCH_HOOKCREATE(0x8002d704,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x8002db8c (li r28, 0) in TopRide_SoloInit (0x8002d9e8), after the color
// assignment and before the visual loop.
CODEPATCH_HOOKCREATE(0x8002db8c,
    "",
    GateColors_ValidateTopRideColors,
    "",
    0
)

// Hook at 0x80029e34 (li r5, 0) in CSS_airRide_FreeTimeUpdate (0x80029bd8), the Free Run
// / Time Attack CSS with its own color[0..3] init. r4 is reloaded just below.
CODEPATCH_HOOKCREATE(0x80029e34,
    "",
    GateColors_ValidateAirRideColors,
    "",
    0
)

static void CTRepaintPlayer(GameData *gd, int slot)
{
    u8 pkind = gd->city_select_ply.ply_pkind[slot];
    s8 anim_kind = (gd->city_select_ply.mode == CITYMODE_FREERUN &&
                    !(gd->city_select_ply.active_pad_mask & (1 << slot)) &&
                    pkind == 4)
                       ? 5
                       : (s8)pkind;

    CitySelect_UpdatePlayer((s8)slot, anim_kind,
                            Gm_GetColorAnimFrame((s8)gd->city_select_ply.ply_color[slot]));
}

static void GateColors_OnCityTrialCpuAdded(int slot)
{
    GameData *gd = Gm_GetGameData();
    if (slot < 0 || slot >= 4)
        return;

    int color = GateColors_RandomForPanel(gd->city_select_ply.slot_kind,
                                          gd->city_select_ply.ply_color, slot, SELECTSLOT_HMN);
    gd->city_select_ply.ply_color[slot] = (u8)color;
    OSReport("[GateColors] CT CSS: CPU %d took color %d\n", slot, color);
}

// Hook at 0x80033560 in CitySelect_InputUpdate (0x80032d34), the one slot_kind 3 -> 2 (CPU)
// branch, which reloads ply_color and repaints right after. A manual pick goes through
// CitySelect_ChangeColor and survives until the panel is switched off. r25 = slot; the
// clobbered add r3, r23, r25 is recomputed after the call.
CODEPATCH_HOOKCREATE(0x80033560,
    "mr 3, 25\n\t",
    GateColors_OnCityTrialCpuAdded,
    "",
    0
)

// panel_pkind has seven inlined writers, some stepping into CPU rather than storing 2, so
// CPU transitions are caught by diffing a mirror each lobby frame.
static u8 tr_prev_pkind[4];
static int tr_pkind_seeded;

static void GateColors_OnTopRideLobbyInit(void)
{
    tr_pkind_seeded = 0;
}

static void GateColors_OnTopRideLobbyThink(void)
{
    GameData *gd = Gm_GetGameData();

    // Seeded on the first think, after the lobby's own setup, so a panel opening as CPU
    // keeps its color.
    if (!tr_pkind_seeded)
    {
        memcpy(tr_prev_pkind, gd->topride_select_ply.panel_pkind, sizeof(tr_prev_pkind));
        tr_pkind_seeded = 1;
        return;
    }

    for (int i = 0; i < 4; i++)
    {
        u8 kind = gd->topride_select_ply.panel_pkind[i];

        if (kind == TR_PANEL_CPU && tr_prev_pkind[i] != TR_PANEL_CPU)
        {
            int color = GateColors_RandomForPanel(gd->topride_select_ply.panel_pkind,
                                                  gd->topride_select_ply.color, i, TR_PANEL_HMN);

            gd->topride_select_ply.color[i] = (u8)color;
            TopRide_UpdatePanel((s8)i, (s8)kind, Gm_GetColorAnimFrame((s8)color));
            OSReport("[GateColors] TR lobby: CPU %d took color %d\n", i, color);
        }
        tr_prev_pkind[i] = kind;
    }
}

// In TopRide_LobbyInit (0x8002dc9c) and TopRide_LobbyThink (0x8002dd34). Both clobber
// stw r31, 12(r1), past the LR save and using only preserved registers.
CODEPATCH_HOOKCREATE(0x8002dca8, "", GateColors_OnTopRideLobbyInit, "", 0)
CODEPATCH_HOOKCREATE(0x8002dd40, "", GateColors_OnTopRideLobbyThink, "", 0)

// City Trial seeds ply_color only on a sub-mode change, so a load can carry a now-locked
// color from an earlier session.
void GateColors_ValidateCityTrialColors(void)
{
    GameData *gd = Gm_GetGameData();

    // This runs after the screen painted, so a clamped panel is redrawn.
    int fallback = FirstUnlockedColor();
    for (int i = 0; i < 4; i++)
    {
        if (GateColors_IsColorUnlocked(gd->city_select_ply.ply_color[i]))
            continue;
        gd->city_select_ply.ply_color[i] = (u8)fallback;
        CTRepaintPlayer(gd, i);
    }
}

void GateColors_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8002176c);
    CODEPATCH_HOOKAPPLY(0x8002a510);
    CODEPATCH_HOOKAPPLY(0x8002f350);
    CODEPATCH_HOOKAPPLY(0x800236a8);
    CODEPATCH_HOOKAPPLY(0x800295e8);
    CODEPATCH_HOOKAPPLY(0x80029e34);
    CODEPATCH_HOOKAPPLY(0x8002d06c);
    CODEPATCH_HOOKAPPLY(0x8002d704);
    CODEPATCH_HOOKAPPLY(0x8002db8c);
    CODEPATCH_HOOKAPPLY(0x80033560);
    CODEPATCH_HOOKAPPLY(0x8002dca8);
    CODEPATCH_HOOKAPPLY(0x8002dd40);

    OSReport("[GateColors] Hooks installed\n");
}

int GateColors_UnlockColor(int color_idx, int announce)
{
    if (color_idx < 0 || color_idx >= KIRBYCOLOR_NUM)
        return 0;

    ap_save->color_unlocked_mask |= (1 << color_idx);
    if (!ap_regrant_quiet)
        OSReport("[GateColors] Color %d (%s) unlocked (mask = %s)\n",
                 color_idx, KirbyColor_Names[color_idx], MaskBits(ap_save->color_unlocked_mask, KIRBYCOLOR_NUM));
    if (announce)
        APAnnounce_Grant("Unlocked Color: ", KirbyColor_Names[color_idx],
                         tb_api->KirbyColors[color_idx], " Kirby");
    return 1;
}

