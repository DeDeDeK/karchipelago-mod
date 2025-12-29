#include "game.h"

#include "deathlink.h"
#include "text.h"
#include "textbox.h"
#include "main.h"

// TODO: test this with invincible candy, in the hydra/dragoon cutscene, and when traveling on a boost pad thingy (volcano and white square near the city/volcano intersection)
void DeathLink_PerFrame(GOBJ *r) {
    RiderData *rd = r->userdata;
    GOBJ *mg = rd->machine_gobj;
    // check if player is on a machine first
    if (mg) {
        MachineData *md = mg->userdata;
        // check for death, and send deathlink if player is dead
        if (md->hp <= 0) {
            OSReport("Death detected, sending deathlink\n");
            TextBox_AddMessage("Death detected, sending deathlink");
            archipelago_data->deathlink_send = 1;
        }

        // check for deathlink receive, and give death if it is 1
        if (archipelago_data->deathlink_receive == 1) {
            // set this as a self-destruct
            DmgLog dl = md->dmg_log;
            dl.attacker_ply = 0;
            OSReport("Deathlink received!\n");
            TextBox_AddMessage("Deathlink received!");
            Ply_AddDeath(rd->ply, &dl, md->is_bike, md->kind);
            Ply_SetHP(rd->ply, 0);
            archipelago_data->deathlink_receive = 0;
        }    
    }
}

void DeathLink_On3DLoadEnd() {
    if (Gm_IsInCity()) {
        // add the deathlink check to all human players
        for (int i = 0; i < 4; i++) {
            if (Ply_GetPKind(i) == PKIND_HMN) {
                GOBJ *r = Ply_GetRiderGObj(i);
                // RDPRI is set to after damage is applied
                GObj_AddProc(r, DeathLink_PerFrame, RDPRI_DMGAPPLY + 1);
            }
        }
    }    
}