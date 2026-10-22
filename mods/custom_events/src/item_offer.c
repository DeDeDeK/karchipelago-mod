#include "game.h"
#include "item.h"

#include "item_offer.h"

int ItemOffer_IsOffered(ItemKind kind)
{
    grBoxGeneObj *obj = *stc_grBoxGeneObj;
    for (int box = 0; obj && box < BOXKIND_NUM; box++)
    {
        for (int i = 0; i < obj->item_group_spawn[box].num; i++)
        {
            if (obj->item_group_spawn[box].it_kind[i] == kind && obj->item_group_spawn[box].chance[i] > 0)
                return 1;
        }
    }

    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (!info || !info->item_desc)
        return 0;
    for (int i = 0; i < info->item_desc->event_source_drop_num; i++)
    {
        ItemEventSourceDrop *row = &info->item_desc->event_source_drop[i];
        if (row->it_kind == kind &&
            (row->chance_dyna > 0 || row->chance_tac > 0 || row->chance_meteor > 0 ||
             row->chance_destructible > 0 || row->chance_chamber > 0 || row->chance_ufo > 0))
            return 1;
    }
    return 0;
}
