#include "ability_item.h"
#include "main.h"
#include "textbox_api.h"
#include "ap_announce.h"

// Give a copy ability to every human Kirby rider via the raw rider API. The AP
// copy-ability path for City Trial and Air Ride - no ITKIND_COPY* item is spawned,
// so it needs no item data tables and works in stadiums and Free Run. Top Ride maps
// abilities to their TR item analog instead.
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
        Rider_GiveAbility(rd, copy_kind);
        applied++;
    }

    if (applied)
    {
        OSReport("[AbilityItem] Gave the %s ability to %d player(s)\n",
                 CopyKind_Names[copy_kind], applied);
        if (CopyKind_Names[copy_kind])
            APAnnounce_Grant("Received: ", CopyKind_Names[copy_kind],
                             tb_api->AbilityColors[copy_kind], " ability");
    }
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
        case ITKIND_COPYICE:     return COPYKIND_FREEZE;
        case ITKIND_COPYTORNADO: return COPYKIND_TORNADO;
        case ITKIND_COPYBIRD:    return COPYKIND_BIRD;
        default:                 return COPYKIND_NONE;
    }
}
