#include <stdio.h>

#include "game.h"
#include "machine.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "patch_cap.h"
#include "os.h"
#include "textbox_api.h"
#include "ap_announce.h"

// Vanilla's ceiling: 18 patches from the -2 start reach Patch_GetMaxValue's raw 16.
#define PATCH_CAP_VANILLA 18

int PatchCap_GetMax(void)
{
    int t = (int)ap_save->options.city_trial_patch_cap_max;
    if (!ap_save->options_received || t <= 0)
        return PATCH_CAP_VANILLA;
    return t > PATCH_STAT_MAX ? PATCH_STAT_MAX : t;
}

// The non-HP raw ceiling, floored at 1 so the stat ratio never divides by zero or flips.
static int PatchCap_GetRawMax(void)
{
    int raw = PatchCap_GetMax() - 2;
    return raw < 1 ? 1 : raw;
}

int PatchCap_GetCap(void)
{
    int max = PatchCap_GetMax();
    if (!ap_save->options_received)
        return max;
    int cap = (int)ap_save->options.city_trial_patch_cap_min + (int)ap_save->patch_cap_count;
    return cap > max ? max : cap;
}

float PatchCap_GetStatStart(int kind)
{
    return (kind == PATCHKIND_HP) ? 0.0f : -2.0f;
}

int PatchCap_IsStatAt(const float *values, int kind, int patches)
{
    float target = PatchCap_GetStatStart(kind) + (float)patches;
    float ceiling = (float)PatchCap_GetRawMax();
    return values[kind] >= (target > ceiling ? ceiling : target);
}

// The cap counts patches from each stat's start, so it is uniform across the nine stats
// until HP meets the raw ceiling. Stat-down deltas pass through.
static int PatchCap_ClampDelta(int kind, float current, int delta)
{
    if (delta <= 0) return delta;
    int cap = PatchCap_GetCap();
    float room = (PatchCap_GetStatStart(kind) + (float)cap) - current;
    if (room <= 0.0f) return 0;
    if ((float)delta > room) return (int)room;
    return delta;
}

// Replaces Machine_GivePatch (0x801cacf4) with the delta pre-clamped.
static void PatchCap_GivePatch(MachineData *md, PatchKind kind, int num)
{
    num = PatchCap_ClampDelta(kind, md->stats.values[kind], num);
    Machine_ApplyStatClamped(md->stats.values, kind, num);
    Machine_UpdateAppearance(md);
    if (!md->suppress_attr_recalc)
        Machine_AdjustAttributes(md);
}

// Replaces Machine_GiveAllUp (0x801cad40), clamping each stat. The all-up counter is
// credited the uncapped num, as in vanilla.
static void PatchCap_GiveAllUp(MachineData *md, int num)
{
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        int capped = PatchCap_ClampDelta(i, md->stats.values[i], num);
        Machine_ApplyStatClamped(md->stats.values, i, capped);
    }

    int ply = Machine_GetRiderPly(md);
    if (ply != PLY_NUM)
        Ply_SetAllUpCollected(ply, num + Ply_GetAllUpCollected(ply));

    Machine_UpdateAppearance(md);
    if (!md->suppress_attr_recalc)
        Machine_AdjustAttributes(md);
}

// Replaces Patch_GetMaxValue (0x8000aaf0): the raw clamp of every stat and the divisor of
// Machine_GetStatRatio, which feeds the HUD and the machine physics. Returning the non-HP
// raw ceiling keeps a full stat at ratio 1 and leaves HP two patches short, as in vanilla.
static int PatchCap_GetMaxValue(void)
{
    return PatchCap_GetRawMax();
}

void PatchCap_Increment(void)
{
    int before = PatchCap_GetCap();
    if (ap_save->patch_cap_count < 255)
        ap_save->patch_cap_count++;
    int cap = PatchCap_GetCap();
    int max = PatchCap_GetMax();

    if (cap > before)
        OSReport("[PatchCap] Cap %d -> %d (max %d)\n", before, cap, max);
    else
        OSReport("[PatchCap] Cap already at the %d max, item had no effect\n", max);

    char suffix[32];
    sprintf(suffix, " increased! (%d/%d)", cap, max);
    APAnnounce_Grant(NULL, "Patch cap", tb_api->PatchColors[PATCHKIND_CHARGE], suffix);
}

void PatchCap_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(Patch_GetMaxValue, PatchCap_GetMaxValue);
    CODEPATCH_REPLACEFUNC(Machine_GivePatch, PatchCap_GivePatch);
    CODEPATCH_REPLACEFUNC(Machine_GiveAllUp, PatchCap_GiveAllUp);
    OSReport("[PatchCap] Hooks installed\n");
}
