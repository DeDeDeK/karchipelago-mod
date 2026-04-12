#include "game.h"
#include "event.h"
#include "code_patch/code_patch.h"
#include "os.h"

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

// Spawn the given item for all human players
void SpawnItemHumans(ItemKind item_kind)
{
    OSReport("Spawning item kind %d for all human players...\n", item_kind);
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_HMN)
            SpawnItemPlayer(i, item_kind);
    }
}

// Replacement for CityItem_ProcessFakeItem (0x802542dc).
// Vanilla only applies fake item effects when the fake items event is active
// (DAT_805dd8cc+0x1C4 is non-NULL). This replacement falls back to looking up
// the fake data directly from the event table, so fake items spawned outside
// the event (e.g. as AP trap items) still deal damage on pickup.
static int ProcessFakeItem(GOBJ *item_gobj, void *hurt_params)
{
    // Check vanilla event data first (active fake items event)
    static void **stc_item_event_data = (void **)(0x805dd0e0 + 0x7EC);
    void *dat = *stc_item_event_data;
    if (dat)
    {
        void *event_fake_data = *(void **)((char *)dat + 0x1C4);
        if (event_fake_data)
        {
            Event_FakeItems_FillHurtParams(event_fake_data, hurt_params);
            return 1;
        }
    }

    // Fallback: look up fake data from the event table directly
    GOBJ *ev_gobj = *stc_eventcheck_gobj;
    if (!ev_gobj)
        return 0;

    EventCheckData *ev = ev_gobj->userdata;
    if (!ev || !ev->data || !ev->data->bgm_sky)
        return 0;

    void *fake_data = ev->data->bgm_sky[EVKIND_FAKEPOWERUPS].event_data;
    if (!fake_data)
        return 0;

    Event_FakeItems_FillHurtParams(fake_data, hurt_params);
    return 1;
}

void SpawnItem_OnBoot()
{
    CODEPATCH_REPLACEFUNC(CityItem_ProcessFakeItem, ProcessFakeItem);
}
