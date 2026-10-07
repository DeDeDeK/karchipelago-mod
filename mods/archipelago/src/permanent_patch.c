#include "game.h"
#include "inline.h"
#include "machine.h"
#include "os.h"

#include "main.h"
#include "permanent_patch.h"
#include "patch_item.h"
#include "settings_menu.h"
#include "textbox_api.h"
#include "ap_announce.h"

// The stats land at the next round start: applying here too would double up against
// the stats a Trial carries into its stadium and against the round-start apply.
int PermanentPatch_GiveItem(PatchKind kind)
{
    if (ap_save->permanent_patches[kind] < PATCH_STAT_MAX)
        ap_save->permanent_patches[kind]++;

    OSReport("[PermanentPatch] %s patch received (total %d)\n",
             PatchKind_Names[kind], ap_save->permanent_patches[kind]);
    APAnnounce_Grant("Received: permanent +1 ", PatchKind_Names[kind], tb_api->PatchColors[kind], NULL);
    return 1;
}

int PermanentPatch_GiveAllUp(void)
{
    for (int i = 0; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < PATCH_STAT_MAX)
            ap_save->permanent_patches[i]++;
    }

    OSReport("[PermanentPatch] All-up received\n");
    APAnnounce_Grant("Received: permanent +1 ", "All Up", tb_api->PatchColors[PATCHKIND_CHARGE], NULL);
    return 1;
}

static int permanent_patches_applied;

// Consolidates into all-ups where it can, to cut the number of calls.
static void PermanentPatch_DoApply(void)
{
    u8 min_patches = ap_save->permanent_patches[0];
    for (int i = 1; i < PATCHKIND_NUM; i++)
    {
        if (ap_save->permanent_patches[i] < min_patches)
            min_patches = ap_save->permanent_patches[i];
    }

    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];

    OSReport("[PermanentPatch] Applied (all-up: %d, total: %d): "
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

    // Only the City Trial map counts as one game for the "10+ X Patches" cells.
    int credit = Gm_IsInCity();

    for (int p = 0; p < PLY_NUM; p++)
    {
        if (Ply_GetPKind(p) != PKIND_HMN)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(p);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        float before[PATCHKIND_NUM];
        memcpy(before, md->stats.values, sizeof(before));

        if (min_patches > 0)
            Machine_GiveAllUp(md, min_patches);

        for (int i = 0; i < PATCHKIND_NUM; i++)
        {
            int remainder = ap_save->permanent_patches[i] - min_patches;
            if (remainder > 0)
                Machine_GivePatch(md, i, remainder);
        }

        // Machine_GivePatch skips the pickup counter, so credit what landed under the cap.
        // Ply_IncrementItemCollectNum would also feed the first-20-seconds aggregate.
        if (credit)
        {
            PlayerStats *st = Ply_GetStats(p);
            for (int i = 0; i < PATCHKIND_NUM; i++)
            {
                int got = (int)(md->stats.values[i] - before[i] + 0.5f);
                if (got > 0)
                    st->item_collect[PatchItem_PatchKindToItKind(i)] += got;
            }
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

// Gm_IsInCity excludes stadiums, so this dispatches on the CT major and city_mode. Free
// Run has no item data tables for a patch ejection, and a Trial's closing stadium already
// carries the patched stats.
static int PermanentPatch_ShouldApply(void)
{
    if (Scene_GetCurrentMajor() == MJRKIND_CITY)
    {
        CityMode cm = Gm_GetCityMode();
        if (cm == CITYMODE_FREERUN)
            return 0;
        if (cm == CITYMODE_STADIUM)
            return ap_menu_settings.ct_stadium_permanent_patches_enabled;
        if (CityTrial_IsInStadium())
            return 0;
        return ap_menu_settings.ct_permanent_patches_enabled;
    }
    return ap_menu_settings.ar_permanent_patches_enabled;
}

void PermanentPatch_On3DLoadEnd(void)
{
    if (!PermanentPatch_ShouldApply())
        return;

    int total = 0;
    for (int i = 0; i < PATCHKIND_NUM; i++)
        total += ap_save->permanent_patches[i];
    if (total == 0)
        return;

    permanent_patches_applied = 0;
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, PermanentPatch_PerFrame, 0, 0, 0, 0);
}
