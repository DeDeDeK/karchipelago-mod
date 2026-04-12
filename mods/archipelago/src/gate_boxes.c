#include "game.h"
#include "hsd.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_boxes.h"
#include "mask_fmt.h"
#include "textbox.h"

// Tracks whether each box type has at least one item with chance > 0
// after all spawn table filtering. Updated by GateBoxes_UpdateItemAvailability().
static u8 stc_box_has_items[BOXKIND_NUM];

static const char *box_names[BOXKIND_NUM] = {
    [BOXKIND_BLUE]  = "Blue Box",
    [BOXKIND_GREEN] = "Green Box",
    [BOXKIND_RED]   = "Red Box",
};

// Check each box type's spawn pool after all item filtering.
// A box type is disabled if it has no items with chance > 0.
void GateBoxes_UpdateItemAvailability()
{
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!obj)
        return;

    for (int box = 0; box < BOXKIND_NUM; box++)
    {
        stc_box_has_items[box] = 0;
        for (int i = 0; i < obj->item_group_spawn[box].num; i++)
        {
            if (obj->item_group_spawn[box].chance[i] > 0)
            {
                stc_box_has_items[box] = 1;
                break;
            }
        }
    }
}

// Replacement for GrBoxGeneratorDetermine (0x800ebc04).
// Skips locked box colors AND box colors with no remaining items
// by zeroing their chance entries before doing weighted random selection.
// Returns box_color (or -1 if no data) — the original returns this in r3 and the
// caller at CityItemSpawn_Think (0x800eb210) saves it for later use.
int GateBoxes_DetermineBoxType(int *box_color, int *box_size)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc || !info->item_desc->box_spawn_chances)
        return -1;

    u8 *src = (u8 *)info->item_desc->box_spawn_chances;
    u8 chances[9];
    u8 mask = ap_save->box_unlocked_mask;

    // Copy chances, zeroing out locked colors and colors with no items
    for (int color = 0; color < BOXKIND_NUM; color++)
    {
        int enabled = (mask & (1 << color)) && stc_box_has_items[color];
        for (int size = 0; size < 3; size++)
        {
            int idx = color * 3 + size;
            chances[idx] = enabled ? src[idx] : 0;
        }
    }

    // Sum all chances
    int total = 0;
    for (int i = 0; i < 9; i++)
        total += chances[i];

    if (total == 0)
        return -1;

    // Weighted random selection
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
             kind, box_names[kind], MaskBits(ap_save->box_unlocked_mask, 8));
    TextBox_Enqueue(box_names[kind]);
    return 1;
}
