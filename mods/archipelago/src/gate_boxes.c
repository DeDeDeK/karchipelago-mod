#include <string.h>

#include "game.h"
#include "hsd.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_boxes.h"
#include "inline.h"
#include "textbox_api.h"

// True if the post-filter pool for `box` still has at least one item with chance > 0.
// Other gating systems (abilities/patches/items) zero entries in this pool, so a
// box color may end up empty even when its bit in box_unlocked_mask is set.
static int BoxHasItems(grBoxGeneObj *obj, int box)
{
    for (int i = 0; i < obj->item_group_spawn[box].num; i++)
    {
        if (obj->item_group_spawn[box].chance[i] > 0)
            return 1;
    }
    return 0;
}

// Replacement for GrBoxGeneratorDetermine (0x800ebc04).
// Skips locked box colors AND box colors with no remaining items
// by zeroing their chance entries before doing weighted random selection.
// Returns box_color (or -1 if no data) — the original returns this in r3 and the
// caller at CityItemSpawn_Think (0x800eb210) saves it for later use.
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
    tb_api->EnqueueColoredNoun(NULL, BoxKind_Names[kind], tb_api->BoxColor, NULL);
    return 1;
}
