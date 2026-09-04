#include "game.h"
#include "machine.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "patch_cap.h"
#include "os.h"
#include "textbox_api.h"

// Per-slot patch cap ceiling: what PatchCap_GetCap clamps to, and the Max Stats
// goal threshold. A stored 0 means options have not been received yet, so it maps
// to PATCH_STAT_MAX.
static int PatchCap_GetMax()
{
    int t = (int)ap_save->options.city_trial_patch_cap_max;
    if (t <= 0) t = PATCH_STAT_MAX;
    if (t > PATCH_STAT_MAX) t = PATCH_STAT_MAX;
    return t;
}

// Current effective cap: starts at city_trial_patch_cap_min and each Patch Cap
// Increase item adds one, clamped to the max. min == max is a flat cap. A stored
// min of 0 means options have not been received yet.
static int PatchCap_GetCap()
{
    int min = (int)ap_save->options.city_trial_patch_cap_min;
    int max = PatchCap_GetMax();
    if (min == 0)
        return max;
    int cap = min + (int)ap_save->patch_cap_count;
    if (cap > max)
        cap = max;
    return cap;
}

// City Trial stats spawn at -2, except HP at 0. The cap counts patches collected,
// which is (value - start), so the clamp ceiling and the Max Stats goal share
// this one baseline.
float PatchCap_GetStatStart(int kind)
{
    return (kind == PATCHKIND_HP) ? 0.0f : -2.0f;
}

// Clamp a positive delta to the patch cap. The cap is measured in patches and a
// stat holds (value - start) patches, so the raw ceiling is start + cap - which
// keeps the patch count uniform across all nine despite their differing starts.
// Negative deltas (stat-down patches) pass through unchanged.
static int PatchCap_ClampDelta(int kind, float current, int delta)
{
    if (delta <= 0) return delta;
    int cap = PatchCap_GetCap();
    float room = (PatchCap_GetStatStart(kind) + (float)cap) - current;
    if (room <= 0.0f) return 0;
    if ((float)delta > room) return (int)room;
    return delta;
}

// Replacement for Machine_GivePatch: pre-clamp to the cap, then run the original
// function's logic.
void PatchCap_GivePatch(MachineData *md, PatchKind kind, int num)
{
    num = PatchCap_ClampDelta(kind, md->stats.values[kind], num);
    Machine_ApplyStatClamped(md->stats.values, kind, num);
    Machine_UpdateAppearance(md);
    if (!md->suppress_attr_recalc)
        Machine_AdjustAttributes(md);
}

// Replacement for Machine_GiveAllUp: pre-clamp each stat individually, then run
// the original function's visual/attribute update and all-up tracking. The all-up
// counter is credited the uncapped num, matching vanilla.
void PatchCap_GiveAllUp(MachineData *md, int num)
{
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        int capped = PatchCap_ClampDelta(i, md->stats.values[i], num);
        Machine_ApplyStatClamped(md->stats.values, i, capped);
    }

    int ply;
    if (md->rider_gobj == 0)
        ply = 5;
    else
        ply = RiderGObj_GetPly(md->rider_gobj);

    if (ply != 5)
    {
        int collected = Ply_GetAllUpCollected(ply);
        Ply_SetAllUpCollected(ply, num + collected);
    }

    Machine_UpdateAppearance(md);
    if (!md->suppress_attr_recalc)
        Machine_AdjustAttributes(md);
}

// Replacement for Patch_GetMaxValue. Returns the per-slot ceiling so HUD
// normalization scales to the full range; actual stat growth is still clamped to
// the current cap by PatchCap_ClampDelta, so this doesn't uncap stats.
int PatchCap_GetMaxValue()
{
    return PatchCap_GetMax();
}

void PatchCap_Increment()
{
    int before = PatchCap_GetCap();
    ap_save->patch_cap_count++;
    int cap = PatchCap_GetCap();
    int max = PatchCap_GetMax();

    if (cap > before)
        OSReport("[PatchCap] Cap %d -> %d (max %d)\n", before, cap, max);
    else
        OSReport("[PatchCap] Cap already at the %d max, item had no effect\n", max);
    tb_api->EnqueueColoredNounFmt(NULL, "Patch cap", tb_api->PatchColors[PATCHKIND_CHARGE],
                                  " increased! (%d/%d)", cap, max);
}

void PatchCap_OnBoot()
{
    CODEPATCH_REPLACEFUNC(Patch_GetMaxValue, PatchCap_GetMaxValue);
    CODEPATCH_REPLACEFUNC(Machine_GivePatch, PatchCap_GivePatch);
    CODEPATCH_REPLACEFUNC(Machine_GiveAllUp, PatchCap_GiveAllUp);
    OSReport("[PatchCap] Hooks installed\n");
}
