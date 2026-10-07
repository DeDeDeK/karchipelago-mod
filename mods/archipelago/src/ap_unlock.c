#include "game.h"

#include "main.h"
#include "ap_unlock.h"
#include "gate_ap_star.h"

static const struct
{
    const char *name;
    u8 bits;
} unlock_cats[AP_UNLOCK_NUM] = {
    [AP_UNLOCK_MACHINE]       = { "machines",        AP_MACHINE_BIT_NUM },
    [AP_UNLOCK_ABILITY]       = { "abilities",       COPYKIND_NUM },
    [AP_UNLOCK_EVENT]         = { "events",          EVKIND_NUM },
    [AP_UNLOCK_PATCH]         = { "patch types",     PATCHKIND_NUM },
    [AP_UNLOCK_ITEM]          = { "CT items",        ITUNLOCK_NUM },
    [AP_UNLOCK_BOX]           = { "boxes",           BOXKIND_NUM },
    [AP_UNLOCK_AIRRIDE_STAGE] = { "AR stages",       AIRRIDE_NUM },
    [AP_UNLOCK_TOPRIDE_STAGE] = { "TR stages",       TOPRIDE_NUM },
    [AP_UNLOCK_TOPRIDE_ITEM]  = { "TR items",        TRITEM_NUM },
    [AP_UNLOCK_COLOR]         = { "colors",          KIRBYCOLOR_NUM },
    [AP_UNLOCK_STADIUM]       = { "stadiums",        STKIND_NUM },
    [AP_UNLOCK_BASE_ABILITY]  = { "base abilities",  BASEABILITY_NUM },
    [AP_UNLOCK_AP_STAR_PIECE] = { "AP Star spheres", AP_STAR_PIECE_NUM },
};

const char *APUnlock_Name(APUnlockCategory cat)
{
    return (unsigned)cat < AP_UNLOCK_NUM ? unlock_cats[cat].name : "?";
}

int APUnlock_Bits(APUnlockCategory cat)
{
    return (unsigned)cat < AP_UNLOCK_NUM ? unlock_cats[cat].bits : 0;
}

u32 APUnlock_GetMask(APUnlockCategory cat)
{
    switch (cat)
    {
        case AP_UNLOCK_MACHINE:        return ap_save->machine_unlocked_mask;
        case AP_UNLOCK_ABILITY:        return ap_save->ability_unlocked_mask;
        case AP_UNLOCK_EVENT:          return ap_save->event_unlocked_mask;
        case AP_UNLOCK_PATCH:          return ap_save->patch_unlocked_mask;
        case AP_UNLOCK_ITEM:           return ap_save->item_unlocked_mask;
        case AP_UNLOCK_BOX:            return ap_save->box_unlocked_mask;
        case AP_UNLOCK_AIRRIDE_STAGE:  return ap_save->airride_stage_unlocked_mask;
        case AP_UNLOCK_TOPRIDE_STAGE:  return ap_save->topride_stage_unlocked_mask;
        case AP_UNLOCK_TOPRIDE_ITEM:   return ap_save->topride_item_unlocked_mask;
        case AP_UNLOCK_COLOR:          return ap_save->color_unlocked_mask;
        case AP_UNLOCK_STADIUM:        return ap_save->stadium_unlocked_mask;
        case AP_UNLOCK_BASE_ABILITY:   return ap_save->base_ability_unlocked_mask;
        case AP_UNLOCK_AP_STAR_PIECE:  return ap_save->ap_star_piece_unlocked_mask;
        default:                       return 0;
    }
}

void APUnlock_SetMask(APUnlockCategory cat, u32 mask)
{
    switch (cat)
    {
        case AP_UNLOCK_MACHINE:        ap_save->machine_unlocked_mask        = (u32)mask; break;
        case AP_UNLOCK_ABILITY:        ap_save->ability_unlocked_mask        = (u16)mask; break;
        case AP_UNLOCK_EVENT:          ap_save->event_unlocked_mask          = (u32)mask; break;
        case AP_UNLOCK_PATCH:          ap_save->patch_unlocked_mask          = (u16)mask; break;
        case AP_UNLOCK_ITEM:           ap_save->item_unlocked_mask           = (u32)mask; break;
        case AP_UNLOCK_BOX:            ap_save->box_unlocked_mask            = (u8)mask;  break;
        case AP_UNLOCK_AIRRIDE_STAGE:  ap_save->airride_stage_unlocked_mask  = (u16)mask; break;
        case AP_UNLOCK_TOPRIDE_STAGE:  ap_save->topride_stage_unlocked_mask  = (u16)mask; break;
        case AP_UNLOCK_TOPRIDE_ITEM:   ap_save->topride_item_unlocked_mask   = (u32)mask; break;
        case AP_UNLOCK_COLOR:          ap_save->color_unlocked_mask          = (u8)mask;  break;
        case AP_UNLOCK_STADIUM:        ap_save->stadium_unlocked_mask        = (u32)mask; break;
        case AP_UNLOCK_BASE_ABILITY:   ap_save->base_ability_unlocked_mask   = (u8)mask;  break;
        case AP_UNLOCK_AP_STAR_PIECE:  ap_save->ap_star_piece_unlocked_mask  = (u8)mask;  break;
        default: break;
    }

    // The spheres ride a red carrier box, so both masks feed ap_star's sphere gate.
    if (cat == AP_UNLOCK_AP_STAR_PIECE || cat == AP_UNLOCK_BOX)
        GateApStar_PushMask();
}
