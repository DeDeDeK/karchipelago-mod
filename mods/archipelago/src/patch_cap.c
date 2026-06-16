#include "game.h"
#include "machine.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "patch_cap.h"
#include "os.h"
#include "textbox_api.h"

// Returns the per-slot patch cap ceiling chosen via the YAML option. This is
// both the ceiling that PatchCap_GetCap clamps to and the threshold the Max
// Stats goal compares stats against. Clamped to PATCH_STAT_MAX so a malformed
// option value can never exceed the PowerPC hardware ceiling. A stored 0 means
// options have not been received yet (memset save default), so it maps to the
// hardware ceiling - an uncapped vanilla baseline until the real value arrives.
static int PatchCap_GetMax()
{
    int t = (int)ap_save->options.city_trial_patch_cap_max;
    if (t <= 0) t = PATCH_STAT_MAX;
    if (t > PATCH_STAT_MAX) t = PATCH_STAT_MAX;
    return t;
}

// Returns the current effective patch cap: the player starts at
// city_trial_patch_cap_min and each Patch Cap Increase item adds one, clamped
// to the max. min == max yields a flat cap (no Patch Cap Increase items exist).
// A stored min of 0 means options have not been received yet, so we fall back
// to the (also-uncapped) max - vanilla behavior until the real options arrive.
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

// City Trial stats spawn at -2, except HP which spawns at 0. The cap counts
// patches collected, which is (value - start), so keeping this baseline in one
// place lets the clamp ceiling and the Max Stats goal use the same per-stat
// reference.
float PatchCap_GetStatStart(int kind)
{
    return (kind == PATCHKIND_HP) ? 0.0f : -2.0f;
}

// Clamp a positive delta so the stat does not exceed the patch cap. The cap is
// measured in patches, and a stat holds (value - start) patches, so the raw
// ceiling is start + cap. This keeps the number of patches each stat can hold
// uniform across all nine despite their differing start values.
// Negative deltas (stat-down patches) are passed through unchanged.
static int PatchCap_ClampDelta(int kind, float current, int delta)
{
    if (delta <= 0) return delta;
    int cap = PatchCap_GetCap();
    float room = (PatchCap_GetStatStart(kind) + (float)cap) - current;
    if (room <= 0.0f) return 0;
    if ((float)delta > room) return (int)room;
    return delta;
}

// Replacement for Machine_GivePatch.
// Caps the stat increase so the stat value does not exceed our patch cap,
// then performs the original function's logic.
void PatchCap_GivePatch(MachineData *md, PatchKind kind, int num)
{
    num = PatchCap_ClampDelta(kind, md->stats.values[kind], num);
    Machine_ApplyStatClamped(md->stats.values, kind, num);
    Machine_UpdateAppearance(md);
    if (!md->suppress_attr_recalc)
        Machine_AdjustAttributes(md);
}

// Replacement for Machine_GiveAllUp.
// Caps each stat individually so none exceed our patch cap,
// then performs the original function's visual/attribute update and
// player all-up tracking.
void PatchCap_GiveAllUp(MachineData *md, int num)
{
    // Apply individually capped deltas per stat
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        int capped = PatchCap_ClampDelta(i, md->stats.values[i], num);
        Machine_ApplyStatClamped(md->stats.values, i, capped);
    }

    // Track all-up collected count for the player
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

// Replacement for Patch_GetMaxValue.
// Returns the per-slot ceiling so HUD attribute normalization scales to the
// full reachable range. Internal stat-up clamping is handled separately by
// PatchCap_ClampDelta against PatchCap_GetCap (the current effective cap),
// so returning the ceiling here doesn't let stats grow past current cap.
int PatchCap_GetMaxValue()
{
    return PatchCap_GetMax();
}

// Increment the patch cap by 1.
void PatchCap_Increment()
{
    ap_save->patch_cap_count++;
    int cap = PatchCap_GetCap();
    int max = PatchCap_GetMax();
    OSReport("[PatchCap] Patch cap increased to %d (max %d).\n", cap, max);
    tb_api->EnqueueColoredNounFmt(NULL, "Patch cap", tb_api->PatchColors[PATCHKIND_CHARGE],
                                  " increased! (%d/%d)", cap, max);
}

// Apply Machine_GivePatch and Machine_GiveAllUp hooks. Call from OnBoot.
void PatchCap_OnBoot()
{
    OSReport("[PatchCap] Applying patch cap hooks...\n");
    CODEPATCH_REPLACEFUNC(Patch_GetMaxValue, PatchCap_GetMaxValue);
    CODEPATCH_REPLACEFUNC(Machine_GivePatch, PatchCap_GivePatch);
    CODEPATCH_REPLACEFUNC(Machine_GiveAllUp, PatchCap_GiveAllUp);
}
