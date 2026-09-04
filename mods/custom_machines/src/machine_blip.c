// The City Trial machine blip for a machine the engine has no icon for.
//
// The blip GX callback indexes an 18-frame TexAnim by Machine_GetAbsoluteKind
// (0x801c85bc), which folds a custom machine's star slot onto the bike half of
// the range - no blip, a bike's icon, or a read past the table into the live
// instance joints. Replacing the one call that forms the index is the whole fix;
// a custom machine borrows its clone_kind's blip.

#include "os.h"
#include "obj.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Replaces the bl at 0x80122414, inside the blip GX callback, where r3 is the
// machine's GObj.
static MachineKind BlipKind(GOBJ *machine_gobj)
{
    MachineData *md = machine_gobj->userdata;

    if (!md->is_bike && md->kind >= VCSTAR_NUM)
    {
        CustomMachineEntry *e =
            CustomMachines_FindByKind(VCKIND_NUM + (md->kind - VCSTAR_NUM));
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
