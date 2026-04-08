#include "game.h"
#include "item.h"

// Spawn an item at the given player's location.
// The item is collided with before the next frame begins.
void SpawnItemPlayer(int ply_int, ItemKind item_kind)
{
    GOBJ *mg = Ply_GetMachineGObj(ply_int);
    if (!mg) return;
    MachineData *md = mg->userdata;
    Vec3 spawn_pos = {
        .X = md->pos.X,
        .Y = md->pos.Y,
        .Z = md->pos.Z
    };
    ItemDesc item_desc;
    Item_InitDesc(&item_desc, item_kind, 1.0, 0, &spawn_pos, &md->up, &md->forward, -1, -1, 1, 3, -1, -1);
    GOBJ *item_gobj = Item_Create(&item_desc);
    if (!item_gobj)
        return;
    ItemData *id = item_gobj->userdata;
    Machine_OnTouchItem(md, id);
}
