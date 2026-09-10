// The City Trial machine blip. Its GX callback indexes an 18-frame TexAnim by
// Machine_GetAbsoluteKind (0x801c85bc), which folds a custom machine's star slot
// onto the bike half of the range and off the end of the table. A custom machine
// borrows its clone_kind's blip instead.

#include "os.h"
#include "obj.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Replaces the bl at 0x80122414 in CityBlip_GXCallback (0x80122380), where r3 is
// the machine's GObj.
static MachineKind BlipKind(GOBJ *machine_gobj)
{
    MachineData *md = machine_gobj->userdata;

    if (!md->is_bike && md->kind >= VCSTAR_NUM)
    {
        CustomMachineEntry *e = CustomMachines_FindByStarSlot(md->kind);
        if (e != NULL && e->clone_kind >= 0 && e->clone_kind < VCSTAR_NUM)
            return (MachineKind)e->clone_kind;
        return VCKIND_SLICK;
    }
    return Machine_GetAbsoluteKind(machine_gobj);
}

void CustomMachineBlip_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x80122414, BlipKind); // bl Machine_GetAbsoluteKind
    OSReport("[MachineBlip] City blip kind lookup replaced\n");
}
