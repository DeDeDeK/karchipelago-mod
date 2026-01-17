#include "game.h"

#include "main.h"
#include "textbox.h"

int prev_obj_destroyed = 0;
float prev_stats[9];

// check if object destroyed count or stats have increased
// TODO: charging a machine gives energy, receiving energy charges machine
void EnergyLink_PerFrame(GOBJ *rg) {
    RiderData *rd = rg->userdata;

    // objects destroyed
    int diff = stc_playerdata[rd->ply].objectsDestroyed - prev_obj_destroyed;
    prev_obj_destroyed = stc_playerdata[rd->ply].objectsDestroyed;
    if (diff > 0) {
        OSReport("generated %d energy from destroying objects\n", diff);
        archipelago_data->energy_give += diff;
        OSReport("energy_give is now %f\n", archipelago_data->energy_give);
    }

    // patches collected
    int sum = 0;
    for (int i = 0; i < 9; i++) {
        float diff = rd->stats.values[i] - prev_stats[i];
        if (diff > 0) {
            sum += diff;
        }
        prev_stats[i] = rd->stats.values[i];
    }
    if (sum > 0) {
        OSReport("generated %d energy from collecting patches\n", sum);
        archipelago_data->energy_give += sum;
        OSReport("energy is now %f\n", archipelago_data->energy_give);
    }

    // check for received energy, and add it to vehicle charge
    float energy_received = archipelago_data->energy_receive;
    if (energy_received > 0) {
        GOBJ *mg = rd->machine_gobj;
        if (mg) {
            MachineData *md = mg->userdata;
            // this can overcharge the vehicle, which will unleash all extra charge over 1 as a large boost.
            // TODO: clamp to 10 max value (this is a huge boost)
            OSReport("adding energy received (%f) to machine charge...\n", energy_received);
            TextBox_Enqueue("adding energy received (%f) to machine charge (%f)\n", energy_received, md->charge_value);
            md->charge_value += energy_received;
            archipelago_data->energy_receive -= energy_received;
        }
    }
}

void EnergyLink_On3DLoadEnd() {
    // add the energylink check to all human players, in City Trial Only
    if (Gm_IsInCity()) {
        for (int i = 0; i < 4; i++) {
            if (Ply_GetPKind(i) == PKIND_HMN) {
                GOBJ *r = Ply_GetRiderGObj(i);
                if (r) {
                    // RDPRI is set to after hit collision is applied
                    GObj_AddProc(r, EnergyLink_PerFrame, RDPRI_HITCOLL + 1);
                }
            }
        }
    }
}