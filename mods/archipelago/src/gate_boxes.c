#include <string.h>

#include "game.h"
#include "hsd.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_boxes.h"
#include "inline.h"
#include "textbox_api.h"
#include "ap_announce.h"

// The ability / patch / item filters zero entries in this pool, so a box color can end
// up empty even when its bit in box_unlocked_mask is set.
static int BoxHasItems(grBoxGeneObj *obj, int box)
{
    for (int i = 0; i < obj->item_group_spawn[box].num; i++)
    {
        if (obj->item_group_spawn[box].chance[i] > 0)
            return 1;
    }
    return 0;
}

// Replaces GrBoxGeneratorDetermine (0x800ebc04). Returns box_color, or -1 when nothing
// is eligible - PowerUp_SpawnFromSky treats -1 as "place no box".
int GateBoxes_DetermineBoxType(int *box_color, int *box_size)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!info || !info->item_desc || !info->item_desc->box_spawn_chances || !obj)
        return -1;

    u8 chances[9];
    memcpy(chances, info->item_desc->box_spawn_chances, 9);

    u8 mask = ap_save->box_unlocked_mask;
    for (int color = 0; color < BOXKIND_NUM; color++)
    {
        if ((mask & (1 << color)) && BoxHasItems(obj, color))
            continue;
        chances[color * 3 + 0] = 0;
        chances[color * 3 + 1] = 0;
        chances[color * 3 + 2] = 0;
    }

    int total = 0;
    for (int i = 0; i < 9; i++)
        total += chances[i];

    if (total == 0)
        return -1;

    int roll = HSD_Randi(total);
    int cumulative = 0;
    int selected = 0;
    for (int i = 0; i < 9; i++)
    {
        cumulative += chances[i];
        if (roll < cumulative)
        {
            selected = i;
            break;
        }
    }

    *box_color = selected / 3;
    *box_size = selected % 3;
    return *box_color;
}

void GateBoxes_OnBoot()
{
    CODEPATCH_REPLACEFUNC(GrBoxGeneratorDetermine, GateBoxes_DetermineBoxType);
    OSReport("[GateBoxes] Box type gating hook installed\n");
}

int GateBoxes_UnlockBox(BoxKind kind)
{
    if (kind >= BOXKIND_NUM)
        return 0;

    ap_save->box_unlocked_mask |= (1 << kind);
    OSReport("[GateBoxes] Box %d (%s) unlocked (mask = %s)\n",
             kind, BoxKind_Names[kind], MaskBits(ap_save->box_unlocked_mask, 8));
    APAnnounce_Grant("Unlocked Box: ", BoxKind_Names[kind], tb_api->BoxColors[kind], NULL);
    return 1;
}
