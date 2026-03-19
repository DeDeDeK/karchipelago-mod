#include "game.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "deathlink.h"
#include "text.h"
#include "textbox.h"

// Hook on Rider_CheckToDieOnMachine — sets deathlink_send when any human player dies.
// TODO: this also triggers for deaths caused by deathlink_receive itself. The Python
// client's built-in cooldown prevents the resulting echo from causing a loop.
void Rider_OnDeath(RiderData *rd)
{
    if (hoshi_menu_settings.deathlink_enabled)
    {
        if (!Ply_CheckIfCPU(rd->ply))
        {
            OSReport("Death detected for human player [%d]. Sending deathlink...\n", rd->ply);
            archipelago_data->deathlink_send = 1;
        }
    }
}
CODEPATCH_HOOKCREATE(0x801a06d0, "mr 3, 31\n\t", Rider_OnDeath, "", 0)

// Check for deathlink receive and kill all human players
void DeathLink_PerFrame(GOBJ *g)
{
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    if (archipelago_data->deathlink_receive != 1)
        return;

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;

        GOBJ *mg = rd->machine_gobj;
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        DmgLog dl = md->dmg_log;
        dl.attacker_ply = 0;
        OSReport("Deathlink received! Killing player %d\n", rd->ply);
        Ply_AddDeath(rd->ply, &dl, md->is_bike, md->kind);
        Ply_SetHP(rd->ply, 0);
    }

    TextBox_Enqueue("Deathlink received!");
    archipelago_data->deathlink_receive = 0;
}

void DeathLink_On3DLoadEnd()
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_PerFrame, 0, 0, 0, 0);
}

// Apply patches needed for deathlink
void DeathLink_OnBoot()
{
    OSReport("Applying deathlink patches...\n");
    // Insert into Rider_CheckToDieOnMachine
    CODEPATCH_HOOKAPPLY(0x801a06d0);
}
