#include "game.h"
#include "patch_item.h"
#include "main.h"
#include "textbox.h"
#include "item.h"
#include "machine.h"
#include "os.h"

// Give PatchKind to every human rider on a machine
int Patch_GiveItem(PatchKind kind, int num)
{
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
        {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg)
            {
                MachineData *md = mg->userdata;
                Machine_GivePatch(md, kind, num);
                OSReport("Giving %d patches of kind %d to player %d...\n", num, kind, i);
            }
        }
    }
    return 1;
}

// Give num of AllUp to every human rider on a machine
int Patch_AllUp_GiveItem(int num)
{
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
        {
            GOBJ *mg = Ply_GetMachineGObj(i);
            if (mg)
            {
                MachineData *md = mg->userdata;
                Machine_GiveAllUp(md, num);
                OSReport("Giving %d all ups to player %d...\n", num, i);
            }
        }
    }
    return 1;
}
