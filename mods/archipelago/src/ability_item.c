#include "ability_item.h"
#include "main.h"
#include "textbox_api.h"
#include "ap_announce.h"

int Ability_GiveHumans(CopyKind copy_kind)
{
    int applied = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        if (rd->kind != RDKIND_KIRBY)
            continue;
        // On foot, the new ability's anim callbacks deref rd->machine_gobj (sleep's
        // Rider_CopyInputToMachine) and crash.
        if (!Rider_IsOnMachine(rd))
            continue;
        Rider_GiveAbility(rd, copy_kind);
        applied++;
    }
    return applied;
}

int Ability_GiveItem(CopyKind copy_kind)
{
    int applied = Ability_GiveHumans(copy_kind);
    if (applied)
    {
        OSReport("[AbilityItem] Gave the %s ability to %d player(s)\n",
                 CopyKind_Names[copy_kind], applied);
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
        case ITKIND_COPYTIRE:    return COPYKIND_TIRE;
        case ITKIND_COPYSLEEP:   return COPYKIND_SLEEP;
        case ITKIND_COPYSWORD:   return COPYKIND_SWORD;
        case ITKIND_COPYBOMB:    return COPYKIND_BOMB;
        case ITKIND_COPYPLASMA:  return COPYKIND_PLASMA;
        case ITKIND_COPYNEEDLE:  return COPYKIND_NEEDLE;
        case ITKIND_COPYMIKE:    return COPYKIND_MIKE;
        case ITKIND_COPYICE:     return COPYKIND_ICE;
        case ITKIND_COPYTORNADO: return COPYKIND_TORNADO;
        case ITKIND_COPYBIRD:    return COPYKIND_BIRD;
        default:                 return COPYKIND_NONE;
    }
}
