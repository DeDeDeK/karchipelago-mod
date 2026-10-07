#include <string.h>

#include "os.h"
#include "obj.h"
#include "game.h"
#include "rider.h"
#include "machine.h"

#include "custom_machines.h"

// MachineKind owed per player, -1 for none. Set exactly while MountProc is on the rider.
static s16 stc_pending[PLY_NUM] = { [0 ... PLY_NUM - 1] = -1 };
// MachineKind of each player's last mount this scene, -1 for none.
static s16 stc_respawn[PLY_NUM] = { [0 ... PLY_NUM - 1] = -1 };

// One-shot, on the rider's GObj: the recreate destroys the machine, so it cannot run on a
// caller inside Machine_OnTouchItem or a collision callback. The engine's own runs from
// the rider's procs too (RiderState_LegendaryAssemblyAnimThink, 0x801bdb2c). The mount
// becomes the player's respawn kind, so a later respawn keeps the machine.
static void MountProc(GOBJ *rg)
{
    GObj_FreeProc(*stc_gobjproc_cur);

    RiderData *rd = rg->userdata;
    int ply = rd->ply;
    int kind = stc_pending[ply];
    stc_pending[ply] = -1;

    int is_bike;
    int class_slot = CustomMachines_ClassIndexFromKind(kind, &is_bike);

    stc_respawn[ply] = (s16)kind;
    Rider_RespawnFullRecreate(rd, is_bike, (u8)class_slot, 0, 0, 1, 0, 0);
    OSReport("[MachineMount] Player %d mounted kind %d (class %d slot %d)\n",
             ply + 1, kind, is_bike, class_slot);
}

int CustomMachineMount_Queue(int machine_kind, int ply)
{
    if (ply < 0 || ply >= PLY_NUM || machine_kind < 0 ||
        machine_kind >= CustomMachines_GetKindCeiling())
        return 0;
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (rg == NULL)
        return 0;

    if (stc_pending[ply] < 0)
        GObj_AddProc(rg, MountProc, RDPRI_ANIM);
    stc_pending[ply] = (s16)machine_kind;
    return 1;
}

// A mount owed across a scene change goes with the rider GObj its proc sat on.
void CustomMachineMount_OnSceneChange(void)
{
    memset(stc_pending, -1, sizeof(stc_pending));
    memset(stc_respawn, -1, sizeof(stc_respawn));
}

int CustomMachineMount_GetRespawnKind(int ply)
{
    if (ply < 0 || ply >= PLY_NUM)
        return -1;
    return stc_respawn[ply];
}

void CustomMachineMount_SetRespawnKind(int ply, int machine_kind)
{
    if (ply >= 0 && ply < PLY_NUM)
        stc_respawn[ply] = (s16)machine_kind;
}
