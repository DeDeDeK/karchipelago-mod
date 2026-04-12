#include "game.h"
#include "os.h"

#include "main.h"
#include "gate_patches.h"
#include "mask_fmt.h"
#include "textbox.h"

// Note: this file has no OnBoot. GatePatches_FilterSpawnTables and
// GatePatches_FilterEventDropTables are invoked externally by item_spawn_filter.c
// alongside the other gate filter functions (abilities, items).

static const char *patch_names[PATCHKIND_NUM] = {
    [PATCHKIND_WEIGHT]   = "Weight Patch",
    [PATCHKIND_ACCEL]    = "Boost Patch",
    [PATCHKIND_TOPSPEED] = "Top Speed Patch",
    [PATCHKIND_TURN]     = "Turn Patch",
    [PATCHKIND_CHARGE]   = "Charge Patch",
    [PATCHKIND_GLIDE]    = "Glide Patch",
    [PATCHKIND_OFFENSE]  = "Offense Patch",
    [PATCHKIND_DEFENSE]  = "Defense Patch",
    [PATCHKIND_HP]       = "HP Patch",
};

// Map ITKIND stat patch items (up, down, fake) to their PatchKind.
// Returns -1 for non-patch items.
static int ItemKindToPatchKind(u8 it_kind)
{
    switch (it_kind)
    {
        case ITKIND_WEIGHT:      case ITKIND_WEIGHTDOWN:  case ITKIND_WEIGHTFAKE:  return PATCHKIND_WEIGHT;
        case ITKIND_ACCEL:       case ITKIND_ACCELDOWN:   case ITKIND_ACCELFAKE:   return PATCHKIND_ACCEL;
        case ITKIND_TOPSPEED:    case ITKIND_TOPSPEEDDOWN:case ITKIND_TOPSPEEDFAKE:return PATCHKIND_TOPSPEED;
        case ITKIND_TURN:        case ITKIND_TURNDOWN:    case ITKIND_TURNFAKE:    return PATCHKIND_TURN;
        case ITKIND_CHARGE:      case ITKIND_CHARGEDOWN:  case ITKIND_CHARGEFAKE:  return PATCHKIND_CHARGE;
        case ITKIND_GLIDE:       case ITKIND_GLIDEDOWN:   case ITKIND_GLIDEFAKE:   return PATCHKIND_GLIDE;
        case ITKIND_OFFENSE:     case ITKIND_OFFENSEDOWN: case ITKIND_OFFENSEFAKE: return PATCHKIND_OFFENSE;
        case ITKIND_DEFENSE:     case ITKIND_DEFENSEDOWN: case ITKIND_DEFENSEFAKE: return PATCHKIND_DEFENSE;
        case ITKIND_HP:          return PATCHKIND_HP;
        default:                 return -1;
    }
}

static void FilterPatchItemsFromPool(u8 *pool_kinds, u8 *pool_chances, u8 *pool_num)
{
    u8 num = *pool_num;
    u8 write = 0;
    u16 mask = ap_save->patch_unlocked_mask;

    for (u8 read = 0; read < num; read++)
    {
        int pk = ItemKindToPatchKind(pool_kinds[read]);
        if (pk >= 0 && !(mask & (1 << pk)))
            continue;

        if (write != read)
        {
            pool_kinds[write] = pool_kinds[read];
            pool_chances[write] = pool_chances[read];
        }
        write++;
    }

    *pool_num = write;
}

void GatePatches_FilterSpawnTables()
{
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    if (!obj)
        return;

    for (int box = 0; box < BOXKIND_NUM; box++)
    {
        FilterPatchItemsFromPool(
            obj->item_group_spawn[box].it_kind,
            obj->item_group_spawn[box].chance,
            &obj->item_group_spawn[box].num);
    }

    FilterPatchItemsFromPool(
        obj->sameitem_it_kind,
        obj->sameitem_chance,
        &obj->sameitem_num);

    FilterPatchItemsFromPool(
        obj->subsequent_it_kind,
        obj->subsequent_chance,
        &obj->subsequent_num);
}

void GatePatches_FilterEventDropTables()
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc)
        return;

    u16 mask = ap_save->patch_unlocked_mask;

    for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
    {
        int pk = ItemKindToPatchKind(info->item_desc->event_source_drop[i].it_kind);
        if (pk >= 0 && !(mask & (1 << pk)))
        {
            info->item_desc->event_source_drop[i].chance_misc = 0;
            info->item_desc->event_source_drop[i].chance_tac = 0;
            info->item_desc->event_source_drop[i].chance_meteor = 0;
            info->item_desc->event_source_drop[i].chance_pilar = 0;
            info->item_desc->event_source_drop[i].chance_chamber = 0;
            info->item_desc->event_source_drop[i].chance_ufo = 0;
        }
    }
}

int GatePatches_UnlockPatch(PatchKind kind)
{
    if (kind >= PATCHKIND_NUM)
        return 0;

    ap_save->patch_unlocked_mask |= (1 << kind);
    OSReport("[GatePatches] Patch %d (%s) unlocked (mask = %s)\n",
             kind, patch_names[kind], MaskBits(ap_save->patch_unlocked_mask, 16));
    TextBox_Enqueue(patch_names[kind]);
    return 1;
}
