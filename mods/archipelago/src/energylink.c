#include "game.h"

#include "main.h"
#include "textbox.h"

int prev_obj_destroyed = 0;
float prev_stats[9];
float energy_give_accumulator = 0;

// check if object destroyed count or stats have increased
// TODO: charging a machine gives energy, receiving energy charges machine
void EnergyLink_PerFrame(GOBJ *rg) {
    RiderData *rd = rg->userdata;

    // objects destroyed
    int diff = stc_playerdata[rd->ply].objects_destroyed_num - prev_obj_destroyed;
    prev_obj_destroyed = stc_playerdata[rd->ply].objects_destroyed_num;
    if (diff > 0) {
        OSReport("generated %d energy from destroying objects\n", diff);
        energy_give_accumulator += diff;
    }

    // patches collected
    int sum = 0;
    for (int i = 0; i < 9; i++) {
        float stat_diff = rd->stats.values[i] - prev_stats[i];
        if (stat_diff > 0) {
            sum += stat_diff;
        }
        prev_stats[i] = rd->stats.values[i];
    }
    if (sum > 0) {
        OSReport("generated %d energy from collecting patches\n", sum);
        energy_give_accumulator += sum;
    }

    // Flush accumulated energy to shared field when client has cleared it
    if (energy_give_accumulator > 0 && archipelago_data->energy_give == 0) {
        archipelago_data->energy_give = energy_give_accumulator;
        OSReport("flushed %f energy to energy_give\n", energy_give_accumulator);
        energy_give_accumulator = 0;
    }

    // Check for received energy and apply it to vehicle charge
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
        }
        // Clear so client can write the next value
        archipelago_data->energy_receive = 0;
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
