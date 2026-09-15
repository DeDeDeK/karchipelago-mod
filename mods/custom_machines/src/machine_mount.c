// The plain mount: a player put straight onto a machine through the same recreate the
// assembly cutscene ends in, with no presentation around it. It waits for the frame
// boundary because the recreate tears down the rider's current machine, which a caller
// inside a collision or item callback is still running on.

#include "os.h"
#include "obj.h"
#include "game.h"
#include "rider.h"
#include "machine.h"

#include "custom_machines.h"

// MachineKind owed per player, -1 for none.
static s16 stc_pending[PLY_NUM] = { -1, -1, -1, -1, -1 };

int CustomMachineMount_Queue(int machine_kind, int ply)
{
    if (ply < 0 || ply >= PLY_NUM || machine_kind < 0 ||
        machine_kind >= CustomMachines_GetKindCeiling() || Ply_GetRiderGObj(ply) == NULL)
        return 0;

    stc_pending[ply] = (s16)machine_kind;
    return 1;
}

// A mount owed across a scene load has no rider left to land on.
void CustomMachineMount_On3DLoadStart(void)
{
    for (int i = 0; i < PLY_NUM; i++)
        stc_pending[i] = -1;
}

// starting_machine_idx follows the mount, so a later respawn keeps the machine.
void CustomMachineMount_OnFrameStart(void)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        int kind = stc_pending[ply];
        if (kind < 0)
            continue;
        stc_pending[ply] = -1;

        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (rg == NULL)
            continue;

        int is_bike;
        int class_slot = CustomMachines_ClassIndexFromKind(kind, &is_bike);
        RiderData *rd = rg->userdata;

        rd->starting_machine_idx = (MachineKind)kind;
        Rider_RespawnFullRecreate(rd, is_bike, (u8)class_slot, 0, 0, 1, 0, 0);
        OSReport("[MachineMount] Player %d mounted kind %d (class %d slot %d)\n",
                 ply + 1, kind, is_bike, class_slot);
    }
}
