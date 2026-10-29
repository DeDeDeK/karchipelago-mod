#include "game.h"
#include "hsd.h"
#include "os.h"
#include "stage.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_boxes.h"
#include "gate_ap_star.h"
#include "inline.h"
#include "textbox_api.h"
#include "ap_announce.h"

#define BOX_SIZE_NUM 3

// The ability / patch / item filters zero entries in this pool, so a box color can end up
// empty even with its bit in box_unlocked_mask set.
static int BoxHasItems(grBoxGeneObj *obj, int box)
{
    for (int i = 0; i < obj->item_group_spawn[box].num; i++)
    {
        if (obj->item_group_spawn[box].chance[i] > 0)
            return 1;
    }
    return 0;
}

int GateBoxes_RollSize(int color)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc || !info->item_desc->box_spawn_chances)
        return 0;

    int weight[BOX_SIZE_NUM] = { 0 };
    int total = 0;
    for (int c = 0; c < BOXKIND_NUM; c++)
    {
        if (color >= 0 && c != color)
            continue;
        for (int size = 0; size < BOX_SIZE_NUM; size++)
        {
            weight[size] += info->item_desc->box_spawn_chances[c][size];
            total += info->item_desc->box_spawn_chances[c][size];
        }
    }
    if (total == 0)
        return 0;

    return Gm_Roll(weight, BOX_SIZE_NUM);
}

// Replaces GrBoxGeneratorDetermine (0x800ebc04). Returns the box's ItemKind, which is the
// picked color for the three vanilla kinds, or -1 when nothing is eligible -
// PowerUp_SpawnFromSky treats -1 as "place no box".
static int GateBoxes_DetermineBoxType(int *box_color, int *box_size)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!info || !info->item_desc || !info->item_desc->box_spawn_chances || !obj)
        return -1;

    int total = 0;
    int weight[BOXKIND_NUM][BOX_SIZE_NUM];
    for (int color = 0; color < BOXKIND_NUM; color++)
    {
        int eligible = GateBoxes_IsUnlocked(color) && BoxHasItems(obj, color);
        for (int size = 0; size < BOX_SIZE_NUM; size++)
        {
            weight[color][size] = eligible ? info->item_desc->box_spawn_chances[color][size] : 0;
            total += weight[color][size];
        }
    }

    if (total == 0)
        return -1;

    int idx = Gm_Roll(&weight[0][0], BOXKIND_NUM * BOX_SIZE_NUM);
    if (idx < 0)
        return -1;
    *box_color = idx / BOX_SIZE_NUM;
    *box_size = idx % BOX_SIZE_NUM;
    return *box_color;
}

int GateBoxes_IsUnlocked(BoxKind kind)
{
    if (kind < 0 || kind >= BOXKIND_NUM)
        return 0;
    return (ap_save->box_unlocked_mask & (1 << kind)) != 0;
}

void GateBoxes_OnBoot()
{
    CODEPATCH_REPLACEFUNC(GrBoxGeneratorDetermine, GateBoxes_DetermineBoxType);
    OSReport("[GateBoxes] Hooks installed\n");
}

int GateBoxes_UnlockBox(BoxKind kind)
{
    if (kind >= BOXKIND_NUM)
        return 0;

    ap_save->box_unlocked_mask |= (1 << kind);
    OSReport("[GateBoxes] Box %d (%s) unlocked (mask = %s)\n",
             kind, BoxKind_Names[kind], MaskBits(ap_save->box_unlocked_mask, BOXKIND_NUM));
    APAnnounce_Grant("Unlocked Box: ", BoxKind_Names[kind], tb_api->BoxColors[kind], NULL);

    // Red boxes carry the AP Star spheres.
    if (kind == BOXKIND_RED)
        GateApStar_PushMask();
    return 1;
}
