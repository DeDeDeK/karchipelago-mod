#include "game.h"
#include "machine.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "patch_cap.h"
#include "os.h"
#include "textbox.h"
#include "textbox_colors.h"

// Returns the current patch cap value.
// When progressive patch caps are off, returns PATCH_STAT_MAX (vanilla behavior).
// When progressive patch caps are on, returns starting cap + items received.
static int PatchCap_GetCap()
{
    if (!ap_save->options.city_trial_progressive_patch_caps)
        return PATCH_STAT_MAX;
    int cap = (int)ap_save->options.city_trial_patch_cap_amount + (int)ap_save->patch_cap_count;
    if (cap > PATCH_STAT_MAX)
        cap = PATCH_STAT_MAX;
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
// Returns the fixed stat max so the game's attribute normalization
// and HUD display scale to the full range.
int PatchCap_GetMaxValue()
{
    return PATCH_STAT_MAX;
}

// Increment the patch cap by 1.
void PatchCap_Increment()
{
    ap_save->patch_cap_count++;
    int cap = PatchCap_GetCap();
    OSReport("[PatchCap] Patch cap increased to %d.\n", cap);
    TextBox_EnqueueColoredNounFmt(NULL, "Patch cap", TextBox_PatchColor,
                                  " increased! (%d/%d)", cap, PATCH_STAT_MAX);
}

// Apply Machine_GivePatch and Machine_GiveAllUp hooks. Call from OnBoot.
void PatchCap_OnBoot()
{
    OSReport("[PatchCap] Applying patch cap hooks...\n");
    CODEPATCH_REPLACEFUNC(Patch_GetMaxValue, PatchCap_GetMaxValue);
    CODEPATCH_REPLACEFUNC(Machine_GivePatch, PatchCap_GivePatch);
    CODEPATCH_REPLACEFUNC(Machine_GiveAllUp, PatchCap_GiveAllUp);
}
