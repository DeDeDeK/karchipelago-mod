#include "game.h"
#include "inline.h"
#include "item.h"
#include "machine.h"
#include "os.h"

#include "main.h"
#include "patch_item.h"
#include "energylink.h"

static const ItemKind patch_itkinds[PATCHKIND_NUM] = {
    [PATCHKIND_WEIGHT]   = ITKIND_WEIGHT,
    [PATCHKIND_ACCEL]    = ITKIND_ACCEL,
    [PATCHKIND_TOPSPEED] = ITKIND_TOPSPEED,
    [PATCHKIND_TURN]     = ITKIND_TURN,
    [PATCHKIND_CHARGE]   = ITKIND_CHARGE,
    [PATCHKIND_GLIDE]    = ITKIND_GLIDE,
    [PATCHKIND_OFFENSE]  = ITKIND_OFFENSE,
    [PATCHKIND_DEFENSE]  = ITKIND_DEFENSE,
    [PATCHKIND_HP]       = ITKIND_HP,
};

ItemKind PatchItem_PatchKindToItKind(PatchKind kind)
{
    return patch_itkinds[kind];
}

PatchKind PatchItem_ItKindToPatchKind(ItemKind it_kind)
{
    for (int k = 0; k < PATCHKIND_NUM; k++)
        if (patch_itkinds[k] == it_kind)
            return (PatchKind)k;
    return PATCHKIND_NUM;
}

// City Trial spawns the pickup, for the normal +1 visual. Air Ride has no item data tables
// (SpawnItem would crash), so it calls Machine_GivePatch.
int PatchItem_Give(PatchKind kind)
{
    int use_item_spawn = Gm_IsInCity();
    int applied = 0;

    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;

        if (use_item_spawn)
        {
            SpawnItemPlayer(i, patch_itkinds[kind]);
        }
        else
        {
            Machine_GivePatch(mg->userdata, kind, 1);
            EnergyLink_RebaseStats(i);
        }
        applied++;
    }

    if (applied)
        OSReport("[PatchItem] Gave a %s patch to %d player(s) (%s)\n",
                 PatchKind_Names[kind], applied, use_item_spawn ? "item" : "direct");
    return applied;
}

// The same City Trial / Air Ride split as PatchItem_Give.
int PatchItem_GiveAllUp(int num)
{
    int use_item_spawn = (num > 0) && Gm_IsInCity();
    int applied = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;

        if (use_item_spawn)
        {
            for (int n = 0; n < num; n++)
                SpawnItemPlayer(i, ITKIND_ALLUP);
        }
        else
        {
            Machine_GiveAllUp(mg->userdata, num);
            EnergyLink_RebaseStats(i);
        }
        applied++;
    }

    if (applied)
        OSReport("[PatchItem] Gave %d all-up(s) to %d player(s) (%s)\n",
                 num, applied, use_item_spawn ? "item" : "direct");
    return applied;
}

int PatchItem_DropTrap(void)
{
    int dropped = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        Rider_DropPatches(rd, rd->stats.values, HSD_Randi(3));
        dropped++;
    }

    if (dropped)
        OSReport("[PatchItem] Drop-patches trap applied to %d player(s)\n", dropped);
    return dropped;
}
