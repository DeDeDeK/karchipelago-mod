#include "ability_item.h"
#include "main.h"
#include "textbox.h"

// Give copy ability to every human Kirby rider via the raw rider API.
// Used in Air Ride (and as a CT fallback when item data tables are unavailable);
// City Trial normally spawns a real ITKIND_COPY* item through SpawnItemHumans so
// the pickup visual plays. Skips non-Kirby riders and players with no rider GObj.
int Ability_GiveItem(CopyKind copy_kind)
{
    int applied = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        if (!rd || rd->kind != RDKIND_KIRBY)
            continue;
        // Off-vehicle riders crash inside the new ability's anim callbacks,
        // which deref rd->machine_gobj (e.g. sleep -> Rider_CopyInputToMachine).
        if (!Rider_IsOnMachine(rd))
            continue;
        OSReport("[AbilityItem] Giving ability %d to player %d...\n", copy_kind, rd->ply);
        Rider_GiveAbility(rd, copy_kind);
        applied = 1;
    }
    if (applied && copy_kind < COPYKIND_NUM && CopyKind_Names[copy_kind])
        TextBox_Enqueue("Got %s ability!", CopyKind_Names[copy_kind]);
    return applied;
}

CopyKind Ability_ItKindToCopyKind(ItemKind it_kind)
{
    switch (it_kind)
    {
        case ITKIND_COPYFIRE:    return COPYKIND_FIRE;
        case ITKIND_COPYTIRE:    return COPYKIND_WHEEL;
        case ITKIND_COPYSLEEP:   return COPYKIND_SLEEP;
        case ITKIND_COPYSWORD:   return COPYKIND_SWORD;
        case ITKIND_COPYBOMB:    return COPYKIND_BOMB;
        case ITKIND_COPYPLASMA:  return COPYKIND_PLASMA;
        case ITKIND_COPYSPIKE:   return COPYKIND_NEEDLE;
        case ITKIND_COPYMIC:     return COPYKIND_MIC;
        case ITKIND_COPYICE:     return COPYKIND_ICE;
        case ITKIND_COPYTORNADO: return COPYKIND_TORNADO;
        case ITKIND_COPYBIRD:    return COPYKIND_BIRD;
        default:                 return COPYKIND_NONE;
    }
}
