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

// Map PatchKind to the matching "+1" ITKIND. Same ordering as PatchKind.
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

// Give PatchKind to every human rider on a machine.
// In City Trial, positive deltas go through the item pickup pipeline so the
// player sees the normal "+1 stat" visual. Air Ride has no item data tables
// loaded (SpawnItem would crash), so AR — and any negative delta — falls back
// to a direct Machine_GivePatch that only applies the stat change.
int Patch_GiveItem(PatchKind kind, int num)
{
    int use_item_spawn = (num > 0) && Gm_IsInCity() && (kind < PATCHKIND_NUM);
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
            // Stats just changed; mask from EnergyLink so the receive doesn't
            // refund energy back into the pool. The CT spawn-pickup branch
            // also applies same-frame (SpawnItemPlayer drives
            // Machine_OnTouchItem for non-fake kinds) but doesn't rebase yet
            // — see docs/energylink.md.
            EnergyLink_RebaseStats(i);
        }
        OSReport("[PatchItem] Giving %d patches of kind %d to player %d (%s)...\n",
                 num, kind, i, use_item_spawn ? "item" : "direct");
    }
    return 1;
}

// Give num of AllUp to every human rider on a machine.
// Same City Trial / Air Ride split as Patch_GiveItem.
// Returns 1 if at least one player got the apply, 0 otherwise — Top Ride has
// no MachineData so every iter skips, and AP_ITEM_ALL_DOWN (the only negative
// caller) is a one-shot trap that must defer instead of being consumed.
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
        OSReport("[PatchItem] Giving %d all ups to player %d (%s)...\n",
                 num, i, use_item_spawn ? "item" : "direct");
        applied = 1;
    }
    return applied;
}

// Drop-patches trap: eject each human rider's current stats as physical
// patches behind the machine. Caller must guarantee item data is loaded
// (CT Trial/Stadium only — Free Run and non-CT modes would crash inside
// Rider_DropPatches when it tries to spawn the patch items).
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

// Record a permanent +1 patch in save data. The actual stat application
// happens at the next round start via PermanentPatch_On3DLoadEnd — applying
// here too would double up against the carry-over of stats into stadium loads
// (and against the round-start re-apply on subsequent rounds).
int PermanentPatch_GiveItem(PatchKind kind)
{
    if (ap_save->permanent_patches[kind] < PATCH_STAT_MAX)
        ap_save->permanent_patches[kind]++;

    OSReport("[PatchItem] Permanent patch %d received (total: %d).\n", kind, ap_save->permanent_patches[kind]);
    if (kind < PATCHKIND_NUM)
        tb_api->EnqueueColoredNoun("Received: permanent +1 ", PatchKind_Names[kind], tb_api->PatchColors[kind], NULL);
    return 1;
}

// Record a permanent +1 all-up in save data. As with PermanentPatch_GiveItem,
// stat application is deferred to the next round start.
int PermanentPatch_GiveAllUp()
{
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < PATCH_STAT_MAX)
            ap_save->permanent_patches[i]++;
    }

    OSReport("[PatchItem] Permanent all-up received.\n");
    tb_api->EnqueueColoredNoun("Received: permanent +1 ", "All Up", tb_api->PatchColors[PATCHKIND_CHARGE], NULL);
    return 1;
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

    OSReport("[PatchItem] Applying permanent patches (all-up: %d, total: %d): "
             "Weight=%d Accel=%d TopSpd=%d Turn=%d Charge=%d Glide=%d Offense=%d Defense=%d HP=%d\n",
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

// Round-start permanent patch re-application, per-mode menu toggle.
// Gm_IsInCity() is stage-based (only true on the CT main map, stage_kind 9/52)
// and excludes stadiums, so we dispatch off the CT major + city_mode instead.
// Free Run never loads item data tables, so inflated stats from perm patches
// would crash Item_GetItDataPtr on damage-driven patch ejection.
static int PermanentPatch_ShouldApply(void)
{
    if (Scene_GetCurrentMajor() == MJRKIND_CITY)
    {
        CityMode cm = Gm_GetCityMode();
        if (cm == CITYMODE_FREERUN)
        {
            OSReport("[PermanentPatch] Skipping in Free Run (item data not loaded).\n");
            return 0;
        }
        if (cm == CITYMODE_STADIUM)
            return ap_menu_settings.ct_stadium_permanent_patches_enabled;
        return ap_menu_settings.ct_permanent_patches_enabled;
    }
    return ap_menu_settings.ar_permanent_patches_enabled;
}

// Called from On3DLoadEnd to set up the per-frame GObj for round-start
// application. Applies whatever is in ap_save->permanent_patches[] to all
// human MachineData, gated by mode + menu toggle. Works for any 3D mode that
// has Rider/Machine objects (City Trial and Air Ride).
void PermanentPatch_On3DLoadEnd()
{
    if (!PermanentPatch_ShouldApply())
        return;

    // Check if there are any patches to apply
    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];
    if (total == 0)
        return;

    permanent_patches_applied = 0;
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, PermanentPatch_PerFrame, 0, 0, 0, 0);
}
