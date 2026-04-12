#include "game.h"
#include "inline.h"
#include "patch_item.h"
#include "main.h"
#include "textbox.h"
#include "item.h"
#include "machine.h"
#include "os.h"

// Give PatchKind to every human rider on a machine
int Patch_GiveItem(PatchKind kind, int num)
{
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
        {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg)
            {
                MachineData *md = mg->userdata;
                Machine_GivePatch(md, kind, num);
                OSReport("[PatchItem] Giving %d patches of kind %d to player %d...\n", num, kind, i);
            }
        }
    }
    return 1;
}

// Give num of AllUp to every human rider on a machine
int Patch_AllUp_GiveItem(int num)
{
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
        {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg)
            {
                MachineData *md = mg->userdata;
                Machine_GiveAllUp(md, num);
                OSReport("[PatchItem] Giving %d all ups to player %d...\n", num, i);
            }
        }
    }
    return 1;
}

// Record a permanent +1 patch in save data and apply to all human players.
// Called from APItems_HandleItem after scene + intro gate.
int PermanentPatch_GiveItem(PatchKind kind)
{
    if (ap_save->permanent_patches[kind] < PATCH_STAT_MAX)
        ap_save->permanent_patches[kind]++;

    OSReport("[PatchItem] Permanent patch %d received (total: %d).\n", kind, ap_save->permanent_patches[kind]);
    return Patch_GiveItem(kind, 1);
}

// Record a permanent +1 all-up in save data and apply to all human players.
// Called from APItems_HandleItem after scene + intro gate.
int PermanentPatch_GiveAllUp()
{
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < PATCH_STAT_MAX)
            ap_save->permanent_patches[i]++;
    }

    OSReport("[PatchItem] Permanent all-up received.\n");
    return Patch_AllUp_GiveItem(1);
}

// Flag to track whether round-start patches have been applied this scene
static int permanent_patches_applied;

// Apply accumulated permanent patches to all human players.
// Consolidates into all-ups where possible to reduce the number of calls.
static void PermanentPatch_DoApply()
{
    // Find the minimum across all stats — this is how many all-ups we can apply
    u8 min_patches = ap_save->permanent_patches[0];
    for (int i = 1; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < min_patches)
            min_patches = ap_save->permanent_patches[i];
    }

    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];

    OSReport("[PatchItem] Applying permanent patches (all-up: %d, total: %d)...\n", min_patches, total);

    for (int p = 0; p < 5; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(p);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        // Apply consolidated all-ups
        if (min_patches > 0)
            Machine_GiveAllUp(md, min_patches);

        // Apply individual remainders
        for (int i = 0; i < PATCHKIND_NUM; i++)
        {
            int remainder = ap_save->permanent_patches[i] - min_patches;
            if (remainder > 0)
                Machine_GivePatch(md, i, remainder);
        }
    }
}

// Per-frame check: wait for intro to finish, then apply permanent patches once.
static void PermanentPatch_PerFrame(GOBJ *g)
{
    if (permanent_patches_applied)
        return;
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    permanent_patches_applied = 1;
    PermanentPatch_DoApply();
}

// Called from On3DLoadEnd to set up the per-frame GObj for round-start application.
// Caller is responsible for gating by mode + menu toggle — this just applies
// whatever is in ap_save->permanent_patches[] to all human MachineData, so it
// works for any 3D mode that has Rider/Machine objects (City Trial and Air Ride).
void PermanentPatch_On3DLoadEnd()
{
    // Check if there are any patches to apply
    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];
    if (total == 0)
        return;

    permanent_patches_applied = 0;
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, PermanentPatch_PerFrame, 0, 0, 0, 0);
}
