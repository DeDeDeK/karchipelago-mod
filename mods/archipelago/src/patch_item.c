#include "game.h"
#include "inline.h"
#include "patch_item.h"
#include "main.h"
#include "settings_menu.h"
#include "textbox_api.h"
#include "item.h"
#include "machine.h"
#include "os.h"
#include "energylink.h"

// PatchKind to the matching "+1" ITKIND.
static const ItemKind stc_patch_itkinds[PATCHKIND_NUM] = {
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

// Returns PATCHKIND_NUM for any ItemKind that is not one of the nine stat "+1"
// patches (down/max/fake variants, All Up, food, boxes, copy abilities, etc.).
PatchKind Patch_ItKindToPatchKind(ItemKind it_kind)
{
    for (int k = 0; k < PATCHKIND_NUM; k++)
        if (stc_patch_itkinds[k] == it_kind)
            return (PatchKind)k;
    return PATCHKIND_NUM;
}

// Give PatchKind to every human rider on a machine. In City Trial, positive
// deltas go through the item pickup pipeline so the player sees the normal
// "+1 stat" visual. Air Ride has no item data tables loaded (SpawnItem would
// crash), so AR - and any negative delta - falls back to Machine_GivePatch.
int Patch_GiveItem(PatchKind kind, int num)
{
    int use_item_spawn = (num > 0) && Gm_IsInCity() && (kind < PATCHKIND_NUM);
    const char *kind_name = (kind < PATCHKIND_NUM) ? PatchKind_Names[kind] : "?";
    int applied = 0;

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;

        if (use_item_spawn)
        {
            for (int n = 0; n < num; n++)
                SpawnItemPlayer(i, stc_patch_itkinds[kind]);
        }
        else
        {
            MachineData *md = mg->userdata;
            Machine_GivePatch(md, kind, num);
            // Rebase so the stat change doesn't refund energy into the pool.
            EnergyLink_RebaseStats(i);
        }
        applied++;
    }

    OSReport("[PatchItem] Gave %d %s patch(es) to %d player(s) (%s)\n",
             num, kind_name, applied, use_item_spawn ? "item" : "direct");
    return 1;
}

// Same City Trial / Air Ride split as Patch_GiveItem. Returns 1 if at least one
// player got the apply: Top Ride has no MachineData so every iteration skips, and
// AP_ITEM_ALL_DOWN must defer rather than be consumed there.
int Patch_AllUp_GiveItem(int num)
{
    int use_item_spawn = (num > 0) && Gm_IsInCity();
    int applied = 0;
    for (int i = 0; i < 5; i++)
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
            MachineData *md = mg->userdata;
            Machine_GiveAllUp(md, num);
            EnergyLink_RebaseStats(i);
        }
        applied++;
    }

    OSReport("[PatchItem] Gave %d all-up(s) to %d player(s) (%s)\n",
             num, applied, use_item_spawn ? "item" : "direct");
    return applied;
}

// Eject each human rider's current stats as physical patches behind the machine.
// Caller must guarantee item data is loaded - the open City Trial phase only;
// elsewhere Rider_DropPatches crashes trying to spawn the patch items.
int Patch_DropTrap()
{
    int dropped = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        int drop_mode = HSD_Randi(3);
        Rider_DropPatches(rd, rd->stats.values, drop_mode);
        OSReport("[PatchItem] Drop-patches trap applied to player %d (mode %d)\n", i, drop_mode);
        dropped = 1;
    }
    return dropped;
}

// Record a permanent +1 patch in save data. Stat application is deferred to the
// next round start - applying here too would double up against the carry-over of
// stats into stadium loads and against the round-start re-apply.
int PermanentPatch_GiveItem(PatchKind kind)
{
    if (ap_save->permanent_patches[kind] < PATCH_STAT_MAX)
        ap_save->permanent_patches[kind]++;

    OSReport("[PatchItem] Permanent %s patch received (total %d)\n",
             PatchKind_Names[kind], ap_save->permanent_patches[kind]);
    if (kind < PATCHKIND_NUM)
        tb_api->EnqueueColoredNoun("Received: permanent +1 ", PatchKind_Names[kind], tb_api->PatchColors[kind], NULL);
    return 1;
}

// Stat application is deferred to the next round start, as with the single-stat
// permanent patch.
int PermanentPatch_GiveAllUp()
{
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < PATCH_STAT_MAX)
            ap_save->permanent_patches[i]++;
    }

    OSReport("[PatchItem] Permanent all-up received\n");
    tb_api->EnqueueColoredNoun("Received: permanent +1 ", "All Up", tb_api->PatchColors[PATCHKIND_CHARGE], NULL);
    return 1;
}

static int permanent_patches_applied;

// Apply accumulated permanent patches to all human players, consolidating into
// all-ups where possible to reduce the number of calls.
static void PermanentPatch_DoApply()
{
    // The minimum across all stats is how many all-ups we can apply.
    u8 min_patches = ap_save->permanent_patches[0];
    for (int i = 1; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < min_patches)
            min_patches = ap_save->permanent_patches[i];
    }

    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];

    OSReport("[PatchItem] Applying permanent patches (all-up: %d, total: %d): "
             "Weight=%d Boost=%d TopSpd=%d Turn=%d Charge=%d Glide=%d Offense=%d Defense=%d HP=%d\n",
             min_patches, total,
             ap_save->permanent_patches[PATCHKIND_WEIGHT],
             ap_save->permanent_patches[PATCHKIND_ACCEL],
             ap_save->permanent_patches[PATCHKIND_TOPSPEED],
             ap_save->permanent_patches[PATCHKIND_TURN],
             ap_save->permanent_patches[PATCHKIND_CHARGE],
             ap_save->permanent_patches[PATCHKIND_GLIDE],
             ap_save->permanent_patches[PATCHKIND_OFFENSE],
             ap_save->permanent_patches[PATCHKIND_DEFENSE],
             ap_save->permanent_patches[PATCHKIND_HP]);

    for (int p = 0; p < 5; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(p);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        if (min_patches > 0)
            Machine_GiveAllUp(md, min_patches);

        for (int i = 0; i < PATCHKIND_NUM; i++)
        {
            int remainder = ap_save->permanent_patches[i] - min_patches;
            if (remainder > 0)
                Machine_GivePatch(md, i, remainder);
        }
    }
}

static void PermanentPatch_PerFrame(GOBJ *g)
{
    if (permanent_patches_applied)
        return;
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    permanent_patches_applied = 1;
    PermanentPatch_DoApply();
}

// Gm_IsInCity() is stage-based (only true on the CT main map, stage_kind 9/52)
// and excludes stadiums, so dispatch off the CT major + city_mode instead. Free
// Run never loads item data tables, so inflated stats from perm patches would
// crash Item_GetItDataPtr on damage-driven patch ejection.
static int PermanentPatch_ShouldApply(void)
{
    if (Scene_GetCurrentMajor() == MJRKIND_CITY)
    {
        CityMode cm = Gm_GetCityMode();
        if (cm == CITYMODE_FREERUN)
            return 0; // item data is not loaded; inflated stats crash patch ejection
        if (cm == CITYMODE_STADIUM)
            return ap_menu_settings.ct_stadium_permanent_patches_enabled;
        return ap_menu_settings.ct_permanent_patches_enabled;
    }
    return ap_menu_settings.ar_permanent_patches_enabled;
}

// Set up the per-frame GObj that applies ap_save->permanent_patches[] to all
// human MachineData at round start, gated by mode + menu toggle.
void PermanentPatch_On3DLoadEnd()
{
    if (!PermanentPatch_ShouldApply())
        return;

    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];
    if (total == 0)
        return;

    if (Scene_GetCurrentMajor() == MJRKIND_CITY && Gm_GetCityMode() == CITYMODE_FREERUN)
    {
        OSReport("[PatchItem] Free Run - holding %d permanent patch(es) (item data not loaded)\n",
                 total);
        return;
    }

    permanent_patches_applied = 0;
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, PermanentPatch_PerFrame, 0, 0, 0, 0);
}
