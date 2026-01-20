#include "game.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "deathlink.h"
#include "text.h"
#include "textbox.h"

// function to detect death and set deathlink_send if deathlink is enabled
// currently, only one global deathlink_send for all players
// TODO: this triggers for deaths from deathlink_receive as well. Might not be an
// issue as the python client has a built-in cooldown for deathlink triggers
void Rider_OnDeath(RiderData *rd)
{
    if (hoshi_menu_settings.deathlink_enabled) {
        if (!Ply_CheckIfCPU(rd->ply)) {
            OSReport("Death detected for human player [%d]. Sending deathlink...\n", rd->ply);
            TextBox_Enqueue("Death detected for human player [%d]. Sending deathlink...\n", rd->ply);
            archipelago_data->deathlink_send = 1;
        }
    }
}
CODEPATCH_HOOKCREATE(0x801a06d0, "mr 3, 31\n\t", Rider_OnDeath, "", 0)

// check for deathlink receive, and give death if it is 1
// TODO: test this with invincible candy, in the hydra/dragoon cutscene, 
// and when traveling on a boost pad thingy (volcano and white square near the city/volcano intersection)
void DeathLink_PerFrame(GOBJ *r) {
    RiderData *rd = r->userdata;
    GOBJ *mg = rd->machine_gobj;
    // check if player is on a machine first
    if (mg) {
        MachineData *md = mg->userdata;
        if (archipelago_data->deathlink_receive == 1) {
            // set this as a self-destruct
            DmgLog dl = md->dmg_log;
            dl.attacker_ply = 0;
            OSReport("Deathlink received! Adding death for player %d\n", rd->ply);
            TextBox_Enqueue("Deathlink received! Adding death for player %d\n", rd->ply);
            Ply_AddDeath(rd->ply, &dl, md->is_bike, md->kind);
            Ply_SetHP(rd->ply, 0);
            archipelago_data->deathlink_receive = 0;
        }
    }
}

void DeathLink_On3DLoadEnd() {
    // add the deathlink check to all human players
    for (int i = 0; i < 4; i++) {
        if (Ply_GetPKind(i) == PKIND_HMN) {
            GOBJ *r = Ply_GetRiderGObj(i);
            if (r) {
                // RDPRI is set to after damage is applied
                GObj_AddProc(r, DeathLink_PerFrame, RDPRI_DMGAPPLY + 1);
            }
        }
    }    
}

// apply patches needed for deathlink
void DeathLink_OnBoot() {
    OSReport("Applying deathlink patches...\n");
    // insert into Rider_CheckToDieOnMachine
    CODEPATCH_HOOKAPPLY(0x801a06d0);
}