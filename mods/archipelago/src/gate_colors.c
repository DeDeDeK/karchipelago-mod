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

// Prefers an unlocked color none of `taken` already shows, so CPUs stay distinct
// the way vanilla's per-slot {0,1,2,3} seeding did. Repeats only once the unlocked
// set is too small to give every slot its own.
int GateColors_RandomUnlockedColorExcept(const u8 *taken, int num_taken)
{
    int pool[KIRBYCOLOR_NUM];
    int count = 0;

    for (int allow_repeat = 0; allow_repeat < 2 && count == 0; allow_repeat++)
    {
        for (int i = 0; i < KIRBYCOLOR_NUM; i++)
        {
            if (!(ap_save->color_unlocked_mask & (1 << i)))
                continue;

            if (!allow_repeat)
            {
                int used = 0;
                for (int j = 0; j < num_taken; j++)
                {
                    if (taken[j] == i)
                    {
                        used = 1;
                        break;
                    }
                }
                if (used)
                    continue;
            }
            pool[count++] = i;
        }
    }

    if (count == 0)
        return 0;
    return pool[HSD_Randi(count)];
}

int GateColors_RandomUnlockedColor(void)
{
    return GateColors_RandomUnlockedColorExcept(NULL, 0);
}

// Replaces the shared per-slot default on the CPU-slot branch only, so humans keep
// their CSS pick. slot_base = airride_select_ply base + slot; color[] is at +0x51.
// loadCPU walks the slots in order, so the slots already picked are visible here.
void GateColors_SetCpuAirRideColor(u8 *slot_base)
{
    GameData *gd = Gm_GetGameData();
    u8 taken[4];
    int num_taken = 0;

    if (gd)
    {
        int slot = (int)(&slot_base[0x51] - gd->airride_select_ply.color);
        for (int i = 0; i < 4; i++)
        {
            u8 kind = gd->airride_select_ply.slot_kind[i];
            if (i != slot && (kind == 0 || kind == 2))
                taken[num_taken++] = gd->airride_select_ply.color[i];
        }
    }
    slot_base[0x51] = (u8)GateColors_RandomUnlockedColorExcept(taken, num_taken);
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

// City Trial and Top Ride both use 2 for a CPU panel; the value meaning "active human"
// differs between them, so the caller passes it.
#define CSS_KIND_CPU 2

// The colors the other visible panels are showing, for the distinctness pass.
static int css_collect_taken(const u8 *kinds, const u8 *colors, int slot,
                             u8 human_kind, u8 *taken)
{
    int num_taken = 0;
    for (int i = 0; i < 4; i++)
    {
        if (i != slot && (kinds[i] == human_kind || kinds[i] == CSS_KIND_CPU))
            taken[num_taken++] = colors[i];
    }
    return num_taken;
}

static void ct_repaint_player(GameData *gd, int slot)
{
    u8 pkind = gd->city_select_ply.ply_pkind[slot];
    s8 anim_kind = (gd->city_select_ply.x1d0 == 2 &&
                    !(gd->city_select_ply.x1d4 & (1 << slot)) &&
                    pkind == 4)
                       ? 5
                       : (s8)pkind;

    CitySelect_UpdatePlayer((s8)slot, anim_kind,
                            CitySelect_GetColorAnimFrame((s8)gd->city_select_ply.ply_color[slot]));
}

// CitySelect_InputUpdate holds the only site that turns a City Trial panel into a CPU
// (the x215 3 -> 2 branch), and it reloads ply_color and repaints two instructions
// later, so storing the color is the whole job. Cycling a CPU's color by hand goes
// through CitySelect_ChangeColor and never reaches here, so a manual pick survives
// until the panel is switched off and back on.
void GateColors_OnCityTrialCpuAdded(int slot)
{
    GameData *gd = Gm_GetGameData();
    u8 taken[4];
    int num_taken;
    int color;

    if (!gd || slot < 0 || slot >= 4)
        return;

    num_taken = css_collect_taken(gd->city_select_ply.x215,
                                  gd->city_select_ply.ply_color, slot, 0, taken);
    color = GateColors_RandomUnlockedColorExcept(taken, num_taken);
    gd->city_select_ply.ply_color[slot] = (u8)color;
    OSReport("[GateColors] CT CSS: CPU %d took color %d\n", slot, color);
}

// Clobbered: add r3, r23, r25 - recomputed from callee-saved registers after the call.
// r25 = slot.
CODEPATCH_HOOKCREATE(0x80033560,
    "mr 3, 25\n\t",
    GateColors_OnCityTrialCpuAdded,
    "",
    0
)

// Top Ride writes panel_pkind from seven inlined sites, three of which reach CPU by
// stepping the kind rather than storing 2, so the kinds are compared against a mirror
// instead. TopRide_LobbyThink is minor 9's think function, so this runs only while the
// lobby is up.
static u8 tr_prev_pkind[4];
static int tr_pkind_seeded;

void GateColors_OnTopRideLobbyInit(void)
{
    tr_pkind_seeded = 0;
}

void GateColors_OnTopRideLobbyThink(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    // Seeding on the first frame rather than in the init hook lets the lobby's own
    // setup run first, and leaves a panel that opens as CPU on the color it has.
    if (!tr_pkind_seeded)
    {
        for (int i = 0; i < 4; i++)
            tr_prev_pkind[i] = gd->topride_select_ply.panel_pkind[i];
        tr_pkind_seeded = 1;
        return;
    }

    for (int i = 0; i < 4; i++)
    {
        u8 kind = gd->topride_select_ply.panel_pkind[i];

        if (kind == CSS_KIND_CPU && tr_prev_pkind[i] != CSS_KIND_CPU)
        {
            u8 taken[4];
            int num_taken = css_collect_taken(gd->topride_select_ply.panel_pkind,
                                              gd->topride_select_ply.color, i, 1, taken);
            int color = GateColors_RandomUnlockedColorExcept(taken, num_taken);

            gd->topride_select_ply.color[i] = (u8)color;
            TopRide_UpdatePanel((s8)i, (s8)kind, CitySelect_GetColorAnimFrame((s8)color));
            OSReport("[GateColors] TR lobby: CPU %d took color %d\n", i, color);
        }
        tr_prev_pkind[i] = kind;
    }
}

// Both clobber stw r31, 12(r1), past the LR save and using only preserved registers.
CODEPATCH_HOOKCREATE(0x8002dca8, "", GateColors_OnTopRideLobbyInit, "", 0)
CODEPATCH_HOOKCREATE(0x8002dd40, "", GateColors_OnTopRideLobbyThink, "", 0)

// CT seeds ply_color with {0,1,2,3} at four sites - CitySelect_InitSelectData
// (0x80038c40) and one copy in each of the three CSS sub-loaders - but every one is
// guarded on the CT sub-mode changing, so a load arrives with either those values or
// the previous session's, and either can hold a now-locked color.
void GateColors_ValidateCityTrialColors(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    // Unlike the AR and TR validators, this runs after its screen has already
    // painted, so a clamped panel has to be redrawn.
    int fallback = first_unlocked_color();
    for (int i = 0; i < 4; i++)
    {
        if (GateColors_IsColorUnlocked(gd->city_select_ply.ply_color[i]))
            continue;
        gd->city_select_ply.ply_color[i] = (u8)fallback;
        ct_repaint_player(gd, i);
    }
}

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
    CODEPATCH_HOOKAPPLY(0x80033560);  // CT CSS panel turned CPU
    CODEPATCH_HOOKAPPLY(0x8002dca8);  // TR lobby init
    CODEPATCH_HOOKAPPLY(0x8002dd40);  // TR lobby think

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

