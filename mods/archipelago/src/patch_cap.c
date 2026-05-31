#include "game.h"
#include "machine.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "patch_cap.h"
#include "os.h"
#include "textbox_api.h"

// Cap-progression starting value when progressive patch caps are on.
// Player starts able to hold 1 of each stat; Patch Cap Increase items grow
// the cap toward city_trial_patch_cap_amount (the per-slot target).
#define PATCH_CAP_PROGRESSIVE_START 1

// Returns the per-slot target patch cap chosen via the YAML option. This is
// both the ceiling that PatchCap_GetCap clamps to and the threshold the Max
// Stats goal compares stats against. Clamped to PATCH_STAT_MAX so a malformed
// option value can never exceed the PowerPC hardware ceiling.
static int PatchCap_GetTarget()
{
    int t = (int)ap_save->options.city_trial_patch_cap_amount;
    if (t <= 0) t = PATCH_STAT_MAX;
    if (t > PATCH_STAT_MAX) t = PATCH_STAT_MAX;
    return t;
}

// Returns the current patch cap value.
// When progressive patch caps are off, returns the target (player has full
// cap from the start). When progressive patch caps are on, returns
// PATCH_CAP_PROGRESSIVE_START + items received, clamped to the target.
static int PatchCap_GetCap()
{
    int target = PatchCap_GetTarget();
    if (!ap_save->options.city_trial_progressive_patch_caps)
        return target;
    int cap = PATCH_CAP_PROGRESSIVE_START + (int)ap_save->patch_cap_count;
    if (cap > target)
        cap = target;
    return cap;
}

// Clamp a positive delta so the stat does not exceed the patch cap.
// Negative deltas (stat-down patches) are passed through unchanged.
static int PatchCap_ClampDelta(float current, int delta)
{
    if (delta <= 0) return delta;
    int cap = PatchCap_GetCap();
    float room = (float)cap - current;
    if (room <= 0.0f) return 0;
    if ((float)delta > room) return (int)room;
    return delta;
}

// Replacement for Machine_GivePatch.
// Caps the stat increase so the stat value does not exceed our patch cap,
// then performs the original function's logic.
void PatchCap_GivePatch(MachineData *md, PatchKind kind, int num)
{
    num = PatchCap_ClampDelta(md->stats.values[kind], num);
    Machine_ApplyStatClamped(md->stats.values, kind, num);
    Machine_UpdateAppearance(md);
    if ((s8)md->xc38 >= 0)
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
        int capped = PatchCap_ClampDelta(md->stats.values[i], num);
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
    if ((s8)md->xc38 >= 0)
        Machine_AdjustAttributes(md);
}

// Replacement for Patch_GetMaxValue.
// Returns the per-slot target so HUD attribute normalization scales to the
// full reachable range. Internal stat-up clamping is handled separately by
// PatchCap_ClampDelta against PatchCap_GetCap (the current effective cap),
// so returning the target here doesn't let stats grow past current cap.
int PatchCap_GetMaxValue()
{
    return PatchCap_GetTarget();
}

// Increment the patch cap by 1.
void PatchCap_Increment()
{
    ap_save->patch_cap_count++;
    int cap = PatchCap_GetCap();
    int target = PatchCap_GetTarget();
    OSReport("[PatchCap] Patch cap increased to %d (target %d).\n", cap, target);
    tb_api->EnqueueColoredNounFmt(NULL, "Patch cap", tb_api->PatchColors[PATCHKIND_CHARGE],
                                  " increased! (%d/%d)", cap, target);
}

// Apply Machine_GivePatch and Machine_GiveAllUp hooks. Call from OnBoot.
void PatchCap_OnBoot()
{
    OSReport("[PatchCap] Applying patch cap hooks...\n");
    CODEPATCH_REPLACEFUNC(Patch_GetMaxValue, PatchCap_GetMaxValue);
    CODEPATCH_REPLACEFUNC(Machine_GivePatch, PatchCap_GivePatch);
    CODEPATCH_REPLACEFUNC(Machine_GiveAllUp, PatchCap_GiveAllUp);
}
